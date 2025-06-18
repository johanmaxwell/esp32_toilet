#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <ESPAsyncWebServer.h>
#include <SPIFFS.h>
#include <Preferences.h>
#include <ESPmDNS.h>
#include <MQTTClient.h>
#include <ArduinoJson.h>
#include <esp_sleep.h>
#include <DNSServer.h>
#include "HX711.h"

// Sensor Pins and Variables
#define trigPin 26
#define echoPin 27
#define irRightPin 22
#define irLeftPin 23
#define irSensorPin 21 
#define buttonPin 18
#define pumpPin 19
#define batteryPin 34
#define ledPin 2  // Built-in LED pin
#define resetButtonPin 15  // New reset button pin
#define visitorResetPin 17  // New visitor reset button pin
#define bauPin 35  // New bau sensor pin
#define tisuDT 4   // New tisu sensor pins
#define tisuSCK 16

long duration, distance;
bool isOccupied = false;
bool previousOccupied = false;  // Add this line for tracking occupancy state changes
unsigned long enterTime = 0;
unsigned long exitTime = 0;

volatile int soapCounter = 0;
volatile bool resetFlag = false;
volatile unsigned long buttonPressTime = 0;
bool previousIRState = HIGH;
unsigned long lastDispenseTime = 0;
const unsigned long debounceDelay = 1000;
const unsigned long resetHoldTime = 3000; // 3 seconds for reset

const float R1 = 100000.0; // ohms
const float R2 = 100000.0; // ohms
const float adcMax = 4095.0;
const float adcVoltage = 3.3;
const float batteryMin = 3.0; // 18650 min voltage (0%)
const float batteryMax = 4.2; // 18650 max voltage (100%)
unsigned long lastBatteryPublish = 0;
const unsigned long batteryPublishInterval = 120000; // 120 seconds

unsigned long lastMqttReconnectAttempt = 0;
const unsigned long mqttReconnectInterval = 10000; // 10 seconds
bool wasConnected = false;

unsigned long lastUltrasonicCheck = 0;
const unsigned long ultrasonicInterval = 500; 
const long occupanyTimeLimit = 5000;

// FSM and Debounce Settings for People Counter
enum State { IDLE, RIGHT_FIRST, LEFT_FIRST, WAIT_RELEASE };
State state = IDLE;
const unsigned long STATE_TIMEOUT = 1000;
const int DEBOUNCE_READS = 5;
const int DEBOUNCE_THRESHOLD = 4;
int visitorCounter = 0; // Renamed from peopleCount to maintain consistency
unsigned long stateStartTime = 0;

// Global variables for configuration
String wifi_ssid = "";
String wifi_password = "";
String mqtt_server = "";
String mqtt_port = "8883";
String mqtt_user = "";
String mqtt_password = "";
String company = "";
String gedung = "";
String lokasi = "";
String gender = "";
String nomor_perangkat = "";
String okupansi = "";
String pengunjung = "";
String tisu = "";
String sabun = "";
String bau = "";
String nomor_toilet = "";
String jarak_deteksi = "";
String berat_tisu = "";
String nomor_dispenser = "";
String is_luar = "";
String placement = "";  // Add placement variable for people counter sensor
bool init_done = false;
bool config_done = false;

// State machine flags
bool shouldConnectWiFi = false;
bool shouldConnectMQTT = false;
bool shouldPublish = false;
bool serverRunning = false;
bool useDeepSleep = false;
bool useSavePowerMode = false;

Preferences prefs;
AsyncWebServer server(80);
WiFiClientSecure espClient;
MQTTClient client(1024);
DNSServer dnsServer; // For captive portal
const byte DNS_PORT = 53;

// Add new variables for reset button
volatile unsigned long resetButtonPressTime = 0;
const unsigned long resetButtonHoldTime = 3000; // 3 seconds for reset

// Add HX711 library and instance
HX711 scale;

// Add new timing variables
unsigned long lastBauPublish = 0;
unsigned long lastTisuPublish = 0;
const unsigned long bauPublishInterval = 10000;  // 10 seconds
const unsigned long tisuPublishInterval = 10000; // 10 seconds

// Add with other global variables at the top
volatile bool pumpActive = false;
unsigned long pumpStartTime = 0;
int lastSentCount = 0;  // Add this to track last count we sent data for
bool firstPeopleCountSend = true;  // Add this to track first people count send
unsigned long lastMqttCheck = 0;  // Add this to track last MQTT connection check
const unsigned long mqttCheckInterval = 60000;  // 1 minute in milliseconds
volatile unsigned long lastButtonPress = 0;  // Track last button press for double press
volatile unsigned long lastVisitorButtonPress = 0;  // Track last visitor button press for double press
const unsigned long doublePressInterval = 500;  // 500ms for double press detection
volatile int buttonPressCount = 0;  // Count button presses
volatile int visitorButtonPressCount = 0;  // Count visitor button presses

// Function to get MAC address
String getMacAddress() {
  WiFi.mode(WIFI_STA);
  delay(100);
  return WiFi.macAddress();
}

String generateClientId() {
  String clientId = getMacAddress() + "-" + String(random(10000, 99999));
  return clientId;
}

