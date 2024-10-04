#include "esp_camera.h"
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <MFRC522.h>
#include <SPI.h>
#include <HTTPClient.h>
#include "base64.h"

#define SS_PIN 13
#define RST_PIN 16
#define MOSI_PIN 15
#define MISO_PIN 12
#define SCK_PIN 14

#define ACTIVE_RELE 2

#define CAMERA_MODEL_AI_THINKER
#include "camera_pins.h"

const char* ssid = "TP-Link_5648";
const char* password = "12345678";
const char* serverName = "http://192.168.1.104:5001/upload_image";

MFRC522 rfid(SS_PIN, RST_PIN);
MFRC522::MIFARE_Key key;

byte nuidPICC[4];

String DatoHex;
const String UserReg_1 = "C3CBAB40";
const String UserReg_2 = "B33786A3";
const String UserReg_3 = "7762C83B";

void setup() {
   Serial.begin(115200);
   pinMode(ACTIVE_RELE, OUTPUT);
   WiFi.begin(ssid, password);

   while (WiFi.status() != WL_CONNECTED) {
       delay(1000);
       Serial.println("Connecting to WiFi...");
   }
   Serial.println("Connected to WiFi");

   SPI.begin(SCK_PIN, MISO_PIN, MOSI_PIN, SS_PIN);
   rfid.PCD_Init();
   Serial.println();
   Serial.print(F("Reader :"));
   rfid.PCD_DumpVersionToSerial();

   for (byte i = 0; i < 6; i++) {
     key.keyByte[i] = 0xFF;
   } 
   DatoHex = printHex(key.keyByte, MFRC522::MF_KEY_SIZE);

   // Configurar la cámara
   camera_config_t config;
   config.ledc_channel = LEDC_CHANNEL_0;
   config.ledc_timer = LEDC_TIMER_0;
   config.pin_d0 = Y2_GPIO_NUM;
   config.pin_d1 = Y3_GPIO_NUM;
   config.pin_d2 = Y4_GPIO_NUM;
   config.pin_d3 = Y5_GPIO_NUM;
   config.pin_d4 = Y6_GPIO_NUM;
   config.pin_d5 = Y7_GPIO_NUM;
   config.pin_d6 = Y8_GPIO_NUM;
   config.pin_d7 = Y9_GPIO_NUM;
   config.pin_xclk = XCLK_GPIO_NUM;
   config.pin_pclk = PCLK_GPIO_NUM;
   config.pin_vsync = VSYNC_GPIO_NUM;
   config.pin_href = HREF_GPIO_NUM;
   config.pin_sscb_sda = SIOD_GPIO_NUM;
   config.pin_sscb_scl = SIOC_GPIO_NUM;
   config.pin_pwdn = PWDN_GPIO_NUM;
   config.pin_reset = RESET_GPIO_NUM;
   config.xclk_freq_hz = 20000000;
   config.pixel_format = PIXFORMAT_JPEG;
   config.frame_size = FRAMESIZE_QVGA;
   config.jpeg_quality = 12;
   config.fb_count = 1;

   // Inicializar la cámara
   esp_err_t err = esp_camera_init(&config);
   if (err != ESP_OK) {
     Serial.printf("Camera init failed with error 0x%x", err);
     return;
   }
   Serial.println("Camera init done");
}

