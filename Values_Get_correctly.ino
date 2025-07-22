#include <Arduino.h>
#include <ESP8266WiFi.h>
#include "HX711.h"
#include <ESP8266Firebase.h> 
#include <NTPClient.h>       
#include <WiFiUdp.h>       

// --- WiFi and Firebase Configuration ---
#define WIFI_SSID "CHANDUPA_PC"         
#define WIFI_PASSWORD "1234567890"      
#define REFERENCE_URL "https://grainguard-1cbcb-default-rtdb.firebaseio.com/" 

const String USER_ID = "Fr7Ycngqu1pTVrZK6E0A"; //  user ID
const String CONTAINER_TYPE = "rice";       // container (e.g., "rice", "dal", etc.)

Firebase firebase(REFERENCE_URL); 

const int LOADCELL_DOUT_PIN = 12; 
const int LOADCELL_SCK_PIN = 13;  

HX711 scale; 

static float calibration_factor = -1000; // Initial calibration for get empty value 0g


WiFiUDP ntpUDP;

NTPClient timeClient(ntpUDP, "pool.ntp.org", 19800, 60000); // Update every 60 seconds

float last_uploaded_weight = -999.0; 
const float WEIGHT_CHANGE_THRESHOLD = 1.0; 

void setup() {
  Serial.begin(9600);
  while (!Serial);
  Serial.println("\n--- ESP8266 Weight to Firebase Tool ---");

  // --- WiFi Connection Setup ---
  Serial.print("Connecting to WiFi");
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  while (WiFi.status() != WL_CONNECTED) {
    Serial.print(".");
    delay(500);
  }
  Serial.println();
  Serial.println("WiFi Connected!");
  Serial.print("IP Address: ");
  Serial.println(WiFi.localIP());

  Serial.println("Initializing NTP client...");
  timeClient.begin();

  while(!timeClient.update()) {
    Serial.print("Waiting for NTP sync...");
    delay(1000);
  }
  Serial.println("NTP time synchronized!");

  // --- Firebase Connection Test ---
  Serial.println("Attempting to connect to Firebase...");
  if (firebase.setString("ConnectionTest", "Testing")) {
    Serial.println("Firebase write successful!");
    String readValue = firebase.getString("ConnectionTest");
    if (readValue == "Testing") {
      Serial.println("Firebase read successful!");
      Serial.println("Firebase connection verified!");
      firebase.setString("Status", "Device Connected"); 
    } else {
      Serial.println("Failed to read from Firebase - connection issue.");
      Serial.print("Received value: ");
      Serial.println(readValue);
    }
  } else {
    Serial.println("Failed to write to Firebase - check URL/rules.");
  }


  pinMode(LOADCELL_SCK_PIN, OUTPUT);
  digitalWrite(LOADCELL_SCK_PIN, LOW);

  scale.begin(LOADCELL_DOUT_PIN, LOADCELL_SCK_PIN);

  scale.power_down();
  delay(500);
  scale.power_up();

  Serial.println("\n--- HX711 Initial Raw Value Check (no weight) ---");
  for (int i = 0; i < 5; i++) { // Read 5 raw values
    Serial.print("Raw reading ");
    Serial.print(i + 1);
    Serial.print(": ");
    Serial.println(scale.read());
    delay(200);
  }

  // (set current reading as zero)
  Serial.println("\nTaring the scale (removing current weight as zero)...");
  scale.tare(20); 

  scale.set_scale(calibration_factor); 

  Serial.println("\n--- Ready for Weight Measurement ---");
  Serial.println("Place known weight to calibrate.");
  Serial.println("Commands: '+' = increase factor by 100, '-' = decrease factor by 100, 't' = tare.");
}

void loop() {

  float units = scale.get_units(10);
  float weight_grams = units * 3.12; 

  
  Serial.print("Weight: ");
  Serial.print(weight_grams, 2); // 0.00 decimal
  Serial.print("g");
  Serial.print(" | Factor: ");
  Serial.println(calibration_factor);

  String userContainerPath = "users/" + USER_ID + "/containers/" + CONTAINER_TYPE + "/";

  if (WiFi.status() == WL_CONNECTED) {
    if (!firebase.setFloat(userContainerPath + "currentWeight", weight_grams)) {
      Serial.println("Firebase 'currentWeight' upload failed!");
    }
  } else {
    Serial.println("WiFi disconnected. Attempting to reconnect...");
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
    delay(1000);
  }

  if (WiFi.status() == WL_CONNECTED && (abs(weight_grams - last_uploaded_weight) >= WEIGHT_CHANGE_THRESHOLD || last_uploaded_weight == -999.0)) {
    timeClient.update(); 
    unsigned long epochTime = timeClient.getEpochTime();

    char timestampBuffer[25];
    time_t rawtime = epochTime;
    struct tm * ti = localtime(&rawtime); // Use localtime 

    sprintf(timestampBuffer, "%04d-%02d-%02d_%02d-%02d-%02d",
            ti->tm_year + 1900, ti->tm_mon + 1, ti->tm_mday,
            ti->tm_hour, ti->tm_min, ti->tm_sec);

    String historyPath = userContainerPath + "weightHistory/" + String(timestampBuffer);

    if (firebase.setFloat(historyPath, weight_grams)) {
      Serial.print("History uploaded: ");
      Serial.print(timestampBuffer);
      Serial.print(" -> ");
      Serial.print(weight_grams, 2);
      Serial.println("g");
      last_uploaded_weight = weight_grams; 
    } else {
      Serial.println("Firebase 'weightHistory' upload failed!");
    }
  }

  if (Serial.available()) {
    char command = Serial.read();
    switch (command) {
      case '+':
        calibration_factor += 100;
        scale.set_scale(calibration_factor);
        Serial.print("New factor: ");
        Serial.println(calibration_factor);
        break;
      case '-':
        calibration_factor -= 100;
        scale.set_scale(calibration_factor);
        Serial.print("New factor: ");
        Serial.println(calibration_factor);
        break;
      case 't':
        Serial.println("Taring...");
        scale.tare(20);
        Serial.println("Tare complete.");
        last_uploaded_weight = -999.0;
        break;
      default:
        break;
    }
  }

  
  delay(2000);
}