// Function to create MQTT topic for sensors
String makeSensorTopic(const String& type, const String& nomor = "") {
  String topic = "sensor/" + company + "/" + gedung + "/" + lokasi + "/" + gender + "/" + type;
  if (!nomor.isEmpty()) {
    topic += "/" + nomor;
  }
  return topic;
}

// Function to create configuration MQTT topic
String createMQTTTopic(const String& prefix, const String& company, const String& macAddress) {
  return prefix + "/" + company + "/" + macAddress;
}

// Function to connect to WiFi with timeout
bool connectToWiFi() {
  if (serverRunning) {
    Serial.println("In AP mode, skipping WiFi connection");
    return false;
  }

  wifi_ssid.trim();
  wifi_password.trim();
  if (wifi_ssid.isEmpty() || wifi_password.isEmpty()) {
    Serial.println("WiFi SSID or password is empty");
    Serial.printf("SSID: '%s', Password: '%s'\n", wifi_ssid.c_str(), wifi_password.c_str());
    return false;
  }

  Serial.print("Connecting to WiFi SSID: ");
  Serial.println(wifi_ssid);

  WiFi.mode(WIFI_STA);  // Changed from WIFI_AP_STA to WIFI_STA
  WiFi.begin(wifi_ssid.c_str(), wifi_password.c_str());

  unsigned long startTime = millis();
  const unsigned long timeout = 10000;
  while (WiFi.status() != WL_CONNECTED && millis() - startTime < timeout) {
    delay(500);
    Serial.print(".");
  }

  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("\nWiFi Connected");
    Serial.print("IP address: ");
    Serial.println(WiFi.localIP());
    return true;
  } else {
    Serial.println("\nFailed to connect to WiFi");
    Serial.printf("Final WiFi Status: %d\n", WiFi.status());
    return false;
  }
}

// Function to publish with retry
bool publishMqtt(const String& topic, const String& payload, bool retain = false, int qos = 0) {
  Serial.print("Publishing to topic: ");
  Serial.println(topic);
  Serial.print("Message: ");
  Serial.println(payload);

  for (int attempt = 0; attempt < 3; attempt++) {
    if (client.publish(topic, payload, retain, qos)) {
      Serial.println("Published successfully with QoS " + String(qos) + "!");
      return true;
    }
    Serial.println("Publish failed, retrying...");
    delay(1000);
  }

  Serial.println("Publishing failed after 3 attempts");
  return false;
}

// Function to connect to MQTT with timeout
bool connectToMQTT() {
  if (mqtt_server.isEmpty() || mqtt_user.isEmpty() || mqtt_password.isEmpty()) {
    Serial.println("MQTT credentials not set properly");
    digitalWrite(ledPin, LOW);  // Turn off LED if credentials not set
    return false;
  }

  String macAddress = getMacAddress();
  bool connected = false;
  int connectAttempts = 0;
  const int maxAttempts = 5;
  unsigned long startTime = millis();
  const unsigned long timeout = 10000;

  // Clean up previous connection
  client.disconnect();
  espClient.stop();

  espClient.setInsecure();
  int port = mqtt_port.toInt();
  if (port == 0) port = 8883;

  Serial.print("Connecting to MQTT server: ");
  Serial.print(mqtt_server);
  Serial.print(":");
  Serial.println(port);

  client.begin(mqtt_server.c_str(), port, espClient);
  client.setKeepAlive(30);
  client.onMessage(callback);

  String clientId = generateClientId();
  Serial.println("Connecting with client ID: " + clientId);

  while (!connected && connectAttempts < maxAttempts && millis() - startTime < timeout) {
    connectAttempts++;
    if (client.connect(clientId.c_str(), mqtt_user.c_str(), mqtt_password.c_str())) {
      Serial.println("MQTT Connected");
      digitalWrite(ledPin, HIGH);  // Turn on LED when connected
      connected = true;
    } else {
      Serial.println("Connection failed, attempt " + String(connectAttempts) + "/" + String(maxAttempts));
      digitalWrite(ledPin, LOW);  // Turn off LED on failed attempt
      delay(2000);
    }
  }

  if (!connected) {
    Serial.println("MQTT connection failed after " + String(maxAttempts) + " attempts");
    digitalWrite(ledPin, LOW);  // Turn off LED if connection failed
  }
  return connected;
}

// Subscribe to MQTT topics
void subscribeToMQTTTopics() {
  if (!client.connected()) {
    Serial.println("Cannot subscribe - MQTT not connected");
    return;
  }

  if (company.isEmpty()) {
    Serial.println("Cannot subscribe - Company name is empty");
    return;
  }

  String macAddress = getMacAddress();
  String updateTopic = createMQTTTopic("update", company, macAddress);
  if (client.subscribe(updateTopic)) {
    Serial.println("Successfully subscribed to " + updateTopic);
  } else {
    Serial.println("Failed to subscribe to " + updateTopic);
  }
}

