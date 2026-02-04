// IOT-BASED TECHNOLOGIES FOR PRECISION AGRICULTURE AND ENVIRONMENTAL MONITORING 
// Soil Moisture Monitoring System with Temperature, Humidity & Motor Control
// ESP32 DevKit v1 on Wokwi Platform
// Author: Kalaivathanan
// Date: 2025

#include <WiFi.h>
#include <PubSubClient.h>
#include <DHT.h>
#include <ArduinoJson.h>
#include <time.h>


// ============== PIN DEFINITIONS ==============
#define DHTPIN 12           // DHT22 sensor pin
#define SOIL_MOISTURE_PIN 32 // Soil moisture analog pin (ADC1_4)
#define PH_SENSOR_PIN 34    // pH sensor analog pin (ADC1_6)
#define MOTOR_PIN 2         // Motor/Relay control pin
#define ALERT_LED_PIN 4     // Yellow LED alert pin
#define BUZZER_PIN 5        // Buzzer pin
#define DHTTYPE DHT22       // DHT sensor type

// ============== SENSOR OBJECTS ==============
DHT dht(DHTPIN, DHTTYPE);
WiFiClient espClient;
PubSubClient client(espClient);

// ============== CONFIGURATION ==============

  const char* ssid = "Wokwi-GUEST";
  const char* password = "";

  const char* mqtt_server = "demo.thingsboard.io";
  const int mqtt_port = 1883;
  const char* mqtt_device_id = "1ca8d3e0-9d56-11f0-a9b5-792e2194a5d4";
  const char* mqtt_access_token = "KXE2eYJgRuLgRQWhomnS";


// ============== TIMING PARAMETERS ==============
const unsigned long SENSOR_INTERVAL = 60000;      // 60 seconds sampling
const unsigned long LOOP_INTERVAL = 1000;         // Main loop interval
const unsigned long MOTOR_RUNTIME = 300000;       // 5 minutes motor runtime
unsigned long lastSensorTime = 0;
unsigned long motorStartTime = 0;

// ============== THRESHOLD VALUES ==============
const float MOISTURE_THRESHOLD_LOW = 30.0;
const float MOISTURE_THRESHOLD_HIGH = 35.0;
const float TEMP_HEAT_STRESS = 35.0;
const float HUMIDITY_HEAT_STRESS = 40.0;
const float PH_MIN = 6.0;
const float PH_MAX = 7.5;
const int ADC_MAX = 4095;

// ============== MOVING AVERAGE FILTER ==============
const int BUFFER_SIZE = 2;
float moistureBuffer[BUFFER_SIZE] = {0};
float tempBuffer[BUFFER_SIZE] = {0};
float humidityBuffer[BUFFER_SIZE] = {0};
int bufferIndex = 0;

// ============== ALERT COUNTERS ==============
int lowMoistureCount = 0;
int heatStressCount = 0;
int phImbalanceCount = 0;

// ============== ALERT FLAGS ==============
bool irrigationAlertActive = false;
bool heatStressAlertActive = false;
bool phImbalanceAlertActive = false;
bool motorRunning = false;

// ============== SENSOR DATA STRUCTURE ==============
struct SensorData {
  float temperature;
  float humidity;
  float soilMoisture;
  float pH;
  float moistureFiltered;
  float tempFiltered;
  float humidityFiltered;
  bool irrigationAlert;
  bool heatStressAlert;
  bool phAlert;
  bool motorStatus;
  unsigned long timestamp;
};

SensorData currentData;

// ============== FUNCTION PROTOTYPES ==============
void setup_wifi();
void reconnect();
void callback(char* topic, byte* payload, unsigned int length);
void readSensors();
void updateMovingAverage(float moisture, float temp, float humidity);
float getFilteredMoisture();
float getFilteredTemp();
float getFilteredHumidity();
float calibrateMoisture(int adcValue);
float calibratePH(int adcValue);
void checkAlertConditions();
void controlAlerts();
void controlMotor();
void publishData();

