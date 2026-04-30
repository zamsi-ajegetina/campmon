#include <SPI.h>
#include <RF24.h>

#define NRF_CE  4
#define NRF_CSN 5
#define LDR_PIN 34
#define LED_PIN 25  // external LED on GPIO25 (GPIO2=strapping, 4/5/18/19/23=radio/SPI)

RF24 radio(NRF_CE, NRF_CSN);

const byte NODE_C_ADDR[6] = "NODEC";  // Node B TX → Node C RX
const byte CMD_B_ADDR[6]  = "CMD_B";  // Node C TX → Node B RX (LED commands relayed by C)

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

  radio.setPALevel(RF24_PA_LOW);
  radio.setDataRate(RF24_250KBPS);
  radio.setChannel(100);
  radio.openReadingPipe(1, CMD_B_ADDR);  // receive LED commands from gateway (via Node C)
  radio.openWritingPipe(NODE_C_ADDR);
  radio.stopListening();

  Serial.println("Node B ready — sending to Node C every 10s");
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
  snprintf(msg, sizeof(msg), "NodeB LDR:%d", ldrValue);

  bool success = radio.write(msg, sizeof(msg));

  if (success) {
    Serial.print("Sent to Node C: ");
    Serial.println(msg);
  } else {
    Serial.println("Send failed!");
  }

  checkLedCmd();

  delay(9000);
}
