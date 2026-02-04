// IOT-BASED TECHNOLOGIES FOR PRECISION AGRICULTURE AND ENVIRONMENTAL MONITORING 
// Forest Fire Detection System with Multi-Sensor Integration & ThingsBoard
// ESP32 DevKit v1 on Wokwi Platform
// TESTING VERSION - Multi-tier Alert System with Cloud Integration
// Author: Kalaivathanan
// Date: 2025

#include <WiFi.h>
#include <PubSubClient.h>
#include <DHT.h>
#include <ArduinoJson.h>

// ============== PIN DEFINITIONS ==============
#define DHTPIN 15           // DHT22 sensor pin
#define MQ2_PIN 32          // MQ-2 gas sensor (combustible gases) ADC1_4
#define MQ7_PIN 34          // MQ-7 CO sensor ADC1_6
#define SMOKE_PIN 13        // Smoke detector digital input
#define LED_LEVEL1_PIN 2    // Green LED - Level 1 Early Warning
#define LED_LEVEL2_PIN 4    // Yellow LED - Level 2 Elevated Risk
#define LED_LEVEL3_PIN 5    // Red LED - Level 3 Critical Fire
#define BUZZER_PIN 12       // Buzzer for audio alert
#define DHTTYPE DHT22       // DHT sensor type

// ============== WIFI & MQTT CONFIGURATION ==============
const char* ssid = "Wokwi-GUEST";
const char* password = "";

// ThingsBoard Configuration
const char* mqtt_server = "demo.thingsboard.io";
const int mqtt_port = 1883;
const char* mqtt_device_id = "b80bc3e0-9d62-11f0-a9b5-792e2194a5d4";  // Change to your device ID
const char* mqtt_access_token = "xdgPBhdrpVLB1ro6DNJp";    // Replace with your ThingsBoard token

// ============== SENSOR OBJECTS ==============
DHT dht(DHTPIN, DHTTYPE);
WiFiClient espClient;
PubSubClient client(espClient);

// ============== TIMING PARAMETERS ==============
const unsigned long SENSOR_INTERVAL = 30000;     // 30 seconds sampling (faster for fire detection)
const unsigned long LOOP_INTERVAL = 1000;        // Main loop interval
const unsigned long MQTT_RECONNECT_INTERVAL = 10000;  // Try reconnect every 10 seconds
unsigned long lastSensorTime = 0;
unsigned long lastMqttReconnectTime = 0;
const int ADC_MAX = 4095;

// ============== FIRE DETECTION THRESHOLDS ==============
const float TEMP_THRESHOLD = 40.0;               // Temperature threshold for fire detection
const float HUMIDITY_THRESHOLD = 30.0;           // Low humidity threshold
const int GAS_BASELINE_INCREASE = 20;            // 20% above baseline for gas detection
const int MQ2_BASELINE = 50;                     // MQ-2 baseline in clean air
const int MQ7_BASELINE = 50;                     // MQ-7 baseline in clean air

// ============== SLIDING WINDOW FOR TEMPORAL ANALYSIS ==============
const int WINDOW_SIZE = 10;                      // Store last 10 readings (300 seconds)
float tempWindow[WINDOW_SIZE] = {0};
float humidityWindow[WINDOW_SIZE] = {0};
int mq2Window[WINDOW_SIZE] = {0};
int mq7Window[WINDOW_SIZE] = {0};
bool smokeWindow[WINDOW_SIZE] = {false};
int windowIndex = 0;

// ============== ALERT COUNTERS & FLAGS ==============
int level1AlertCount = 0;
int level2AlertCount = 0;
int level3AlertCount = 0;

bool level1AlertActive = false;  // Early warning
bool level2AlertActive = false;  // Elevated risk
bool level3AlertActive = false;  // Critical fire

bool motorRunning = false;
unsigned long motorStartTime = 0;

// ============== SENSOR DATA STRUCTURE ==============
struct FireSensorData {
  float temperature;
  float humidity;
  int mq2_raw;              // MQ-2 raw ADC
  int mq7_raw;              // MQ-7 raw ADC
  float mq2_ppm;            // MQ-2 PPM concentration
  float mq7_ppm;            // MQ-7 PPM concentration
  bool smokeDetected;
  float tempRate;           // Temperature rate of change
  int fireIndicators;       // Count of active fire indicators
  int alertLevel;           // 0=None, 1=Early, 2=Elevated, 3=Critical
  unsigned long timestamp;
};

FireSensorData currentData;

