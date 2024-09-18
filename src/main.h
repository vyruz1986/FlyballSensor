#include "Arduino.h"

// Includes
#include <Structs.h>
#include <config.h>

// Public libs
#include <LiquidCrystal.h>
#ifdef WiFiON
#include <WiFi.h>
#include <WiFiMulti.h>
#include <ESPmDNS.h>
#include <ArduinoOTA.h>
#include <WiFiUdp.h>
#include <WiFiClientSecure.h>
#endif
#include <EEPROM.h>
#include <NeoPixelBus.h>
#include <ESPmDNS.h>
// #include <time.h>

// Private libs
#include "GPSHandler.h"
#include "SettingsManager.h"
#ifdef WiFiON
#include "WebHandler.h"
#endif
#include "LCDController.h"
#include "RaceHandler.h"
#include "LightsController.h"
#include "BatterySensor.h"
#include "SDcardController.h"
// #include "SystemManager.h"
// #include "SlaveHandler.h"
// #include "WifiManager.h"

// Set simulate to true to enable simulator class (see Simulator.cpp/h)
#if Simulate
#include "Simulator.h"
#endif

// Function prototypes
void IRAM_ATTR Sensor1Wrapper();
void IRAM_ATTR Sensor2Wrapper();
void ResetRace();
void StartStopRace();
void StartRaceMain();
void StopRaceMain();
void mdnsServerSetup();
void serialEvent();
void HandleSerialCommands();
void HandleRemoteAndButtons();
void ToggleWifi();
void Core1Race(void *parameter);
void Core1Lights(void *parameter);
void Core1LCD(void *parameter);
String GetButtonString(uint8_t _iActiveBit);
#ifdef WiFiON
void WiFiEvent(arduino_event_id_t event);
#endif

// Photoelectric sensors
const uint8_t iS1Pin = 34; // S1 (handler side) photoelectric sensors
const uint8_t iS2Pin = 33; // S2 (box side) photoelectric sensors

// 40x4 LCD
const uint8_t iLCDE1Pin = 2;     // E1 pin of virtual LCD1 (raw 1 & 2 of 40x4 LCD)
const uint8_t iLCDE2Pin = 15;    // E1 pin of virtual LCD2 (raw 3 & 4 of 40x4 LCD)
const uint8_t iLCDData4Pin = 13; // Data4
const uint8_t iLCDData5Pin = 26; // Data5
const uint8_t iLCDData6Pin = 14; // Data6
const uint8_t iLCDData7Pin = 27; // Data7
const uint8_t iLCDRSPin = 25;    // RS pin

// GPS module pins
const uint8_t iGPSrxPin = 39; // TXD pin of GPS module (ESP32 RX)

// control pins for SD card (require HW 5.0.0 rev.S or higher)
// const uint8_t iSDdata0Pin = 2;  // Data0
// const uint8_t iSDdata1Pin = 4;  // Data1
// const uint8_t iSDclockPin = 14; // Clock
// const uint8_t iSDcmdPin = 15;   // Command
// const uint8_t iSDdetectPin = 5; // Detect (state low when card inserted)

// control pins for 74HC166 (remote + side switch)
// const uint8_t iLatchPin = 23;  // latchPin (CE)   of 74HC166
// const uint8_t iClockPin = 18;  // clockPin (CP)   of 74HC166
// const uint8_t iDataInPin = 19; // dataOutPin (Q7) of 74HC166

// 74HC166 pinouts
//    - D0: Mode (former Side switch) button
//    - D1: remote D0 start/stop
//    - D2: remote D1 reset
//    - D3: remote D2 dog 1 fault
//    - D4: remote D5 dog 4 fault
//    - D5: remote D4 dog 3 fault
//    - D6: remote D3 dog 2 fault
//    - D7: Laser trigger button

// Other
const uint8_t iBatterySensorPin = 35; // Battery sensor (voltage divider)
const uint8_t iLaserOutputPin = 12;   // Laser diodes enable signal
const uint8_t iLaserTriggerPin = 32;
const uint8_t iLightsDataPin = 0; // WS2811B lights data

// Not in use
// 1: free/TX
// 3: free/RX

// Global variables
bool bCheckWsClinetStatus = false; // flag to check if WS client should be disconnected
IPAddress ipTocheck;               // IP address of disconnected WiFi user

unsigned int uiLastProgress = 0; // last % OTA progress value

uint16_t iLaserOnTime = 180; // initial value of laser diode on time
bool bLaserActive = false;   // laser diode state

// variables for handling 74HC166
unsigned long long llLastDebounceTime = 0;
unsigned long long llPressedTime[8] = {0, 0, 0, 0, 0, 0, 0, 0};
unsigned long long llReleasedTime[8] = {0, 0, 0, 0, 0, 0, 0, 0};
uint8_t iLastActiveBit = 0;
byte byDataIn = 0;
byte byLastStadyState = 0;
byte byLastFlickerableState = 0;
const uint16_t DEBOUNCE_DELAY = 30;    // in ms
const uint16_t SHORT_PRESS_TIME = 700; // in ms

// String for serial comms storage
String strSerialData;
byte bySerialIndex = 0;
bool bSerialStringComplete = false;

TaskHandle_t taskRace;
TaskHandle_t taskLights;
TaskHandle_t taskLCD;

struct stInputSignal
{
    uint8_t Pin;
    unsigned long LastTriggerTime;
    word CoolDownTime;
};

stInputSignal SideSwitch = {5, 0, 500};