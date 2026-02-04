// IOT-BASED TECHNOLOGIES FOR PRECISION AGRICULTURE AND ENVIRONMENTAL MONITORING 
// Water Quality Monitoring System with Multi-Parameter Analysis & ThingsBoard
// ESP32 DevKit v1 on Wokwi Platform
// TESTING VERSION - Cloud Integration
// Author: Kalaivathanan
// Date: 2025

#include <WiFi.h>
#include <PubSubClient.h>
#include <DHT.h>
#include <ArduinoJson.h>
#include <math.h>

// ============== PIN DEFINITIONS ==============
#define DHTPIN 15              // DHT22 sensor pin (ambient conditions)
#define PH_PIN 32              // pH sensor ADC1_4
#define TURBIDITY_PIN 34       // Turbidity sensor ADC1_6
#define DO_PIN 33              // Dissolved Oxygen sensor ADC1_5
#define CONDUCTIVITY_PIN 35    // Conductivity sensor ADC1_7
#define LED_NORMAL_PIN 2       // Green LED - Normal Quality
#define LED_CAUTION_PIN 4      // Yellow LED - Caution Zone
#define LED_CRITICAL_PIN 5     // Red LED - Critical
#define BUZZER_PIN 12          // Buzzer for alert
#define DHTTYPE DHT22          // DHT sensor type

// ============== WIFI & MQTT CONFIGURATION ==============
const char* ssid = "Wokwi-GUEST";
const char* password = "";

// ThingsBoard Configuration
const char* mqtt_server = "demo.thingsboard.io";
const int mqtt_port = 1883;
const char* mqtt_access_token = "IhPZg8v2Rj1RWbOqYFPa";

// ============== SENSOR OBJECTS ==============
DHT dht(DHTPIN, DHTTYPE);
WiFiClient espClient;
PubSubClient client(espClient);

// ============== TIMING PARAMETERS ==============
const unsigned long SENSOR_INTERVAL = 45000;       // 45 seconds sampling
const unsigned long LOOP_INTERVAL = 1000;
const unsigned long MQTT_RECONNECT_INTERVAL = 5000;
unsigned long lastSensorTime = 0;
unsigned long lastMqttReconnectTime = 0;
const int ADC_MAX = 4095;

// ============== WATER QUALITY THRESHOLDS ==============
// pH Thresholds
const float PH_OPTIMAL_MIN = 6.5;
const float PH_OPTIMAL_MAX = 8.5;
const float PH_CRITICAL_MIN = 4.0;
const float PH_CRITICAL_MAX = 10.0;

// Turbidity Thresholds (NTU - Nephelometric Turbidity Units)
const float TURBIDITY_NORMAL = 5.0;
const float TURBIDITY_CAUTION = 10.0;
const float TURBIDITY_CRITICAL = 20.0;

// Dissolved Oxygen Thresholds (mg/L)
const float DO_OPTIMAL = 7.0;
const float DO_CAUTION = 5.0;
const float DO_CRITICAL = 3.0;

// Conductivity Thresholds (μS/cm)
const float CONDUCTIVITY_NORMAL = 500.0;
const float CONDUCTIVITY_CAUTION = 1000.0;
const float CONDUCTIVITY_CRITICAL = 2000.0;

// ============== BASELINE CHARACTERIZATION ==============
const int BASELINE_READINGS = 32;  // 32 readings × 45 sec = 24 minutes
int baselineCounter = 0;
bool baselineComplete = false;

float ph_baseline[BASELINE_READINGS] = {0};
float turbidity_baseline[BASELINE_READINGS] = {0};
float do_baseline[BASELINE_READINGS] = {0};
float conductivity_baseline[BASELINE_READINGS] = {0};

float ph_baseline_avg = 7.0;
float turbidity_baseline_avg = 2.0;
float do_baseline_avg = 7.0;
float conductivity_baseline_avg = 300.0;

// ============== ALERT COUNTERS & FLAGS ==============
int cautionAlertCount = 0;
int criticalAlertCount = 0;
int contaminationDetectionCount = 0;

bool cautionAlertActive = false;
bool criticalAlertActive = false;
bool contaminationDetected = false;

// ============== SENSOR DATA STRUCTURE ==============
struct WaterQualityData {
  float pH;
  float turbidity;
  float dissolvedOxygen;
  float conductivity;
  float waterTemp;
  float airTemp;
  float humidity;
  float wqi;                 // Water Quality Index (0-100)
  int qualityRating;        // 0=Excellent, 1=Good, 2=Moderate, 3=Poor, 4=Unsuitable
  int alertLevel;           // 0=None, 1=Caution, 2=Critical
  int contaminationLevel;   // 0=Normal, 1=Suspected, 2=Confirmed
  unsigned long timestamp;
};

