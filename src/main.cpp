#include "FS.h"
#include "SPIFFS.h"
#include <Wire.h>
#include <MPU6050.h>
#include <ArduinoJson.h>
#include <PubSubClient.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <TimeLib.h>
#include <WiFiUdp.h>
#include <NTPClient.h>

// URL used for http connection
String url = "https://fakestoreapi.com/products";

// WiFi network configuration
// Set network name
const char *ssid = "PUBLICA";
// Set network password
const char *password = "";

// MQTT server configuration
const char *mqttServer = "broker.hivemq.com";
const int mqttPort = 1883;
const char *clientName = "ESP32ClienteIcesiA00381213";

// Topic configuration
const char *topic = "test/icesi/dlp";

// Time configuration
WiFiUDP ntpUDP;

// -18000 seconds for UTC-5
NTPClient timeClient(ntpUDP, "pool.ntp.org", -18000, 60000);


// Object WiFiClient
WiFiClient wifiClient;

// Object PubSubClient
PubSubClient mqttClient(wifiClient);
HTTPClient http;

// First sensor MPU6050
MPU6050 mpu1;      
// Second sensor MPU6050    
MPU6050 mpu2;

JsonDocument measurementsDoc;

// Definition of metadata
const char *evaluatedID = "1";
String typeOfTest;
String date;
String timeOfTest;
String location;

void saveJson(const char *path, JsonDocument &jsonDoc){
  // Create or overwrite JSON file in SPIFFS
  File file = SPIFFS.open(path, FILE_WRITE);
  if (!file) {
    Serial.println("Error al abrir el archivo para escribir");
    return;
  }

  // Write the JSON object (all measurements) to file
  serializeJson(jsonDoc, file);
  file.close();
}

// Function to read and display the JSON file on the serial port
void showJson(const char *path) {
  // Open the JSON file in SPIFFS
  File file = SPIFFS.open(path, FILE_READ);
  if (!file) {
    Serial.println("Error: No se pudo abrir el archivo para leer.");
    return;
  }

  JsonDocument jsonObjectDoc;
  DeserializationError error = deserializeJson(jsonObjectDoc, file);
  if (error) {
    Serial.print("Error al parsear el archivo JSON: ");
    Serial.println(error.f_str());
    file.close();
    return;
  }

  Serial.println("Contenido del archivo JSON:");
  // Display the JSON in a readable form
  serializeJsonPretty(jsonObjectDoc, Serial); 
  Serial.println();
  file.close();
}

// Function to collect data from a specific sensor
void collectSensorData(MPU6050 &mpu, const char *sensorName, const String &sampleName) {
  // Read sensor data MPU6050
  int16_t ax, ay, az;
  int16_t gx, gy, gz;
  mpu.getAcceleration(&ax, &ay, &az);
  mpu.getRotation(&gx, &gy, &gz);

  JsonObject sample = measurementsDoc["datos"][sensorName].createNestedObject(sampleName);
  sample["ax"] = ax;
  sample["ay"] = ay;
  sample["az"] = az;
  sample["gx"] = gx;
  sample["gy"] = gy;
  sample["gz"] = gz;
}

void sendJsonAsPostRequest(const char *filename) {
  File file = SPIFFS.open(filename, FILE_READ);
  if (!file) {
    Serial.println("Error al abrir el archivo para leer");
    return;
  }

  String jsonContent = file.readString();
  file.close();

  // Prepare and send the POST request
  http.begin(url.c_str());
  http.addHeader("Content-Type", "application/json");
  int httpResponseCode = http.POST(jsonContent.c_str());

  if (httpResponseCode > 0) {
    Serial.printf("HTTP Response code: %d\n", httpResponseCode);
    String payload = http.getString();
    Serial.println(payload);
  }
  else {
    Serial.printf("Error code: %d\n", httpResponseCode);
  }

  http.end();
}

// A 5-second test will start for both sensors
void doTest(int testDurationMs, int samplesPerSecond) {
  Serial.println("Iniciando toma de datos...");

  // Metadata initialization
  timeClient.update();
    
  // Get time from NTP
  unsigned long epochTime = timeClient.getEpochTime();

  // Convert time to date and time
  setTime(epochTime);
    
  // Date and time formatting
  date = String(day()) + "/" + String(month()) + "/" + String(year()); // DD/MM/YYYY
  timeOfTest = String(hour()) + ":" + String(minute()) + ":" + String(second()); // HH:MM:SS

  date = String(day() < 10 ? "0" : "") + String(day()) + "/" +
       String(month() < 10 ? "0" : "") + String(month()) + "/" +
       String(year());

  timeOfTest = String(hour() < 10 ? "0" : "") + String(hour()) + ":" +
               String(minute() < 10 ? "0" : "") + String(minute()) + ":" +
               String(second() < 10 ? "0" : "") + String(second());

  measurementsDoc["metadatos"]["evaluadoID"] = evaluatedID;
  measurementsDoc["metadatos"]["tipoPrueba"] = typeOfTest;
  measurementsDoc["metadatos"]["fecha"] = date;
  measurementsDoc["metadatos"]["hora"] = timeOfTest;
  measurementsDoc["metadatos"]["ubicacion"] = location;

  int totalSamples = (testDurationMs * samplesPerSecond) / 1000;
  int delayBetweenSamples = 1000 / samplesPerSecond;

  // Test start time
  unsigned long startTime = millis();

  for (int i = 0; i < totalSamples; i++) {

    // Time at the beginning of each sampling
    unsigned long sampleStartTime = millis();

    String sampleName = "muestra" + String(i + 1);

    collectSensorData(mpu1, "sensor1", sampleName);
    collectSensorData(mpu2, "sensor2", sampleName);

    // Calculate the time remaining to maintain the sampling rate
    unsigned long elapsedTime = millis() - sampleStartTime;
    if(elapsedTime < delayBetweenSamples) {
      delay(delayBetweenSamples - elapsedTime);
    }
  }

  unsigned long testDuration = millis() - startTime;
  Serial.printf("Toma de datos finalizada. Duración real: %lu ms\n", testDuration);

  // Save and display JSON files for each sensor
  saveJson("/sensor_data.json", measurementsDoc);
  showJson("/sensor_data.json");

  // Send the JSON as a POST request
  Serial.println("Enviando datos...");
  sendJsonAsPostRequest("/sensor_data.json");
  Serial.println("Envío de datos completado.");
}