// MQTT callback for configuration updates
void callback(String &topic, String &payload) {
  Serial.println("Message received on topic: " + topic);
  Serial.println("Payload: " + payload);

  StaticJsonDocument<1024> doc;
  DeserializationError error = deserializeJson(doc, payload);

  if (error) {
    Serial.print("JSON parsing failed: ");
    Serial.println(error.c_str());
    return;
  }

  if (!doc.containsKey("version") || !doc["version"].is<const char*>()) {
    Serial.println("Missing or invalid config version. Ignoring.");
    return;
  }

  const char* newVersion = doc["version"];
  prefs.begin("config", false);
  String oldVersion = prefs.getString("version", "");

  if (oldVersion.equals(newVersion)) {
    Serial.println("Config version already applied. Ignoring.");
    prefs.end();
    return;
  }

  for (JsonPair kv : doc.as<JsonObject>()) {
    const char* key = kv.key().c_str();
    const char* value = kv.value().as<const char*>();
    if (value != nullptr) {
      prefs.putString(key, value);
      Serial.printf("Saved preference: %s = %s\n", key, value);
    }
  }

  prefs.end(); 
  Serial.println("New config saved. Restarting device...");
  delay(500);
  ESP.restart();
}

// Modify setVariablesFromPreferences to include soap counter data
void setVariablesFromPreferences() {
  prefs.begin("config", true);
  wifi_ssid = prefs.getString("wifi_ssid", "");
  wifi_password = prefs.getString("wifi_password", "");
  mqtt_server = prefs.getString("mqtt_server", "");
  mqtt_port = prefs.getString("mqtt_port", "8883");
  mqtt_user = prefs.getString("mqtt_user", "");
  mqtt_password = prefs.getString("mqtt_password", "");
  company = prefs.getString("company", "");
  gedung = prefs.getString("gedung", "");
  lokasi = prefs.getString("lokasi", "");
  gender = prefs.getString("gender", "");
  nomor_perangkat = prefs.getString("nomor_perangkat", "");
  okupansi = prefs.getString("okupansi", "");
  pengunjung = prefs.getString("pengunjung", "");
  tisu = prefs.getString("tisu", "");
  sabun = prefs.getString("sabun", "");
  bau = prefs.getString("bau", "");
  nomor_toilet = prefs.getString("nomor_toilet", "");
  jarak_deteksi = prefs.getString("jarak_deteksi", "");
  berat_tisu = prefs.getString("berat_tisu", "");
  nomor_dispenser = prefs.getString("nomor_dispenser", "");
  is_luar = prefs.getString("is_luar", "");
  placement = prefs.getString("placement", "");
  init_done = prefs.getBool("init_done", false);
  config_done = prefs.getBool("config_done", false);
  soapCounter = prefs.getInt("soap_counter", 0);
  lastSentCount = prefs.getInt("soap_last_sent", 0);
  visitorCounter = prefs.getInt("visitor_counter", 0);  // Load visitor counter
  prefs.end();
}

// Prepare MQTT payload for configuration
String prepareMQTTPayload() {
  return gedung + ";" + lokasi + ";" + gender + ";" + nomor_perangkat + ";" +
         wifi_ssid + ";" + wifi_password + ";" + mqtt_server + ";" +
         mqtt_port + ";" + mqtt_user + ";" + mqtt_password + ";" +
         okupansi + ";" + pengunjung + ";" + tisu + ";" + sabun + ";" + bau + ";" +
         nomor_toilet + ";" + nomor_dispenser + ";" + is_luar + ";" +
         jarak_deteksi + ";" + berat_tisu + ";" + placement;
}

