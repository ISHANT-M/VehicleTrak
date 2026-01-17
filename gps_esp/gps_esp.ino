/*
   ESP32 + GPS + Firebase Data Logging (UTC Epoch)
   -----------------------------------------------
   - Reads GPS data using TinyGPSPlus
   - Converts UTC date/time from GPS → epoch
   - Logs latitude, longitude, and UTC epoch timestamp to Firebase
   - Uploads data every 1 minute (60,000 ms)
   - Latitude & Longitude stored as DOUBLE with 5 decimal precision
*/

#define ENABLE_USER_AUTH
#define ENABLE_DATABASE

#include <Arduino.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <FirebaseClient.h>
#include <TinyGPSPlus.h>

// ===================== USER CONFIG =====================

// WiFi credentials
#define WIFI_SSID      " "
#define WIFI_PASSWORD  " "

// Firebase credentials
#define Web_API_KEY    " "
#define DATABASE_URL   " "
#define USER_EMAIL     " "
#define USER_PASS      " "

// GPS pins (ESP32)
static const int RXPin = 16;   // GPS TX → ESP32 RX
static const int TXPin = 17;   // GPS RX → ESP32 TX (optional)
static const uint32_t GPSBaud = 9600;

// ========================================================

// TinyGPSPlus object
TinyGPSPlus gps;
HardwareSerial SerialGPS(2);

// Firebase Authentication
UserAuth user_auth(Web_API_KEY, USER_EMAIL, USER_PASS);

// Firebase core components
FirebaseApp app;
WiFiClientSecure ssl_client;
using AsyncClient = AsyncClientClass;
AsyncClient aClient(ssl_client);
RealtimeDatabase Database;

// Store UID of the logged-in Firebase user
String uid;

// Firebase path
String databasePath;

// Paths for JSON structure
String latPath = "/latitude";
String lngPath = "/longitude";
String timePath = "/timestamp"; // will store epoch UTC

// JSON objects
object_t jsonData, obj1, obj2, obj3;
JsonWriter writer;

// Timing control
unsigned long lastSendTime = 0;
const unsigned long sendInterval = 60000; // 1 min (60,000 ms)


bool isLeapYear(int year) {
  return (year % 4 == 0 && year % 100 != 0) || (year % 400 == 0);
}


unsigned long gpsToEpoch(int year, int month, int day, int hour, int minute, int second) {

  int daysInMonth[] = {0, 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
  if (isLeapYear(year)) daysInMonth[2] = 29;

  // Count days since 1970
  unsigned long days = 0;


  for (int y = 1970; y < year; y++) {
    days += isLeapYear(y) ? 366 : 365;
  }


  for (int m = 1; m < month; m++) {
    days += daysInMonth[m];
  }


  days += day - 1;


  unsigned long epoch = days * 86400UL + hour * 3600UL + minute * 60UL + second;

  return epoch;
}

// Get GPS epoch in UTC
unsigned long getGPSEpochUTC() {
  if (gps.date.isValid() && gps.time.isValid()) {
    int year   = gps.date.year();
    int month  = gps.date.month();
    int day    = gps.date.day();
    int hour   = gps.time.hour();
    int minute = gps.time.minute();
    int second = gps.time.second();

    return gpsToEpoch(year, month, day, hour, minute, second);
  }
  return 0; // invalid
}

// Firebase callback
void processData(AsyncResult &aResult) {
  if (!aResult.isResult()) return;

  if (aResult.isEvent())
    Firebase.printf("Event: %s\n", aResult.eventLog().message().c_str());

  if (aResult.isError())
    Firebase.printf("Error: %s (code %d)\n",
                    aResult.error().message().c_str(),
                    aResult.error().code());

  if (aResult.available())
    Firebase.printf("Payload: %s\n", aResult.c_str());
}




void setup() {
  Serial.begin(115200);
  SerialGPS.begin(GPSBaud, SERIAL_8N1, RXPin, TXPin);

  // WiFi
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  Serial.print("Connecting to WiFi ..");
  while (WiFi.status() != WL_CONNECTED) {
    Serial.print('.');
    delay(1000);
  }
  Serial.println(" CONNECTED!");

  // SSL config
  ssl_client.setInsecure();
  ssl_client.setConnectionTimeout(1000);
  ssl_client.setHandshakeTimeout(5);

  // Firebase init
  initializeApp(aClient, app, getAuth(user_auth), processData, "authTask");
  app.getApp<RealtimeDatabase>(Database);
  Database.url(DATABASE_URL);

  Serial.println("Setup complete. Waiting for GPS...");
}






void loop() {
  // Maintain Firebase session
  app.loop();

  // Feed GPS data continuously
  while (SerialGPS.available() > 0) {
    gps.encode(SerialGPS.read());
  }

  // Only upload if authenticated AND GPS valid
  if (app.ready() && gps.location.isValid() && gps.date.isValid() && gps.time.isValid()) {
    unsigned long currentTime = millis();

    if (currentTime - lastSendTime >= sendInterval) {
      lastSendTime = currentTime;

      uid = app.getUid().c_str();
      databasePath = "/UsersData/" + uid + "/gpsLogs";

      // Get GPS data
      double latitude  = gps.location.lat();
      double longitude = gps.location.lng();
      unsigned long utcEpoch = getGPSEpochUTC();

      // Round latitude & longitude to 5 decimal places (still double)
      latitude  = ((long)(latitude  * 100000)) / 100000.0;
      longitude = ((long)(longitude * 100000)) / 100000.0;

      // Debug print
      Serial.println("---- GPS Data ----");
      Serial.print("Latitude : "); Serial.println(latitude, 5);
      Serial.print("Longitude: "); Serial.println(longitude, 5);
      Serial.print("UTC Epoch: "); Serial.println(utcEpoch);

      // Create JSON payload
      writer.create(obj1, latPath, "0.0000000");   // double with 5 decimals
      writer.create(obj2, lngPath, longitude);  // double with 5 decimals
      writer.create(obj3, timePath, utcEpoch);  // epoch UTC
      writer.join(jsonData, 3, obj1, obj2, obj3);

      String parentPath = databasePath + "/" + String(millis()); // unique key

      // Push to Firebase
      Database.set<object_t>(aClient, parentPath, jsonData, processData, "GPS_Data");
    }
  }

  // Failsafe: detect no GPS
  if (millis() > 10000 && gps.charsProcessed() < 10) {
    Serial.println("No GPS detected: check wiring.");
    while (true);
  }
}