void handleMqttMessage(const String message) {
  // Convert the message to lowercase for case insensitive case comparison
  String lowerMessage = message;
  lowerMessage.toLowerCase();

  if (lowerMessage == "init") {
    Serial.println("Comando 'init' recibido. Iniciando test...");
    // Call doTest with the desired parameters (e.g. 5 seconds at 20 Hz)
    doTest(5000, 20);
  }
  else {
    Serial.println("Comando no reconocido: " + message);
  }
}

void callback(char *topic, byte *payload, unsigned int length) {
  // Create a String with the received payload
  String message;
  for (unsigned int i = 0; i < length; i++) {
    message += (char)payload[i];
  }

  // Print the received message
  Serial.print("Mensaje recibido en el topic ");
  Serial.print(topic);
  Serial.print(": ");
  Serial.println(message);

  // Call handleMqttMessage with the received message
  handleMqttMessage(message);
}

void keepAlive() {
  if (!mqttClient.connected()) {
    Serial.println("Reconectando");
    // Attempts to connect to the MQTT server
    while (!mqttClient.connected()) {
      Serial.println("Intentando conectar al servidor MQTT...");
      if (mqttClient.connect(clientName)) {
        Serial.println("¡Conectado al servidor MQTT!");
      }
      else {
        Serial.print("Error al conectar: ");
        Serial.println(mqttClient.state());
        delay(5000);
      }
    }
    mqttClient.subscribe(topic);
  }
}



void setup() {
  Serial.begin(115200);

  // Initialize the SPIFFS file system
  if (!SPIFFS.begin(true)) {
    Serial.println("Error al montar SPIFFS");
    return;
  }
  Serial.println("SPIFFS montado correctamente");

  // Initialize the MPU6050 sensors
  Wire.begin();
  mpu1.initialize();
  mpu2.initialize();

  if (mpu1.testConnection()) {
    Serial.println("Conexión exitosa con el MPU6050 1");
  }
  else {
    Serial.println("Conexión fallida con el MPU6050 1");
    return;
  }

  if (mpu2.testConnection()) {
    Serial.println("Conexión exitosa con el MPU6050 2");
  }
  else {
    Serial.println("Conexión fallida con el MPU6050 2");
    return;
  }

  // Connect to Wi-Fi network
  Serial.println("Conectando a la red Wi-Fi...");
  WiFi.disconnect();
  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) {
    delay(5000);
    Serial.print(".");
  }

  // Initializes the MQTT client
  mqttClient.setServer(mqttServer, mqttPort);
  mqttClient.setCallback(callback);

  // Attempts to connect to the MQTT server
  while (!mqttClient.connected()) {
    Serial.println("Intentando conectar al servidor MQTT...");
    if (mqttClient.connect(clientName)) {
      Serial.println("¡Conectado al servidor MQTT!");
    }
    else {
      Serial.print("Error al conectar: ");
      Serial.println(mqttClient.state());
      delay(5000);
    }
  }

  // Subscribe to the topic
  mqttClient.subscribe(topic);

  // Initialize the NTP client
  timeClient.begin();
  // Synchronize time at startup
  timeClient.update();

  Serial.println("Escribe 'init' para comenzar con la prueba");
}

void loop() {

  mqttClient.loop();
  keepAlive();

  // Update NTP time
  timeClient.update();

  // Read input from serial monitor
  if (Serial.available()) {
    String input = Serial.readStringUntil('\n');
    // Remove blank spaces around
    input.trim();

    if (input.equalsIgnoreCase("init")) {
      Serial.println("Comando 'init' recibido. Iniciando prueba...");
      // Request test type and location
      Serial.println("Ingrese el tipo de prueba:");
      while (!Serial.available());
      typeOfTest = Serial.readStringUntil('\n');
      typeOfTest.trim();

      Serial.println("Ingrese la ubicación:");
      while (!Serial.available());
      location = Serial.readStringUntil('\n');
      location.trim();
      // Start the test
      doTest(5000, 20);
    } else {
      Serial.println("Comando no reconocido: " + input);
    }
  }
}

void serialEvent() {
  
}