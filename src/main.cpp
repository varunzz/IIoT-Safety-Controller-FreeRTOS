#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/queue.h>
#include <freertos/semphr.h>

// Display, Sensor & Network Libraries
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <DHT.h>
#include <ESP32Servo.h>
#include <WiFi.h>
#include <PubSubClient.h>

// Hardware Pin Definitions
#define DHTPIN 15
#define DHTTYPE DHT22
#define PIR_PIN 13
#define SERVO_PIN 18
#define LED_R 2
#define LED_G 4
#define LED_B 5

// Network Credentials (Wokwi Virtual Router)
const char* ssid = "Wokwi-GUEST";
const char* password = "";

// MQTT Broker Settings (Free Public HiveMQ)
const char* mqtt_server = "broker.hivemq.com";
const char* mqtt_topic = "Project/smarthome/telemetry";

// Object Initializations
Adafruit_SSD1306 display(128, 64, &Wire, -1);
DHT dht(DHTPIN, DHTTYPE);
Servo mitigationServo;
WiFiClient espClient;
PubSubClient mqttClient(espClient);

// 1. Data Structures
typedef struct {
  float temperature;
  int smokeLevel;
  bool motionDetected;
} SensorData;

typedef struct {
  float temp;
  int smoke;
  bool motion;
  bool isAlarmActive;
} DisplayState;

DisplayState currentSystemState = {0.0, 0, false, false};

// 2. RTOS Handles
QueueHandle_t sensorQueue;
SemaphoreHandle_t displayMutex;

// 3. Task Prototypes
void TaskReadSensors(void *pvParameters);
void TaskHazardMonitor(void *pvParameters);
void TaskUpdateDisplay(void *pvParameters);
void TaskUpdateCloud(void *pvParameters); // Our new Cloud Task

void setup() {
  Serial.begin(115200);
  
  // Initialize Hardware
  display.begin(SSD1306_SWITCHCAPVCC, 0x3C);
  display.clearDisplay();
  display.setTextColor(WHITE);
  
  dht.begin();
  mitigationServo.attach(SERVO_PIN);
  mitigationServo.write(0);

  pinMode(LED_R, OUTPUT);
  pinMode(LED_G, OUTPUT);
  pinMode(LED_B, OUTPUT);
  pinMode(PIR_PIN, INPUT);

  // Configure MQTT Broker
  mqttClient.setServer(mqtt_server, 1883);

  // RTOS Setup
  sensorQueue = xQueueCreate(5, sizeof(SensorData));
  displayMutex = xSemaphoreCreateMutex();

  if (sensorQueue != NULL && displayMutex != NULL) {
    xTaskCreate(TaskReadSensors, "ReadSensors", 2048, NULL, 1, NULL);
    xTaskCreate(TaskHazardMonitor, "HazardMonitor", 2048, NULL, 3, NULL); 
    xTaskCreate(TaskUpdateDisplay, "UpdateDisplay", 4096, NULL, 2, NULL); 
    xTaskCreate(TaskUpdateCloud, "UpdateCloud", 4096, NULL, 1, NULL); // Priority 1 (Low)
  }
}

void loop() {}

// --- TASK DEFINITIONS ---

void TaskReadSensors(void *pvParameters) {
  pinMode(34, INPUT);
  while (1) {
    SensorData currentReadings;
    currentReadings.smokeLevel = analogRead(34); 
    currentReadings.temperature = dht.readTemperature();
    currentReadings.motionDetected = digitalRead(PIR_PIN);
    
    xQueueSend(sensorQueue, &currentReadings, 10);
    vTaskDelay(2000 / portTICK_PERIOD_MS); 
  }
}

