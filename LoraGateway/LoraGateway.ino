#define LWIP_IPV4 1
#define CONFIG_LWIP_AUTOIP 1
#define CONFIG_LWIP_IP_FORWARD 1
#define CONFIG_LWIP_IP_DEBUG 1
#define CONFIG_LWIP_IPV4_NAPT 1
#define CONFIG_LWIP_DHCP_DOES_ARP_CHECK 0
#define CONFIG_LWIP_NETIF_DEBUG 1
#define CONFIG_LWIP_NETIF_API 1
#define CONFIG_LWIP_NETIF_LOOPBACK 1
#define CONFIG_LWIP_PBUF_DEBUG 1
#define CONFIG_LWIP_API_LIB_DEBUG 1
#define CONFIG_LWIP_SOCKETS_DEBUG 1
#define CONFIG_LWIP_TCP_DEBUG 1
#define TCP_INPUT_DEBUG 1
#define TCP_OUTPUT_DEBUG 1
#define LWIP_HOOK_FILENAME "xxx.h"

//#include "LoraPPP.h"

#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

#include <LoRa.h>
#include <WiFi.h>
#include <WiFiClient.h>
#include <WebServer.h>

#include <lwip/raw.h>
#include <lwip/netif.h>
#include <lwip/autoip.h>
#include <esp_wifi.h>

//#define GATEWAY 1

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

IPAddress localhost(127, 0, 0, 1);
IPAddress any_ip(0, 0, 0, 0);
IPAddress subnet(255, 255, 255, 0);

WebServer server(80);
raw_pcb *pcbIcmpRecv;
raw_pcb *pcbIcmpSend;

TaskHandle_t loraReceiveMessageTaskHandler = NULL;
SemaphoreHandle_t loraReceiveMessageMutex = xSemaphoreCreateMutex();

TaskHandle_t loraSendMessageTaskHandler = NULL;
SemaphoreHandle_t loraSendMessageMutex = xSemaphoreCreateMutex();

int messageCount = 0;
int packetRssi = 0;
int receivedPacketSize = 0;
int sentPacketSize = 0;
#define BYTE_TO_BINARY_PATTERN "%c%c%c%c%c%c%c%c"
#define BYTE_TO_BINARY(byte)  \
  ((byte) & 0x80 ? '1' : '0'), \
  ((byte) & 0x40 ? '1' : '0'), \
  ((byte) & 0x20 ? '1' : '0'), \
  ((byte) & 0x10 ? '1' : '0'), \
  ((byte) & 0x08 ? '1' : '0'), \
  ((byte) & 0x04 ? '1' : '0'), \
  ((byte) & 0x02 ? '1' : '0'), \
  ((byte) & 0x01 ? '1' : '0') 

  
void handle_http_root() {
  Serial.println("GET /");
  char *data = "<h1>Hello</h1>\0";
  server.send(200, "text/html", data);
}


char base46_map[] = {'A', 'B', 'C', 'D', 'E', 'F', 'G', 'H', 'I', 'J', 'K', 'L', 'M', 'N', 'O', 'P',
                     'Q', 'R', 'S', 'T', 'U', 'V', 'W', 'X', 'Y', 'Z', 'a', 'b', 'c', 'd', 'e', 'f',
                     'g', 'h', 'i', 'j', 'k', 'l', 'm', 'n', 'o', 'p', 'q', 'r', 's', 't', 'u', 'v',
                     'w', 'x', 'y', 'z', '0', '1', '2', '3', '4', '5', '6', '7', '8', '9', '+', '/'};