WaterQualityData currentData;

// ============== WATER QUALITY INDICATOR STRUCTURE ==============
struct QualityIndicators {
  bool ph_abnormal;
  bool turbidity_high;
  bool do_low;
  bool conductivity_high;
};

QualityIndicators indicators;

// ============== FUNCTION PROTOTYPES ==============
void setup_wifi(); 
void reconnect(); 
void callback(char* topic, byte* payload, unsigned int length); 
void readSensors();
void updateBaselineCharacterization();
float calibratePH(int rawValue);
float calibrateTurbidity(int rawValue);
float calibrateDissolvedOxygen(int rawValue, float temperature);
float calibrateConductivity(int rawValue);
void evaluateQualityIndicators();
void checkContaminationEvent();
void calculateWaterQualityIndex();
void controlStatusLEDs();
void controlBuzzer();
void publishDataToThingsBoard(); 

// ============== SETUP FUNCTION ==============
void setup() {
  Serial.begin(115200);
  delay(1000);
  
  Serial.println("\n\n==========================================");
  Serial.println("Water Quality Monitoring System");
  Serial.println("ESP32 Multi-Parameter Aquatic Assessment");
  Serial.println("ThingsBoard Cloud Integration");
  Serial.println("==========================================\n");
  
  // Initialize GPIO pins - LEDs
  pinMode(LED_NORMAL_PIN, OUTPUT);
  digitalWrite(LED_NORMAL_PIN, LOW);
  
  pinMode(LED_CAUTION_PIN, OUTPUT);
  digitalWrite(LED_CAUTION_PIN, LOW);
  
  pinMode(LED_CRITICAL_PIN, OUTPUT);
  digitalWrite(LED_CRITICAL_PIN, LOW);
  
  // Initialize buzzer
  pinMode(BUZZER_PIN, OUTPUT);
  digitalWrite(BUZZER_PIN, LOW);
  
  Serial.println("[SETUP] GPIO pins initialized");
  Serial.println("[SETUP] - GPIO 2: LED Normal (Green)");
  Serial.println("[SETUP] - GPIO 4: LED Caution (Yellow)");
  Serial.println("[SETUP] - GPIO 5: LED Critical (Red)");
  Serial.println("[SETUP] - GPIO 12: Buzzer");
  
  // Initialize DHT sensor
  dht.begin();
  Serial.println("[SETUP] DHT22 sensor initialized on GPIO 15");
  
  // Initialize WiFi
  setup_wifi();
  
  // Setup MQTT
  client.setServer(mqtt_server, mqtt_port);
  client.setBufferSize(1024);
 client.setCallback(callback);
  
  Serial.println("[SETUP] System initializing - Baseline characterization phase");
  Serial.println("[SETUP] Will take approximately 24 minutes...\n");
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
    
    if (client.connect("ESP32 Water Quality", mqtt_access_token, "")) {
      Serial.println(" CONNECTED");
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
  //Read ambient DHT22 (water temperature approximation + air humidity)
  float airTemp = dht.readTemperature();
  float humidity = dht.readHumidity();
  
  if (isnan(airTemp) || isnan(humidity)) {
    Serial.println("[ERROR] DHT22 sensor read failed");
    return;
  }
  
  // For water temperature, use slightly adjusted ambient temp (simulated)
  float waterTemp = airTemp + 2.0;  // Water typically 1-3°C warmer
  
  // Read pH sensor - GPIO 32
  int ph_raw = analogRead(PH_PIN);
  float pH = calibratePH(ph_raw);
  
  // Read Turbidity sensor - GPIO 34
  int turbidity_raw = analogRead(TURBIDITY_PIN);
  float turbidity = calibrateTurbidity(turbidity_raw);
  
  // Read Dissolved Oxygen sensor - GPIO 33
  int do_raw = analogRead(DO_PIN);
  float dissolvedOxygen = calibrateDissolvedOxygen(do_raw, waterTemp);
  
  // Read Conductivity sensor - GPIO 35
  int conductivity_raw = analogRead(CONDUCTIVITY_PIN);
  float conductivity = calibrateConductivity(conductivity_raw);
  
  // Store sensor data
  currentData.pH = pH;
  currentData.turbidity = turbidity;
  currentData.dissolvedOxygen = dissolvedOxygen;
  currentData.conductivity = conductivity;
  currentData.waterTemp = waterTemp;
  currentData.airTemp = airTemp;
  currentData.humidity = humidity;
  currentData.timestamp = millis();
  
  // Debug output
  Serial.println("\n========= WATER QUALITY READINGS =========");
  Serial.print("Water Temperature: ");
  Serial.print(waterTemp, 2);
  Serial.println(" °C");
  Serial.print("Air Temperature: ");
  Serial.print(airTemp, 2);
  Serial.println(" °C");
  Serial.print("Humidity: ");
  Serial.print(humidity, 2);
  Serial.println(" %");
  
  Serial.print("pH Level: ");
  Serial.print(pH, 2);
  Serial.println(" (Optimal: 6.5-8.5)");
  
  Serial.print("Turbidity: ");
  Serial.print(turbidity, 2);
  Serial.println(" NTU (Normal: < 5 NTU)");
  
  Serial.print("Dissolved Oxygen: ");
  Serial.print(dissolvedOxygen, 2);
  Serial.println(" mg/L (Optimal: > 7 mg/L)");
  
  Serial.print("Conductivity: ");
  Serial.print(conductivity, 2);
  Serial.println(" μS/cm (Normal: < 500 μS/cm)");
  
  Serial.println("==========================================");
}

// ============== CALIBRATION: pH ==============
float calibratePH(int rawValue) {
  // Two-point calibration: pH 4.0 and pH 7.0
  float voltage = (float)rawValue / ADC_MAX * 3.3;
  
  float pH4_voltage = 2.0;
  float pH7_voltage = 1.5;
  
  float voltsPerPH = (pH4_voltage - pH7_voltage) / (4.0 - 7.0);
  float pH = 7.0 - ((voltage - pH7_voltage) / voltsPerPH);
  
  pH = constrain(pH, 0, 14);
  return pH;
}

// ============== CALIBRATION: TURBIDITY ==============
// Turbidity in NTU (Nephelometric Turbidity Units)
float calibrateTurbidity(int rawValue) {
  // Higher ADC = more light scattered = higher turbidity
  float ntu = (float)rawValue / ADC_MAX * 40.0;  // Scale to 0-40 NTU range
  return constrain(ntu, 0, 40);
}

// ============== CALIBRATION: DISSOLVED OXYGEN ==============
// Temperature compensation for DO measurement
float calibrateDissolvedOxygen(int rawValue, float temperature) {
  float voltage = (float)rawValue / ADC_MAX * 3.3;
  
  // Basic conversion to mg/L
  float do_measured = (voltage / 3.3) * 15.0;  // Scale to 0-15 mg/L
  
  // Temperature compensation: DO decreases with temperature
  // DO_compensated = DO_measured × (1 + 0.0236 × (T - 20))
  float do_compensated = do_measured * (1.0 + 0.0236 * (temperature - 20.0));
  
  return constrain(do_compensated, 0, 15);
}

// ============== CALIBRATION: CONDUCTIVITY ==============
// Conductivity in μS/cm (microSiemens per centimeter)
float calibrateConductivity(int rawValue) {
  float voltage = (float)rawValue / ADC_MAX * 3.3;
  
  // Convert voltage to conductivity
  float conductivity = (voltage / 3.3) * 3000.0;  // Scale to 0-3000 μS/cm
  
  return constrain(conductivity, 0, 3000);
}

// ============== BASELINE CHARACTERIZATION ==============
void updateBaselineCharacterization() {
  if (baselineComplete) return;
  
  // Store readings in baseline arrays
  ph_baseline[baselineCounter] = currentData.pH;
  turbidity_baseline[baselineCounter] = currentData.turbidity;
  do_baseline[baselineCounter] = currentData.dissolvedOxygen;
  conductivity_baseline[baselineCounter] = currentData.conductivity;
  
  baselineCounter++;
  
  if (baselineCounter >= BASELINE_READINGS) {
    // Calculate baseline averages
    float ph_sum = 0, turb_sum = 0, do_sum = 0, cond_sum = 0;
    
    for (int i = 0; i < BASELINE_READINGS; i++) {
      ph_sum += ph_baseline[i];
      turb_sum += turbidity_baseline[i];
      do_sum += do_baseline[i];
      cond_sum += conductivity_baseline[i];
    }
    
    ph_baseline_avg = ph_sum / BASELINE_READINGS;
    turbidity_baseline_avg = turb_sum / BASELINE_READINGS;
    do_baseline_avg = do_sum / BASELINE_READINGS;
    conductivity_baseline_avg = cond_sum / BASELINE_READINGS;
    
    baselineComplete = true;
    
    Serial.println("\n========== BASELINE CHARACTERIZATION COMPLETE ==========");
    Serial.print("pH Baseline: ");
    Serial.println(ph_baseline_avg, 2);
    Serial.print("Turbidity Baseline: ");
    Serial.println(turbidity_baseline_avg, 2);
    Serial.print("DO Baseline: ");
    Serial.println(do_baseline_avg, 2);
    Serial.print("Conductivity Baseline: ");
    Serial.println(conductivity_baseline_avg, 2);
    Serial.println("=========================================================\n");
  }
}

// ============== EVALUATE QUALITY INDICATORS ==============
void evaluateQualityIndicators() {
  indicators.ph_abnormal = (currentData.pH < PH_OPTIMAL_MIN || currentData.pH > PH_OPTIMAL_MAX);
  indicators.turbidity_high = (currentData.turbidity > TURBIDITY_NORMAL);
  indicators.do_low = (currentData.dissolvedOxygen < DO_OPTIMAL);
  indicators.conductivity_high = (currentData.conductivity > CONDUCTIVITY_NORMAL);
}

// ============== CHECK CONTAMINATION EVENT ==============
void checkContaminationEvent() {
  if (!baselineComplete) return;
  
  evaluateQualityIndicators();
  
  // Count active contamination indicators
  int activeIndicators = 0;
  if (indicators.ph_abnormal) activeIndicators++;
  if (indicators.turbidity_high) activeIndicators++;
  if (indicators.do_low) activeIndicators++;
  if (indicators.conductivity_high) activeIndicators++;
  
  // ========== CAUTION LEVEL ==========
  if (activeIndicators >= 1) {
    cautionAlertCount++;
    
    if (cautionAlertCount >= 2) {  // 2 readings = 90 seconds
      if (!cautionAlertActive) {
        cautionAlertActive = true;
        Serial.println("\n[CAUTION] Water Quality Degradation Detected");
        Serial.print("Active indicators: ");
        Serial.println(activeIndicators);
      }
      currentData.alertLevel = 1;
      contaminationDetectionCount++;
    }
  } else {
    cautionAlertCount = 0;
    cautionAlertActive = false;
  }
  
  // ========== CRITICAL LEVEL ==========
  if ((currentData.pH < PH_CRITICAL_MIN || currentData.pH > PH_CRITICAL_MAX) ||
      (currentData.turbidity > TURBIDITY_CRITICAL) ||
      (currentData.dissolvedOxygen < DO_CRITICAL) ||
      (currentData.conductivity > CONDUCTIVITY_CRITICAL)) {
    
    criticalAlertCount++;
    
    if (criticalAlertCount >= 1) {
      if (!criticalAlertActive) {
        criticalAlertActive = true;
        Serial.println("\n!!! CRITICAL ALERT !!!");
        Serial.println(">>> Water contamination confirmed");
        Serial.println(">>> IMMEDIATE ACTION REQUIRED");
      }
      currentData.alertLevel = 2;
      contaminationDetected = true;
    }
  } else {
    criticalAlertCount = 0;
    criticalAlertActive = false;
    contaminationDetected = false;
  }
  
  // No alert
  if (activeIndicators == 0) {
    cautionAlertActive = false;
    criticalAlertActive = false;
    currentData.alertLevel = 0;
  }
}

// ============== CALCULATE WATER QUALITY INDEX ==============
void calculateWaterQualityIndex() {
  // Weighted mean WQI calculation based on WHO guidelines
  // WQI = Σ(Wi × Qi) / Σ(Wi)
  
  // Weights: pH(4), Turbidity(5), DO(5), Conductivity(3)
  float w_ph = 4.0;
  float w_turbidity = 5.0;
  float w_do = 5.0;
  float w_conductivity = 3.0;
  float total_weight = w_ph + w_turbidity + w_do + w_conductivity;
  
  // Quality ratings (0-100 scale)
  // pH: Optimal at 7.0, decreases away from center
  float q_ph = 100.0 - (fabs(currentData.pH - 7.0) * 20.0);
  q_ph = constrain(q_ph, 0, 100);
  
  // Turbidity: Lower is better
  float q_turbidity = 100.0 - (currentData.turbidity * 5.0);
  q_turbidity = constrain(q_turbidity, 0, 100);
  
  // DO: Higher is better
  float q_do = (currentData.dissolvedOxygen / DO_OPTIMAL) * 100.0;
  q_do = constrain(q_do, 0, 100);
  
  // Conductivity: Lower is better
  float q_conductivity = 100.0 - (currentData.conductivity / 30.0);
  q_conductivity = constrain(q_conductivity, 0, 100);
  
  // Calculate WQI
  currentData.wqi = (w_ph * q_ph + w_turbidity * q_turbidity + 
                     w_do * q_do + w_conductivity * q_conductivity) / total_weight;
  
  // Determine quality rating
  if (currentData.wqi >= 90) {
    currentData.qualityRating = 0;  // Excellent
  } else if (currentData.wqi >= 70) {
    currentData.qualityRating = 1;  // Good
  } else if (currentData.wqi >= 50) {
    currentData.qualityRating = 2;  // Moderate
  } else if (currentData.wqi >= 25) {
    currentData.qualityRating = 3;  // Poor
  } else {
    currentData.qualityRating = 4;  // Unsuitable
  }
  
  Serial.print("Water Quality Index: ");
  Serial.print(currentData.wqi, 2);
  Serial.print(" (Rating: ");
  
  switch(currentData.qualityRating) {
    case 0: Serial.println("EXCELLENT)"); break;
    case 1: Serial.println("GOOD)"); break;
    case 2: Serial.println("MODERATE)"); break;
    case 3: Serial.println("POOR)"); break;
    case 4: Serial.println("UNSUITABLE)"); break;
  }
}

// ============== CONTROL STATUS LEDS ==============
void controlStatusLEDs() {
  if (criticalAlertActive) {
    digitalWrite(LED_CRITICAL_PIN, HIGH);
    digitalWrite(LED_CAUTION_PIN, HIGH);
    digitalWrite(LED_NORMAL_PIN, LOW);
    Serial.println("[LED STATUS] Critical Alert: Red & Yellow ON");
  } 
  else if (cautionAlertActive) {
    digitalWrite(LED_CRITICAL_PIN, LOW);
    digitalWrite(LED_CAUTION_PIN, HIGH);
    digitalWrite(LED_NORMAL_PIN, LOW);
    Serial.println("[LED STATUS] Caution Alert: Yellow ON");
  }
  else {
    digitalWrite(LED_CRITICAL_PIN, LOW);
    digitalWrite(LED_CAUTION_PIN, LOW);
    digitalWrite(LED_NORMAL_PIN, HIGH);
    Serial.println("[LED STATUS] Normal Quality: Green ON");
  }
}

// ============== CONTROL BUZZER ==============
void controlBuzzer() {
  if (criticalAlertActive) {
    digitalWrite(BUZZER_PIN, HIGH);
    Serial.println("[BUZZER] CRITICAL - Continuous alarm");
  } 
  else if (cautionAlertActive) {
    digitalWrite(BUZZER_PIN, HIGH);
    Serial.println("[BUZZER] CAUTION - Beeping");
  }
  else {
    digitalWrite(BUZZER_PIN, LOW);
  }
}

// ============== PUBLISH DATA TO THINGSBOARD ==============
void publishDataToThingsBoard() {
  if (!client.connected()) {
    return;
  }
  
  StaticJsonDocument<512> doc;
  
  doc["pH"] = currentData.pH;
  doc["turbidity_ntu"] = currentData.turbidity;
  doc["dissolved_oxygen_mg_L"] = currentData.dissolvedOxygen;
  doc["conductivity_uS_cm"] = currentData.conductivity;
  doc["water_temperature"] = currentData.waterTemp;
  doc["air_temperature"] = currentData.airTemp;
  doc["humidity"] = currentData.humidity;
  doc["water_quality_index"] = currentData.wqi;
  doc["quality_rating"] = currentData.qualityRating;
  doc["alert_level"] = currentData.alertLevel;
  doc["caution_active"] = cautionAlertActive;
  doc["critical_active"] = criticalAlertActive;
  doc["baseline_complete"] = baselineComplete;
  
  char buffer[512];
  serializeJson(doc, buffer);

  
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
    if (millis() - lastMqttReconnectTime >= MQTT_RECONNECT_INTERVAL) {
      lastMqttReconnectTime = millis();
     reconnect();
    }
  } else {
    client.loop();
  }
  
  // Read sensors every 45 seconds
  
  if (millis() - lastSensorTime >= SENSOR_INTERVAL) {
    lastSensorTime = millis();
    
    readSensors();
    
    // Baseline characterization phase (first 24 minutes)
    if (!baselineComplete) {
      updateBaselineCharacterization();
      Serial.print("[BASELINE] Reading ");
      Serial.print(baselineCounter);
      Serial.print("/");
      Serial.println(BASELINE_READINGS);
    } 
    // Normal operation phase
    else {
      checkContaminationEvent();
      calculateWaterQualityIndex();
      controlStatusLEDs();
      controlBuzzer();
    }
    
    publishDataToThingsBoard();
  }
  
  delay(LOOP_INTERVAL);
}