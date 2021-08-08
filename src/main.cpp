#include <Arduino.h>

#include <ESP8266WiFi.h>
#include <ESP8266WiFiMulti.h>
#include <LittleFS.h>
#include <WebSocketsServer.h>
#include <ESP8266mDNS.h>
#include <EEPROM.h>
#include <Hash.h>
#include <WiFiUdp.h>
#include "credentials.h"

// step length
#define STEP_US 1000

// name of main page file
#define PAGE_FILE_NAME "index.html"

// http request timeout (in ms)
#define HTTP_TIMEOUT 4000

// listeting port for UDP
#define UDP_LISTEN_PORT 49502

// size of buffer for sending data over http
#define BUFFER_SIZE 16384

// set to false in normal operation
// true for debug over serial
#define DEBUG false
#define DEBUG_SERIAL if(DEBUG)Serial

#define GRID_SIZE 8

#define PIN_STR 0
#define PIN_DAT 1
#define PIN_CLK 2

// data buffer
char buffer[BUFFER_SIZE];

unsigned char grid_arr[GRID_SIZE];
unsigned char temp_grid_arr[GRID_SIZE];

bool update = true;

// Variable to store the HTTP request
String header;

// Current time
unsigned long currentTime = millis();
// Previous time
unsigned long previousTime = 0; 

// Set websocket server port number to 81
WebSocketsServer webSocket = WebSocketsServer(81);
// Set web server port number to 80
WiFiServer server(90);

WiFiClient server_client;
ESP8266WiFiMulti WiFiMulti;

bool last =false;

WiFiUDP UDP;

unsigned long long secondary = 0;

int row = 0;
unsigned long long last_micros = micros();


void store() {
  digitalWrite(PIN_STR, HIGH);
  //delayMicroseconds(10);
  digitalWrite(PIN_STR, LOW);
  //delayMicroseconds(10);
}
void draw(){
  DEBUG_SERIAL.println("draw call");
  
  for(int i = 0; i < GRID_SIZE; i++){
    shiftOut(PIN_DAT, PIN_CLK, LSBFIRST, ~(0b10000000 >> i));
    shiftOut(PIN_DAT, PIN_CLK, LSBFIRST, grid_arr[i]);
    store();
    delay(100);
  }

  update = false;
}

void readEEPROM(){
  for(int i = 0; i < GRID_SIZE; i++)
    grid_arr[i] = EEPROM.read(i);
}

void updateEEPROM(){
  for(int i = 0; i < GRID_SIZE; i++)
    EEPROM.write(i, grid_arr[i]);
  EEPROM.commit();
}

void checkUDP(){
  char packet[48];
  int packet_size = UDP.parsePacket();
  if(packet_size)
    DEBUG_SERIAL.printf("UDP payload, p = %d\n", packet_size);
  // upon receiving single byte from UDP send WoL packet
  if (packet_size == 9){
    UDP.read(packet, packet_size);
    
    int n = (int)packet[0];
    DEBUG_SERIAL.printf("UDP payload, n = %d\n", n);

    if(n == 0){
      for(int i = 1; i < 9; i++){
        grid_arr[i - 1] = (unsigned char)packet[i];
        DEBUG_SERIAL.println(grid_arr[i - 1], BIN);
      }
      updateEEPROM();
    }else{
      for(int i = 1; i < 9; i++){
        temp_grid_arr[i - 1] = (unsigned char)packet[i];
        DEBUG_SERIAL.println(temp_grid_arr[i - 1], BIN);
      }
      secondary = ((unsigned long long)(n * 100)) / ((unsigned long long)(((float)STEP_US)/1000.0f));
      DEBUG_SERIAL.printf("Secondary = %ull\n", secondary);
    }
  }
}

void startWiFi() { // Start a Wi-Fi access point, and try to connect to some given access points. Then wait for either an AP or STA connection
  WiFi.mode(WIFI_STA);
  WiFiMulti.addAP(WIFI_SSID,  WIFI_PASSWORD);
  DEBUG_SERIAL.print("Connecting to ");
  DEBUG_SERIAL.println(WIFI_SSID);
  //WiFiMulti.begin(WIFI_SSID, WIFI_PASSWORD);
  while (WiFiMulti.run() != WL_CONNECTED) {
    delay(500);
    //digitalWrite(LED_BUILTIN, LOW);
    DEBUG_SERIAL.print(".");
    delay(50);
    //digitalWrite(LED_BUILTIN, HIGH);
  }
  DEBUG_SERIAL.println();
}


