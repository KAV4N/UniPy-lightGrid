// #define ESP32 true
// #define FASTLED_ALLOW_INTERRUPTS 1
// #define FASTLED_RMT_MAX_CHANNELS 8
#include <WiFi.h>
#include <WiFiAP.h>
#include <WiFiUdp.h>
#include <AsyncTCP.h>
#include <esp_wifi.h>
#include <ESP32Ping.h>
#include <NeoPixelBus.h>
#include <algorithm>

#include <vector>
#include <queue>

#include <iostream>

using namespace std;

#define VIDEO_CORE_DELAY 20
/*Serial connection baudrate*/
#define BAUDRATE 500000
/*SSID and password for network*/
#define SSID_NET "mcuNET"
#define PASSWORD_NET "grid0123"
/*ESP pin + LED strip params */
#define DATA_PIN 18
#define NUM_LEDS 81

/*color pallete*/
/*
  const byte _r[128] = {0, 36, 60, 255, 255, 255, 125, 60, 255, 255, 194, 64, 255, 255, 190, 97, 133, 80, 48, 32, 72, 0, 0, 0, 72, 0, 0, 0, 60, 0, 0, 0, 72, 0, 0, 0, 89, 60, 0, 0, 72, 0, 0, 0, 48, 0, 0, 0, 133, 44, 20, 16, 255, 255, 93, 60, 255, 255, 93, 48, 255, 149, 117, 32, 0, 0, 0, 0, 0, 36, 125, 28, 255, 186, 174, 97, 12, 0, 0, 0, 60, 121, 174, 60, 255, 133, 113, 0, 56, 85, 52, 89, 48, 133, 210, 255, 255, 182, 141, 129, 56, 16, 12, 20, 20, 101, 165, 218, 214, 255, 157, 101, 28, 218, 125, 153, 141, 60, 113, 222, 157, 52, 24, 4, 182, 60, 178, 72};
  const byte _g[128] = {0, 36, 60, 255, 72, 0, 0, 0, 186, 68, 48, 16, 165, 255, 190, 97, 255, 255, 121, 89, 255, 255, 125, 60, 255, 255, 113, 89, 255, 255, 125, 60, 255, 255, 97, 64, 194, 226, 97, 56, 133, 85, 52, 28, 32, 0, 0, 0, 72, 0, 0, 0, 72, 0, 0, 0, 72, 0, 0, 0, 20, 52, 80, 64, 56, 85, 80, 0, 68, 0, 125, 28, 0, 255, 234, 255, 137, 255, 165, 40, 0, 0, 24, 32, 72, 222, 255, 255, 255, 255, 255, 137, 80, 125, 28, 0, 125, 174, 255, 89, 40, 72, 76, 20, 28, 56, 0, 80, 105, 222, 222, 178, 28, 255, 255, 149, 101, 60, 113, 255, 0, 0, 206, 64, 174, 48, 93, 20};
  const byte _b[128] = {0, 36, 60, 255, 72, 0, 0, 0, 105, 0, 0, 0, 36, 0, 0, 0, 64, 0, 0, 0, 72, 0, 0, 0, 93, 24, 12, 12, 60, 85, 32, 28, 255, 149, 52, 48, 255, 255, 113, 97, 255, 255, 121, 60, 255, 255, 125, 60, 255, 255, 113, 56, 255, 255, 93, 60, 133, 72, 28, 16, 0, 0, 0, 4, 0, 52, 125, 255, 76, 202, 125, 28, 0, 44, 4, 8, 0, 133, 255, 255, 255, 255, 121, 0, 0, 4, 20, 0, 36, 109, 202, 255, 194, 230, 255, 89, 0, 0, 0, 4, 0, 12, 52, 40, 89, 24, 8, 60, 24, 36, 44, 12, 44, 105, 186, 255, 255, 60, 113, 255, 0, 0, 0, 0, 0, 0, 0, 0};
*/

/*Setup AP informations*/
WiFiUDP Udp;
IPAddress local_IP(10, 10, 10, 1);
IPAddress gateway(10, 10, 10, 1);
IPAddress subnet(255, 255, 255, 0);
String cur_ip;
String replyPacket = "";
bool server_mode = false;

/*control message for sending information about connected devices*/
const byte con_message[4] = {120 | 0x80, 0, 0, 0};

/*LED Strip */
NeoPixelBus<NeoGrbFeature, NeoEsp32I2s1800KbpsMethod> leds(NUM_LEDS, DATA_PIN);

/*Multicore Task handlers */
TaskHandle_t Task1;
TaskHandle_t Task2;

/*color params*/
int r_velocity = 0;
int g_velocity = 0;
int b_velocity = 0;
RgbColor rgb_Black = RgbColor(0, 0, 0);

/*storage for frames from the effect*/
queue<vector<byte>> frame_storage;

