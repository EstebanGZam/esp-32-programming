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

// Constants
const int TEST_DURATION_MS = 1000; // Test duration in milliseconds (1 second)
const int SAMPLES_PER_SECOND = 5;  // Number of samples per second// URL used for http connection

String url = "http://IP_ADDRESS:8080/hardware_controller/receive_data";

// WiFi network configuration
// Set network name
const char *ssid = "NETWORK_NAME";
// Set network password
const char *password = "PASSWORD";

// MQTT server configuration
const char *mqttServer = "broker.hivemq.com";
const int mqttPort = 1883;
const char *clientName = "ESP32ClienteMicasaA00395902";

// Topic configuration
const char *topic = "test/icesi/dlp";

bool testRunning = false;

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
const int ledPinR = 14;
const int ledPinG = 13;
const int ledPinB = 12;

// Function to get a description of the error based on the connection status
const char *getMqttErrorDescription(int state)
{
  switch (state)
  {
  case 0: // Successful connection
    return "Conexión exitosa.";
  case -1: // Generic connection error
    return "Error al intentar conectar.";
  case -2: // Connection refused
    return "Conexión rechazada.";
  case -3: // Connection timeout
    return "Tiempo de conexión agotado.";
  case -4: // Connection lost
    return "Conexión perdida.";
  case 5: // Subscription error
    return "Error al intentar suscribirse al tema.";
  default:
    return "Estado desconocido.";
  }
}

void keepAlive()
{
  if (!mqttClient.connected())
  {
    Serial.println("Reconectando");
    // Attempts to connect to the MQTT server
    while (!mqttClient.connected())
    {
      Serial.println("Intentando conectar al servidor MQTT...");
      if (mqttClient.connect(clientName))
      {
        Serial.println("¡Conectado al servidor MQTT!");
      }
      else
      {
        Serial.print("Error al conectar: ");
        Serial.println(getMqttErrorDescription(mqttClient.state())); // Show detailed description of the error
        delay(5000);
      }
    }
    mqttClient.subscribe(topic);
  }
}

void saveJson(const char *path, JsonDocument &jsonDoc)
{
  // Create or overwrite JSON file in SPIFFS
  File file = SPIFFS.open(path, FILE_WRITE);
  if (!file)
  {
    Serial.println("Error al abrir el archivo para escribir");
    return;
  }

  // Write the JSON object (all measurements) to file
  serializeJson(jsonDoc, file);
  file.close();
}