void webSocketEvent(uint8_t num, WStype_t type, uint8_t * payload, size_t length) {
    switch(type) {
        case WStype_DISCONNECTED:
        {
            DEBUG_SERIAL.printf("[%u] Disconnected!\n", num);
        }
            break;
        case WStype_CONNECTED:
        {
           
            //update = true;

            IPAddress ip = webSocket.remoteIP(num);
            DEBUG_SERIAL.printf("[%u] Connected from %d.%d.%d.%d url: %s\n", num, ip[0], ip[1], ip[2], ip[3], payload);
            
            // send message to client
            webSocket.sendTXT(num, "Connected");
            webSocket.sendBIN(num, (const uint8_t *)&grid_arr[0], (size_t)GRID_SIZE);
        }   
            break;
        case WStype_TEXT:
        {
            DEBUG_SERIAL.printf("[%u] get Text: %s\n", num, payload);
            switch(payload[0]){
              case 'G':
                webSocket.sendBIN(num, (const uint8_t *)&grid_arr[0], (size_t)GRID_SIZE);
              break;
              case 'S':
                updateEEPROM();
              break;
            }
        }
            break;
        case WStype_BIN:
        {
            DEBUG_SERIAL.printf("binary data: \n");

            update = true;    

            for(int i = 0; i < length; i++){
              grid_arr[i] = (unsigned char)payload[i];
              DEBUG_SERIAL.println(grid_arr[i], BIN);
            }
            DEBUG_SERIAL.println();
        }
            break;
    }

}

void startWebSocket() { // Start a WebSocket server
  webSocket.begin();
  webSocket.onEvent(webSocketEvent);

  DEBUG_SERIAL.println("WebSocket server started.");
}

void handleClient(WiFiClient &client){
  DEBUG_SERIAL.println("New Client.");          // print a message out in the DEBUG_SERIAL port
  String currentLine = "";                // make a String to hold incoming data from the client
  currentTime = millis();
  previousTime = currentTime;
  char request_type = '0';
  while (client.connected() && currentTime - previousTime <= HTTP_TIMEOUT) { // loop while the client's connected
    currentTime = millis();         

    if (client.available()) {             // if there's bytes to read from the client,

      char c = client.read();             // read a byte, then
      header += c;
      if (c == '\n') {                    // if the byte is a newline character
      
        if (currentLine.length() == 0) {
          
          client.println("HTTP/1.1 200 OK");

          if(request_type == 'R'){

            // Display the HTML web page
            String name = PAGE_FILE_NAME;
            String str ="/"+name;
            File page_file = LittleFS.open(str, "r");
            if(!page_file){
              DEBUG_SERIAL.println("page_file open failed");
            }else{
              client.println("Content-type:text/html");
              client.println("Connection: keep-alive");
              client.printf("Content-length: %d\n", page_file.size());
              client.println();
              
              int size = page_file.size();
              int remaining_bytes = size;
              while(remaining_bytes > 0){
                if(remaining_bytes >= BUFFER_SIZE){
                  page_file.readBytes(buffer, BUFFER_SIZE);
                  
                  client.write(buffer, BUFFER_SIZE);

                  remaining_bytes -= BUFFER_SIZE;
                }else{
                  page_file.readBytes(buffer, remaining_bytes);
                  client.write(buffer, remaining_bytes);
                  remaining_bytes = 0;
                }
              } 
            }
            
          }

          // The HTTP response ends with another blank line
          client.println();
          client.println();

          // Break out of the while loop
          break;
        } 
        else { // if you got a newline, then clear currentLine
          DEBUG_SERIAL.println(currentLine);
          if(currentLine.indexOf("GET / HTTP/1.1") != -1){
            DEBUG_SERIAL.println("RECEIVED GET ROOT REQUEST");
            request_type = 'R';
          }

          currentLine = "";
        }
      } 
      else if (c != '\r') {  // if you got anything else but a cgrid_arriage return character,
        currentLine += c;      // add it to the end of the currentLine
      }
    }
  }
  
  // clear the header variable
  header = "";
  // close the connection
  client.stop();
  DEBUG_SERIAL.println("Client disconnected.");
}

void setup() {
  //pinMode(LED_BUILTIN, OUTPUT);

  DEBUG_SERIAL.begin(115200);        // Start the Serial communication to send messages to the computer

  // init wifi
  startWiFi(); 
  
  //GPIO 1 (TX) swap the pin to a GPIO.
  pinMode(1, FUNCTION_3); 

  // init server
  server.begin();
  
  // init littleFS
  if(!LittleFS.begin())
    DEBUG_SERIAL.println("LittleFS fail");

  // init websocket
  startWebSocket();

  // init EEPROM
  EEPROM.begin(512);

  // read grid from EEPROM
  readEEPROM();

  // init UDP
  if(!UDP.begin(UDP_LISTEN_PORT))
    DEBUG_SERIAL.println("UDP fail");

  pinMode(PIN_CLK, OUTPUT);
  pinMode(PIN_DAT, OUTPUT);
  pinMode(PIN_STR, OUTPUT);
}


void loop() {
    // constantly check for websocket events
  webSocket.loop();                           


  if(micros() - last_micros >= STEP_US){
    last_micros = micros();

    bool sec = secondary > 0;

    shiftOut(PIN_DAT, PIN_CLK, LSBFIRST, ~(0b10000000 >> row));
    shiftOut(PIN_DAT, PIN_CLK, LSBFIRST, sec ? temp_grid_arr[row] : grid_arr[row]);
    store();
    
    secondary -= (int)sec;
    row = (row + 1) % 8;
  }

  // disable LED
  //digitalWrite(LED_BUILTIN, HIGH);

  // check   for incoming clients
  server_client = server.available();   

  // handle incoming requests
  if (server_client){
    //digitalWrite(LED_BUILTIN, LOW);
    handleClient(server_client);
  }

  checkUDP();

  //digitalWrite(LED_BUILTIN, HIGH);
}