/*startup animation*/
int neo_sin8(int num)
{
  float angle = map(num, 0, 255, 0, 360);
  float rad_angle = angle * DEG_TO_RAD;
  return (int)((sin(rad_angle) + 1) * 179);
}

int neo_cos8(int num)
{
  float angle = map(num, 0, 255, 0, 360);
  float rad_angle = angle * DEG_TO_RAD;
  return (int)((cos(rad_angle) + 1) * 179);
}

void startup_animation()
{
  leds.ClearTo(rgb_Black);
  leds.Show();
  byte offset = 80;		// counter for radial color wave motion
  int plasVector = 15000; // counter for orbiting plasma center

  int fadeUp = 10;

  for (int i = 0; i < 200; i++)
  {
    // Calculate current center of plasma pattern (can be offscreen)
    int xOffset = neo_cos8(plasVector / 360);
    int yOffset = neo_sin8(plasVector / 360);
    for (int x = 0; x < 9; x++)
    {
      for (int y = 0; y < 9; y++)
      {
        int color = neo_sin8(sqrt(sq(((float)x - 7.5) * 10 + xOffset - 179) + sq(((float)y - 2) * 10 + yOffset - 179)) + offset);
        leds.SetPixelColor(getIndex(x, y), HsbColor(color / 360.0f, 1.0f, fadeUp / 360.0f));
      }
    }

    if (fadeUp < 350)
    {
      fadeUp += 3;
    }
    offset++;			// wraps at 255 for sin8
    plasVector += 1128; // using an int for slower orbit (wraps at 65536)
    delay(30);
  }
  for (int i = 0; i < 20; i++)
  {
    // fadeToBlackBy(leds, NUM_LEDS, 64);
    for (int j = 0; j < NUM_LEDS; j++)
    {
      RgbColor color = leds.GetPixelColor(j);
      color.Darken(20);
      leds.SetPixelColor(j, color);
    }
    delay(15);
  }
  // Draw one frame of the animation into the LED array
  delay(2000);
}

uint16_t getIndex(uint16_t x, uint16_t y)
{
  uint16_t index;
  if (y == 0)
    index = x;
  else if (y % 2 == 0)
    index = y * 9 + x;
  else
    index = ((y * 9) + (9 - 1)) - x;
  return index;
}

void WiFiStationDisconnected(WiFiEvent_t event, WiFiEventInfo_t info) {
  if (!server_mode){
    server_mode = network_setup();
  }
}
void device_connection_control(WiFiEvent_t event, WiFiEventInfo_t info) {
  if (server_mode){
    Serial.println("connected");
    replyPacket = cur_ip;
    String remote_ip = Udp.remoteIP().toString();
    String list_ip = "";
    wifi_sta_list_t wifi_sta_list;
    tcpip_adapter_sta_list_t adapter_sta_list;
  
    memset(&wifi_sta_list, 0, sizeof(wifi_sta_list));
    memset(&adapter_sta_list, 0, sizeof(adapter_sta_list));
  
    esp_wifi_ap_get_sta_list(&wifi_sta_list);
    tcpip_adapter_get_sta_list(&wifi_sta_list, &adapter_sta_list);
  
    // collecting all asigned ip addresses
    for (int i = 0; i < adapter_sta_list.num; i++)
    {
      tcpip_adapter_sta_info_t station = adapter_sta_list.sta[i];
      list_ip = ip4addr_ntoa(&(station.ip));
      if (list_ip != remote_ip)
      {
        replyPacket += "|";
        replyPacket += list_ip;
      }
    }
  }
}

bool network_setup()
{
  String net = "";
  int n_net = WiFi.scanNetworks();
  bool net_found = false;
  bool server = false;

  char *ssid = SSID_NET;
  char *password = PASSWORD_NET;
  unsigned int localUdpPort = 4210;

  WiFi.mode(WIFI_STA);
  // search for ssid
  for (int i = 0; i < n_net; i++)
  {
    net = WiFi.SSID(i);
    if (net == ssid)
    {
      net_found = true;
      break;
    }
  }

  // client
  if (net_found)
  {
    WiFi.begin(ssid, password);
    WiFi.onEvent(WiFiStationDisconnected, SYSTEM_EVENT_STA_DISCONNECTED);
  }
  // server
  else
  {
    WiFi.mode(WIFI_AP);
    WiFi.softAPConfig(local_IP, gateway, subnet);
    WiFi.softAP(ssid, password);
    delay(500);

    cur_ip = WiFi.softAPIP().toString();
    replyPacket = cur_ip;

    WiFi.onEvent(device_connection_control, SYSTEM_EVENT_AP_STAIPASSIGNED);
    WiFi.onEvent(device_connection_control, SYSTEM_EVENT_AP_STADISCONNECTED);

    server = true;
  }
  esp_wifi_set_ps(WIFI_PS_NONE);
  delay(500);
  Udp.begin(localUdpPort);
  return server;
}