void loop() {
   if (!rfid.PICC_IsNewCardPresent()) {
       return;
   }

   if (!rfid.PICC_ReadCardSerial()) {
       return;
   }

   Serial.print(F("PICC type: "));
   MFRC522::PICC_Type piccType = rfid.PICC_GetType(rfid.uid.sak);
   Serial.println(rfid.PICC_GetTypeName(piccType));

   if (piccType != MFRC522::PICC_TYPE_MIFARE_MINI && piccType != MFRC522::PICC_TYPE_MIFARE_1K && piccType != MFRC522::PICC_TYPE_MIFARE_4K) {
       Serial.println("Su Tarjeta no es del tipo MIFARE Classic.");
       return;
   }

   if (rfid.uid.uidByte[0] != nuidPICC[0] || rfid.uid.uidByte[1] != nuidPICC[1] || rfid.uid.uidByte[2] != nuidPICC[2] || rfid.uid.uidByte[3] != nuidPICC[3]) {
       Serial.println("Se ha detectado una nueva tarjeta.");
       
       for (byte i = 0; i < 4; i++) {
         nuidPICC[i] = rfid.uid.uidByte[i];
       }

       DatoHex = printHex(rfid.uid.uidByte, rfid.uid.size);
       Serial.print("Codigo Tarjeta: ");
       Serial.println(DatoHex);

       if (UserReg_1 == DatoHex) {
         Serial.println("USUARIO 1 - PUEDE INGRESAR");
         opendoor();
       } else if (UserReg_2 == DatoHex) {
         Serial.println("USUARIO 2 - PUEDE INGRESAR");
         opendoor();
       } else if (UserReg_3 == DatoHex) {
         Serial.println("USUARIO 3 - PUEDE INGRESAR");
         opendoor();
       } else {
         Serial.println("NO ESTA REGISTRADO - PROHIBIDO EL INGRESO");
       }

       // Tomar foto y enviar al servidor
       captureAndSendPhoto(DatoHex);
       Serial.println();
   } else {
       Serial.println("Tarjeta leida previamente");
   }

   rfid.PICC_HaltA();
   rfid.PCD_StopCrypto1();
}

void captureAndSendPhoto(String cardCode) {
    camera_fb_t * fb = esp_camera_fb_get();
    if (!fb) {
        Serial.println("Camera capture failed");
        return;
    }

    if (WiFi.status() == WL_CONNECTED) {
        HTTPClient http;
        http.begin(serverName);

        String boundary = "----WebKitFormBoundary7MA4YWxkTrZu0gW";
        String contentType = "multipart/form-data; boundary=" + boundary;
        http.addHeader("Content-Type", contentType);

        String postData = "--" + boundary + "\r\n";
        postData += "Content-Disposition: form-data; name=\"photo\"; filename=\"photo.jpg\"\r\n";
        postData += "Content-Type: image/jpeg\r\n\r\n";

        String imageBase64 = base64::encode((const uint8_t *)fb->buf, fb->len);
        postData += imageBase64 + "\r\n";

        postData += "--" + boundary + "\r\n";
        postData += "Content-Disposition: form-data; name=\"RFID-Code\"\r\n\r\n";
        postData += cardCode + "\r\n";
        postData += "--" + boundary + "--\r\n";

        Serial.println("Sending POST request...");
        Serial.println(postData);

        int httpResponseCode = http.POST((uint8_t*)postData.c_str(), postData.length());
        if (httpResponseCode > 0) {
            String response = http.getString();
            Serial.print("HTTP Response code: ");
            Serial.println(httpResponseCode);
            Serial.println(response);
        } else {
            Serial.print("Error on sending POST: ");
            Serial.println(httpResponseCode);
        }

        http.end();
    } else {
        Serial.println("WiFi Disconnected");
    }

    esp_camera_fb_return(fb);
}



String printHex(byte *buffer, byte bufferSize) {
   String DatoHexAux = "";
   for (byte i = 0; i < bufferSize; i++) {
       if (buffer[i] < 0x10) {
         DatoHexAux = DatoHexAux + "0";
         DatoHexAux = DatoHexAux + String(buffer[i], HEX);
       } else {
         DatoHexAux = DatoHexAux + String(buffer[i], HEX);
       }
   }

   for (int i = 0; i < DatoHexAux.length(); i++) {
     DatoHexAux[i] = toupper(DatoHexAux[i]);
   }
   return DatoHexAux;
}

int opendoor(){
  Serial.println("PUERTA ABIERTA");
  digitalWrite(ACTIVE_RELE, HIGH);
  delay(5000);
  digitalWrite(ACTIVE_RELE, LOW);
   Serial.println("PUERTA CERRADA");
  return 0;
}
