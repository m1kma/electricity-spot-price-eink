/*
 * (c) Mika Mäkelä - 2025
 * Show electricity spot price on the e-ink display
 * Board: NodeMCU (ESP8266)
 * Display: Waveshare 5.83" E-INK RAW DISPLAY 600X448
 */

#define ENABLE_GxEPD2_GFX 0

#include <GxEPD2_BW.h>
#include <WiFiUdp.h>
#include <NTPClient.h>
#include <ESP8266WiFi.h>
#include <ESP8266HTTPClient.h>
#include <WiFiClientSecureBearSSL.h>
#include "arduino_secrets.h"

GxEPD2_BW<GxEPD2_583, GxEPD2_583::HEIGHT / 2> display(GxEPD2_583(5, 0, 2, 4));

const char *ssid_home = SECRET_SSID_HOME;
const char *password_home = SECRET_PASS_HOME;

WiFiUDP ntpUDP;
NTPClient timeClient(ntpUDP, "pool.ntp.org");

double nextHours[12];   // store the next prices
int currentMinute = 0;
int currentHour = 0;

void setup()
{
  Serial.begin(115200);
  Serial.println("setup");
  delay(100);
  display.init(115200);

  connectWifi();

  timeClient.begin();
  while(!timeClient.update()) {
    timeClient.forceUpdate();
  }

  currentMinute = timeClient.getMinutes();
  currentHour = timeClient.getHours() + 2;

  // Get Unix time from NTP
  unsigned long epochTime = timeClient.getEpochTime();

  // Convert to UTC date/time
  time_t rawTime = epochTime;
  struct tm * timeinfo = gmtime(&rawTime);

  // Format timestamp: YYYY-MM-DDTHH:00:00.000Z
  char timestamp[30];
  sprintf(timestamp,
          "%04d-%02d-%02dT%02d:00:00.000Z",
          timeinfo->tm_year + 1900,
          timeinfo->tm_mon + 1,
          timeinfo->tm_mday,
          timeinfo->tm_hour);

  Serial.print("Current timestamp: ");
  Serial.println(timestamp);

  // CSV API URL with current timestamp
  String url = String("https://sahkotin.fi/prices.csv?fix&vat&start=") + timestamp;

  // make request to the API
  String csvData = callPriceAPI(url);
  
  if (csvData == "FAIL") {
    drawError("VIRHE TIETOJEN LATAUKSESSA");
    ESP.deepSleep(300e6); // 300e6 = 5min
  }

  // parse CSV
  double priceNow = 0.0;
  double priceNext = 0.0;
  String timestampNow = "";
  parseCSV(csvData, priceNow, priceNext, timestampNow);

  // update e-paper display
  drawEpaper(priceNow, priceNext, timestampNow);

  Serial.println("setup done");

  int sleepTimeMinutes = 60 - currentMinute + 5; // ESP8266 deep sleep is inaccurate and few extra minutes should be added
  Serial.print("Sleep delay: ");
  Serial.println(sleepTimeMinutes);

  unsigned int sleepTimeMicroseconds = sleepTimeMinutes * 60000000; // 60000000 = 60sec

  if (sleepTimeMicroseconds < 60000000)
  {
    ESP.deepSleep(900e6);
  }

  ESP.deepSleep(sleepTimeMicroseconds);
}

void loop() {}


// ================== API CALL =======================

String callPriceAPI(String url)
{
  String payload = "";

  if (WiFi.status() == WL_CONNECTED)
  {
    std::unique_ptr<BearSSL::WiFiClientSecure> client(new BearSSL::WiFiClientSecure);
    client->setInsecure();

    HTTPClient https;
    Serial.print("[HTTPS] begin...\n");
    Serial.print(url);

    if (https.begin(*client, url))
    {
      https.addHeader("accept", "text/csv");

      Serial.print("[HTTPS] GET...\n");
      int httpCode = https.GET();

      if (httpCode > 0)
      {
        Serial.printf("[HTTPS] GET... code: %d\n", httpCode);
        if (httpCode == HTTP_CODE_OK)
        {
          payload = https.getString();
          Serial.print(payload);
        }
      }
      else
      {
        Serial.printf("[HTTPS] GET... failed, error: %s\n", https.errorToString(httpCode).c_str());
        payload = "FAIL";
      }

      https.end();
    }
    else
    {
      Serial.printf("[HTTPS] Unable to connect\n");
      payload = "FAIL";
    }

    Serial.print("[HTTPS] END");
  }

  return payload;
}


// ================== CSV PARSE =======================

