#include <SPI.h>
#include <LoRa.h>

#define LORA_SS   8
#define LORA_RST  7
#define LORA_DIO0 2
#define LDR_PIN   A0
#define LED_PIN   6   // external LED on D6 (D2=DIO0, D7=RST, D8=SS, D10-13=SPI)

void setup() {
  Serial.begin(9600);
  pinMode(LED_PIN, OUTPUT);
  delay(1000);

  pinMode(LORA_RST, OUTPUT);
  digitalWrite(LORA_RST, LOW);
  delay(500);
  digitalWrite(LORA_RST, HIGH);
  delay(500);

  LoRa.setPins(LORA_SS, LORA_RST, LORA_DIO0);

  int attempts = 0;
  while (!LoRa.begin(433.5E6)) {
    Serial.print("Attempt ");
    Serial.print(++attempts);
    Serial.println(" failed...");
    delay(500);
    if (attempts >= 10) {
      Serial.println("LoRa gave up.");
      while (true);
    }
  }

  LoRa.setSpreadingFactor(7);
  LoRa.setSignalBandwidth(125E3);
  LoRa.setCodingRate4(5);
  LoRa.setSyncWord(0xF3);
  Serial.println("Node A ready");
}

void checkLoRaCmd() {
  // Listen for up to 2 seconds for a LED command from the gateway
  unsigned long deadline = millis() + 2000;
  LoRa.receive();
  while (millis() < deadline) {
    int pkt = LoRa.parsePacket();
    if (pkt > 0) {
      String cmd = "";
      while (LoRa.available()) cmd += (char)LoRa.read();
      cmd.trim();
      if (cmd == "LED:1") {
        digitalWrite(LED_PIN, HIGH);
        Serial.println("LED ON");
      } else if (cmd == "LED:0") {
        digitalWrite(LED_PIN, LOW);
        Serial.println("LED OFF");
      }
    }
  }
  LoRa.idle();
}

void loop() {
  int ldrValue = analogRead(LDR_PIN);

  String msg = "NodeA LDR:" + String(ldrValue);

  LoRa.beginPacket();
  LoRa.print(msg);
  LoRa.endPacket();

  checkLoRaCmd();  // enter RX immediately — gateway responds within ~5ms of receiving TX

  Serial.print("Sent: ");
  Serial.println(msg);

  delay(28000);
}
