#include <SPI.h>
#include <LoRa.h>
#include <RF24.h>

#define LORA_SS   5
#define LORA_RST  4
#define LORA_DIO0 26

#define NRF_CE    14
#define NRF_CSN   15

RF24 radio(NRF_CE, NRF_CSN);
const byte GATEWAY_ADDR[6] = "GTWY1";

// ── Timing ─────────────────────────────────
#define LORA_LISTEN  120000  // 2 minutes
#define IDLE_PAUSE   60000   // 1 minute
#define NRF_LISTEN   120000  // 2 minutes

// ── States ─────────────────────────────────
enum GatewayState {
  STATE_LORA,
  STATE_IDLE,
  STATE_NRF
};

GatewayState currentState = STATE_LORA;
unsigned long stateStart = 0;

void startLoRa() {
  LoRa.receive();
  radio.stopListening();
  Serial.println("════════════════════════════════");
  Serial.println("Listening on LoRa for 2 minutes");
  Serial.println("════════════════════════════════");
}

void startIdle() {
  LoRa.idle();
  radio.stopListening();
  Serial.println("════════════════════════════════");
  Serial.println("Idle pause for 1 minute");
  Serial.println("════════════════════════════════");
}

void startNRF() {
  LoRa.idle();
  radio.startListening();
  Serial.println("════════════════════════════════");
  Serial.println("Listening on nRF24 for 2 minutes");
  Serial.println("════════════════════════════════");
}

void setup() {
  Serial.begin(115200);
  delay(1000);

  // ── LoRa init ──────────────────────────
  pinMode(LORA_RST, OUTPUT);
  digitalWrite(LORA_RST, LOW);
  delay(500);
  digitalWrite(LORA_RST, HIGH);
  delay(500);

  LoRa.setPins(LORA_SS, LORA_RST, LORA_DIO0);

  int attempts = 0;
  while (!LoRa.begin(433.5E6)) {
    Serial.print("LoRa attempt ");
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
  Serial.println("LoRa ready");

  // ── nRF24 init ─────────────────────────
  if (!radio.begin()) {
    Serial.println("nRF24 init failed!");
    Serial.print("Chip connected? ");
    Serial.println(radio.isChipConnected() ? "YES" : "NO");
    while (true);
  }
  radio.setPALevel(RF24_PA_LOW);
  radio.setDataRate(RF24_250KBPS);
  radio.setChannel(100);
  radio.openReadingPipe(1, GATEWAY_ADDR);
  const byte ADDR_NODE_D[6] = "ATWY1";
  radio.openReadingPipe(3, ADDR_NODE_D);
  Serial.println("nRF24 ready");

  // ── Start with LoRa ────────────────────
  currentState = STATE_LORA;
  stateStart = millis();
  startLoRa();
}

void loop() {
  unsigned long now = millis();
  unsigned long elapsed = now - stateStart;

  // ── State machine ──────────────────────
  switch (currentState) {

    case STATE_LORA:
      // Read LoRa
      if (LoRa.parsePacket()) {
        String msg = "";
        while (LoRa.available()) {
          char c = (char)LoRa.read();
          if (c >= 32 && c <= 126) msg += c;
        }
        if (msg.length() > 0) {
          Serial.print("[LoRa] Received: ");
          Serial.print(msg);
          Serial.print("  RSSI: ");
          Serial.println(LoRa.packetRssi());
        }
      }
      // Switch to idle after 2 minutes
      if (elapsed >= LORA_LISTEN) {
        currentState = STATE_IDLE;
        stateStart = now;
        startIdle();
      }
      break;

    case STATE_IDLE:
      // Do nothing — just wait
      if (elapsed >= IDLE_PAUSE) {
        currentState = STATE_NRF;
        stateStart = now;
        startNRF();
      }
      break;

    case STATE_NRF:
      // Read nRF24
      if (radio.available()) {
        char msg[32] = "";
        radio.read(&msg, sizeof(msg));
        if (msg[0] >= 32 && msg[0] <= 126) {
          Serial.print("[nRF24] Received: ");
          Serial.println(msg);
        }
      }
      // Switch back to LoRa after 2 minutes
      if (elapsed >= NRF_LISTEN) {
        currentState = STATE_LORA;
        stateStart = now;
        startLoRa();
      }
      break;
  }
}