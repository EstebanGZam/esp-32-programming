#include "FS.h"
#include "SPIFFS.h"
#include <Wire.h>
#include <MPU6050.h>
#include <ArduinoJson.h>

MPU6050 mpu1;  // Primer sensor MPU6050
MPU6050 mpu2;  // Segundo sensor MPU6050
bool isCollecting = false;  // Estado de recolección de datos
int measurementID1 = 0;  // Contador para asignar claves únicas al sensor 1
int measurementID2 = 0;  // Contador para asignar claves únicas al sensor 2
StaticJsonDocument<4096> measurementsDoc1;  // Documento JSON para el sensor 1
StaticJsonDocument<4096> measurementsDoc2;  // Documento JSON para el sensor 2

void setup() {
  Serial.begin(115200);

  // Inicializar el sistema de archivos SPIFFS
  if (!SPIFFS.begin(true)) {
    Serial.println("Error al montar SPIFFS");
    return;
  }
  Serial.println("SPIFFS montado correctamente");

  // Inicializar los sensores MPU6050
  Wire.begin();
  mpu1.initialize();
  mpu2.initialize();

  if (mpu1.testConnection()) {
    Serial.println("Conexión exitosa con el MPU6050 1");
  } else {
    Serial.println("Conexión fallida con el MPU6050 1");
    return;
  }

  if (mpu2.testConnection()) {
    Serial.println("Conexión exitosa con el MPU6050 2");
  } else {
    Serial.println("Conexión fallida con el MPU6050 2");
    return;
  }

  Serial.println("Escribe 'start' para iniciar la toma de datos y 'finish' para detenerla.");
}

void loop() {
  // Leer entrada del monitor serial
  if (Serial.available() > 0) {
    String input = Serial.readStringUntil('\n');
    input.trim();  // Eliminar espacios en blanco y saltos de línea

    if (input.equals("start")) {
      Serial.println("Iniciando toma de datos...");
      isCollecting = true;
    } else if (input.equals("finish")) {
      Serial.println("Deteniendo toma de datos...");
      isCollecting = false;
      // Guardar y mostrar archivos JSON para cada sensor
      saveJson("/sensor1_data.json", measurementsDoc1);  
      saveJson("/sensor2_data.json", measurementsDoc2);
      showJson("/sensor1_data.json");
      showJson("/sensor2_data.json");
    }
  }

  // Si se debe recolectar datos
  if (isCollecting) {
    collectSensorData(mpu1, measurementsDoc1, measurementID1, "Sensor 1");
    collectSensorData(mpu2, measurementsDoc2, measurementID2, "Sensor 2");
    delay(1000);  // Esperar 1 segundo antes de la siguiente toma de datos
  }
}

// Función para recolectar datos de un sensor específico
void collectSensorData(MPU6050& mpu, StaticJsonDocument<4096>& doc, int& measurementID, const char* sensorName) {
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

// Función para guardar el archivo JSON en SPIFFS
void saveJson(const char* path, StaticJsonDocument<4096>& jsonDoc) {
  // Crear o sobrescribir el archivo JSON en SPIFFS
  File file = SPIFFS.open(path, FILE_WRITE);
  if (!file) {
    Serial.println("Error al abrir el archivo para escribir");
    return;
  }

  // Escribir el objeto JSON (todas las mediciones) en el archivo
  serializeJson(jsonDoc, file);
  file.close();
}

// Función para leer y mostrar el archivo JSON en el puerto serial
void showJson(const char* path) {
  // Abrir el archivo JSON en SPIFFS
  File file = SPIFFS.open(path, FILE_READ);
  if (!file) {
    Serial.println("Error: No se pudo abrir el archivo para leer.");
    return;
  }

  StaticJsonDocument<4096> jsonObjectDoc;
  DeserializationError error = deserializeJson(jsonObjectDoc, file);
  if (error) {
    Serial.print("Error al parsear el archivo JSON: ");
    Serial.println(error.f_str());
    file.close();
    return;
  }

  Serial.println("Contenido del archivo JSON:");
  serializeJsonPretty(jsonObjectDoc, Serial);  // Mostrar el JSON de manera legible
  Serial.println();
  file.close();
}
