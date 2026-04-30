#include <SPI.h>
#include <RF24.h>

#define NRF_CE  4
#define NRF_CSN 5
#define LDR_PIN 34
#define LED_PIN 2

RF24 radio(NRF_CE, NRF_CSN);

const byte NODE_C_ADDR[6]  = "NODEC";  // Node B TX → Node C RX (pipe 1)
const byte GATEWAY_ADDR[6] = "GTWY1";  // Node C TX → gateway (Node C data + Node B forwarded)
const byte CMD_C_ADDR[6]   = "CMDC1";  // gateway TX → Node C RX (own LED commands, pipe 2)
const byte CMD_B_FWD[6]    = "CMDB1";  // gateway TX → Node C RX (LED command to relay to B, pipe 3)
const byte CMD_B_ADDR[6]   = "CMD_B";  // Node C TX → Node B RX (relayed LED command)

bool listeningForNodes = true;
unsigned long lastOwnSend = 0;
#define OWN_SEND_INTERVAL 10000

void startListening() {
  radio.openReadingPipe(1, NODE_C_ADDR);  // data from Node B
  radio.openReadingPipe(2, CMD_C_ADDR);   // LED cmd for Node C
  radio.openReadingPipe(3, CMD_B_FWD);    // LED cmd to relay to Node B
  radio.startListening();
  listeningForNodes = true;
}

void sendToGateway(const char *msg) {
  radio.stopListening();
  radio.openWritingPipe(GATEWAY_ADDR);
  bool success = radio.write(msg, 32);
  if (success) {
    Serial.print("Forwarded to Gateway: ");
    Serial.println(msg);
  } else {
    Serial.println("Forward to Gateway failed!");
  }
  startListening();
}

void relayLedToB(const char *cmd) {
  radio.stopListening();
  radio.openWritingPipe(CMD_B_ADDR);
  bool ok = radio.write(cmd, 32);
  Serial.printf("Relay LED to Node B: %s %s\n", cmd, ok ? "OK" : "FAIL");
  startListening();
}

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

  startListening();

  Serial.println("Node C ready — relay + own data + LED control");
}

void loop() {
  uint8_t pipe;
  if (radio.available(&pipe)) {
    char msg[32] = "";
    radio.read(msg, sizeof(msg));

    if (pipe == 1) {
      // Data from Node B — forward to gateway
      if (msg[0] >= 32 && msg[0] <= 126) {
        Serial.print("Received from Node B: ");
        Serial.println(msg);
        sendToGateway(msg);
      }
    } else if (pipe == 2) {
      // LED command for Node C itself
      if (strncmp(msg, "LED:1", 5) == 0) {
        digitalWrite(LED_PIN, HIGH);
        Serial.println("LED ON (self)");
      } else if (strncmp(msg, "LED:0", 5) == 0) {
        digitalWrite(LED_PIN, LOW);
        Serial.println("LED OFF (self)");
      }
    } else if (pipe == 3) {
      // LED command destined for Node B — relay it
      Serial.print("Relaying LED cmd to Node B: ");
      Serial.println(msg);
      relayLedToB(msg);
    }
  }

  // Send Node C's own LDR data to gateway periodically
  unsigned long now = millis();
  if (now - lastOwnSend >= OWN_SEND_INTERVAL) {
    int ldrValue = analogRead(LDR_PIN);
    char myMsg[32];
    snprintf(myMsg, sizeof(myMsg), "NodeC LDR:%d", ldrValue);
    sendToGateway(myMsg);
    lastOwnSend = now;
  }
}