// ============== FIRE INDICATOR STRUCTURE ==============
struct FireIndicators {
  bool tempHigh;            // Temp > 40°C
  bool humidityLow;         // Humidity < 30%
  bool mq2Elevated;         // MQ-2 gas elevated
  bool mq7Elevated;         // MQ-7 CO elevated
  bool smokePresent;        // Smoke detected
};

FireIndicators indicators;

// ============== FUNCTION PROTOTYPES ==============
void setup_wifi();
void reconnect();
void callback(char* topic, byte* payload, unsigned int length);
void readSensors();
void updateSlidingWindow(float temp, float humidity, int mq2, int mq7, bool smoke);
float convertMQ2ToPPM(int rawValue);
float convertMQ7ToPPM(int rawValue);
float calculateTemperatureRateOfChange();
void evaluateFireIndicators();
void checkFireAlertLevels();
void controlAlertLEDs();
void controlBuzzer();
void publishDataToThingsBoard();

// ============== SETUP FUNCTION ==============
void setup() {
  Serial.begin(115200);
  delay(1000);
  
  Serial.println("\n\n==========================================");
  Serial.println("Forest Fire Detection System");
  Serial.println("ESP32 Multi-Sensor Fire Monitoring");
  Serial.println("ThingsBoard Cloud Integration");
  Serial.println("==========================================\n");
  
  // Initialize GPIO pins - LEDs
  pinMode(LED_LEVEL1_PIN, OUTPUT);
  digitalWrite(LED_LEVEL1_PIN, LOW);
  
  pinMode(LED_LEVEL2_PIN, OUTPUT);
  digitalWrite(LED_LEVEL2_PIN, LOW);
  
  pinMode(LED_LEVEL3_PIN, OUTPUT);
  digitalWrite(LED_LEVEL3_PIN, LOW);
  
  // Initialize buzzer
  pinMode(BUZZER_PIN, OUTPUT);
  digitalWrite(BUZZER_PIN, LOW);
  
  // Initialize smoke detector input
  pinMode(SMOKE_PIN, INPUT);
  
  Serial.println("[SETUP] GPIO pins initialized");
  Serial.println("[SETUP] - GPIO 2: LED Level 1 (Green)");
  Serial.println("[SETUP] - GPIO 4: LED Level 2 (Yellow)");
  Serial.println("[SETUP] - GPIO 5: LED Level 3 (Red)");
  Serial.println("[SETUP] - GPIO 12: Buzzer");
  Serial.println("[SETUP] - GPIO 13: Smoke Detector (Input)");
  
  // Initialize DHT sensor
  dht.begin();
  Serial.println("[SETUP] DHT22 sensor initialized on GPIO 15");
  
  // Initialize WiFi
  setup_wifi();
  
  // Setup MQTT
  client.setServer(mqtt_server, mqtt_port);
  client.setBufferSize(1024);
  client.setCallback(callback);
  
  // Initialize sliding window
  for(int i = 0; i < WINDOW_SIZE; i++) {
    tempWindow[i] = 0;
    humidityWindow[i] = 0;
    mq2Window[i] = 0;
    mq7Window[i] = 0;
    smokeWindow[i] = false;
  }
  
  Serial.println("[SETUP] Sensor buffers initialized");
  Serial.println("[SETUP] System ready - Monitoring for fire conditions\n");
}

// ============== WIFI SETUP ==============
void setup_wifi() {
  delay(10);
  Serial.println("\n[WiFi] Starting WiFi connection...");
  Serial.print("[WiFi] Connecting to: ");
  Serial.println(ssid);
  
  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, password);
  
  int attempts = 0;
  while (WiFi.status() != WL_CONNECTED && attempts < 20) {
    delay(500);
    Serial.print(".");
    attempts++;
  }
  
  Serial.println();
  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("[WiFi] WiFi connected successfully");
    Serial.print("[WiFi] IP Address: ");
    Serial.println(WiFi.localIP());
  } else {
    Serial.println("[WiFi] WiFi connection failed - will retry");
  }
}

// ============== MQTT RECONNECT ==============
void reconnect() {
  if (!client.connected()) {
    Serial.print("[MQTT] Attempting connection to ThingsBoard...");
    
    // MQTT connection with access token as password
    if (client.connect(mqtt_device_id, mqtt_access_token, "")) {
      Serial.println(" CONNECTED");
      Serial.println("[MQTT] Connected to ThingsBoard");
      
      // Subscribe to commands topic (for future use)
      client.subscribe("v1/devices/me/rpc/request/+");
      
    } else {
      Serial.print(" FAILED (rc=");
      Serial.print(client.state());
      Serial.println(")");
    }
  }
}