void TaskHazardMonitor(void *pvParameters) {
  SensorData receivedData;
  while (1) {
    if (xQueueReceive(sensorQueue, &receivedData, portMAX_DELAY) == pdPASS) {
      bool alarmFlag = false;
      
      if (receivedData.temperature > 45.0 || receivedData.smokeLevel > 3000) {
        alarmFlag = true;
        digitalWrite(LED_R, HIGH);
        digitalWrite(LED_G, LOW);
        digitalWrite(LED_B, LOW);
        mitigationServo.write(90); 
      } else if (receivedData.motionDetected) {
        digitalWrite(LED_R, LOW);
        digitalWrite(LED_G, LOW);
        digitalWrite(LED_B, HIGH); 
        mitigationServo.write(0);
      } else {
        digitalWrite(LED_R, LOW);
        digitalWrite(LED_G, HIGH); 
        digitalWrite(LED_B, LOW);
        mitigationServo.write(0); 
      }

      if (xSemaphoreTake(displayMutex, portMAX_DELAY) == pdTRUE) {
        currentSystemState.temp = receivedData.temperature;
        currentSystemState.smoke = receivedData.smokeLevel;
        currentSystemState.motion = receivedData.motionDetected;
        currentSystemState.isAlarmActive = alarmFlag;
        xSemaphoreGive(displayMutex); 
      }
    }
  }
}

void TaskUpdateDisplay(void *pvParameters) {
  DisplayState localState;
  while (1) {
    if (xSemaphoreTake(displayMutex, portMAX_DELAY) == pdTRUE) {
      localState = currentSystemState;
      xSemaphoreGive(displayMutex);
    }

    display.clearDisplay();
    display.setTextSize(1);
    display.setCursor(0, 0);
    display.println("Local Control Panel");

    display.setCursor(0, 16);
    display.print("Smoke ADC: ");
    display.println(localState.smoke);

    display.setCursor(0, 26);
    display.print("Temp (C): ");
    display.println(localState.temp);
    
    display.setCursor(0, 36);
    display.print("Motion: ");
    display.println(localState.motion ? "DETECTED" : "CLEAR");

    display.setCursor(0, 50);
    if (localState.isAlarmActive) {
      display.println("STATUS: ALARM!");
    } else {
      display.println("STATUS: NORMAL");
    }

    display.display(); 
    vTaskDelay(500 / portTICK_PERIOD_MS);
  }
}

// --- NEW TASK: CLOUD COMMUNICATOR ---
void TaskUpdateCloud(void *pvParameters) {
  DisplayState localState;
  char payload[128]; // Buffer to hold our JSON string

  while (1) {
    // 1. Check Wi-Fi Connection
    if (WiFi.status() != WL_CONNECTED) {
      Serial.println("Connecting to Wi-Fi...");
      WiFi.begin(ssid, password);
      while (WiFi.status() != WL_CONNECTED) {
        vTaskDelay(500 / portTICK_PERIOD_MS);
      }
      Serial.println("Wi-Fi Connected!");
    }

    // 2. Check MQTT Connection
    if (!mqttClient.connected()) {
      Serial.println("Connecting to MQTT Broker...");
      // Use a random client ID so it doesn't clash with other devices
      String clientId = "ESP32Client-" + String(random(0, 0xffff), HEX);
      if (mqttClient.connect(clientId.c_str())) {
        Serial.println("MQTT Connected!");
      }
    }

    // 3. Send Data if Connected
    if (mqttClient.connected()) {
      mqttClient.loop(); // Keep the MQTT connection alive

      // Safely grab the latest data from the Mutex
      if (xSemaphoreTake(displayMutex, portMAX_DELAY) == pdTRUE) {
        localState = currentSystemState;
        xSemaphoreGive(displayMutex);
      }

      // Format the data as a JSON string using snprintf
      snprintf(payload, sizeof(payload), 
               "{\"temp\":%.1f, \"smoke\":%d, \"motion\":%s, \"alarm\":%s}", 
               localState.temp, 
               localState.smoke, 
               localState.motion ? "true" : "false", 
               localState.isAlarmActive ? "true" : "false");

      // Publish to the cloud
      mqttClient.publish(mqtt_topic, payload);
      Serial.print("Published to Cloud: ");
      Serial.println(payload);
    }

    // Wait 5 seconds before pushing data again (Save bandwidth)
    vTaskDelay(5000 / portTICK_PERIOD_MS);
  }
}