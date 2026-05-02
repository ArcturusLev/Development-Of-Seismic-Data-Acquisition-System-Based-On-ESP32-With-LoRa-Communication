#include <Wire.h>
#include <SPI.h>

// ================= PIN =================
#define I2C_SDA 8
#define I2C_SCL 9

#define CS_PIN  10  
#define SCK_PIN 12  
#define MISO_PIN 11  
#define MOSI_PIN 13  

#define LORA_TX 43
#define LORA_RX 44

#define INTERNAL_ADC_PIN 5 // Kembali ke pin 5 untuk ADC Internal ESP32

// ================= ADS =================
#define ADS1_ADDR 0x48 
#define ADS2_ADDR 0x49 
#define ADS3_ADDR 0x4A 

#define SAMPLE_US 1200  // ~800 SPS

// ================= FLASH =================
const uint32_t MAX_ADDR = 16777216;

struct __attribute__((packed)) DataLog {
  uint32_t counter;    // 4 bytes
  uint32_t time_us;    // 4 bytes
  int16_t v1_raw;      // 2 bytes
  int16_t v2_raw;      // 2 bytes
  int16_t v3_raw;      // 2 bytes
  int16_t v_internal;  // 2 bytes
  uint8_t padding[14]; // 14 bytes (Digeser ke atas crc)
  uint16_t crc;        // 2 bytes (🔥 WAJIB ADA DI PALING BAWAH!)
};

// ================= GLOBAL =================
QueueHandle_t dataQueue;

uint32_t currentAddr = 0;
uint32_t globalCounter = 0;

bool recording = true;

// ================= CRC =================
uint16_t calcCRC(uint8_t* data, uint16_t len) {
  uint16_t crc = 0xFFFF;
  for (uint16_t i = 0; i < len; i++) {
    crc ^= (uint16_t)data[i] << 8;
    for (uint8_t j = 0; j < 8; j++) {
      if (crc & 0x8000) crc = (crc << 1) ^ 0x1021;
      else crc <<= 1;
    }
  }
  return crc;
}

// ================= FLASH =================
void waitForReady() {
  uint8_t status;
  do {
    digitalWrite(CS_PIN, LOW);
    SPI.transfer(0x05);
    status = SPI.transfer(0);
    digitalWrite(CS_PIN, HIGH);
  } while (status & 0x01);
}

void writeEnable() {
  digitalWrite(CS_PIN, LOW);
  SPI.transfer(0x06);
  digitalWrite(CS_PIN, HIGH);
}

void eraseSector(uint32_t addr) {
  writeEnable();
  digitalWrite(CS_PIN, LOW);
  SPI.transfer(0x20);
  SPI.transfer((addr >> 16) & 0xFF);
  SPI.transfer((addr >> 8) & 0xFF);
  SPI.transfer(addr & 0xFF);
  digitalWrite(CS_PIN, HIGH);
  waitForReady();
}

void writeData(uint32_t addr, DataLog data) {
  data.crc = calcCRC((uint8_t*)&data, sizeof(DataLog) - 2);
  uint8_t* p = (uint8_t*)&data;

  writeEnable();
  digitalWrite(CS_PIN, LOW);
  SPI.transfer(0x02);
  SPI.transfer((addr >> 16) & 0xFF);
  SPI.transfer((addr >> 8) & 0xFF);
  SPI.transfer(addr & 0xFF);

  for (int i = 0; i < sizeof(DataLog); i++) SPI.transfer(p[i]);

  digitalWrite(CS_PIN, HIGH);
  waitForReady();
}

// FUNGSI BARU: Membaca data dari Flash
void readData(uint32_t addr, DataLog* data) {
  uint8_t* p = (uint8_t*)data;
  digitalWrite(CS_PIN, LOW);
  SPI.transfer(0x03); // Instruksi Read Data
  SPI.transfer((addr >> 16) & 0xFF);
  SPI.transfer((addr >> 8) & 0xFF);
  SPI.transfer(addr & 0xFF);

  for (int i = 0; i < sizeof(DataLog); i++) {
    p[i] = SPI.transfer(0x00);
  }

  digitalWrite(CS_PIN, HIGH);
}

// ================= TASK ADC =================
void taskADC(void *param) {
  unsigned long lastSample = 0;
  analogReadResolution(12);

  for (;;) {
    if (!recording) {
      vTaskDelay(pdMS_TO_TICKS(10));
      continue;
    }

    unsigned long now = micros();

    if (now - lastSample >= SAMPLE_US) {
      lastSample = now;

      DataLog log = {0}; // 🔥 Pastikan inisialisasi dengan 0 agar padding bersih
      log.counter = globalCounter++;
      log.time_us = now;

      Wire.requestFrom(ADS1_ADDR, 2);
      log.v1_raw = (Wire.read() << 8) | Wire.read();

      Wire.requestFrom(ADS2_ADDR, 2);
      log.v2_raw = (Wire.read() << 8) | Wire.read();

      Wire.requestFrom(ADS3_ADDR, 2);
      log.v3_raw = (Wire.read() << 8) | Wire.read();

      log.v_internal = analogRead(INTERNAL_ADC_PIN);

      xQueueSend(dataQueue, &log, portMAX_DELAY);
    } else {
      vTaskDelay(1); 
    }
  }
}