// ============== MQTT CALLBACK ==============
void callback(char* topic, byte* payload, unsigned int length) {
  Serial.print("[MQTT] Message received on topic: ");
  Serial.println(topic);
}

// ============== READ SENSORS ==============
void readSensors() {
  // Read DHT22 (temperature and humidity)
  float humidity = dht.readHumidity();
  float temperature = dht.readTemperature();
  
  // Check DHT read errors
  if (isnan(humidity) || isnan(temperature)) {
    Serial.println("[ERROR] DHT22 sensor read failed");
    return;
  }
  
  // Read MQ-2 gas sensor (combustible gases) - GPIO 32
  int mq2_raw = analogRead(MQ2_PIN);
  float mq2_ppm = convertMQ2ToPPM(mq2_raw);
  
  // Read MQ-7 CO sensor - GPIO 34
  int mq7_raw = analogRead(MQ7_PIN);
  float mq7_ppm = convertMQ7ToPPM(mq7_raw);
  
  // Read smoke detector - GPIO 13 (HIGH = smoke detected)
  bool smokeDetected = digitalRead(SMOKE_PIN);
  
  // Update sliding window
  updateSlidingWindow(temperature, humidity, mq2_raw, mq7_raw, smokeDetected);
  
  // Store in current data structure
  currentData.temperature = temperature;
  currentData.humidity = humidity;
  currentData.mq2_raw = mq2_raw;
  currentData.mq7_raw = mq7_raw;
  currentData.mq2_ppm = mq2_ppm;
  currentData.mq7_ppm = mq7_ppm;
  currentData.smokeDetected = smokeDetected;
  currentData.tempRate = calculateTemperatureRateOfChange();
  currentData.timestamp = millis();
  
  // Debug output
  Serial.println("\n========== FIRE SENSOR READINGS ==========");
  Serial.print("Temperature: ");
  Serial.print(temperature, 2);
  Serial.println(" °C");
  Serial.print("Humidity: ");
  Serial.print(humidity, 2);
  Serial.println(" %");
  Serial.print("Temp Rate of Change: ");
  Serial.print(currentData.tempRate, 2);
  Serial.println(" °C/min");
  
  Serial.print("MQ-2 Gas (Raw): ");
  Serial.print(mq2_raw);
  Serial.print(" ADC | PPM: ");
  Serial.println(mq2_ppm, 2);
  
  Serial.print("MQ-7 CO (Raw): ");
  Serial.print(mq7_raw);
  Serial.print(" ADC | PPM: ");
  Serial.println(mq7_ppm, 2);
  
  Serial.print("Smoke Detected: ");
  Serial.println(smokeDetected ? "YES" : "NO");
  Serial.println("=========================================");
}

// ============== MQ-2 CALIBRATION: PPM CONVERSION ==============
// MQ-2 detects combustible gases (LPG, Propane, Alcohol, Smoke)
float convertMQ2ToPPM(int rawValue) {
  // Formula: ppm = A × (Rs/R0)^B
  // For MQ-2: A = 574, B = -2.22
  float voltage = (float)rawValue / ADC_MAX * 3.3;
  
  // Convert voltage to PPM (simplified)
  // Higher voltage = more gas
  float ppm = (voltage / 3.3) * 500.0;  // Scale to max 500 PPM
  
  return constrain(ppm, 0, 500);
}

// ============== MQ-7 CALIBRATION: CO PPM CONVERSION ==============
// MQ-7 detects carbon monoxide (CO)
float convertMQ7ToPPM(int rawValue) {
  // Formula: ppm = A × (Rs/R0)^B
  // For MQ-7: A = 99.04, B = -1.52
  float voltage = (float)rawValue / ADC_MAX * 3.3;
  
  // Convert voltage to PPM (simplified)
  // CO is earlier indicator than temperature
  float ppm = (voltage / 3.3) * 100.0;  // Scale to max 100 PPM
  
  return constrain(ppm, 0, 100);
}

// ============== SLIDING WINDOW UPDATE ==============
void updateSlidingWindow(float temp, float humidity, int mq2, int mq7, bool smoke) {
  tempWindow[windowIndex] = temp;
  humidityWindow[windowIndex] = humidity;
  mq2Window[windowIndex] = mq2;
  mq7Window[windowIndex] = mq7;
  smokeWindow[windowIndex] = smoke;
  
  windowIndex = (windowIndex + 1) % WINDOW_SIZE;
}