// ============== SETUP FUNCTION ==============
void setup() {
  Serial.begin(115200);
  delay(1000);

  Serial.println("\n\n===================================");
  Serial.println("Soil Moisture Monitoring System");
  Serial.println("ESP32 Precision Irrigation Controller");
  Serial.println("===================================\n");

  // Initialize GPIO pins
  pinMode(MOTOR_PIN, OUTPUT);
  digitalWrite(MOTOR_PIN, LOW);

  pinMode(ALERT_LED_PIN, OUTPUT);
  digitalWrite(ALERT_LED_PIN, LOW);

  pinMode(BUZZER_PIN, OUTPUT);
  digitalWrite(BUZZER_PIN, LOW);

  Serial.println("GPIO pins initialized");

  // Initialize DHT sensor
  dht.begin();
  Serial.println("DHT22 sensor initialized");

  // Initialize WiFi
  
    setup_wifi();

    // Setup MQTT
    client.setServer(mqtt_server, mqtt_port);
    client.setBufferSize(1024);
    client.setCallback(callback);
  
  // Initialize sensor buffers
  for (int i = 0; i < BUFFER_SIZE; i++) {
    moistureBuffer[i] = 0;
    tempBuffer[i] = 0;
    humidityBuffer[i] = 0;
  }

  Serial.println("System initialized successfully\n");
}