//Serial mode
void vizualize_matrix(byte start_com)
{
  r_velocity = Serial.read();
  g_velocity = Serial.read();
  b_velocity = Serial.read();
  leds.SetPixelColor(start_com, RgbColor(r_velocity, g_velocity, b_velocity));
  // leds[start_com].setRGB(r_velocity, g_velocity, b_velocity);
}

void clear_matrix()
{
  Serial.read();
  Serial.read();
  Serial.read();
  leds.ClearTo(rgb_Black);
}


void send_info()
{
  Serial.read();
  Serial.read();
  Serial.read();
  Serial.write(con_message, 4);
}


// WiFi mode
int find_message()
{
  int packet_size = Udp.parsePacket();
  if (packet_size > 3)
  {
    byte *incoming_packet = new byte[packet_size];
    Udp.read(incoming_packet, packet_size);
    vector<byte> cur_packet(incoming_packet, incoming_packet + packet_size);
    frame_storage.push(cur_packet);
    delete[] incoming_packet;
    cur_packet.clear();
  }

  return frame_storage.size();
}

void vizualize_matrix(int idx, vector<byte> frame)
{
  int poz = frame[idx] & 0x7F;
  r_velocity = frame[idx + 1];
  g_velocity = frame[idx + 2];
  b_velocity = frame[idx + 3];
  leds.SetPixelColor(poz, RgbColor(r_velocity, g_velocity, b_velocity));
}


void clear_matrix(int idx, vector<byte> frame)
{
  leds.ClearTo(rgb_Black);
}



void send_info(int idx, vector<byte> frame)
{
  byte test_commands[4] = {frame[idx], frame[idx + 1], frame[idx + 2], frame[idx + 3]};
  if (std::equal(std::begin(con_message), std::end(con_message), std::begin(test_commands)))
  {
    int len_packet = replyPacket.length();
    char *arr_reply = &replyPacket[0];
    // sending back informations about clients
    Udp.beginPacket(Udp.remoteIP(), Udp.remotePort());
    for (int i = 0; i < len_packet; i++)
      Udp.write((uint8_t)arr_reply[i]);
    Udp.endPacket();
  }
}

//Main code
void MainClientServer(void *pvParameters)
{
  /*TODO: change command format*/
  byte start_com = 0;
  int frame_size = 0;
  String remote_ip = "";
  char test_buf[33];
  startup_animation();
  
  for (;;)
  {

    //Wifi connection
    if (find_message())
    {
      frame_size = frame_storage.front().size();
      for (int i = 0; i < frame_size; i++)
      {
        start_com = frame_storage.front()[i] & 0x7F;
        // if server_mode send ip addresses to pc
        if ((server_mode && start_com == 120) && (i + 3 < frame_size))
        {
          send_info(i, frame_storage.front());
          i += 3;
        }
        else if (start_com < 100 && (i + 3 < frame_size))
        {
          vizualize_matrix(i, frame_storage.front());
          i += 3;
        }
        // clear matrix
        else if (start_com == 110 && (i + 3 < frame_size))
        {
          clear_matrix(i, frame_storage.front());
          i += 3;
        }

        //else if ((start_com == 101))

        //find_message();
      }
      frame_storage.pop();
    }
    
    //Serial connection
    if (Serial.available() > 3) {
      start_com = Serial.read();
      if ((start_com & 0x80) == 0x80)
      {
        start_com &= 0x7F;
        if (start_com < 100)
        {
          vizualize_matrix(start_com);
        }
        else if (start_com == 110)
        {
          clear_matrix();
        }/*
        else if (start_com == 120)
        {
          send_info();
        }*/
      }
    }
  }
}
//Video code
void VideoCode(void *pvParameters)
{
  for (;;)
  {
    leds.Show();
    delay(VIDEO_CORE_DELAY);
  }
}





void setup()
{
  Serial.setRxBufferSize(2048);
  Serial.begin(BAUDRATE);
  delay(500);
  leds.Begin();
  //leds.SetBrightness(BRIGHTNESS);
  leds.Show();
  delay(500);
  server_mode = network_setup();
  
  delay(500);
  

  xTaskCreatePinnedToCore(
    MainClientServer,					 /*Task function. */
    "Main",								 /*name of task. */
    10000,								 /*Stack size of task */
    NULL,								 /*parameter of the task */
    1, /*priority of the task */ &Task1, /*Task handle to keep track of created task */
    1);									 /*pin task to core 0 */
  delay(500);
  // create a task that will be executed in the VideoCode() function, with priority 1 and executed on core 0
  xTaskCreatePinnedToCore(
    VideoCode,							 /*Task function. */
    "Video",							 /*name of task. */
    10000,								 /*Stack size of task */
    NULL,								 /*parameter of the task */
    1, /*priority of the task */ &Task2, /*Task handle to keep track of created task */
    0);									 /*pin task to core 1 */
  delay(500);
}

void loop() {}