// ============== CALCULATE TEMPERATURE RATE OF CHANGE ==============
// Rapid temperature increase indicates active fire
float calculateTemperatureRateOfChange() {
  if (windowIndex == 0) return 0;  // Not enough data
  
  // Compare current temp with temp from 5 minutes ago
  int oldIndex = (windowIndex - 10 + WINDOW_SIZE) % WINDOW_SIZE;
  float tempDifference = currentData.temperature - tempWindow[oldIndex];
  
  // Convert to rate of change per minute
  // 10 readings × 30 seconds = 300 seconds = 5 minutes
  float tempRate = (tempDifference / 5.0);
  
  return tempRate;
}

// ============== EVALUATE FIRE INDICATORS ==============
void evaluateFireIndicators() {
  indicators.tempHigh = (currentData.temperature > TEMP_THRESHOLD);
  indicators.humidityLow = (currentData.humidity < HUMIDITY_THRESHOLD);
  indicators.mq2Elevated = (currentData.mq2_raw > (MQ2_BASELINE + GAS_BASELINE_INCREASE));
  indicators.mq7Elevated = (currentData.mq7_raw > (MQ7_BASELINE + GAS_BASELINE_INCREASE));
  indicators.smokePresent = currentData.smokeDetected;
  
  // Count active indicators
  currentData.fireIndicators = 0;
  if (indicators.tempHigh) currentData.fireIndicators++;
  if (indicators.humidityLow) currentData.fireIndicators++;
  if (indicators.mq2Elevated) currentData.fireIndicators++;
  if (indicators.mq7Elevated) currentData.fireIndicators++;
  if (indicators.smokePresent) currentData.fireIndicators++;
}

// ============== CHECK FIRE ALERT LEVELS ==============
void checkFireAlertLevels() {
  evaluateFireIndicators();
  
  // ========== LEVEL 1: EARLY WARNING ==========
  // ANY 2 of 4 indicators for 5+ consecutive readings
  if ((indicators.tempHigh || indicators.humidityLow || 
       indicators.mq2Elevated || indicators.mq7Elevated) &&
      currentData.fireIndicators >= 2) {
    level1AlertCount++;
    
    if (level1AlertCount >= 5) {  // 5 readings = 150 seconds = 2.5 minutes
      if (!level1AlertActive) {
        level1AlertActive = true;
        Serial.println("\n!!! LEVEL 1 ALERT: EARLY WARNING !!!");
        Serial.println(">>> Fire risk conditions detected");
        Serial.print(">>> Active indicators: ");
        Serial.println(currentData.fireIndicators);
      }
      currentData.alertLevel = 1;
    }
  } else {
    level1AlertCount = 0;
    level1AlertActive = false;
  }
  
  // ========== LEVEL 2: ELEVATED RISK ==========
  // ANY 3 of 4 indicators for 2+ consecutive readings
  if (currentData.fireIndicators >= 3) {
    level2AlertCount++;
    
    if (level2AlertCount >= 2) {  // 2 readings = 60 seconds
      if (!level2AlertActive) {
        level2AlertActive = true;
        Serial.println("\n!!! LEVEL 2 ALERT: ELEVATED RISK !!!");
        Serial.println(">>> Strong fire conditions detected");
        Serial.print(">>> Active indicators: ");
        Serial.println(currentData.fireIndicators);
      }
      currentData.alertLevel = 2;
      level1AlertActive = true;  // Keep Level 1 active
    }
  } else {
    level2AlertCount = 0;
    level2AlertActive = false;
  }
  
  // ========== LEVEL 3: CRITICAL/CONFIRMED FIRE ==========
  // ALL 4 indicators for 1+ consecutive reading
  if (currentData.fireIndicators >= 4) {
    level3AlertCount++;
    
    if (level3AlertCount >= 1) {  // Immediate activation
      if (!level3AlertActive) {
        level3AlertActive = true;
        Serial.println("\n!!! LEVEL 3 ALERT: CRITICAL FIRE DETECTED !!!");
        Serial.println(">>> EMERGENCY: CONFIRMED FIRE CONDITIONS");
        Serial.println(">>> EMERGENCY RESPONSE REQUIRED");
        Serial.print(">>> ALL fire indicators active: ");
        Serial.println(currentData.fireIndicators);
      }
      currentData.alertLevel = 3;
      level1AlertActive = true;   // Keep Level 1 active
      level2AlertActive = true;   // Keep Level 2 active
    }
  } else {
    level3AlertCount = 0;
    level3AlertActive = false;
  }
  
  // Reset all if no danger
  if (currentData.fireIndicators < 2) {
    level1AlertActive = false;
    level2AlertActive = false;
    level3AlertActive = false;
    currentData.alertLevel = 0;
  }
}

