#include <SPI.h>
#include <RF24.h>
#include "esp_sleep.h"

#define NRF_CE  4
#define NRF_CSN 5
#define LDR_PIN 34
#define LED_PIN 25  // external LED on GPIO25 (GPIO2=strapping, 4/5/18/19/23=radio/SPI)

RF24 radio(NRF_CE, NRF_CSN);

const byte GATEWAY_ADDR[6] = "ATWY1";  // Node D TX → gateway
const byte CMD_D_ADDR[6]   = "CMDD1";  // gateway TX → Node D RX (LED commands)

void setup() {
  Serial.begin(115200);
  pinMode(LED_PIN, OUTPUT);
  delay(1000);

  if (!radio.begin()) {
    Serial.println("nRF24 init failed!");
    Serial.print("Chip connected? ");
    Serial.println(radio.isChipConnected() ? "YES" : "NO");
    while (true);
  }

  radio.setPALevel(RF24_PA_HIGH);
  radio.setDataRate(RF24_250KBPS);
  radio.setChannel(100);
  radio.openReadingPipe(1, CMD_D_ADDR);  // receive LED commands from gateway
  radio.openWritingPipe(GATEWAY_ADDR);
  radio.stopListening();

  Serial.println("Node D ready — sending to Gateway every 60s");
}

void checkLedCmd() {
  radio.startListening();
  unsigned long deadline = millis() + 1000;  // 1 second RX window
  while (millis() < deadline) {
    if (radio.available()) {
      char cmd[32] = "";
      radio.read(cmd, sizeof(cmd));
      if (strncmp(cmd, "LED:1", 5) == 0) {
        digitalWrite(LED_PIN, HIGH);
        Serial.println("LED ON");
      } else if (strncmp(cmd, "LED:0", 5) == 0) {
        digitalWrite(LED_PIN, LOW);
        Serial.println("LED OFF");
      }
    }
  }
  radio.stopListening();
}

void loop() {
  int ldrValue = analogRead(LDR_PIN);

  char msg[32];
  snprintf(msg, sizeof(msg), "NodeD LDR:%d", ldrValue);

  bool success = radio.write(msg, sizeof(msg));

  if (success) {
    Serial.print("Sent to Gateway: ");
    Serial.println(msg);
  } else {
    Serial.println("Send failed!");
  }

  checkLedCmd();

  // 59s light sleep — CPU halted, SPI/nRF24 state preserved, resumes here on wakeup
  Serial.flush();  // drain UART before CPU halts or last print is corrupted
  esp_sleep_enable_timer_wakeup(59000000ULL);
  esp_light_sleep_start();
}
