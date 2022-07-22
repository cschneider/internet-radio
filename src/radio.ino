#include "Arduino.h"
#include <WiFiClient.h>
#include <DNSServer.h>
#include <WiFiManager.h> // https://github.com/tzapu/WiFiManager
#include "Audio.h"
#include "SPI.h"
#include "SD.h"
#include "FS.h"
#include <Wire.h>
#include <Rotary.h>
#include <OneButton.h>
#include <LiquidCrystal_I2C.h>

String volumeBar = "####################";

String titles[] = { 
    "BIG FM",
    "NHK World Radio Japan",
    "Eins live", 
    "Macslons Irish pub radio", 
    "FFH",
    "Radio BOB",
    "Radio 21 Classic Rock"
 };
String streams[] = { 
    "http://streams.bigfm.de/bigfm-deutschland-128-aac?usid=0-0-H-A-D-30",
    "https://nhkworld.webcdn.stream.ne.jp/www11/radiojapan/all/263943/live_s.m3u8",
    "http://www.wdr.de/wdrlive/media/einslive.m3u", 
    "http://macslons-irish-pub-radio.com/media.asx", 
    "http://mp3.ffh.de/radioffh/hqlivestream.aac",
    "http://streams.radiobob.de/101/mp3-128/streams.radiobob.de/play.m3u",
    "http://188.94.97.91/radio21.mp3"
 };

// Digital I/O used
#define SD_CS          5
#define SPI_MOSI      23
#define SPI_MISO      19
#define SPI_SCK       18

#define I2S_DOUT      25
#define I2S_BCLK      27
#define I2S_LRC       26

#define ROTARY_A      33
#define ROTARY_B      15
#define ROTARY_SW     32

#define NUM_STATIONS   7
#define MAX_VOLUME    10

#define MODE_VOLUME   0
#define MODE_STATION  1

Audio audio;
WiFiManager wm;
LiquidCrystal_I2C lcd(0x3F,20,4);
OneButton playB = OneButton(ROTARY_SW, PULLUP);
Rotary rotary = Rotary(ROTARY_A, ROTARY_B);

int station = 0;
int volume = 5;
int mode = MODE_VOLUME;
const char* curStream;
const char* curTitle;

void setup() {
    Serial.begin(115200);
    //attachInterrupt(digitalPinToInterrupt(ROTARY_A), rotate, CHANGE); // Causes crash
    Wire.setClock(10000);
    lcd.init();
    lcd.backlight();
    lcd.setCursor(0, 0);
    lcdprint(0, "Connecting to WIFI");
    lcdprint(1, "Long press for defaults");
    playB.attachClick(handleButton);
    playB.attachLongPressStart(handleReset);
    

    //pinMode(SD_CS, OUTPUT);
    //digitalWrite(SD_CS, HIGH);
    //SPI.begin(SPI_SCK, SPI_MISO, SPI_MOSI);
    //SPI.setFrequency(1000000);
    
    //SD.begin(SD_CS);
    
    WiFi.mode(WIFI_STA);
    WiFi.hostname("Radio");
    //wm.resetSettings();
    wm.autoConnect("Radio");
    audio.setPinout(I2S_BCLK, I2S_LRC, I2S_DOUT);
    audio.setVolume(12); // 0...21
    TaskHandle_t Task1;
    xTaskCreatePinnedToCore(
      core2, /* Function to implement the task */
      "rotary", /* Name of the task */
      10000,  /* Stack size in words */
      NULL,  /* Task input parameter */
      0,  /* Priority of the task */
      &Task1,  /* Task handle. */
      1); /* Core where the task should run */
    switchStation();
}

void loop()
{
    audio.loop();
    playB.tick();
    
    if(Serial.available()){ // put streamURL in serial monitor 
        audio.stopSong();
        String r=Serial.readString(); r.trim();
        if(r.length()>5) audio.connecttohost(r.c_str());
        log_i("free heap=%i", ESP.getFreeHeap());
    }
}

void core2( void * parameter) {
    for(;;) {
        rotate();
    }
}

// optional
void audio_info(const char *info){
//    Serial.print("info        "); Serial.println(info);
}
void audio_id3data(const char *info){  //id3 metadata
    Serial.print("id3data     ");Serial.println(info);
}
void audio_eof_mp3(const char *info){  //end of file
    Serial.print("eof_mp3     ");Serial.println(info);
}
void audio_showstation(const char *info){
    Serial.print("station     ");Serial.println(info);
}
void audio_showstreamtitle(const char *info){
    Serial.print("streamtitle ");Serial.println(info);
    String infoS = String(info);
    lcdprint(2, infoS);
    lcdprint(3, infoS.substring(20));
}
void audio_bitrate(const char *info){
    Serial.print("bitrate     ");Serial.println(info);
}
void audio_commercial(const char *info){  //duration in sec
    Serial.print("commercial  ");Serial.println(info);
}
void audio_icyurl(const char *info){  //homepage
    Serial.print("icyurl      ");Serial.println(info);
}
void audio_lasthost(const char *info){  //stream URL played
    Serial.print("lasthost    ");Serial.println(info);
}

void configModeCallback (WiFiManager *myWiFiManager) {
    lcdprint(0, "Entered config mode");
    lcdprint(1, "SSID:" + myWiFiManager->getConfigPortalSSID());
    lcdprint(2, "IP:" + WiFi.softAPIP());
}

int getDelta() {
    unsigned char result = rotary.process();
    if (result == DIR_CW) {
        return 1;
    } else if (result == DIR_CCW) {
        return -1;
    } else {
        return 0;
    }
}

int limit(int value, int min, int max) {
    if (value < min) {
        return min;
    } else if (value > max) {
        return max;
    } else {
        return value;
    }
}

// rotate is called anytime the rotary inputs change state.
void rotate() {
    int delta = getDelta();
    if (delta != 0) {
        if (mode == MODE_VOLUME) {
            volume = limit(volume + delta, 0 , MAX_VOLUME);
            showVolume();
        } else {
            station = limit(station + delta, 0, NUM_STATIONS);
            showCurrentStation();
        }
    }
}

void showVolume() {
    String volumeSt = volumeBar.substring(0, volume*2);
    lcdprint(1, volumeSt);
    audio.setVolume(volume*2);
}

void showCurrentStation() {
    curTitle = (titles[station]).c_str();
    lcdprint(0, curTitle);
}
void handleButton() {
    if (mode == MODE_VOLUME) {
        mode = MODE_STATION;
        showCurrentStation();
    } else {
        mode = MODE_VOLUME;
        switchStation();
    }
    Serial.print("click new mode: ");
    Serial.println(mode);
}
void switchStation() {
    lcd.clear();
    showCurrentStation();
    /*
    if (audio.isRunning()) {
        audio.stopSong();
    } else {
        */
        curStream = (streams[station]).c_str();
        audio.connecttohost(curStream);
    //}
}

void lcdprint(int lineNum, String text) {
    Serial.println(text);
    String line = text.substring(0,20);
    lcd.setCursor(0, lineNum);
    lcd.print(line);
    padLine(20-line.length());
}

void padLine(int remaining) {
    if (remaining > 0) {
        for (int c; c < remaining; c++) {
            lcd.print(' ');
        }
    }
}

void handleReset() {
    lcdprint(0, "Wifi reset");
    wm.resetSettings();
    ESP.restart();
}


