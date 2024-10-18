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
String url = "http://192.168.86.23:8080/hardware_controller/receive_data";

// WiFi network configuration
// Set network name
const char *ssid = "ABC";
// Set network password
const char *password = "abi11261119";

// MQTT server configuration
const char *mqttServer = "broker.hivemq.com";
const int mqttPort = 1883;
const char *clientName = "ESP32ClienteMicasaA00381213";

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
String idString;
String typeOfTest;
String date;
String timeOfTest;
String location;

// Led Pins

const int ledPinG = 4;
const int ledPinB = 5;


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

  JsonObject sample = measurementsDoc["data"][sensorName].createNestedObject(sampleName);
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

  digitalWrite(ledPinG, HIGH);
  digitalWrite(ledPinB,LOW);
  
   // Metadata initialization
  timeClient.update();
    
  // Get time from NTP
  unsigned long epochTime = timeClient.getEpochTime();

  // Convert time to date and time
  setTime(epochTime);
    
  // Date and time formatting
  date = String(day()) + ":" + String(month()) + ":" + String(year()); // DD:MM:YYYY
  timeOfTest = String(hour()) + ":" + String(minute()) + ":" + String(second()); // HH:MM:SS

  date = String(day() < 10 ? "0" : "") + String(day()) + "/" +
       String(month() < 10 ? "0" : "") + String(month()) + "/" +
       String(year());

  timeOfTest = String(hour() < 10 ? "0" : "") + String(hour()) + ":" +
               String(minute() < 10 ? "0" : "") + String(minute()) + ":" +
               String(second() < 10 ? "0" : "") + String(second());

  measurementsDoc["metadata"]["evaluatedId"] = idString;
  measurementsDoc["metadata"]["typeOfTest"] = typeOfTest;
  measurementsDoc["metadata"]["dateAndTime"] = date + ":" + timeOfTest;

  int totalSamples = (testDurationMs * samplesPerSecond) / 1000;
  int delayBetweenSamples = 1000 / samplesPerSecond;

  // Test start time
  unsigned long startTime = millis();

  for (int i = 0; i < totalSamples; i++) {

    // Time at the beginning of each sampling
    unsigned long sampleStartTime = millis();

    String sampleName = "sample" + String(i + 1);

    collectSensorData(mpu1, "sensor1", sampleName);
    collectSensorData(mpu2, "sensor2", sampleName);

    // Calculate the time remaining to maintain the sampling rate
    unsigned long elapsedTime = millis() - sampleStartTime;
    if(elapsedTime < delayBetweenSamples) {
      delay(delayBetweenSamples - elapsedTime);
    }
  }

  unsigned long testDuration = millis() - startTime;

  digitalWrite(ledPinG,LOW);
  digitalWrite(ledPinB, HIGH);

  Serial.printf("Toma de datos finalizada. Duración real: %lu ms\n", testDuration);

  // Save and display JSON files for each sensor
  saveJson("/sensor_data.json", measurementsDoc);
  showJson("/sensor_data.json");

  // Send the JSON as a POST request
  Serial.println("Enviando datos...");
  sendJsonAsPostRequest("/sensor_data.json");
  Serial.println("Envío de datos completado.");
}

void handleMqttMessage(const String &message) {
  // Convert the message to lowercase for case-insensitive comparison
  String lowerMessage = message;
  lowerMessage.toLowerCase();

  // Check if the message starts with "init~~"
  if (lowerMessage.startsWith("init~~")) {
    Serial.println("Command 'init' received. Starting test...");

    // Find the delimiters '~~'
    int firstDelimiterIndex = lowerMessage.indexOf("~~");
    int secondDelimiterIndex = lowerMessage.indexOf("~~", firstDelimiterIndex + 2);

    // Check if delimiters are found
    if (firstDelimiterIndex == -1 || secondDelimiterIndex == -1) {
      Serial.println("Error: Invalid format of the message.");\
      return;
    }

    // Extract the evaluated ID
    idString = lowerMessage.substring(firstDelimiterIndex + 2, secondDelimiterIndex);
    
    // Extract the type of test
    typeOfTest = lowerMessage.substring(secondDelimiterIndex + 2);

    // Call doTest with the parameters obtained (e.g., 5 seconds, 20 Hz)
    doTest(1000, 5);
  } else {
    Serial.println("Unrecognized command: " + message);
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
  

  if (String(topic) == "test/icesi/dlp/check"){
    handleStatusCheck();
  }else{
    handleMqttMessage(message);
  }
}
void handleStatusCheck(){
  String responseTopic = "test/icesi/dlp/check_response";

  bool sensorsConnected = checkSensors();

  bool mqttConnected = mqttClient.connected();

  if (sensorsConnected && mqttConnected) {
    // Todo está bien, publicar respuesta "ok"
    String payload = "{\"status\": \"ok\"}";
    mqttClient.publish(responseTopic.c_str(), payload.c_str());
    Serial.println("Respuesta de estado: OK");
  } else {
    // Si hay un error, publicar el error correspondiente
    String errorMessage = "";
    if (!sensorsConnected) {
      errorMessage = "Error: Sensor desconectado";
    } else if (!mqttConnected) {
      errorMessage = "Error: Desconexión del servidor MQTT";
    }
    String payload = "{\"status\": \"error\", \"message\": \"" + errorMessage + "\"}";
    mqttClient.publish(responseTopic.c_str(), payload.c_str());
    Serial.println("Respuesta de estado con error: " + errorMessage);
  }
}


bool checkSensors(){
  bool controlFlag1 = false;
  bool controlFlag2 = false;
  if (mpu1.testConnection()) {
    Serial.println("Conexión exitosa con el MPU6050 1");
    controlFlag1 = true;
  }
  else {
    Serial.println("Conexión fallida con el MPU6050 1");
  }

  if (mpu2.testConnection()) {
    Serial.println("Conexión exitosa con el MPU6050 2");
    bool controlFlag2 = true;
  }
  else {
    Serial.println("Conexión fallida con el MPU6050 2");
  }
  return controlFlag1 && controlFlag2;
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

  // Setting up the led Pins
  pinMode(ledPinG, OUTPUT);
  pinMode(ledPinB, OUTPUT);

  //Starting value: OFF

  digitalWrite(ledPinG, LOW);
  digitalWrite(ledPinB, HIGH);

  // Initialize the SPIFFS file system
  if (!SPIFFS.begin(true)) {
    Serial.println("Error al montar SPIFFS");
    return;
  }
  Serial.println("SPIFFS montado correctamente");

  // Initialize the MPU6050 sensors
  Wire.begin();
  mpu1.initialize();
  //mpu2.initialize();

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

  digitalWrite(ledPinB, HIGH);
  digitalWrite(ledPinG, LOW);

  // Update NTP time
  timeClient.update();

  // Read input from serial monitor
  if (Serial.available()) {
    String input = Serial.readStringUntil('\n');
    // Remove blank spaces around
    input.trim();

    if (input.equalsIgnoreCase("init")) {
      Serial.println("Comando 'init' recibido. Iniciando prueba...");
      digitalWrite(ledPinB, LOW);
      // Request test type and location
      Serial.println("Ingrese el tipo de prueba:");
      while (!Serial.available());
      typeOfTest = Serial.readStringUntil('\n');
      typeOfTest.trim();

      // Start the test
      doTest(1000, 5); //1 sec a 5 herrtz
    } else {
      Serial.println("Comando no reconocido: " + input);
    }
  }
}

void serialEvent() {
  
}