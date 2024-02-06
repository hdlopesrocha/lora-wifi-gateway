#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

#include <LoRa.h>
#include <WiFi.h>
#include <WiFiClient.h>
#include <WebServer.h>
#include <lwip/raw.h>
#include <esp_wifi.h>

#define GATEWAY 1
//define the pins used by the LoRa transceiver module
#define SCK 5
#define MISO 19
#define MOSI 27
#define SS 18
#define RST 14
#define DIO0 26
#define BAND 433E6
//OLED pins
#define OLED_SDA 4
#define OLED_SCL 15
#define OLED_RST 16
#define SCREEN_WIDTH 128  // OLED display width, in pixels
#define SCREEN_HEIGHT 64  // OLED display height, in pixels


Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RST);

char *passphrase = "password";  // platform definition

#ifdef GATEWAY
char *ssid = "LoraGateway";  // platform definition
IPAddress local_ip(192, 168, 10, 1);
IPAddress gateway(192, 168, 10, 1);
#else
char *ssid = "LoraNode";  // platform definition
IPAddress local_ip(192, 168, 20, 1);
IPAddress gateway(192, 168, 20, 1);
#endif

IPAddress subnet(255, 255, 255, 0);

WebServer server(80);
raw_pcb *pcb;
TaskHandle_t loraSendMessageTaskHandler = NULL;
TaskHandle_t loraReceiveMessageTaskHandler = NULL;
int messageCount = 0;



void handle_http_root() {
  Serial.println("GET /");
  char *data = "<h1>Hello</h1>\0";
  server.send(200, "text/html", data);
}

void setup() {

  Serial.begin(115200);
  pinMode(LED_BUILTIN, OUTPUT);
  pinMode(OLED_RST, OUTPUT);
  digitalWrite(OLED_RST, LOW);
  delay(20);
  digitalWrite(OLED_RST, HIGH);


  Wire.begin(OLED_SDA, OLED_SCL);
  if (!display.begin(SSD1306_SWITCHCAPVCC, 0x3c, false, false)) {  // Address 0x3C for 128x32
    Serial.println(F("SSD1306 allocation failed"));
    for (;;)
      ;  // Don't proceed, loop forever
  }
  display.clearDisplay();
  display.setTextColor(WHITE);
  display.setTextSize(1);
  display.setCursor(0, 0);
#ifdef GATEWAY
  display.print("LoraGateway");
#else
  display.print("LoraNode");
#endif
  display.display();


  WiFi.mode(WIFI_AP);
  WiFi.softAPConfig(local_ip, gateway, subnet);
  WiFi.softAP(ssid, passphrase);

  Serial.print("IP address = ");
  Serial.println(WiFi.softAPIP());



  pcb = raw_new(IP_PROTO_ICMP);
  if (raw_bind(pcb, IP4_ADDR_ANY) != ERR_OK) {
    Serial.println("ERR: raw_bind");
  }
  raw_recv(pcb, onICMPMessageReceived, NULL);

  SPI.begin(SCK, MISO, MOSI, SS);
  LoRa.setPins(SS, RST, DIO0);

  if (!LoRa.begin(BAND)) {
    Serial.println("Starting LoRa failed!");
    while (1)
      ;
  }
  LoRa.onReceive(onLoraReceive);
  LoRa.receive();


  server.on("/", handle_http_root);
  server.begin();
}

void loraReceiveMessageTask(void *arg) {

  // read packet
  while (LoRa.available()) {
    Serial.printf("%d|", LoRa.read());
  }
  // print RSSI of packet
  Serial.printf("\nRSSI = %d\n", LoRa.packetRssi());
/*
  display.clearDisplay();
  display.setTextColor(WHITE);
  display.setTextSize(1);
  display.setCursor(0, 0);
  display.printf("RSSI=%d\n", LoRa.packetRssi());
  display.display();
  */
}


void onLoraReceive(int packetSize) {
  Serial.printf("Received packet(%d)\n", packetSize);
  xTaskCreate(loraReceiveMessageTask, "loraReceiveMessageTask", 4096, NULL, 1, &loraReceiveMessageTaskHandler);
}

void loraSendMessageTask(void *arg) {
  struct pbuf *p = (pbuf *)arg;

  while (p != NULL) {
    Serial.printf("l=%d/%d\n", p->len, p->tot_len);
    int protocol = IPH_PROTO((struct ip_hdr *)p->payload);
    int version = IP_HDR_GET_VERSION(p->payload);

    Serial.printf("%d %d\n", protocol, version);
    size_t length = p->len;
    uint8_t *loraMessage = (uint8_t *)malloc(length);
    memset(loraMessage, 0, length);
    memcpy(loraMessage, p->payload, length);

    Serial.printf("message = ");
    for (int i = 0; i < length; ++i) {
      Serial.printf("|%hhu", loraMessage[i]);
    }
    Serial.printf("|\n");

    // send packet
    LoRa.beginPacket();
    LoRa.write(loraMessage, length);
    LoRa.endPacket();


    struct pbuf *trash = p;
    p = p->next;

    pbuf_free(trash);
    free(loraMessage);
  }
}


unsigned char onICMPMessageReceived(void *arg, struct raw_pcb *pcb, struct pbuf *p, const ip_addr_t *addr) {
  xTaskCreate(loraSendMessageTask, "loraSendMessageTask", 4096, p, 1, &loraSendMessageTaskHandler);


  return 1;
}


void loop() {
  server.handleClient();
}