// Function to read and display the JSON file on the serial port
void showJson(const char *path)
{
  // Open the JSON file in SPIFFS
  File file = SPIFFS.open(path, FILE_READ);
  if (!file)
  {
    Serial.println("Error: No se pudo abrir el archivo para leer.");
    return;
  }

  JsonDocument jsonObjectDoc;
  DeserializationError error = deserializeJson(jsonObjectDoc, file);
  if (error)
  {
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
void collectSensorData(MPU6050 &mpu, const char *sensorName, const String &sampleName, unsigned long startTime)
{
  int16_t ax, ay, az;
  int16_t gx, gy, gz;
  mpu.getAcceleration(&ax, &ay, &az);
  mpu.getRotation(&gx, &gy, &gz);
  unsigned long relativeTimestamp = millis() - startTime; // Calculate elapsed time since start
  JsonObject sample = measurementsDoc["data"][sensorName][sampleName].to<JsonObject>();
  sample["ax"] = ax;
  sample["ay"] = ay;
  sample["az"] = az;
  sample["gx"] = gx;
  sample["gy"] = gy;
  sample["gz"] = gz;
  sample["timestamp"] = relativeTimestamp; // Use relative time as the timestamp
}

void sendJsonAsPostRequest(const char *filename)
{
  File file = SPIFFS.open(filename, FILE_READ);
  if (!file)
  {
    Serial.println("Error al abrir el archivo para leer");
    return;
  }

  String jsonContent = file.readString();
  file.close();

  Serial.println(url);
  Serial.println(jsonContent);

  // Prepare and send the POST request
  http.begin(url);
  http.addHeader("Content-Type", "application/json");
  int httpResponseCode = http.POST(jsonContent);

  if (httpResponseCode > 0)
  {
    Serial.printf("Código de respuesta HTTP: %d\n", httpResponseCode);
    String payload = http.getString();
    Serial.println(payload);
  }
  else
  {
    Serial.printf("Código de error: %d\n", httpResponseCode);
  }

  http.end();
}

// A 5-second test will start for both sensors
void doTest(int testDurationMs, int samplesPerSecond)
{
  Serial.println("Iniciando toma de datos...");
  testRunning = true;

  digitalWrite(ledPinR, LOW);
  digitalWrite(ledPinB, LOW);
  digitalWrite(ledPinG, HIGH);

  // Metadata initialization
  timeClient.update();

  // Get time from NTP
  unsigned long epochTime = timeClient.getEpochTime();

  // Convert time to date and time
  setTime(epochTime);

  // Date and time formatting
  date = String(day()) + ":" + String(month()) + ":" + String(year());           // DD:MM:YYYY
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

  for (int i = 0; i < totalSamples && testRunning; i++)
  {
    // Time at the beginning of each sampling
    unsigned long sampleStartTime = millis();

    String sampleName = "sample" + String(i + 1);

    collectSensorData(mpu1, "sensor1", sampleName, startTime);
    collectSensorData(mpu2, "sensor2", sampleName, startTime);

    // Calculate the time remaining to maintain the sampling rate
    unsigned long elapsedTime = millis() - sampleStartTime;
    if (elapsedTime < delayBetweenSamples)
    {
      delay(delayBetweenSamples - elapsedTime);
    }
  }

  unsigned long testDuration = millis() - startTime;

  if (testRunning)
  {
    Serial.printf("Toma de datos finalizada. Duración real: %lu ms\n", testDuration);
    // Save and display JSON files for each sensor
    saveJson("/sensor_data.json", measurementsDoc);
    showJson("/sensor_data.json");
    // Send the JSON as a POST request
    Serial.println("Enviando datos...");
    sendJsonAsPostRequest("/sensor_data.json");
    Serial.println("Envío de datos completado.");
    testRunning = false;
  }
  else
  {
    Serial.println("Prueba detenida antes de completarse.");
  }
}

void handleMessageToDoTest(const String &message)
{
  // Convert the message to lowercase for case-insensitive comparison
  String lowerMessage = message;
  lowerMessage.toLowerCase();

  if (lowerMessage.equalsIgnoreCase("stop"))
  {
    Serial.println("Comando 'stop' recibido. Deteniendo prueba...");
    testRunning = false;
    return;
  }

  // Check if the message starts with "init~~"
  if (lowerMessage.startsWith("init~~"))
  {
    Serial.println("Comando 'init' recibido. Iniciando prueba...");

    // Find the delimiters '~~'
    int firstDelimiterIndex = lowerMessage.indexOf("~~");
    int secondDelimiterIndex = lowerMessage.indexOf("~~", firstDelimiterIndex + 2);

    // Check if delimiters are found
    if (firstDelimiterIndex == -1 || secondDelimiterIndex == -1)
    {
      Serial.println("Error: formato de mensaje incorrecto");
      return;
    }

    // Extract the evaluated ID
    idString = lowerMessage.substring(firstDelimiterIndex + 2, secondDelimiterIndex);

    // Extract the type of test
    typeOfTest = lowerMessage.substring(secondDelimiterIndex + 2);

    // Call doTest with the parameters obtained (e.g., 5 seconds, 20 Hz)
    doTest(TEST_DURATION_MS, SAMPLES_PER_SECOND);
  }
  else
  {
    Serial.println("Comando desconocido: " + message);
  }
}

bool checkSensors()
{
  bool controlFlag1 = false;
  bool controlFlag2 = false;
  if (mpu1.testConnection())
  {
    Serial.println("Conexión exitosa con el MPU6050 1");
    controlFlag1 = true;
  }
  else
  {
    Serial.println("Conexión fallida con el sensor MPU6050 1");
  }

  if (mpu2.testConnection())
  {
    Serial.println("Conexión exitosa con el MPU6050 2");
    controlFlag2 = true;
  }
  else
  {
    Serial.println("Conexión fallida con el sensor MPU6050 2");
  }
  return controlFlag1 && controlFlag2;
}

void handleStatusCheck()
{
  String responseTopic = "test/icesi/dlp";

  bool sensorsConnected = checkSensors();

  bool mqttConnected = mqttClient.connected();
  Serial.println(sensorsConnected);
  Serial.println(mqttConnected);

  if (sensorsConnected && mqttConnected)
  {
    // Todo está bien, publicar respuesta "ok"
    String payload = "{\"status\": \"ok\"}";
    mqttClient.publish(responseTopic.c_str(), payload.c_str());
    Serial.println("Respuesta de estado: OK");
  }
  else
  {
    // Si hay un error, publicar el error correspondiente
    String errorMessage = "";
    if (!sensorsConnected)
    {
      errorMessage = "Error: Sensor desconectado";
    }
    else if (!mqttConnected)
    {
      errorMessage = "Error: Desconexión del servidor MQTT";
    }
    String payload = "{\"status\": \"error\", \"message\": \"" + errorMessage + "\"}";
    mqttClient.publish(responseTopic.c_str(), payload.c_str());
    Serial.println("Respuesta de estado con error: " + errorMessage);
  }
}

void callback(char *topic, byte *payload, unsigned int length)
{
  // Create a String with the received payload
  String message;
  for (unsigned int i = 0; i < length; i++)
  {
    message += (char)payload[i];
  }

  // Print the received message
  Serial.print("Mensaje recibido en el topic ");
  Serial.print(topic);
  Serial.print(": ");
  Serial.println(message);

  // Call handleMessageToDoTest with the received message
  if (message == "check_error")
  {
    handleStatusCheck();
  }
  else
  {
    handleMessageToDoTest(message);
  }
}

void setup()
{
  Serial.begin(115200);

  // Setting up the led Pins
  pinMode(ledPinR, OUTPUT);
  pinMode(ledPinG, OUTPUT);
  pinMode(ledPinB, OUTPUT);

  // Starting value: OFF
  digitalWrite(ledPinR, HIGH);
  digitalWrite(ledPinG, LOW);
  digitalWrite(ledPinB, LOW);

  // Initialize the SPIFFS file system
  if (!SPIFFS.begin(true))
  {
    Serial.println("Error al montar SPIFFS");
    return;
  }
  Serial.println("SPIFFS montado correctamente");

  // Initialize the MPU6050 sensors
  Wire.begin();
  mpu1.initialize();
  mpu2.initialize();

  if (mpu1.testConnection())
  {
    Serial.println("Conexión exitosa con el MPU6050 1");
  }
  else
  {
    Serial.println("Conexión fallida con el MPU6050 1");
    return;
  }

  if (mpu2.testConnection())
  {
    Serial.println("Conexión exitosa con el MPU6050 2");
  }
  else
  {
    Serial.println("Conexión fallida con el MPU6050 2");
    return;
  }

  // Connect to Wi-Fi network
  Serial.println("Conectando a la red Wi-Fi...");
  WiFi.disconnect();
  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED)
  {
    delay(5000);
    Serial.print(".");
  }
  Serial.println("\nConexión exitosa a la red Wi-Fi");
  // Get the assigned IP address
  Serial.print("Dirección IP asignada: ");
  Serial.println(WiFi.localIP());

  // Initializes the MQTT client
  mqttClient.setServer(mqttServer, mqttPort);
  mqttClient.setCallback(callback);

  // Attempts to connect to the MQTT server
  while (!mqttClient.connected())
  {
    Serial.println("Intentando conectar al servidor MQTT...");
    if (mqttClient.connect(clientName))
    {
      Serial.println("¡Conectado al servidor MQTT!");
    }
    else
    {
      Serial.print("Error al conectar: ");
      Serial.println(getMqttErrorDescription(mqttClient.state())); // Show detailed description of the error
      delay(5000);
    }
  }

  // Subscribe to the topic
  mqttClient.subscribe(topic);

  // Initialize the NTP client
  timeClient.begin();
  // Synchronize time at startup
  timeClient.update();

  Serial.println("Escribe 'init' para realizar una prueba con parámetros predeterminados.");
}

void loop()
{
  mqttClient.loop();
  keepAlive();

  digitalWrite(ledPinR, LOW);
  digitalWrite(ledPinG, LOW);
  digitalWrite(ledPinB, HIGH);

  // Update NTP time
  timeClient.update();

  // Read input from serial monitor
  if (Serial.available())
  {
    String input = Serial.readStringUntil('\n');
    // Remove blank spaces around
    input.trim();

    if (input.equalsIgnoreCase("init"))
    {
      Serial.println("Comando 'init' recibido. Iniciando prueba con parámetros predeterminados...");

      // Construct message with test data
      String id = "1111111111";                                   // Test ID
      String testType = (random(2) == 0) ? "Zapateo" : "Taconeo"; // Type of test
      String message = "init~~" + id + "~~" + testType;

      // Call the test handling function with the predefined message
      handleMessageToDoTest(message);
    }
    else
    {
      Serial.println("Comando no reconocido: " + input);
    }
  }
}