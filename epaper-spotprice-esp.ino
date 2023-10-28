/*
 * (c) Mika Mäkelä - 2023
 * Show spot price of electricity on the e-ink display
 * Board: NodeMCU (ESP8266)
 */

#define ENABLE_GxEPD2_GFX 0

#include <GxEPD2_BW.h>
#include <WiFiUdp.h>
#include <NTPClient.h>
#include <ESP8266WiFi.h>
#include <ESP8266HTTPClient.h>
#include <WiFiClientSecure.h>
#include <WiFiClientSecureBearSSL.h>
#include <ArduinoJson.h>
#include "arduino_secrets.h"

GxEPD2_BW<GxEPD2_583, GxEPD2_583::HEIGHT / 2> display(GxEPD2_583(/*CS=D6*/ 5, /*DC=D3*/ 0, /*RST=D4*/ 2, /*BUSY=D2*/ 4));

const char *ssid_home = SECRET_SSID_HOME;
const char *password_home = SECRET_PASS_HOME;

// Define NTP Client to get time
WiFiUDP ntpUDP;
NTPClient timeClient(ntpUDP, "pool.ntp.org");

void setup()
{
  Serial.begin(115200);
  Serial.println("setup");
  delay(100);
  display.init(115200);

  // connect to Wifi
  connectWifi();

  timeClient.begin();
  timeClient.update();

  int currentMinute = timeClient.getMinutes();
  Serial.print("Minutes: ");
  Serial.println(currentMinute); 

  String url = "https://api.spot-hinta.fi/JustNow";
  String urlFuture = "https://api.spot-hinta.fi/JustNow?lookForwardHours=1";

  // make request to the API
  String dataNow = callPriceAPI(url);

  if (dataNow == "FAIL") {
    dataNow = callPriceAPI(url);
  }
  
  String dataFuture = callPriceAPI(urlFuture);

  if (dataFuture == "FAIL") {
    dataFuture = callPriceAPI(urlFuture);
  }

  // update content to the e-paper display
  drawEpaper(dataNow, dataFuture);

  Serial.println("setup done");


  // Calculate sleep time. Sleep until the next price update that happens once per hour.
  int sleepTimeMinutes = 60 - currentMinute + 5; 

  Serial.print("Sleep delay: ");
  Serial.println(sleepTimeMinutes); 

  unsigned int sleepTimeMicroseconds = 60000000;
  sleepTimeMicroseconds = sleepTimeMinutes * 60000000;

  // if sleep time is unusually small
  if (sleepTimeMicroseconds < 60000000) {
    ESP.deepSleep(900e6);
  }

  // go to deep sleep
  ESP.deepSleep(sleepTimeMicroseconds);
}

void loop()
{
}

String callPriceAPI(String url) {

  String payload = "";
  
  //Check WiFi connection status
  if(WiFi.status()== WL_CONNECTED)  {

    std::unique_ptr<BearSSL::WiFiClientSecure>client(new BearSSL::WiFiClientSecure);
    client->setInsecure();
    HTTPClient https;
    
    Serial.print("[HTTPS] begin...\n");
    
    if (https.begin(*client, url)) {  // HTTPS

      https.addHeader("accept", "application/json");
      https.setTimeout(8000);

      Serial.print("[HTTPS] GET...\n");
      // start connection and send HTTP header
      int httpCode = https.GET();

      // httpCode will be negative on error
      if (httpCode > 0) {
        
        // HTTP header has been send and Server response header has been handled
        Serial.printf("[HTTPS] GET... code: %d\n", httpCode);

        if (httpCode == HTTP_CODE_OK || httpCode == HTTP_CODE_MOVED_PERMANENTLY) {
          payload = https.getString();
          Serial.println(payload);
        }
        
      } else {
        Serial.printf("[HTTPS] GET... failed, error: %s\n", https.errorToString(httpCode).c_str());
        payload = "FAIL";
      }

      https.end();
      
    } else {
      Serial.printf("[HTTPS] Unable to connect\n");
      payload = "FAIL";
    }

    Serial.print("[HTTPS] END");
  }

  return payload;
}

// Draw the content to the e-ink display
void drawEpaper(String dataNow, String dataFuture)
{
  Serial.println("drawEpaper");

  const size_t capacity = 2 * JSON_ARRAY_SIZE(0) + JSON_ARRAY_SIZE(1) + JSON_OBJECT_SIZE(7) + 800;

  DynamicJsonDocument doc(capacity);
  deserializeJson(doc, dataNow);

  DynamicJsonDocument docFuture(capacity);
  deserializeJson(docFuture, dataFuture);

  const String price = doc["PriceWithTax"];
  double priceInt = price.toDouble();
  priceInt = priceInt * 100;

  const String priceFuture = docFuture["PriceWithTax"];
  double priceFutureInt = priceFuture.toDouble();
  priceFutureInt = priceFutureInt * 100;
  
  const String timestamp = doc["DateTime"];

  Serial.println(price);
  Serial.println(priceInt);

  display.setRotation(1);
  display.setTextColor(GxEPD_BLACK);

  display.setFullWindow();
  display.firstPage();
  
  do
  {
    display.fillScreen(GxEPD_WHITE);

    display.setTextSize(4);

    display.setCursor(10, 50);
    display.print("Hinta nyt:"); // price now

    display.setTextSize(13);

    display.setCursor(30, 120);
    display.print(priceInt);
    
    display.setTextSize(4);

    display.setCursor(10, 280);
    display.print("Seuraava tunti:"); // next hour

    display.setTextSize(13);

    display.setCursor(30, 340);
    display.print(priceFutureInt);

    display.setTextSize(2);

    display.setCursor(10, 520);
    display.print("Paivitetty:"); // update timestamp

    display.setCursor(10, 540);
    display.print(timestamp);

  } while (display.nextPage());

  Serial.println("drawEpaper done");
}

void connectWifi()
{
  Serial.print("Connecting to ");
  Serial.println(ssid_home);

  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid_home, password_home);

  while (WiFi.status() != WL_CONNECTED)
  {
    delay(500);
    Serial.print(".");

    if (WiFi.status() == WL_NO_SSID_AVAIL)
    {
      WiFi.disconnect();
      ESP.deepSleep(600e6);
      break;
    }
  }

  Serial.println("WiFi connected");
  Serial.println("IP address: ");
  Serial.println(WiFi.localIP());
}