// Start the web server and AP with captive portal
void startWebServer() {
  if (serverRunning) {
    server.end();
    WiFi.softAPdisconnect(true);
    dnsServer.stop();
    serverRunning = false;
    Serial.println("Stopped existing web server and AP");
  }

  WiFi.mode(WIFI_AP);  // Changed from WIFI_AP_STA to WIFI_AP
  String macAddress = getMacAddress();
  String accessPointName = "CONFIG-" + macAddress;
  WiFi.softAP(accessPointName.c_str(), "12345678");
  Serial.println("AP started with name: " + accessPointName);
  Serial.println("AP IP address: " + WiFi.softAPIP().toString());

  // Setup DNS server for captive portal
  dnsServer.start(DNS_PORT, "*", WiFi.softAPIP());

  server.serveStatic("/", SPIFFS, "/").setDefaultFile("index.html");

  server.on("/submit", HTTP_POST, [](AsyncWebServerRequest *request) {
    int params = request->params();
    prefs.begin("config", false);
    prefs.clear();
    for (int i = 0; i < params; i++) {
      const AsyncWebParameter *param = request->getParam(i);
      prefs.putString(param->name().c_str(), param->value().c_str());
      Serial.printf("%s: %s\n", param->name().c_str(), param->value().c_str());
    }
    prefs.end();
    Serial.println("=== Data Saved ===");
    request->redirect("/data.html");
  });

  server.on("/stop", HTTP_POST, [](AsyncWebServerRequest *request) {
    prefs.begin("config", false);
    prefs.putBool("init_done", true);
    prefs.end();
    setVariablesFromPreferences();
    shouldConnectWiFi = true;
    shouldConnectMQTT = true;
    shouldPublish = true;
    server.end();
    WiFi.softAPdisconnect(true);
    serverRunning = false;
    Serial.println("Web server stopped. Initialization complete.");
  });

  server.on("/getdata", HTTP_GET, [](AsyncWebServerRequest *request) {
    prefs.begin("config", true);
    String response = "<table border='1'><tr><th>Key</th><th>Value</th></tr>";
    const char* keys[] = {
      "wifi_ssid", "wifi_password", "mqtt_server", "mqtt_port", "mqtt_user", "mqtt_password",
      "company", "gedung", "lokasi", "gender", "nomor_perangkat",
      "okupansi", "pengunjung", "tisu", "sabun", "bau",
      "nomor_toilet", "jarak_deteksi", "berat_tisu",
      "nomor_dispenser", "is_luar", "placement"
    };
    for (const char* key : keys) {
      String value = prefs.getString(key, "");
      if (value != "") {
        response += "<tr><td>" + String(key) + "</td><td>" + value + "</td></tr>";
      }
    }
    response += "</table>";
    prefs.end();
    request->send(200, "text/html", response);
  });

  server.on("/clear", HTTP_POST, [](AsyncWebServerRequest *request) {
    prefs.begin("config", false);
    prefs.clear();
    prefs.end();
    Serial.println("=== Preferences Cleared ===");
    request->redirect("/data.html");
  });

  server.on("/currentData", HTTP_GET, [](AsyncWebServerRequest *request) {
    prefs.begin("config", true);
    String response = "{";
    bool first = true;
    const char* keys[] = {
      "wifi_ssid", "wifi_password", "mqtt_server", "mqtt_port", "mqtt_user", "mqtt_password",
      "company", "gedung", "lokasi", "gender", "nomor_perangkat",
      "okupansi", "pengunjung", "tisu", "sabun", "bau",
      "nomor_toilet", "jarak_deteksi", "berat_tisu",
      "nomor_dispenser", "is_luar"
    };
    for (const char* key : keys) {
      String value = prefs.getString(key, "");
      if (value != "") {
        if (!first) response += ",";
        response += "\"" + String(key) + "\":\"" + value + "\"";
        first = false;
      }
    }
    response += "}";
    prefs.end();
    request->send(200, "application/json", response);
  });

  // Handle all other requests for captive portal
  server.onNotFound([](AsyncWebServerRequest *request) {
    request->redirect("http://" + WiFi.softAPIP().toString() + "/");
  });

  server.begin();
  serverRunning = true;
  Serial.println("Web server started with captive portal");
}

// Add new interrupt handler for reset button
void IRAM_ATTR handleConfigResetButton() {
  if (digitalRead(resetButtonPin) == LOW) {
    resetButtonPressTime = millis();
  } else {
    if (resetButtonPressTime > 0 && (millis() - resetButtonPressTime >= resetButtonHoldTime)) {
      prefs.begin("config", false);
      prefs.putBool("init_done", false);
      prefs.putBool("config_done", false);
      prefs.end();
      ESP.restart();
    }
    resetButtonPressTime = 0;
  }
}

// Sensor Logic
void IRAM_ATTR handleResetButton() {
  unsigned long currentTime = millis();
  
  if (digitalRead(buttonPin) == LOW) {
    // Button pressed - do nothing on press
  } else {
    // Button released
    if (currentTime - lastButtonPress < doublePressInterval) {
      // Double press detected
      buttonPressCount++;
      if (buttonPressCount >= 2) {
        resetFlag = true;
        buttonPressCount = 0;
      }
    } else {
      // Single press, reset counter
      buttonPressCount = 1;
    }
    lastButtonPress = currentTime;
  }
}

void updateMonitorOkupansi() {
  unsigned long currentMillis = millis();
  
  if (currentMillis - lastUltrasonicCheck >= ultrasonicInterval) {
    lastUltrasonicCheck = currentMillis;
    
    digitalWrite(trigPin, LOW);
    delayMicroseconds(2);
    digitalWrite(trigPin, HIGH);
    delayMicroseconds(10);
    digitalWrite(trigPin, LOW);

    duration = pulseIn(echoPin, HIGH, 25000);
    int distanceLimit = jarak_deteksi.toInt();
    if (distanceLimit <= 0) distanceLimit = 10;
    distance = duration / 58.2;
    if (distance == 0) distance = 1000; // Interpret out-of-range readings as 1000 cm

    bool newState = isOccupied;

    if (distance > 0 && distance <= distanceLimit) {
      if (enterTime == 0) {
        enterTime = millis();
      }
      if (millis() - enterTime >= occupanyTimeLimit) {
        newState = true;
      }
      exitTime = 0;
    } else if (distance > distanceLimit) {
      if (exitTime == 0) {
        exitTime = millis();
      }
      if (millis() - exitTime >= occupanyTimeLimit) {
        newState = false;
      }
      enterTime = 0;
    }

    if (newState != isOccupied) {
      isOccupied = newState;
    }
  }
}

// Debounced Sensor Block Check
bool isSensorBlocked(int pin) {
  int blocked = 0;
  for (int i = 0; i < DEBOUNCE_READS; i++) {
    if (digitalRead(pin) == LOW) blocked++;
    delay(1);
  }
  return blocked >= DEBOUNCE_THRESHOLD;
}

