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

/* LED Strip parameters */
#define VIDEO_CORE_DELAY 20
/*Serial connection baudrate*/
#define BAUDRATE 500000
/*SSID and password for network*/
#define SSID_NET "mcuNET"
#define PASSWORD_NET "grid0123"
#define DATA_PIN 18
#define BRIGHTNESS 80
#define NUM_LEDS 300

/*LED Strip */
NeoPixelBus<NeoGrbFeature, NeoEsp32I2s1800KbpsMethod> leds(NUM_LEDS, DATA_PIN);

bool strip_mode_pro = true;
int matrix_pro[] = {
	   0, 11, 2, 13, 4, 15, 6, 17, 8,
	9, 10, 1, 12, 3, 14, 5, 16, 7, 18,
	28, 27, 26, 25, 24, 23, 22, 21, 20, 19,
	29, 30, 31, 32, 33, 34, 35, 36, 37, 38,
	48, 47, 46, 45, 44, 43, 42, 41, 40, 39,
	49, 50, 51, 52, 53, 54, 55, 56, 57, 58,
	68, 67, 66, 65, 64, 63, 62, 61, 60, 59,
	69, 70, 71, 72, 73, 74, 75, 76, 77, 78,
	88, 87, 90, 85, 92, 83, 94, 81, 96, 79,
	    89, 86, 91, 84, 93, 82, 95, 80};

int matrix_grid[] = {
	0, 10, 2, 12, 4, 14, 6, 16, 8,
 	9, 1, 11, 3, 13, 5, 15, 7, 17,
	26, 25, 24, 23, 22, 21, 20, 19, 18,
 	27, 28, 29, 30, 31, 32, 33, 34, 35,
  44, 43, 42, 41, 40, 39, 38, 37, 36,
	45, 46, 47, 48, 49, 50, 51, 52, 53,
	62, 61, 60, 59, 58, 57, 56, 55, 54,
	63, 64, 65, 66, 67, 68, 69, 70, 71,
	80, 79, 78, 77, 76, 75, 74, 73, 72};

/*Multicore Task handlers */
TaskHandle_t Task1;
TaskHandle_t Task2;


/*Setup AP informations*/
WiFiUDP Udp;
IPAddress local_IP(10, 10, 10, 1);
IPAddress gateway(10, 10, 10, 1);
IPAddress subnet(255, 255, 255, 0);
String cur_ip;
String replyPacket = "";
bool server_mode = false;

/*storage for frames from the effect*/
queue<vector<byte>> frame_storage;

/*control message for sending information about connected devices*/
const byte con_message[4] = {120 | 0x80, 0, 0, 0};

/*color params*/
int r_velocity = 0;
int g_velocity = 0;
int b_velocity = 0;
int limit_start = 28, limit_end = 66;
int sum_to_end = 109;
RgbColor rgb_Black = RgbColor(0, 0, 0);




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

void clear_matrix(int idx, vector<byte> frame)
{
  leds.ClearTo(rgb_Black);
}

void vizualize_matrix(int idx, vector<byte> frame)
{
  int poz = frame[idx] & 0x7F;
  r_velocity = frame[idx + 1];
  g_velocity = frame[idx + 2];
  b_velocity = frame[idx + 3];
  vizualize(poz);
}

//Serial mode
void vizualize_matrix(byte poz)
{
  r_velocity = Serial.read();
  g_velocity = Serial.read();
  b_velocity = Serial.read();
  vizualize(poz);
}

void vizualize(byte start_com){
  if (start_com > limit_end)
  {
    start_com = (sum_to_end - limit_end) + (start_com + 1);
    leds.SetPixelColor(136 + start_com,  RgbColor(r_velocity, g_velocity, b_velocity));
    leds.SetPixelColor(135 - start_com,  RgbColor(r_velocity, g_velocity, b_velocity)
    );
  }
  // mid, multiplied by 2
  else if (start_com > limit_start) 
  {
    start_com = limit_start + ((start_com - limit_start) * 2);
    leds.SetPixelColor(136 + start_com,  RgbColor(r_velocity, g_velocity, b_velocity));
    leds.SetPixelColor(136 + (start_com + 1),  RgbColor(r_velocity, g_velocity, b_velocity));
  
    leds.SetPixelColor(135 - start_com,  RgbColor(r_velocity, g_velocity, b_velocity));
    leds.SetPixelColor(135 - (start_com + 1),  RgbColor(r_velocity, g_velocity, b_velocity));
  }
  // end
  else
  {
    leds.SetPixelColor(136 + start_com, RgbColor(r_velocity, g_velocity, b_velocity));
    leds.SetPixelColor(135 - start_com,  RgbColor(r_velocity, g_velocity, b_velocity));
  }
}


void clear_matrix()
{
	Serial.read();
	Serial.read();
	Serial.read();
	leds.ClearTo(rgb_Black);
}
/*
void send_info()
{
	Serial.read();
	Serial.read();
	Serial.read();
	Serial.write(con_message, 4);
}*/

/***** MAIN CORE LOOP *****/
void MainClientServer(void *pvParameters)
{
	byte start_com = 0;
	int frame_size = 0;
	String remote_ip = "";
	char test_buf[33];
	for (;;)
	{
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
      		}
      		frame_storage.pop();
    	}
		if (Serial.available() > 3)
		{
			start_com = Serial.read();
			/***** MIDI NoteOn or Controller Change message - LEDS on *****/
			if ((start_com & 0x80) == 0x80)
			{
				start_com &= 0x7F;
				// generating effects
				if (start_com < 100)
				{
					start_com = strip_mode_pro ? matrix_pro[start_com] : matrix_grid[start_com];
					vizualize_matrix(start_com);	
				}
       
				// strip pro mode (lp)
				else if (start_com == 111)
				{
					Serial.read();
					Serial.read();
					Serial.read();
					limit_start = 28;
					limit_end = 66;
					sum_to_end = 104;
					strip_mode_pro = true;
				}
				// strip grid mode
				else if (start_com == 112)
				{
					Serial.read();
					Serial.read();
					Serial.read();
					limit_start = 13;
					limit_end = 67;
					sum_to_end = 121;
					strip_mode_pro = false;
				}
				// turn off all LEDs
				else if (start_com == 110)
				{
					clear_matrix();
				}
			}
		}
	}
}

/***** VIDEO CORE LOOP *****/
void VideoCode(void *pvParameters)
{
	for (;;)
	{
		leds.Show();
		delay(VIDEO_CORE_DELAY);
	}
}

/***** Setup *****/
void setup()
{
	Serial.setRxBufferSize(2048);
	Serial.begin(BAUDRATE);
	delay(500);
	leds.Begin();
	//leds.setBrightness(BRIGHTNESS);
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

/* do not need loop because we have MainCode and VideoCode loops in separate cores */
void loop() {}
