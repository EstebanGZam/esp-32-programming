#include "FS.h"
#include "SPIFFS.h"
#include <Wire.h>
#include <MPU6050.h>
#include <ArduinoJson.h>
#include <PubSubClient.h>
#include <WiFi.h>

// Configuración de la red Wi-Fi
const char *ssid = "";
const char *password = "";

// Configuración del servidor MQTT
const char *mqttServer = "broker.hivemq.com";
const int mqttPort = 1883;
const char *clientName = "ESP32ClienteIcesiA00381213";

// Configuración del topic
const char *topic = "test/icesi/dlp";

// Objeto WiFiClient
WiFiClient wifiClient;

// Objeto PubSubClient
PubSubClient mqttClient(wifiClient);

MPU6050 mpu1;                              // Primer sensor MPU6050
MPU6050 mpu2;                              // Segundo sensor MPU6050
int measurementID1 = 0;                    // Contador para asignar claves únicas al sensor 1
int measurementID2 = 0;                    // Contador para asignar claves únicas al sensor 2
StaticJsonDocument<4096> measurementsDoc1; // Documento JSON para el sensor 1
StaticJsonDocument<4096> measurementsDoc2; // Documento JSON para el sensor 2

void saveJson(const char *path, StaticJsonDocument<4096> &jsonDoc)
{
  // Crear o sobrescribir el archivo JSON en SPIFFS
  File file = SPIFFS.open(path, FILE_WRITE);
  if (!file)
  {
    Serial.println("Error al abrir el archivo para escribir");
    return;
  }

  // Escribir el objeto JSON (todas las mediciones) en el archivo
  serializeJson(jsonDoc, file);
  file.close();
}

// Función para leer y mostrar el archivo JSON en el puerto serial
void showJson(const char *path)
{
  // Abrir el archivo JSON en SPIFFS
  File file = SPIFFS.open(path, FILE_READ);
  if (!file)
  {
    Serial.println("Error: No se pudo abrir el archivo para leer.");
    return;
  }

  StaticJsonDocument<4096> jsonObjectDoc;
  DeserializationError error = deserializeJson(jsonObjectDoc, file);
  if (error)
  {
    Serial.print("Error al parsear el archivo JSON: ");
    Serial.println(error.f_str());
    file.close();
    return;
  }

  Serial.println("Contenido del archivo JSON:");
  serializeJsonPretty(jsonObjectDoc, Serial); // Mostrar el JSON de manera legible
  Serial.println();
  file.close();
}

// Función para recolectar datos de un sensor específico
void collectSensorData(MPU6050 &mpu, StaticJsonDocument<4096> &doc, int &measurementID, const char *sensorName)
{
  // Leer datos del sensor MPU6050
  int16_t ax, ay, az;
  int16_t gx, gy, gz;
  mpu.getAcceleration(&ax, &ay, &az);
  mpu.getRotation(&gx, &gy, &gz);

  // Crear un objeto JSON para la nueva medición
  StaticJsonDocument<200> jsonDoc;
  jsonDoc["ax"] = ax;
  jsonDoc["ay"] = ay;
  jsonDoc["az"] = az;
  jsonDoc["gx"] = gx;
  jsonDoc["gy"] = gy;
  jsonDoc["gz"] = gz;

  // Crear una clave única para cada medición
  String key = "medicion_" + String(measurementID++);

  // Añadir la nueva medición con su clave única al objeto JSON general
  doc[key] = jsonDoc;

  Serial.print(sensorName);
  Serial.print(": Se ha añadido la medición ");
  Serial.println(key);
}