// ================= TASK PROCESS =================
void taskProcess(void *param) {

  DataLog temp;

  for (;;) {

    if (xQueueReceive(dataQueue, &temp, portMAX_DELAY)) {

      // ===== PRINT TIAP DATA =====
      Serial.printf("%lu,%lu,%.4f,%.4f,%.4f,%.4f\n",
        temp.counter,
        temp.time_us,
        temp.v1_raw * 0.000125,
        temp.v2_raw * 0.000125,
        temp.v3_raw * 0.000125,
        temp.v_internal * (3.3 / 4095.0)
      );

      // ===== FLASH =====
      if (currentAddr % 4096 == 0) eraseSector(currentAddr);
      writeData(currentAddr, temp);
      currentAddr += sizeof(DataLog);

      // ===== LORA =====
      Serial1.write(0xAA);
      Serial1.write((uint8_t*)&temp.time_us, 4);
      Serial1.write((uint8_t*)&temp.v1_raw, 2);
      Serial1.write((uint8_t*)&temp.v2_raw, 2);
      Serial1.write((uint8_t*)&temp.v3_raw, 2);
      Serial1.write((uint8_t*)&temp.v_internal, 2);
      Serial1.write(0x55);
    }
  }
}

// ================= SETUP =================
void setup() {
  Serial.begin(921600); 
  delay(2000);

  Serial1.begin(115200, SERIAL_8N1, LORA_RX, LORA_TX);

  Wire.begin(I2C_SDA, I2C_SCL, 400000);
  SPI.begin(SCK_PIN, MISO_PIN, MOSI_PIN, CS_PIN);

  pinMode(CS_PIN, OUTPUT);
  digitalWrite(CS_PIN, HIGH);

  pinMode(INTERNAL_ADC_PIN, ANALOG);

  // 🔥 KONFIGURASI ADS1115 DI SINI
  auto initADS = [](uint8_t addr) {
    Wire.beginTransmission(addr);
    Wire.write(0x01); // Menunjuk ke Config Register
    Wire.write(0xD2); // 🔥 DIUBAH DARI 0xC2 KE 0xD2 (MUX diubah agar membaca AIN1)
    Wire.write(0xE3); // Data Rate (860 SPS) dll
    Wire.endTransmission();

    Wire.beginTransmission(addr);
    Wire.write(0x00); // Kembali menunjuk ke Conversion Register untuk proses baca
    Wire.endTransmission();
  };

  initADS(ADS1_ADDR);
  initADS(ADS2_ADDR);
  initADS(ADS3_ADDR);

  dataQueue = xQueueCreate(200, sizeof(DataLog));

  xTaskCreatePinnedToCore(taskADC, "ADC", 8000, NULL, 2, NULL, 1);
  xTaskCreatePinnedToCore(taskProcess, "PROC", 12000, NULL, 1, NULL, 0);

  Serial.println("SYSTEM READY 🚀");
  Serial.println("Kirim 'C' untuk CLEAR Flash Memory");
  Serial.println("Kirim 'D' untuk DUMP Flash Memory");
}

// ================= LOOP (COMMAND HANDLER) =================
void loop() {
  if (Serial.available()) {
    char cmd = Serial.read();

    // COMMAND CLEAR
    if (cmd == 'C' || cmd == 'c') {
      Serial.println("\n[SYSTEM] Menghentikan akuisisi sementara...");
      recording = false; 
      
      while (uxQueueMessagesWaiting(dataQueue) > 0) {
        vTaskDelay(pdMS_TO_TICKS(10));
      }
      vTaskDelay(pdMS_TO_TICKS(20)); 

      Serial.println("[SYSTEM] Menghapus memori (Reset Pointer)...");
      currentAddr = 0;     
      globalCounter = 0;   
      
      Serial.println("[SYSTEM] Flash Memory siap. Melanjutkan akuisisi...\n");
      recording = true;
    }

    // COMMAND DUMP
    else if (cmd == 'D' || cmd == 'd') {
      Serial.println("\n[SYSTEM] Menghentikan akuisisi sementara...");
      recording = false;
      
      while (uxQueueMessagesWaiting(dataQueue) > 0) {
        vTaskDelay(pdMS_TO_TICKS(10));
      }
      vTaskDelay(pdMS_TO_TICKS(20)); 

      Serial.println("[SYSTEM] Memulai Dump Data...");
      Serial.println("Counter,Time_us,V1,V2,V3,V_Internal,CRC_Status");

      uint32_t readAddr = 0;
      DataLog temp;

      while (readAddr < currentAddr) {
        readData(readAddr, &temp);
        
        uint16_t calculatedCRC = calcCRC((uint8_t*)&temp, sizeof(DataLog) - 2);
        bool valid = (temp.crc == calculatedCRC);

        Serial.printf("%lu,%lu,%.4f,%.4f,%.4f,%.4f,%s\n",
          temp.counter,
          temp.time_us,
          temp.v1_raw * 0.000125,
          temp.v2_raw * 0.000125,
          temp.v3_raw * 0.000125,
          temp.v_internal * (3.3 / 4095.0),
          valid ? "OK" : "CORRUPT"
        );
        
        readAddr += sizeof(DataLog);
      }

      Serial.println("[SYSTEM] Dump Selesai. Melanjutkan akuisisi...\n");
      recording = true;
    }
  }
  
  vTaskDelay(pdMS_TO_TICKS(10));
}