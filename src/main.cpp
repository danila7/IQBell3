#include <Arduino.h>
#include <SPI.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <WiFi.h>
#include <EEPROM.h>
#include <PubSubClient.h>
#include <time.h>

#define SCREEN_WIDTH 128 // OLED display width, in pixels
#define SCREEN_HEIGHT 64 // OLED display height, in pixels
#define RELAY_PIN 4
#define BELL_ON_TIME 7

const char* ssid     = "JustANet";
const char* password = "wifi4you";
const char* mqttServer = "tailor.cloudmqtt.com";
const int mqttPort = 18846;
const char* mqttUser = "ujqjrjyr";
const char* mqttPassword = "iCmu4k4g5RaB";

const char* ntpServer = "de.pool.ntp.org";
const long  gmtOffset_sec = 10800;
const int   daylightOffset_sec = 0;

struct tm timeinfo;

boolean introFlag = true;

typedef union {
	uint32_t lValue;
	uint8_t bValue[sizeof(lValue)];
} ULongByBytes; //is using for sending and receiving unsigned long via Serial


Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, -1);
WiFiClient wifiClient;
PubSubClient client(wifiClient);

boolean secondTimetable, bellIsOn = false, notSent = true;
byte mode = 0, currentDay = 255, secondPrev = 255, i, ttable[24], prevBellNum = 255, relayOnTime = 0;
byte firstBell, lastBell, prevBell, numOfBell, ringingState = 0;
int firstBellMinute, lastBellMinute, ii;
long timeTillBell;

boolean workshop[15] = {false, true, true, true,  true, false, true,  true, true, true, false, true, true, true, true};
boolean assembly[12] = {false, true, true, false, true, true, false, true, true,  false, true, true};

void intro();
void updateDisplay();
void checkMode();
String get2digits(byte number);
byte validate(byte vl, byte mn, byte mx);
boolean isInside(byte startDay, byte startMonth, byte endDay, byte endMonth);
void sendState();
void sendFullState();
void setData(byte data[]);
void timeTick();
void bellControl();
void manualBell(byte type);
void callback(char* topic, byte *payload, unsigned int length);
void reconnect();
String utf8rus(String source);

void setup() {
	// put your setup code here, to run once:
	intro();
	pinMode(RELAY_PIN, OUTPUT);
	digitalWrite(RELAY_PIN, LOW);
	WiFi.begin(ssid, password);
	while (WiFi.status() != WL_CONNECTED) delay(500);
	configTime(gmtOffset_sec, daylightOffset_sec, ntpServer);
	getLocalTime(&timeinfo);
	client.setServer(mqttServer, mqttPort);
	client.setCallback(callback);
	reconnect();
}



void intro(){ //showing boot animation
	display.begin(SSD1306_SWITCHCAPVCC, 0x3C);  
	// Clear the buffer.
	display.clearDisplay();
	display.setTextSize(2); // Draw 2X-scale text
  	display.setTextColor(SSD1306_WHITE);
  	display.setCursor(10, 0);
	display.println(utf8rus("Загрузка"));
 	display.display();      // Show initial text
 	display.startscrollleft(0x00, 0x0F);
 	delay(2000);
}

void loop() {
	getLocalTime(&timeinfo);
	if (!client.connected()) {
		reconnect();
	}
	client.loop();
	if(notSent){
		sendFullState();
		notSent = false;
	}
	checkMode();
  if(timeinfo.tm_sec !=secondPrev){
	updateDisplay();
	sendState();
		if(mode == 0){
			timeTick();
			bellControl();
		} else ringingState = 0;
		secondPrev = timeinfo.tm_sec;
	}	
}