void updatePeopleCounter() {
  bool rightBlocked = isSensorBlocked(irRightPin);
  bool leftBlocked = isSensorBlocked(irLeftPin);
  
  // Swap sensor readings based on placement
  if (placement == "right") {
    bool temp = rightBlocked;
    rightBlocked = leftBlocked;
    leftBlocked = temp;
  }

  switch (state) {
    case IDLE:
      if (rightBlocked) {
        state = RIGHT_FIRST;
        stateStartTime = millis();
      } else if (leftBlocked) {
        state = LEFT_FIRST;
        stateStartTime = millis();
      }
      break;
      
    case RIGHT_FIRST:
      if (leftBlocked) {
        visitorCounter++;
        Serial.print("Person Entered. Total: ");
        Serial.println(visitorCounter);
        String topic = makeSensorTopic("pengunjung", nomor_perangkat);
        if (client.connected()) {
          if (firstPeopleCountSend) {
            visitorCounter--;  // Subtract 1 for first send
            firstPeopleCountSend = false;
          }
          publishMqtt(topic, String(visitorCounter), false, 0);
        }
        // Save to preferences after increment
        prefs.begin("config", false);
        prefs.putInt("visitor_counter", visitorCounter);
        prefs.end();
        state = WAIT_RELEASE;
      } else if (millis() - stateStartTime > STATE_TIMEOUT) {
        state = IDLE;
      }
      break;
      
    case LEFT_FIRST:
      if (rightBlocked) {
        visitorCounter = max(0, visitorCounter - 1);
        Serial.print("Person Exited. Total: ");
        Serial.println(visitorCounter);
        String topic = makeSensorTopic("pengunjung", nomor_perangkat);
        if (client.connected()) {
          if (firstPeopleCountSend) {
            visitorCounter--;  // Subtract 1 for first send
            firstPeopleCountSend = false;
          }
          publishMqtt(topic, String(visitorCounter), false, 0);
        }
        // Save to preferences after decrement
        prefs.begin("config", false);
        prefs.putInt("visitor_counter", visitorCounter);
        prefs.end();
        state = WAIT_RELEASE;
      } else if (millis() - stateStartTime > STATE_TIMEOUT) {
        state = IDLE;
      }
      break;
      
    case WAIT_RELEASE:
      if (!rightBlocked && !leftBlocked) {
        state = IDLE;
      }
      break;
  }
}

void sendDataSabun(String topic) {
  float amount = 1.0 - (soapCounter / 50.0);
  String status;
  if (amount < 0.3) {
    status = "bad";
  } else if (amount > 0.7) {
    status = "good";
  } else {
    status = "ok";
  }
  String message = status + ";" + String(amount, 2) + ";" + nomor_dispenser;
  if (client.connected()) {
    publishMqtt(topic, message, false, 0);
  }
}

void updateSoapDispenser() {
  bool currentIRState = digitalRead(irSensorPin);

  // Always check pump timing regardless of MQTT connection
  if (pumpActive && millis() - pumpStartTime >= 200) {
    digitalWrite(pumpPin, LOW);
    pumpActive = false;
  }

  if (currentIRState == LOW && previousIRState == HIGH) {
    unsigned long now = millis();
    if (now - lastDispenseTime > debounceDelay && !pumpActive) {
      lastDispenseTime = now;
      pumpActive = true;
      pumpStartTime = now;
      digitalWrite(pumpPin, HIGH);
      soapCounter++;
      // Save counter after increment
      prefs.begin("config", false);
      prefs.putInt("soap_counter", soapCounter);
      prefs.end();
      Serial.print("Soap dispensed. Total count: ");
      Serial.println(soapCounter);
    }
  }

  previousIRState = currentIRState;
}

// Function to handle soap reset logic only
void handleSoapReset() {
  soapCounter = 0;
  lastSentCount = 0;
  // Save both values after reset
  prefs.begin("config", false);
  prefs.putInt("soap_counter", soapCounter);
  prefs.putInt("soap_last_sent", lastSentCount);
  prefs.end();
  Serial.println("Soap counter reset!");
  resetFlag = false;
}

void sendDataBaterai(String topic) {
  int raw = analogRead(batteryPin);
  float voltage = (raw / adcMax) * adcVoltage;
  float batteryVoltage = voltage / (R2 / (R1 + R2));
  float amount = (batteryVoltage - batteryMin) / (batteryMax - batteryMin);
  if (amount < 0) amount = 0;
  if (amount > 1) amount = 1;

  String status;
  if (amount < 0.2) {
    status = "bad";
  } else if (amount > 0.5) {
    status = "good";
  } else {
    status = "ok";
  }
  String message = status + ";" + String(amount, 2) + ";" + nomor_perangkat;
  if (client.connected()) {
    publishMqtt(topic, message, false, 0);
  }

  Serial.print("Raw ADC: ");
  Serial.print(raw);
  Serial.print(" | Voltage: ");
  Serial.print(voltage, 2);
  Serial.print("V | Battery Voltage: ");
  Serial.print(batteryVoltage, 2);
  Serial.print("V | Battery Level: ");
  Serial.print(amount * 100, 2);
  Serial.println("%");
}