// ============== CONTROL ALERT LEDS ==============
void controlAlertLEDs() {
  // Level 3 (Red) - Critical Fire
  if (level3AlertActive) {
    digitalWrite(LED_LEVEL3_PIN, HIGH);
    digitalWrite(LED_LEVEL2_PIN, HIGH);
    digitalWrite(LED_LEVEL1_PIN, HIGH);
    Serial.println("[LED STATUS] All LEDs: ON (Critical Fire)");
  }
  // Level 2 (Yellow) - Elevated Risk
  else if (level2AlertActive) {
    digitalWrite(LED_LEVEL3_PIN, LOW);
    digitalWrite(LED_LEVEL2_PIN, HIGH);
    digitalWrite(LED_LEVEL1_PIN, HIGH);
    Serial.println("[LED STATUS] Level 1 & 2 LEDs: ON (Elevated Risk)");
  }
  // Level 1 (Green) - Early Warning
  else if (level1AlertActive) {
    digitalWrite(LED_LEVEL3_PIN, LOW);
    digitalWrite(LED_LEVEL2_PIN, LOW);
    digitalWrite(LED_LEVEL1_PIN, HIGH);
    Serial.println("[LED STATUS] Level 1 LED: ON (Early Warning)");
  }
  // Normal
  else {
    digitalWrite(LED_LEVEL3_PIN, LOW);
    digitalWrite(LED_LEVEL2_PIN, LOW);
    digitalWrite(LED_LEVEL1_PIN, LOW);
  }
}

// ============== CONTROL BUZZER ==============
void controlBuzzer() {
  if (level3AlertActive) {
    // Rapid beeping for critical fire
    digitalWrite(BUZZER_PIN, HIGH);
    Serial.println("[BUZZER] CRITICAL - Continuous beep");
  } 
  else if (level2AlertActive) {
    // Medium frequency beeping
    digitalWrite(BUZZER_PIN, HIGH);
    Serial.println("[BUZZER] ELEVATED RISK - Beeping");
  }
  else if (level1AlertActive) {
    // Slow beeping for early warning
    digitalWrite(BUZZER_PIN, HIGH);
    Serial.println("[BUZZER] EARLY WARNING - Slow beep");
  }
  else {
    digitalWrite(BUZZER_PIN, LOW);
  }
}

// ============== PUBLISH DATA TO THINGSBOARD ==============
void publishDataToThingsBoard() {
  if (!client.connected()) {
    return;  // Skip publishing if not connected
  }
  
  // Create JSON payload
  StaticJsonDocument<512> doc;
  
  doc["temperature"] = currentData.temperature;
  doc["humidity"] = currentData.humidity;
  doc["mq2_ppm"] = currentData.mq2_ppm;
  doc["mq7_ppm"] = currentData.mq7_ppm;
  doc["smoke_detected"] = currentData.smokeDetected;
  doc["temp_rate_change"] = currentData.tempRate;
  doc["fire_indicators"] = currentData.fireIndicators;
  doc["alert_level"] = currentData.alertLevel;
  doc["level1_active"] = level1AlertActive;
  doc["level2_active"] = level2AlertActive;
  doc["level3_active"] = level3AlertActive;
  
  char buffer[512];
  serializeJson(doc, buffer);
  
  // Publish to ThingsBoard telemetry topic
  bool ok = client.publish("v1/devices/me/telemetry", buffer);

  Serial.print("[MQTT] publish = ");
  Serial.println(ok ? "OK" : "FAILED");

  if (!ok) {
    Serial.print("[MQTT] state = ");
    Serial.println(client.state());
  }
  
  Serial.println("\n[MQTT] Data published to ThingsBoard:");
  serializeJsonPretty(doc, Serial);
}

// ============== MAIN LOOP ==============
void loop() {
  // Maintain WiFi connection
  if (WiFi.status() != WL_CONNECTED) {
    setup_wifi();
  }
  
  // Maintain MQTT connection (try reconnect periodically)
  if (!client.connected()) {
    if (millis() - lastMqttReconnectTime >= MQTT_RECONNECT_INTERVAL) {
      lastMqttReconnectTime = millis();
      reconnect();
    }
  } else {
    client.loop();
  }
  
  // Read sensors every 30 seconds
  static unsigned long lastSensorTime = 0;
  
  if (millis() - lastSensorTime >= SENSOR_INTERVAL) {
    lastSensorTime = millis();
    
    readSensors();
    checkFireAlertLevels();
    controlAlertLEDs();
    controlBuzzer();
    publishDataToThingsBoard();
  }
  
  delay(LOOP_INTERVAL);
}