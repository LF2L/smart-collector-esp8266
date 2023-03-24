#include <Arduino.h>
#include <ESP8266WiFi.h>
#include <ESP8266HTTPClient.h>
#include "HX711.h"
#include <rtc_memory.h>

// WiFi credentials.
const char *WIFI_SSID = "<SSID>";
const char *WIFI_PASS = "<SSID password>";

const String apiURL = "<API_URL>";
const String MCUid = "<MCU_UID>";
const char *APIuser = "<API_USER_NAME>";
const char *APIpass = "<API_PASSWORD>";


const bool battery = false;
const bool production = false;
const bool communicationEnabled = true;

const int SHORT_PRESS_TIME = 2000; // 1000 milliseconds
const int LONG_PRESS_TIME  = 2000;
const int BUTTON_PIN = 14;

#define durationSleep 60 // secondes
#define NB_TRYWIFI 20    // nbr d'essai connexion WiFi, number of try to connect WiFi
#define DOUT SDA
#define SCL1 SCL
// #define BTN_IN 14

float calibration_factor = 422.6;

HX711 scale;

int lastState = LOW;  // the previous state from the input pin
int currentState;

unsigned long initTime  = 0;
unsigned long pressedTime  = 0;
unsigned long releasedTime = 0;

typedef struct
{
  int offset;
} CollectorData;

RtcMemory rtcMem;

void tare_sensor(HX711 loadCellRef)
{
  // tare the sensor
  loadCellRef.tare();

  long offset = loadCellRef.get_offset();

  Serial.println(offset);

  CollectorData *data = rtcMem.getData<CollectorData>();
  data->offset = offset;
  //rtcMem.save();
  rtcMem.persist();
}

void sendDataWifi(float weightData)
{
  char buf[8];
  char weightHex[4];
  int data_in_int = 1201;
  sprintf(weightHex, "%04s", String(data_in_int, HEX));
  Serial.println(weightHex);
  strcpy(buf,weightHex);


  // convert battery data
  // analog read level is 10 bit 0-1023 (0V-1V).
  // our 1M & 220K voltage divider takes the max
  // lipo value of 4.2V and drops it to 0.924V max.
  // this means our min analog read value should be 706 (3.14V)
  // and the max analog read value should be 945 (4.2V).
  int level = analogRead(A0);
  level = map(level, 706, 945, 0, 100);
  Serial.println(level);
  char battHex[4];
  sprintf(battHex, "%04s", String(level, HEX));
  

  if (!production)
    Serial.println("weight data: " + data_in_int);

  if (WiFi.status() == WL_CONNECTED)
  {

    WiFiClient client;
    HTTPClient http;

    http.begin(client, apiURL + MCUid);
    //http.setAuthorization(token);
    http.setAuthorization(APIuser, APIpass);
    http.addHeader("Content-Type", "application/json");
    http.addHeader("Accept", "application/json");
    strcat(buf,battHex);

    char message[46];
    
    Serial.println(weightHex);
    Serial.println(battHex);
    
    sprintf(message, "{\"device\":\"%s\", \"payload\": \"%s\"}",MCUid, buf );


    // String obj = String("{\"device\":\"") + String(MCUid) + "\", \"payload\": \"" + weightHex + battHex + "\"}";
    if (!production)
      Serial.println(message);

    int httpCode = http.POST(message);

    // httpCode will be negative on error
    if (httpCode > 0)
    {
      // HTTP header has been send and Server response header has been handled
      if (!production)
      {
        Serial.printf("[HTTP] POST... code: %d\n", httpCode);
        Serial.print("[HTTP] POST... response:\n");
        Serial.println(http.getString());
      }

      // file found at server
      if (httpCode == HTTP_CODE_OK)
      {
        const String &payload = http.getString();
        Serial.println("received payload:\n<<");
        Serial.println(payload);
        Serial.println(">>");
      }
    }
    else
    {
      if (!production)
        Serial.printf("[HTTP] POST... failed, error: %s\n", http.errorToString(httpCode).c_str());
    }

    http.end();
  }
}

void measure_weight()
{
  // CollectorData *data = rtcMem.getData<CollectorData>();

  // scale.set_offset(data->offset);

  float weight = scale.get_units();

  Serial.println("weight: " + String(weight));

  if (weight < 0)
    weight = 0;

  if (communicationEnabled)
    sendDataWifi(weight);
}

void connect()
{

  // Connect to Wifi.
  if (!production)
  {
    Serial.println();
    Serial.println();
    Serial.print("Connecting to ");
    Serial.println(WIFI_SSID);
  }

  // WiFi.begin(WIFI_SSID, WIFI_PASS);

  // WiFi fix: https://github.com/esp8266/Arduino/issues/2186
  WiFi.persistent(false);
  WiFi.mode(WIFI_OFF);
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASS);

  // unsigned long wifiConnectStart = millis();
  int _try = 0;

  while (WiFi.status() != WL_CONNECTED)
  {
    // WiFi.begin(WIFI_SSID, WIFI_PASS);

    Serial.print("..");
    delay(1000);
    _try++;

    if (_try >= NB_TRYWIFI)
    {
      Serial.println("Impossible to connect WiFi network, go to deep sleep");
      ESP.deepSleep(durationSleep * 1000);
    }
  }

  Serial.println("");
  Serial.println("WiFi connected");
  Serial.println("IP address: ");
  Serial.println(WiFi.localIP());
}

void connectAndSend(){
  if(WiFi.status() != WL_CONNECTED){
      connect();
    }

    measure_weight();

    initTime = millis();
}

void setup()
{

  Serial.begin(115200);
  scale.begin(SDA, SCL);
  pinMode(BUTTON_PIN, INPUT);

  // Wait for serial to initialize.
  while (!Serial)
  {
  }

  /*  if (SPIFFS.begin())
   {
     Serial.println("Done!");
   }
   else
   {
     Serial.println("Error");
   } */

  if (rtcMem.begin())
  {
    Serial.println("Memory initialization done!");
    CollectorData *data = rtcMem.getData<CollectorData>();
    scale.set_offset(data->offset);
  }
  else
  {
    Serial.println("No previous data found. The memory is reset to zeros!");
  }


  scale.set_scale(calibration_factor);

  //initTime = millis();
  connectAndSend();
}

void loop()
{

  // read the state of the switch/button:
  currentState = digitalRead(BUTTON_PIN);


  if(lastState == HIGH && currentState == LOW)        // button is pressed
    pressedTime = millis();
  else if(lastState == LOW && currentState == HIGH) { // button is released
    releasedTime = millis();

    long pressDuration = releasedTime - pressedTime;

    if( pressDuration < SHORT_PRESS_TIME )
      Serial.println("A short press is detected");

  
    if( pressDuration > LONG_PRESS_TIME ){
      Serial.println("A long press is detected");
      tare_sensor(scale);
      connectAndSend();
    }
    
  }else if(lastState== LOW && currentState == LOW && millis() - initTime > durationSleep * 1000){
    // button is not pushed and time is over 
    // do the measure and send it
    connectAndSend();
  }

  // save the the last state
  lastState = currentState;
}