// Function to send bau sensor data
void sendDataBau(String topic) {
  int adcValue = analogRead(bauPin);
  float voltage = (adcValue / 4095.0) * 3.3;  // ESP32 ADC voltage
  float realVoltage = (voltage / 3.5) - 0.1; // Adjusted for gain and offset

  Serial.print("Bau -> Real: ");
  Serial.print(realVoltage, 3);
  Serial.println(" V");

  String status;
  if (realVoltage > 0.341) {
    status = "bad";
  } else {
    status = "good";
  }

  String toiletNumber = nomor_toilet;
  if (is_luar == "ya") {
    toiletNumber = "9999";
  }

  String message = status + ";" + toiletNumber;
  if (client.connected()) {
    publishMqtt(topic, message, false, 0);
  }
}

// Function to send tisu sensor data
void sendDataTisu(String topic) {
  float weight = scale.get_units(20); // Average of 10 samples
  float beratTisu = berat_tisu.toFloat();
  float amount = weight / beratTisu; // Now in 0-1 format
  
  // Limit amount between 0 and 1
  if (amount < 0) amount = 0;
  if (amount > 1) amount = 1;
  
  String status;
  if (amount > 0.6) {
    status = "good";
  } else if (amount < 0.2) {
    status = "bad";
  } else {
    status = "ok";
  }
  
  String message = status + ";" + String(amount, 2) + ";" + nomor_toilet;
  if (client.connected()) {
    publishMqtt(topic, message, false, 0);
  }
}

// Add new interrupt handler for visitor reset button
void IRAM_ATTR handleVisitorResetButton() {
  unsigned long currentTime = millis();
  
  if (digitalRead(visitorResetPin) == LOW) {
    // Button pressed - do nothing on press
  } else {
    // Button released
    if (currentTime - lastVisitorButtonPress < doublePressInterval) {
      // Double press detected
      visitorButtonPressCount++;
      if (visitorButtonPressCount >= 2) {
        visitorCounter = 0;
        // Save to preferences
        prefs.begin("config", false);
        prefs.putInt("visitor_counter", visitorCounter);
        prefs.end();
        // Publish reset count
        if (client.connected()) {
          String topic = makeSensorTopic("pengunjung", nomor_perangkat);
          publishMqtt(topic, "0", false, 0);
        }
        Serial.println("Visitor counter reset to 0!");
        visitorButtonPressCount = 0;
      }
    } else {
      // Single press, reset counter
      visitorButtonPressCount = 1;
    }
    lastVisitorButtonPress = currentTime;
  }
}

void setup() {
  Serial.begin(115200);
  delay(1000);

  Serial.println("=========== START =============");
  
  if (!SPIFFS.begin(true)) {
    Serial.println("Failed to mount SPIFFS");
    return;
  }

  // Initialize sensor pins
  pinMode(trigPin, OUTPUT);
  pinMode(echoPin, INPUT);
  pinMode(irRightPin, INPUT_PULLUP);
  pinMode(irLeftPin, INPUT_PULLUP);
  pinMode(irSensorPin, INPUT);
  pinMode(buttonPin, INPUT_PULLUP);
  pinMode(pumpPin, OUTPUT);
  pinMode(batteryPin, INPUT);
  pinMode(ledPin, OUTPUT);
  pinMode(resetButtonPin, INPUT_PULLUP);
  pinMode(visitorResetPin, INPUT_PULLUP);  // Initialize visitor reset button pin
  pinMode(bauPin, INPUT);
  pinMode(tisuDT, INPUT);
  pinMode(tisuSCK, INPUT);
  digitalWrite(pumpPin, LOW);
  digitalWrite(ledPin, LOW);

  analogReadResolution(12);

  attachInterrupt(digitalPinToInterrupt(buttonPin), handleResetButton, CHANGE);
  attachInterrupt(digitalPinToInterrupt(resetButtonPin), handleConfigResetButton, CHANGE);
  attachInterrupt(digitalPinToInterrupt(visitorResetPin), handleVisitorResetButton, CHANGE);  // Add visitor reset interrupt

  prefs.begin("config", true);
  init_done = prefs.getBool("init_done", false);
  config_done = prefs.getBool("config_done", false);
  prefs.end();

  String macAddress = getMacAddress();
  Serial.print("MAC Address: ");
  Serial.println(macAddress);

  // Reset firstPeopleCountSend flag when device starts
  firstPeopleCountSend = true;

  if (init_done) {
    Serial.println("Initialization already complete. Skipping AP and web server.");
    setVariablesFromPreferences();

    // Check for power save mode - when only one sensor is enabled
    if ((tisu == "on" && sabun != "on" && okupansi != "on" && bau != "on" && pengunjung != "on") ||
        (tisu != "on" && sabun == "on" && okupansi != "on" && bau != "on" && pengunjung != "on") ||
        (tisu != "on" && sabun != "on" && okupansi == "on" && bau != "on" && pengunjung != "on")) {
      useSavePowerMode = true;
      Serial.println("Using save power mode");
    }
    // Check for deep sleep mode - only when only bau is on
    else if (bau == "on" && okupansi != "on" && pengunjung != "on" && sabun != "on" && tisu != "on") {
      useDeepSleep = true;
      Serial.println("Using deep sleep mode.");
    } else {
      Serial.println("Using normal mode.");
    }
  } else {
    startWebServer();
    if (!MDNS.begin("esp32")) {
      Serial.println("Error setting up MDNS responder!");
    } else {
      Serial.println("MDNS responder started as http://esp32.local");
    }
  }

  // Initialize scale
  if(tisu == "on"){
    scale.begin(tisuDT, tisuSCK);
    scale.set_scale(341);
    scale.tare();
    Serial.println("Scale initialized and tared");
  }
}