void updateDisplay(){
	String tm = get2digits(timeinfo.tm_hour) + ":" + get2digits(timeinfo.tm_min) + ":" + get2digits(timeinfo.tm_sec);
	String dt = get2digits(timeinfo.tm_mday) + "/" + get2digits(timeinfo.tm_mon + 1) + "/" + String((timeinfo.tm_year + 1900));
	if(introFlag){
		display.stopscroll();
		introFlag = false;
	}
	display.clearDisplay();
	display.setTextSize(1);
  	display.setCursor(0, 0);
	  display.print(tm + " " + dt);
	display.setCursor(0, 16);
	String dow;
	switch (timeinfo.tm_wday){
	case 0: dow = "Воскресенье";
		break;
	case 1: dow = "Понедельник";
		break;
	case 2: dow = "Вторник";
		break;
	case 3: dow = "Среда";
		break;
	case 4: dow = "Четверг";
		break;
	case 5: dow = "Пятница";
		break;
	default: dow = "Суббота";
		break;
	}
	display.print(utf8rus(dow));
	String textMode = "";
	switch (mode){
	case 0:
		textMode += "Идут уроки";
		break;
	case 1:
		textMode += "Суб/Воскр";
		break;
	case 2:
		textMode += "Каникулы";
		break;
	case 3:
		textMode += "Выходной";
		break;
	case 4:
		textMode += "Уроки ещё не начались";
		break;
	default:
		textMode += "Уроки закончились";
		break;
	}
	display.setCursor(0, 32);
	display.print(utf8rus(textMode));
	if(mode == 0 && numOfBell != 255){
		display.setCursor(0, 25);
		String comingBells=get2digits(byte(timeTillBell/60))+":"+get2digits(byte(timeTillBell%60));
		comingBells+="  "+get2digits(ttable[numOfBell]/12+8)+":"+get2digits(ttable[numOfBell]%12*5);
		display.print(comingBells);
	}
	if(ringingState > 0 && relayOnTime != 0){
		display.setCursor(67, 16);
		display.print(utf8rus("Звонок: "));
		display.print(get2digits(relayOnTime));
	}
	if(mode == 0 || mode > 3){
		display.setCursor(0, 48);
		display.print(utf8rus(secondTimetable?"Сокращеннные уроки":"Обычные уроки"));
	}
	display.display();      // Show initial text
}

void callback(char* topic, byte* message, unsigned int length) {
	if(topic[0] == 's'){
		byte messageTemp[112];
		for (int i = 0; i < 112; i++) {
			messageTemp[i] = message[i];
		}
		setData(messageTemp);
	} else if(topic[0] == 'm'){
		manualBell(byte(message[0]));
	}
	
}

void reconnect() {
  // Loop until we're reconnected
  while (!client.connected()) {
	// Attempt to connect
	if (client.connect("bell",mqttUser,mqttPassword)) {
	  // ... and resubscribe
	  client.subscribe("s");
	  client.subscribe("m");
	} else {
	  // Wait 5 seconds before retrying
	  delay(5000);
	}
  }
}

void checkMode(){
	mode = 0; //average mode
  	if (currentDay != timeinfo.tm_mday) {
		if (timeinfo.tm_wday == 1 || timeinfo.tm_wday == 7) mode = 1;
		else {
			for (i = 0; i < 8; i++) {
				byte startExceptionMonth = EEPROM.read(48 + i * 4);
				byte startExceptionDay = EEPROM.read(49 + i * 4);
				byte endExceptionMonth = EEPROM.read(50 + i * 4);
				byte endExceptionDay = EEPROM.read(51 + i * 4);
				if(startExceptionDay > 31 || endExceptionDay > 31 ||
				startExceptionMonth > 12 || endExceptionMonth > 12 || startExceptionDay == 0 ||
				startExceptionMonth == 0 || endExceptionDay == 0 || endExceptionMonth == 0) continue;
				if (isInside(startExceptionDay, startExceptionMonth, endExceptionDay, endExceptionMonth)){
					mode = 2;
				}
			}
			secondTimetable = false;
			if(mode == 0){
				for (i = 0; i < 16; i++) {
					byte exceptionMonth = EEPROM.read(80 + i * 2);
					byte exceptionDay = EEPROM.read(81 + i * 2);
					boolean shortDay;
					shortDay = (exceptionMonth > 127);
					exceptionMonth &= B01111111;
					if (exceptionMonth == (timeinfo.tm_mon + 1) && exceptionDay == timeinfo.tm_mday) {
						if (shortDay == false) mode = 3;
						else secondTimetable = true;
					}
				}
			}
		}
		for (i = 0; i < 24; i++) ttable[i] = EEPROM.read(secondTimetable ? i + 24 : i);
		firstBell = 255;
		lastBell = 0;
		for(i = 0; i< 24; i++){
			if(ttable[i] > 127) continue;
			if(ttable[i] > lastBell) lastBell = ttable[i];
			if(ttable[i] < firstBell) firstBell = ttable[i];
		}
		firstBellMinute = (firstBell/12+8)*60 + (firstBell%12*5);
		lastBellMinute = (lastBell/12+8)*60 + (lastBell%12*5);
		currentDay = timeinfo.tm_mday;
	}
	if(mode == 0){
		if((firstBellMinute - 10) >= (timeinfo.tm_hour*60+timeinfo.tm_min)) mode = 4; //classes have not started
		else if((lastBellMinute + 10) <= (timeinfo.tm_hour*60+timeinfo.tm_min)) mode = 5; //classes have finished
	}
}

