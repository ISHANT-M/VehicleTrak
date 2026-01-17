#define ENABLE_USER_AUTH
#define ENABLE_DATABASE

#include <Arduino.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <FirebaseClient.h>   // Firebase library
#include <Wire.h>
#include "RTClib.h"           // DS3231 RTC library
#include <time.h>             // For NTP time

// ------------------- USER CONFIG (replace these) -------------------
#define WIFI_SSID " "
#define WIFI_PASSWORD " "

#define Web_API_KEY    " "
#define DATABASE_URL   " "
#define USER_EMAIL     " "
#define USER_PASS      " "
// ------------------------------------------------------------------

// ------------------- Hardware pins -------------------
#define DO_PIN 15      // MQ-2 digital output (LOW = gas present)
#define EMER_PIN 23    // Emergency switch (INPUT_PULLUP). Connect switch between pin & GND
// -----------------------------------------------------

// Firebase/auth objects
UserAuth user_auth(Web_API_KEY, USER_EMAIL, USER_PASS);
FirebaseApp app;
WiFiClientSecure ssl_client;
using AsyncClient = AsyncClientClass;
AsyncClient aClient(ssl_client);
RealtimeDatabase Database;

// RTC object
RTC_DS3231 rtc;

// JSON helper objects
object_t jsonData, obj1, obj2, obj3;
JsonWriter writer;

// Timing control
unsigned long lastSendTime = 0;
const unsigned long sendInterval = 6000UL; // 0.1 minute

// Runtime variables
String uid;
String databasePath;
String parentPath;
unsigned long timestampEpoch = 0;

// Forward declaration
void processData(AsyncResult &aResult);

// ------------------- helper: initialize WiFi -------------------
void initWiFi() {
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  Serial.print("Connecting to WiFi ..");
  while (WiFi.status() != WL_CONNECTED) {
    Serial.print('.');
    delay(500);
  }
  Serial.println("\nWiFi connected. IP: " + WiFi.localIP().toString());
}

// ------------------- helper: get epoch from RTC ----------------
unsigned long getEpochFromRTC() {
  DateTime now = rtc.now();
  return (unsigned long)now.unixtime(); // seconds since 1970 (UTC)
}

// ------------------- setup() -----------------------------------
void setup() {
  Serial.begin(115200);
  delay(100);

  Wire.begin();

  // Initialize MQ-2
  pinMode(DO_PIN, INPUT);
  Serial.println("Warming up MQ-2 sensor (20s)...");
  delay(20000);

  // Initialize emergency switch (with built-in pull-up)
  pinMode(EMER_PIN, INPUT_PULLUP);

  // Initialize RTC
  if (!rtc.begin()) {
    Serial.println("Couldn't find RTC (DS3231)!");
  }

  // Initialize WiFi
  initWiFi();

  // ----- NEW: Sync time from NTP (UTC) and always update RTC -----
  Serial.println("Syncing time with NTP (UTC)...");
  configTime(0, 0, "pool.ntp.org", "time.nist.gov");

  // wait up to 10 seconds for NTP
  unsigned long startMillis = millis();
  time_t now = time(nullptr);
  while (now < 1600000000UL && (millis() - startMillis) < 10000UL) {
    Serial.print('.');
    delay(500);
    now = time(nullptr);
  }
  Serial.println();

  if (now >= 1600000000UL) { // valid NTP epoch
    Serial.print("NTP epoch (UTC): ");
    Serial.println((unsigned long)now);

    // update RTC every boot
    rtc.adjust(DateTime((uint32_t)now));
    Serial.println("RTC updated from NTP (UTC).");
  } else {
    Serial.println("Failed to get NTP time; RTC not updated.");
  }
  // -----------------------------------------------------------------

  // Configure SSL client
  ssl_client.setInsecure();
  ssl_client.setHandshakeTimeout(5);

  // Initialize Firebase
  initializeApp(aClient, app, getAuth(user_auth), processData, "🔐 authTask");
  app.getApp<RealtimeDatabase>(Database);
  Database.url(DATABASE_URL);

  Serial.println("Setup complete.");
}

// ------------------- loop() ------------------------------------
void loop() {
  // Maintain Firebase background tasks
  app.loop();

  if (app.ready()) {
    unsigned long nowMillis = millis();
    if (nowMillis - lastSendTime >= sendInterval) {
      lastSendTime = nowMillis;

      uid = app.getUid().c_str();
      databasePath = "/UsersData/" + uid + "/readings";

      // Get timestamp from RTC
      timestampEpoch = getEpochFromRTC();
      Serial.print("Epoch timestamp: ");
      Serial.println(timestampEpoch);

      // MQ-2 gas state
      bool smoking = (digitalRead(DO_PIN) == LOW); // LOW = gas present

      // Emergency switch state
      bool emergency_press = (digitalRead(EMER_PIN) == LOW); // LOW = pressed

      // Database path
      parentPath = databasePath + "/" + String(timestampEpoch);

      // Create JSON
      writer.create(obj1, "/smoking", smoking);
      writer.create(obj2, "/timestamp", timestampEpoch);
      writer.create(obj3, "/emergency_press", emergency_press);
      writer.join(jsonData, 3, obj1, obj2, obj3);

      // Upload to Firebase
      Database.set<object_t>(aClient, parentPath, jsonData, processData, "RTDB_Send_Data");

      // Debug
      Serial.print("smoking: "); Serial.print(smoking ? "YES" : "NO");
      Serial.print(" | emergency_press: "); Serial.print(emergency_press ? "YES" : "NO");
      Serial.print(" | timestamp: "); Serial.println(timestampEpoch);
    }
  }

  delay(50);
}

// ----------------- Firebase callback -------------------
void processData(AsyncResult &aResult) {
  if (!aResult.isResult())
    return;

  if (aResult.isEvent())
    Firebase.printf("Event: %s, msg: %s, code: %d\n",
                    aResult.uid().c_str(),
                    aResult.eventLog().message().c_str(),
                    aResult.eventLog().code());

  if (aResult.isDebug())
    Firebase.printf("Debug: %s, msg: %s\n",
                    aResult.uid().c_str(),
                    aResult.debug().c_str());

  if (aResult.isError())
    Firebase.printf("Error: %s, msg: %s, code: %d\n",
                    aResult.uid().c_str(),
                    aResult.error().message().c_str(),
                    aResult.error().code());

  if (aResult.available())
    Firebase.printf("Task: %s, payload: %s\n",
                    aResult.uid().c_str(),
                    aResult.c_str());
}


//