void loop() {
  if (init_done && !config_done) {
    if (serverRunning) {
      server.end();
      WiFi.softAPdisconnect(true);
      dnsServer.stop();
      serverRunning = false;
      Serial.println("Stopped web server and AP");
    }

    if (WiFi.status() != WL_CONNECTED) {
      if (connectToWiFi()) {
        Serial.println("WiFi connected successfully");
      } else {
        Serial.println("WiFi connection failed");
        digitalWrite(ledPin, LOW);
        return;
      }
    }

    if (WiFi.status() == WL_CONNECTED && !client.connected()) {
      if (connectToMQTT()) {
        subscribeToMQTTTopics();
        Serial.println("MQTT connected successfully");
      } else {
        Serial.println("MQTT connection failed");
        digitalWrite(ledPin, LOW);
        return;
      }
    }

    if (WiFi.status() == WL_CONNECTED && client.connected()) {
      if (!company.isEmpty()) {
        String macAddress = getMacAddress();
        String topic = createMQTTTopic("config", company, macAddress);
        String payload = prepareMQTTPayload();
        if (publishMqtt(topic, payload, true)) {
          Serial.println("Config published successfully");
          prefs.begin("config", false);
          prefs.putBool("config_done", true);
          prefs.end();
          Serial.println("Restarting device...");
          delay(1000);
          ESP.restart();
        } else {
          Serial.println("Failed to publish config");
          return;
        }
      } else {
        Serial.println("Company name is empty, cannot publish");
        return;
      }
    }
  } else if (useSavePowerMode) {
    // Only connect if we need to send data
    bool needToConnect = false;
    
    // Check if we need to send soap data
    if (sabun == "on" && soapCounter > 0 && soapCounter % 5 == 0 && !pumpActive && soapCounter != lastSentCount) {
      needToConnect = true;
    }
    
    // Check if we need to send tisu data
    if (tisu == "on" && millis() - lastTisuPublish >= tisuPublishInterval) {
      needToConnect = true;
    }
    
    // Check if we need to send okupansi data
    if (okupansi == "on") {
      updateMonitorOkupansi();
      if (isOccupied != previousOccupied) {
        needToConnect = true;
        previousOccupied = isOccupied;
      }
    }

    // Check if we need to send bau data
    if (bau == "on" && millis() - lastBauPublish >= bauPublishInterval) {
      needToConnect = true;
    }

    // Check if we need to send battery data
    if (millis() - lastBatteryPublish >= batteryPublishInterval) {
      needToConnect = true;
    }

    // Check if we need to do periodic MQTT connection check
    if (millis() - lastMqttCheck >= mqttCheckInterval) {
      needToConnect = true;
    }

    // Check if we need to handle soap reset
    if (resetFlag) {
      needToConnect = true;
    }

    // Connect to WiFi and MQTT only if needed
    if (needToConnect) {
      if (WiFi.status() != WL_CONNECTED) {
        if (connectToWiFi()) {
          Serial.println("WiFi connected successfully");
        } else {
          Serial.println("WiFi connection failed");
          digitalWrite(ledPin, LOW);
          delay(5000);
          return;
        }
      }

      if (WiFi.status() == WL_CONNECTED && !client.connected()) {
        if (connectToMQTT()) {
          subscribeToMQTTTopics();
          Serial.println("MQTT connected successfully");
        } else {
          Serial.println("MQTT connection failed");
          digitalWrite(ledPin, LOW);
          WiFi.disconnect();
          delay(5000);
          return;
        }
      }

      // Send data if connected
      if (WiFi.status() == WL_CONNECTED && client.connected()) {
        // Handle soap reset first
        if (resetFlag) {
          handleSoapReset();
          // Send reset message to MQTT
          String sabunTopic = makeSensorTopic("sabun", nomor_perangkat);
          sendDataSabun(sabunTopic);
        }
        
        // If this is just a periodic MQTT check, only subscribe to topics
        if (sabun == "on" && soapCounter > 0 && soapCounter % 5 == 0 && !pumpActive && soapCounter != lastSentCount) {
          String sabunTopic = makeSensorTopic("sabun", nomor_perangkat);
          sendDataSabun(sabunTopic);
          lastSentCount = soapCounter;
          prefs.begin("config", false);
          prefs.putInt("soap_last_sent", lastSentCount);
          prefs.end();
        }
        
        if (tisu == "on") {
          String tisuTopic = makeSensorTopic("tisu", nomor_perangkat);
          sendDataTisu(tisuTopic);
          lastTisuPublish = millis();
        }

        if (okupansi == "on" && isOccupied != previousOccupied) {
          String okupansiTopic = makeSensorTopic("okupansi", nomor_perangkat);
          String status = isOccupied ? "occupied" : "vacant";
          String payload = status + ";" + nomor_toilet;
          publishMqtt(okupansiTopic, payload, false, 0);
        }

        if (bau == "on") {
          String bauTopic = makeSensorTopic("bau", nomor_perangkat);
          sendDataBau(bauTopic);
          lastBauPublish = millis();
        }

        // Send battery data
        if (millis() - lastBatteryPublish >= batteryPublishInterval) {
          String batteryTopic = makeSensorTopic("baterai", nomor_perangkat);
          sendDataBaterai(batteryTopic);
          lastBatteryPublish = millis();
        }

        if (millis() - lastMqttCheck >= mqttCheckInterval) {
          subscribeToMQTTTopics();
          lastMqttCheck = millis();
          Serial.println("Performed periodic MQTT topic subscription check");
        }
      }

      // Disconnect after sending data or checking MQTT
      client.disconnect();
      WiFi.disconnect();
      digitalWrite(ledPin, LOW);
      Serial.println("Disconnected from WiFi and MQTT");
    }
    
    // Always update soap dispenser regardless of connection
    if (sabun == "on") {
      updateSoapDispenser();
    }

    delay(50);
  } else if (useDeepSleep) {
    Serial.println("Deep Sleep Mode Started");

    if (WiFi.status() != WL_CONNECTED) {
      connectToWiFi();
      digitalWrite(ledPin, LOW);
    }
    if (WiFi.status() == WL_CONNECTED && !client.connected()) {
      connectToMQTT();
      subscribeToMQTTTopics();
    }

    if (WiFi.status() == WL_CONNECTED && client.connected()) {
      // Read and publish bau sensor
      if (bau == "on") {
        String bauTopic = makeSensorTopic("bau", nomor_perangkat);
        sendDataBau(bauTopic);
      }

      // Read and publish battery
      String batteryTopic = makeSensorTopic("baterai", nomor_perangkat);
      sendDataBaterai(batteryTopic);
    }

    Serial.println("Entering deep sleep for 30 seconds...");
    esp_sleep_enable_timer_wakeup(30 * 1000000);
    esp_deep_sleep_start();

  } else {
    if (!serverRunning && WiFi.status() != WL_CONNECTED) {
      Serial.println("WiFi disconnected, attempting to reconnect...");
      connectToWiFi();
      digitalWrite(ledPin, LOW);
    }

    if (WiFi.status() == WL_CONNECTED && !client.connected()) {
      if (millis() - lastMqttReconnectAttempt > mqttReconnectInterval) {
        Serial.println("Attempting MQTT reconnect...");
        lastMqttReconnectAttempt = millis();
        if (connectToMQTT()) {
          subscribeToMQTTTopics();
          lastMqttReconnectAttempt = 0;
        } else {
          digitalWrite(ledPin, LOW);
        }
      }
    }

    if (WiFi.status() == WL_CONNECTED && client.connected()) {
      client.loop();
      if (okupansi == "on") {
        updateMonitorOkupansi();
        if (isOccupied != previousOccupied) {
          String okupansiTopic = makeSensorTopic("okupansi", nomor_perangkat);
          String status = isOccupied ? "occupied" : "vacant";
          String payload = status + ";" + nomor_toilet;
          publishMqtt(okupansiTopic, payload, false, 0);
          previousOccupied = isOccupied;
        }
      }
      if (pengunjung == "on") {
        updatePeopleCounter();
      }
      if (sabun == "on") {
        updateSoapDispenser();
        if (soapCounter > 0 && soapCounter % 5 == 0 && !pumpActive && soapCounter != lastSentCount) {
          String sabunTopic = makeSensorTopic("sabun", nomor_perangkat);
          sendDataSabun(sabunTopic);
          lastSentCount = soapCounter;
          // Save lastSentCount after update
          prefs.begin("config", false);
          prefs.putInt("soap_last_sent", lastSentCount);
          prefs.end();
        }
        // Send reset message if reset was just handled
          if (resetFlag) {
            String sabunTopic = makeSensorTopic("sabun", nomor_perangkat);
            sendDataSabun(sabunTopic);
          }
      }
      if (bau == "on") {
        if (millis() - lastBauPublish >= bauPublishInterval) {
          String bauTopic = makeSensorTopic("bau", nomor_perangkat);
          sendDataBau(bauTopic);
          lastBauPublish = millis();
        }
      }
      if (tisu == "on") {
        if (millis() - lastTisuPublish >= tisuPublishInterval) {
          String tisuTopic = makeSensorTopic("tisu", nomor_perangkat);
          sendDataTisu(tisuTopic);
          lastTisuPublish = millis();
        }
      }

      if (millis() - lastBatteryPublish >= batteryPublishInterval) {
        String batteryTopic = makeSensorTopic("baterai", nomor_perangkat);
        sendDataBaterai(batteryTopic);
        lastBatteryPublish = millis();
      }
    }

    delay(50);
  }
}