// Will start a 5 second test for both sensonrs
void doTest(int testDurationMs, int samplesPerSecond)
{
  Serial.println("Iniciando toma de datos...");

  int totalSamples = (testDurationMs * samplesPerSecond) / 1000;
  int delayBetweenSamples = 1000 / samplesPerSecond;

  unsigned long startTime = millis();
  for (int i = 0; i < totalSamples; i++)
  {
    unsigned long sampleStartTime = millis();

    collectSensorData(mpu1, measurementsDoc1, measurementID1, "Sensor 1");
    collectSensorData(mpu2, measurementsDoc2, measurementID2, "Sensor 2");

    // Calcular el tiempo restante para mantener la frecuencia de muestreo
    unsigned long elapsedTime = millis() - sampleStartTime;
    if (elapsedTime < delayBetweenSamples)
    {
      delay(delayBetweenSamples - elapsedTime);
    }
  }

  unsigned long testDuration = millis() - startTime;
  Serial.printf("Toma de datos finalizada. Duración real: %lu ms\n", testDuration);

  // Guardar y mostrar archivos JSON para cada sensor
  saveJson("/sensor1_data.json", measurementsDoc1);
  saveJson("/sensor2_data.json", measurementsDoc2);
  showJson("/sensor1_data.json");
  showJson("/sensor2_data.json");
}

void handleMqttMessage(const String &message)
{
  // Convertir el mensaje a minúsculas para una comparación insensible a mayúsculas/minúsculas
  String lowerMessage = message;
  lowerMessage.toLowerCase();

  if (lowerMessage == "init")
  {
    Serial.println("Comando 'init' recibido. Iniciando test...");
    // Llamar a doTest con los parámetros deseados (por ejemplo, 5 segundos a 20 Hz)
    doTest(5000, 20);
  }
  else
  {
    Serial.println("Comando no reconocido: " + message);
  }
}

void callback(char *topic, byte *payload, unsigned int length)
{
  // Crear un String con el payload recibido
  String message;
  for (unsigned int i = 0; i < length; i++)
  {
    message += (char)payload[i];
  }

  // Imprimir el mensaje recibido
  Serial.print("Mensaje recibido en el topic ");
  Serial.print(topic);
  Serial.print(": ");
  Serial.println(message);

  // Llamar a handleMqttMessage con el mensaje recibido
  handleMqttMessage(message);
}

void keepAlive()
{
  if (!mqttClient.connected())
  {
    Serial.println("Reconectando");
    // Intenta conectarse al servidor MQTT
    while (!mqttClient.connected())
    {
      Serial.println("Intentando conectar al servidor MQTT...");
      if (mqttClient.connect(clientName))
      {
        Serial.println("Conectado al servidor MQTT!");
      }
      else
      {
        Serial.print("Error al conectar: ");
        Serial.println(mqttClient.state());
        delay(5000);
      }
    }
    mqttClient.subscribe(topic);
  }
}

void setup()
{
  Serial.begin(115200);

  // Inicializar el sistema de archivos SPIFFS
  if (!SPIFFS.begin(true))
  {
    Serial.println("Error al montar SPIFFS");
    return;
  }
  Serial.println("SPIFFS montado correctamente");

  // Inicializar los sensores MPU6050
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

  // Conecta a la red Wi-Fi
  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED)
  {
    delay(500);
    Serial.print(".");
  }

  // Inicializa el cliente MQTT
  mqttClient.setServer(mqttServer, mqttPort);
  mqttClient.setCallback(callback);

  // Intenta conectarse al servidor MQTT
  while (!mqttClient.connected())
  {
    Serial.println("Intentando conectar al servidor MQTT...");
    if (mqttClient.connect(clientName))
    {
      Serial.println("Conectado al servidor MQTT!");
    }
    else
    {
      Serial.print("Error al conectar: ");
      Serial.println(mqttClient.state());
      delay(5000);
    }
  }

  // Suscríbete al topic
  mqttClient.subscribe(topic);

  Serial.println("Escribe 'init' para comenzar con la prueba");
}
// Función para guardar el archivo JSON en SPIFFS

void loop()
{

  mqttClient.loop();
  keepAlive();
  // Leer entrada del monitor serial
}

void serialEvent()
{
}