void parseCSV(String csv, double &priceNow, double &priceNext, String &timestampNow)
{
  Serial.print("CSV Parse...");

  if (csv == "FAIL") return;

  int lineStart = csv.indexOf('\n') + 1;
  int i = 0;
  double lastValue = 0;

  while (i < 12)
  {
    int lineEnd = csv.indexOf('\n', lineStart);

    if (lineEnd == -1)   // no more rows
    {
      // fill remaining bars with 0
      for (int j = i; j < 12; j++)
      {
        nextHours[j] = 0;
      }
      break;
    }

    String line = csv.substring(lineStart, lineEnd);

    int comma = line.indexOf(',');
    if (comma == -1) break;

    String ts = line.substring(0, comma);
    double value = line.substring(comma + 1).toFloat();

    if (i == 0)
    {
      timestampNow = ts;
      priceNow = value;
    }
    if (i == 1)
    {
      priceNext = value;
    }

    nextHours[i] = value;
    lastValue = value;

    lineStart = lineEnd + 1;
    i++;
  }

  // If no data at all, ensure array is zero
  if (i == 0)
  {
    for (int k = 0; k < 10; k++)
      nextHours[k] = 0;
  }
}


// ================== DRAW FUNCTIONS =======================

void drawBarChart(int originX, int originY, int width, int height)
{
  Serial.print("drawBarChart...");

  display.setTextSize(2);
  display.setCursor(200, 355);
  display.print("Seuraavat 12 tuntia");

  const double MAX_VALUE = 30.0;   // chart always scaled 0–30

  int barWidth = width / 12;

  for (int i = 0; i < 12; i++)
  {
    double value = nextHours[i];

    // Clamp value to 0–30 range
    if (value < 0) value = 0;
    if (value > MAX_VALUE) value = MAX_VALUE;

    // Normalize to chart height
    double normalized = value / MAX_VALUE;
    int barHeight = normalized * height;

    int x = originX + (i * barWidth);
    int y = originY + height - barHeight;

    display.fillRect(
      x + 2,
      y,
      barWidth - 4,
      barHeight,
      GxEPD_BLACK
    );
  }

  // Outline box
  display.drawRect(originX, originY, width, height, GxEPD_BLACK);
  
  // ===== Reference lines + labels =====
  int values[3] = {10, 20, 30};

  for (int i = 0; i < 3; i++)
  {
    int val = values[i];

    int y = originY + height - ( (val / MAX_VALUE) * height );

    // draw line
    display.drawLine(originX, y, originX + width, y, GxEPD_BLACK);
    display.drawLine(originX, y+1, originX + width, y+1, GxEPD_WHITE);

    // thicker/bolder looking text
    display.setTextSize(2);

    display.setCursor(originX + 5, y + 5);
    display.print(val);
  }
}


void drawEpaper(double priceNow, double priceNext, String timestamp)
{
  Serial.println("drawEpaper");

  display.setRotation(1);
  display.setTextColor(GxEPD_BLACK);

  display.setFullWindow();
  display.firstPage();
  do
  {
    display.fillScreen(GxEPD_WHITE);

    // --- Current time ---
    display.setTextSize(2);
    display.setCursor(340, 10);
    display.print(currentHour);
    display.print(":");
    if (currentMinute < 10) display.print("0");
    display.print(currentMinute);

    // --- Current price ---
    display.setTextSize(4);
    display.setCursor(10, 20);
    display.print("Hinta nyt");

    display.setTextSize(13);
    display.setCursor(30, 90);
    display.print(priceNow);

    // --- Next hour price ---
    display.setTextSize(4);
    display.setCursor(10, 230);
    display.print("Seuraava tunti");

    display.setTextSize(5);
    display.setCursor(30, 275);
    display.print(priceNext);

    drawBarChart(5, 350, 428, 240);

  } while (display.nextPage());

  Serial.println("drawEpaper done");
}

void drawError(String errorMsg)
{
  display.setRotation(1);
  display.setTextColor(GxEPD_BLACK);

  display.setFullWindow();
  display.firstPage();
  do
  {
    display.fillScreen(GxEPD_WHITE);

    // --- Current price ---
    display.setTextSize(3);
    display.setCursor(10, 100);
    display.print(errorMsg);

  } while (display.nextPage());
}


// ================== WIFI =======================

void connectWifi()
{
  Serial.print("Connecting to ");
  Serial.println(ssid_home);

  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid_home, password_home);

  while (WiFi.status() != WL_CONNECTED)
  {
    delay(1000);
    Serial.print(".");
    if (WiFi.status() == WL_NO_SSID_AVAIL)
    {
      WiFi.disconnect();
      drawError("VIRHE WIFI YHTEYDESSA");
      ESP.deepSleep(600e6);
      break;
    }
  }

  Serial.println("WiFi connected");
  Serial.println("IP address: ");
  Serial.println(WiFi.localIP());
}