// ============== WIFI SETUP ==============

  void setup_wifi() {
    delay(10);
    Serial.println("Starting WiFi connection...");
    Serial.print("Connecting to: ");
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
      Serial.println("WiFi connected successfully");
      Serial.print("IP Address: ");
      Serial.println(WiFi.localIP());
    } else {
      Serial.println("WiFi connection failed - will retry");
    }
  }

  // ============== MQTT RECONNECT ==============
  void reconnect() {
    if (!client.connected()) {
      Serial.print("[MQTT] Attempting connection to ThingsBoard...");

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
    Serial.print("MQTT Message: ");
    Serial.println(topic);
  }

// ============== READ SENSORS ==============
void readSensors() {
  // Read DHT22 (temperature and humidity)
  float humidity = dht.readHumidity();
  float temperature = dht.readTemperature();

  // Check DHT read errors
  if (isnan(humidity) || isnan(temperature)) {
    Serial.println("ERROR: DHT22 sensor read failed");
    return;
  }

  // Read soil moisture (analog)
  int moistureRaw = analogRead(SOIL_MOISTURE_PIN);
  float moisture = calibrateMoisture(moistureRaw);

  // Read pH sensor (analog)
  int phRaw = analogRead(PH_SENSOR_PIN);
  float ph = calibratePH(phRaw);

  // Update moving average
  updateMovingAverage(moisture, temperature, humidity);

  // Store in current data structure
  currentData.temperature = temperature;
  currentData.humidity = humidity;
  currentData.soilMoisture = moisture;
  currentData.pH = ph;
  currentData.moistureFiltered = getFilteredMoisture();
  currentData.tempFiltered = getFilteredTemp();
  currentData.humidityFiltered = getFilteredHumidity();
  currentData.timestamp = millis();

  // Debug output
  Serial.println("\n---------- SENSOR READINGS ----------");
  Serial.print("Temperature: ");
  Serial.print(temperature);
  Serial.println(" °C");
  Serial.print("Humidity: ");
  Serial.print(humidity);
  Serial.println(" %");
  Serial.print("Soil Moisture (Raw): ");
  Serial.print(moistureRaw);
  Serial.println(" ADC");
  Serial.print("Soil Moisture: ");
  Serial.print(moisture);
  Serial.println(" %");
  Serial.print("Soil Moisture (Filtered): ");
  Serial.print(currentData.moistureFiltered);
  Serial.println(" %");
  Serial.print("pH Level: ");
  Serial.println(ph);
  Serial.println("-------------------------------------");
}

// ============== CALIBRATION: MOISTURE ==============
float calibrateMoisture(int adcValue) {
  // Formula: Moisture % = 100 - ((ADC / 4095) * 100)
  float moisture = 100.0 - ((float)adcValue / ADC_MAX * 100.0);
  moisture = constrain(moisture, 0, 100);
  return moisture;
}

// ============== CALIBRATION: pH ==============
float calibratePH(int adcValue) {
  // Two-point calibration
  float voltage = (float)adcValue / ADC_MAX * 3.3;

  float pH4_voltage = 2.0;
  float pH7_voltage = 1.5;

  float voltsPerPH = (pH4_voltage - pH7_voltage) / (4.0 - 7.0);
  float pH = 7.0 - ((voltage - pH7_voltage) / voltsPerPH);

  pH = constrain(pH, 0, 14);
  return pH;
}

// ============== MOVING AVERAGE FILTER ==============
void updateMovingAverage(float moisture, float temp, float humidity) {
  moistureBuffer[bufferIndex] = moisture;
  tempBuffer[bufferIndex] = temp;
  humidityBuffer[bufferIndex] = humidity;

  bufferIndex = (bufferIndex + 1) % BUFFER_SIZE;
}

float getFilteredMoisture() {
  float sum = 0;
  for (int i = 0; i < BUFFER_SIZE; i++) {
    sum += moistureBuffer[i];
  }
  return sum / BUFFER_SIZE;
}

float getFilteredTemp() {
  float sum = 0;
  for (int i = 0; i < BUFFER_SIZE; i++) {
    sum += tempBuffer[i];
  }
  return sum / BUFFER_SIZE;
}

float getFilteredHumidity() {
  float sum = 0;
  for (int i = 0; i < BUFFER_SIZE; i++) {
    sum += humidityBuffer[i];
  }
  return sum / BUFFER_SIZE;
}

// ============== CHECK ALERT CONDITIONS ==============
void checkAlertConditions() {
  float filteredMoisture = getFilteredMoisture();
  float filteredTemp = getFilteredTemp();
  float filteredHumidity = getFilteredHumidity();

  // ========== ALERT 1: LOW SOIL MOISTURE ==========
  if (filteredMoisture < MOISTURE_THRESHOLD_LOW) {
    lowMoistureCount++;

    if (lowMoistureCount >= 3) {
      if (!irrigationAlertActive) {
        irrigationAlertActive = true;
        Serial.println("\n!!! ALERT 1: LOW SOIL MOISTURE - IRRIGATION REQUIRED !!!");
      }
      currentData.irrigationAlert = true;
    }
  } else if (filteredMoisture > MOISTURE_THRESHOLD_HIGH) {
    lowMoistureCount = 0;
    irrigationAlertActive = false;
    currentData.irrigationAlert = false;
  } else {
    lowMoistureCount++;
  }

  // ========== ALERT 2: HEAT STRESS ==========
  if (filteredTemp > TEMP_HEAT_STRESS && filteredHumidity < HUMIDITY_HEAT_STRESS) {
    heatStressCount++;

    if (heatStressCount >= 2) {
      if (!heatStressAlertActive) {
        heatStressAlertActive = true;
        Serial.println("\n!!! ALERT 2: HEAT STRESS CONDITIONS !!!");
        Serial.print("Temperature: ");
        Serial.print(filteredTemp);
        Serial.print("°C | Humidity: ");
        Serial.print(filteredHumidity);
        Serial.println("%");
      }
      currentData.heatStressAlert = true;
    }
  } else {
    heatStressCount = 0;
    heatStressAlertActive = false;
    currentData.heatStressAlert = false;
  }

  // ========== ALERT 3: pH IMBALANCE ==========
  if (currentData.pH < PH_MIN || currentData.pH > PH_MAX) {
    phImbalanceCount++;

    if (phImbalanceCount >= 5) {
      if (!phImbalanceAlertActive) {
        phImbalanceAlertActive = true;
        Serial.println("\n!!! ALERT 3: pH IMBALANCE !!!");
        Serial.print("Current pH: ");
        Serial.print(currentData.pH);
        Serial.print(" (Acceptable range: ");
        Serial.print(PH_MIN);
        Serial.print(" - ");
        Serial.print(PH_MAX);
        Serial.println(")");
      }
      currentData.phAlert = true;
    }
  } else {
    phImbalanceCount = 0;
    phImbalanceAlertActive = false;
    currentData.phAlert = false;
  }
}

// ============== CONTROL ALERTS (LED + BUZZER) ==============
void controlAlerts() {
  // Check if ANY alert is active
  bool anyAlertActive = irrigationAlertActive || heatStressAlertActive || phImbalanceAlertActive;

  if (anyAlertActive) {
    digitalWrite(ALERT_LED_PIN, HIGH);  // Yellow LED ON
    tone(BUZZER_PIN, 200);
    Serial.println("[ALERT ACTIVE] Yellow LED & Buzzer ON");
  } else {
    digitalWrite(ALERT_LED_PIN, LOW);   // Yellow LED OFF
    digitalWrite(BUZZER_PIN, LOW);       // Buzzer OFF
  noTone(BUZZER_PIN);
  }
}

// ============== CONTROL MOTOR ==============
void controlMotor() {
  unsigned long currentTime = millis();
  float filteredMoisture = getFilteredMoisture();

  // START MOTOR: if irrigation alert active and motor not running
  if (irrigationAlertActive && !motorRunning) {
    digitalWrite(MOTOR_PIN, HIGH);
    motorRunning = true;
    motorStartTime = currentTime;
    currentData.motorStatus = true;
    Serial.println("\n>>> RED LED ON - MOTOR STARTED");
    Serial.println(">>> Irrigation cycle initiated (5 minute runtime)");
  }

  // STOP MOTOR: if motor has been running for 5 minutes
  if (motorRunning) {
    unsigned long motorElapsedTime = currentTime - motorStartTime;

    if (motorElapsedTime >= MOTOR_RUNTIME) {
      digitalWrite(MOTOR_PIN, LOW);
      motorRunning = false;
      currentData.motorStatus = false;
      Serial.println("\n>>> RED LED OFF - MOTOR STOPPED");
      Serial.println(">>> 5-minute irrigation cycle completed");

      // Check if moisture recovered
      if (filteredMoisture >= MOISTURE_THRESHOLD_HIGH) {
        Serial.println(">>> Soil moisture has recovered sufficiently");
      } else {
        Serial.println(">>> Soil moisture still low - ready for next cycle if needed");
      }
    }
  }

  // ADDITIONAL: Stop motor early if alert is cleared AND moisture recovered
  if (motorRunning && !irrigationAlertActive && filteredMoisture >= MOISTURE_THRESHOLD_HIGH) {
    digitalWrite(MOTOR_PIN, LOW);
    motorRunning = false;
    currentData.motorStatus = false;
    Serial.println("\n>>> RED LED OFF - MOTOR STOPPED EARLY");
    Serial.println(">>> Soil moisture recovered - irrigation not needed");
  }
}

// ============== PUBLISH DATA TO CLOUD ==============

  void publishData() {
    if (!client.connected()) {
      reconnect();
    }

    // Create JSON payload
    StaticJsonDocument<512> doc;

    doc["temperature"] = currentData.tempFiltered;
    doc["humidity"] = currentData.humidityFiltered;
    doc["soil_moisture_filtered"] = currentData.moistureFiltered;
    doc["pH"] = currentData.pH;
    doc["irrigation_alert"] = currentData.irrigationAlert;
    doc["heat_stress_alert"] = currentData.heatStressAlert;
    doc["pH_alert"] = currentData.phAlert;
    doc["motor_status"] = currentData.motorStatus;

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

    // Maintain MQTT connection
    if (!client.connected()) {
      reconnect();
    }
    client.loop();
  
  // Read sensors every 60 seconds
  if (millis() - lastSensorTime >= SENSOR_INTERVAL) {
    lastSensorTime = millis();

    readSensors();
    checkAlertConditions();
    controlAlerts();
    controlMotor();
    publishData();
  }

  delay(LOOP_INTERVAL);
}