void packetHandler(void *buf, wifi_promiscuous_pkt_type_t type) {
  unsigned long t = micros();

  const wifi_promiscuous_pkt_t *packet = (wifi_promiscuous_pkt_t *)buf;
  unsigned int timestamp = packet->rx_ctrl.timestamp;
  unsigned int length = packet->rx_ctrl.sig_len;

    const uint8_t* ethernetPacket = packet->payload; // Size 14 bytes
    const uint8_t* ipPacket = ethernetPacket + 14; //  Size 20 bytes
    const uint8_t* icmpPacket = ipPacket + 20; // Size 8 bytes
    uint16_t sequenceNumber =  (ethernetPacket[1] << 8) | ethernetPacket[0];

    // Extract ethernet headers
    uint16_t etherType = (ethernetPacket[16] << 8) | ethernetPacket[17];
    Serial.printf("ether(seq=%02x,dstMAC=%02x:%02x:%02x:%02x:%02x:%02x,srcMAC=%02x:%02x:%02x:%02x:%02x:%02x)=[",
              sequenceNumber,
              ethernetPacket[4],ethernetPacket[5],ethernetPacket[6],ethernetPacket[7],ethernetPacket[8],ethernetPacket[9],
              ethernetPacket[10],ethernetPacket[11],ethernetPacket[12],ethernetPacket[13],ethernetPacket[14],ethernetPacket[15]
    );
    for (int i = 0; i < 14; ++i) {
      Serial.printf("%02x.", ethernetPacket[i]);
    }
    Serial.printf("]\n");

    // Extract IP headers
    uint8_t ipVersion = ipPacket[0];
    uint8_t ipProtocol = ipPacket[9];

    Serial.printf("ip(version=%02x,proto=%02x) = [",ipVersion, ipProtocol);
    for (int i = 0; i < 20; ++i) {
      Serial.printf("%02x.", ipPacket[i]);
    }
    Serial.printf("]\n");

    if (ipProtocol == 1) { // ICMP protocol value is 1
      // Assuming ICMP header is 8 bytes
      uint8_t icmpType = icmpPacket[0];
      uint8_t icmpCode = icmpPacket[1];
      uint16_t icmpChecksum = (icmpPacket[2] << 8) | icmpPacket[3];
      uint16_t icmpIdentifier = (icmpPacket[4] << 8) | icmpPacket[5];
      uint16_t icmpSequenceNumber = (icmpPacket[6] << 8) | icmpPacket[7];   
      Serial.printf("icmp(type=%02x,code=%02x,checksum=%04x,id=%04x,seq=%04x)\n", icmpType, icmpCode,icmpChecksum,icmpIdentifier,icmpSequenceNumber);
    }

    LoRa.beginPacket();
    LoRa.write(ethernetPacket, length);
    LoRa.endPacket();
    sentPacketSize = length;
  
  //Serial.printf("rx_cb is called: %s\t rt:%u cbt: %lu, tdiff: %lu\n", type_str, timestamp, t, (t - timestamp));
}

void setup() {

  Serial.begin(115200);
  Serial.printf("LWIP_IPV4=%d\n",LWIP_IPV4);
  Serial.printf("LWIP_AUTOIP=%d\n",LWIP_AUTOIP);
  // === DIPLAY INIT ===
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
  display.display();


  WiFi.mode(WIFI_AP);
  WiFi.softAPConfig(local_ip, gateway, subnet);
  WiFi.softAP(ssid, passphrase);

  ip4_addr_t any; 
  any.addr = IPADDR_ANY;
  ip4_addr_t loopback;
  loopback.addr = IPADDR_LOOPBACK;


  // ip4_output_if_src(&any, &loopback);

  Serial.print("IP address = ");
  Serial.println(WiFi.softAPIP());
  
  //netif * interface = netif_get_by_index(2);
  //autoip_accept_packet(interface, &any);

  //pcbIcmpRecv = raw_new(IP_PROTO_ICMP);
  //raw_bind_netif(pcbIcmpRecv, NULL);
  //raw_recv(pcbIcmpRecv, onICMPMessageReceived, NULL);

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

  //startVPN();
  esp_wifi_set_promiscuous(true);
  esp_wifi_set_promiscuous_rx_cb(&packetHandler);
}