boolean isInside(byte startDay, byte startMonth, byte endDay, byte endMonth) {
	int today = timeinfo.tm_mon * 31 + (timeinfo.tm_mday - 1);
	int startDate = (startMonth - 1) * 31 + (startDay - 1);
	int endDate = (endMonth - 1) * 31 + (endDay - 1);
	if(startDate == endDate) return (startDate == today);
	else if (startDate > endDate) {
		if (today < 186) {
			if (today <= endDate) return true;
			else return false;
		}
		else {
			if (today >= startDate) return true;
			else return false;
		}
	}
	else {
		if (today >= startDate && today <= endDate) return true;
		else return false;
	}
}

void timeTick(){
	prevBell = 255;
	numOfBell = 255;
	for(i=0; i<24; i++){
		
		if(ttable[i] > 127) continue;
		if(ttable[i] < prevBell){
			if((((ttable[i]/12+8)*3600+(ttable[i]%12*5)*60) - (timeinfo.tm_hour*3600 + timeinfo.tm_min*60 + timeinfo.tm_sec)) >= 0){
				prevBell = ttable[i];
				numOfBell = i;
			}
		}
	}
	timeTillBell = (((ttable[numOfBell]/12+8)*3600+(ttable[numOfBell]%12*5)*60) - (timeinfo.tm_hour*3600 + timeinfo.tm_min*60 + timeinfo.tm_sec));
	if(timeTillBell == 0) ringingState = 1;
}

byte validate(byte vl, byte mn, byte mx) {
	if (vl < mn) return mn;
	if (vl > mx) return mx;
	return vl;
}

String get2digits(byte number){
	if (number < 10) return '0' + String(number);
	return String(number);
}

void bellControl(){
	switch (ringingState){
	case 1:
		if(!bellIsOn){
			digitalWrite(RELAY_PIN, HIGH);
			relayOnTime = BELL_ON_TIME;
			bellIsOn = true;
		} else{
			if(relayOnTime == 0){
				bellIsOn = false;
				ringingState = 0;
				digitalWrite(RELAY_PIN, LOW);
			} else relayOnTime--;
		}
		break;
	case 2:
		if(!bellIsOn){
			bellIsOn = true;
			relayOnTime = 15;
		}
		if(bellIsOn){
			relayOnTime--;
			digitalWrite(RELAY_PIN, workshop[relayOnTime]);
			if(relayOnTime == 0){
				bellIsOn = false;
				ringingState = 0;
			}
		}
		break;
	case 3:
		if(!bellIsOn){
			bellIsOn = true;
			relayOnTime = 12;
		}
		if(bellIsOn){
			relayOnTime--;
			digitalWrite(RELAY_PIN, assembly[relayOnTime]);
			if(relayOnTime == 0){
				bellIsOn = false;
				ringingState = 0;
			}
		}
		break;
	}
}

void manualBell(byte type){
	if(ringingState == 0 && mode == 0) ringingState = type;
}

void sendState(){
		ULongByBytes toSend;
		time_t now;
		time(&now);
		toSend.lValue = now;
		char sendf[8];
		for (i = 0; i < 4; i++) sendf[i] = toSend.bValue[i]; //sending current time in unix format as 4 bytes
		byte modeToSend = mode;
		if(secondTimetable) modeToSend += 128;
		sendf[4] = modeToSend;
		sendf[5] = ringingState;
		sendf[6] = relayOnTime;
		sendf[7] = numOfBell;
		client.publish("i", sendf, true);
}

void sendFullState(){
	char sendf[112];
	for(i=0;i<112;i++) sendf[i] = EEPROM.read(i);
	client.publish("d", sendf, true);
}

void setData(byte data[]){
	for(i=0; i<112; i++) EEPROM.write(i, data[i]);
	sendFullState();
}
	
/* Recode russian fonts from UTF-8 to Windows-1251 */
String utf8rus(String source){
  int i,k;
  String target;
  unsigned char n;
  char m[2] = { '0', '\0' };
 
  k = source.length(); i = 0;
 
  while (i < k) {
	n = source[i]; i++;
 
	if (n >= 0xBF){
	  switch (n) {
		case 0xD0: {
		  n = source[i]; i++;
		  if (n == 0x81) { n = 0xA8; break; }
		  if (n >= 0x90 && n <= 0xBF) n = n + 0x2F;
		  break;
		}
		case 0xD1: {
		  n = source[i]; i++;
		  if (n == 0x91) { n = 0xB7; break; }
		  if (n >= 0x80 && n <= 0x8F) n = n + 0x6F;
		  break;
		}
	  }
	}
	m[0] = n; target = target + String(m);
  }
return target;
}