void loraReceiveMessageTask(void *arg) {
  TaskHandle_t thisHandler = loraReceiveMessageTaskHandler;
  if (xSemaphoreTake(loraReceiveMessageMutex, portMAX_DELAY) == pdTRUE) {
    int length = (int)arg;
    int index = 0;
    uint8_t *payload = (uint8_t *)malloc(length);

    // read packet
    Serial.printf("lora(%d) = [", length);
    while (LoRa.available()) {
      int data = LoRa.read();
      payload[index++] = (uint8_t)data;
      Serial.printf("%02x", data);
    }
    Serial.printf("]\n");

    // print RSSI of packet
    Serial.printf("\nRSSI = %d\n", LoRa.packetRssi());
    packetRssi = LoRa.packetRssi();
    /*
  pcbIcmpSend = raw_new(IP_PROTO_ICMP);
  raw_bind_netif(pcbIcmpSend, NULL);
  pbuf buffer;
  buffer.payload = (void*) payload;
  buffer.len = length;
  buffer.tot_len = length;
  buffer.next = NULL;


  raw_send(pcbIcmpSend, &buffer);
  raw_remove(pcbIcmpSend);
*/
    free(payload);
    xSemaphoreGive(loraReceiveMessageMutex);
  } else {
    Serial.printf("ERR: mutex did not lock\n");
  }
  vTaskDelete(thisHandler);
}


void onLoraReceive(int packetSize) {
  receivedPacketSize = packetSize;
  xTaskCreate(loraReceiveMessageTask, "loraReceiveMessageTask", 4096, (void *)packetSize, 1, &loraReceiveMessageTaskHandler);
}

void loraSendMessageTask(void *arg) {
  TaskHandle_t thisHandler = loraSendMessageTaskHandler;
  pbuf *root = (pbuf *)arg;

  if (xSemaphoreTake(loraSendMessageMutex, portMAX_DELAY) == pdTRUE) {

    for (pbuf *p = root; p != NULL; p = p->next) {
      //Serial.printf("l=%d/%d\n", p->len, p->tot_len);
      int protocol = IPH_PROTO((struct ip_hdr *)p->payload);
      int version = IP_HDR_GET_VERSION(p->payload);

      //Serial.printf("%d %d\n", protocol, version);
      int length = p->len;
      uint8_t *loraMessage = (uint8_t *)p->payload;

      Serial.printf("icmp(%d) = [", length);
      for (int i = 0; i < length / sizeof(uint8_t); ++i) {
        Serial.printf("%02x", loraMessage[i]);
      }
      Serial.printf("]\n");

      // send packet
      LoRa.beginPacket();
      LoRa.write(loraMessage, length);
      LoRa.endPacket();

      sentPacketSize = length;
    }
    pbuf_free(root);
    xSemaphoreGive(loraSendMessageMutex);
  } else {
    Serial.printf("ERR: mutex did not lock\n");
  }

  vTaskDelete(thisHandler);
}



unsigned char onICMPMessageReceived(void *arg, struct raw_pcb *pcb, struct pbuf *p, const ip_addr_t *addr) {
  //void * ptrs = {p,&loraSendMessageTaskHandler};
  xTaskCreate(loraSendMessageTask, "loraSendMessageTask", 4096, p, 1, &loraSendMessageTaskHandler);

  //loraSendMessageTask(p);
  // 1 if the packet was 'eaten' (aka. deleted),
  // 0 if the packet lives on
  // If returning 1, the callback is responsible for freeing the pbuf if it's not used any more.
  return 1;
}


void loop() {
  server.handleClient();


  display.clearDisplay();
  display.setTextColor(WHITE);
  display.setTextSize(1);
  display.setCursor(0, 0);
#ifdef GATEWAY
  display.print("LoraGateway\n");
#else
  display.print("LoraNode\n");
#endif
  display.printf("%s\n", local_ip.toString().c_str());
  display.printf("Send=%d\n", sentPacketSize);
  display.printf("Recv=%d\n", receivedPacketSize);
  display.printf("RSSI=%d\n", packetRssi);

  display.display();
}
