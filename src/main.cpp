// file:	main.cpp summary: FlyballETS-Software by simonttp78 forked from Alex Goris
//
// Flyball ETS (Electronic Training System) is an open source project which is designed to help
// teams who practice flyball (a dog sport). Read about original project, including extensive
// information on first prototype version of Flyball ETS, on the following link: https://
// sparkydevices.wordpress.com/tag/flyball-ets/
//
// This part of the project (FlyballETS-Software) contains the ESP32 source code for the ESP32
// LoLin32 which controls all components in the Flyball ETS These sources are originally
// distributed from: https://github.com/vyruz1986/FlyballETS-Software.
//
// Copyright (C) 2019 Alex Goris
// This file is part of FlyballETS-Software
// FlyballETS-Software is free software : you can redistribute it and / or modify it under the terms of
// the GNU General Public License as published by the Free Software Foundation, either version 3 of
// the License, or (at your option) any later version.
//
// This program is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY;
// without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License along with this program.If not,
// see <http://www.gnu.org/licenses/>
#include "main.h"

// Declare WS2811B compatibile lights strip
NeoPixelBus<NeoRgbFeature, WS_METHOD> LightsStrip(5 * LIGHTSCHAINS, iLightsDataPin);

// Declare 40x4 LCD by 2 virtual LCDes
LiquidCrystal lcd(iLCDRSPin, iLCDE1Pin, iLCDData4Pin, iLCDData5Pin, iLCDData6Pin, iLCDData7Pin);  // this will be line 1&2 of 40x4 LCD
LiquidCrystal lcd2(iLCDRSPin, iLCDE2Pin, iLCDData4Pin, iLCDData5Pin, iLCDData6Pin, iLCDData7Pin); // this will be line 3&4 of 40x4 LCD

// IP addresses declaration
IPAddress IPGateway(192, 168, 20, 1);
IPAddress IPNetwork(192, 168, 20, 0);
IPAddress IPSubnet(255, 255, 255, 0);

// Statically allocate and initialize the spinlock
static portMUX_TYPE spinlock = portMUX_INITIALIZER_UNLOCKED;

void setup()
{
   EEPROM.begin(EEPROM_SIZE);
   Serial.begin(115200);
   SettingsManager.init();

   // Configure sensors pins
   pinMode(iS1Pin, INPUT_PULLDOWN); // ESP32 has no pull-down resistor on pin 34, but it's pulled-down anyway by 1kohm resistor in voltage leveler circuit
   pinMode(iS2Pin, INPUT_PULLDOWN);

   // Initialize lights
   pinMode(iLightsDataPin, OUTPUT);

   // Configure pins for 74HC166
   // pinMode(iLatchPin, OUTPUT);
   // pinMode(iClockPin, OUTPUT);
   // pinMode(iDataInPin, INPUT_PULLDOWN);

   // Configure pins for SD Card
   // pinMode(iSDdata0Pin, INPUT_PULLUP);
   // pinMode(iSDdata1Pin, INPUT_PULLUP);
   // pinMode(iSDcmdPin, INPUT_PULLUP);
   // pinMode(iSDdetectPin, INPUT_PULLUP);

   // Configure LCD pins
   pinMode(iLCDData4Pin, OUTPUT);
   pinMode(iLCDData5Pin, OUTPUT);
   pinMode(iLCDData6Pin, OUTPUT);
   pinMode(iLCDData7Pin, OUTPUT);
   pinMode(iLCDE1Pin, OUTPUT);
   pinMode(iLCDE2Pin, OUTPUT);
   pinMode(iLCDRSPin, OUTPUT);

   // Set ISR's with wrapper functions
#if !Simulate
   attachInterrupt(digitalPinToInterrupt(iS1Pin), Sensor1Wrapper, CHANGE);
   attachInterrupt(digitalPinToInterrupt(iS2Pin), Sensor2Wrapper, CHANGE);
#endif

   // Configure Laser pins
   pinMode(iLaserOutputPin, OUTPUT);
   pinMode(iLaserTriggerPin, INPUT_PULLUP);

   // Configure GPS PPS pin
   // pinMode(iGPSppsPin, INPUT_PULLDOWN);

   // Print SW version
   Serial.printf("Firmware version: %s\r\n", FW_VER);

   // Initialize BatterySensor class with correct pin
   BatterySensor.init(iBatterySensorPin);

   // Initialize LightsController class
   xTaskCreatePinnedToCore(
       Core1Lights,
       "Lights",
       8192,
       NULL,
       1,
       &taskLights,
       1);

   // Initialize LCDController class with lcd1 and lcd2 objects
   LCDController.init(&lcd, &lcd2);
   /*xTaskCreatePinnedToCore(
      Core1LCD,
      "LCD",
      8192,
      NULL,
      1,
      &taskLCD,
      1);*/

   strSerialData[0] = 0;

   // Initialize GPS
   GPSHandler.init(iGPSrxPin);

   // SD card init
   // if (digitalRead(iSDdetectPin) == LOW)
   //    SDcardController.init();
   // else
   //    Serial.println("SD Card not inserted!");

   // Initialize RaceHandler class with S1 and S2 pins
   xTaskCreatePinnedToCore(
       Core1Race,
       "Race",
       16384,
       NULL,
       1,
       &taskRace,
       1);

#ifdef WiFiON
   // Setup AP
   WiFi.onEvent(WiFiEvent);
   WiFi.mode(WIFI_AP);
   String strAPName = SettingsManager.getSetting("APName");
   String strAPPass = SettingsManager.getSetting("APPass");
   if (!WiFi.softAP(strAPName.c_str(), strAPPass.c_str()))
      log_e("Error initializing softAP!");
   else
      log_i("Wifi started successfully, AP name: %s, pass: %s", strAPName.c_str(), strAPPass.c_str());
   WiFi.softAPConfig(IPGateway, IPGateway, IPSubnet);

   // configure webserver
   WebHandler.init(80);

   // OTA setup
   ArduinoOTA.setPassword(strAPPass.c_str());
   ArduinoOTA.setPort(3232);
   ArduinoOTA.onStart([]()
                      {
      String type;
      if (ArduinoOTA.getCommand() == U_FLASH)
         type = "Firmware";
      else // U_SPIFFS
         type = "Filesystem";
      Serial.println("\n" + type + " update initiated.");
      LCDController.FirmwareUpdateInit(); });
   ArduinoOTA.onEnd([]()
                    { 
      Serial.println("\nUpdate completed.\r\n");
      LCDController.FirmwareUpdateSuccess(); });
   ArduinoOTA.onProgress([](unsigned int progress, unsigned int total)
                         {
      uint16_t iProgressPercentage = (progress / (total / 100));
      if (uiLastProgress != iProgressPercentage)
      {
         Serial.printf("Progress: %u%%\r", iProgressPercentage);
         String sProgressPercentage = String(iProgressPercentage);
         while (sProgressPercentage.length() < 3)
            sProgressPercentage = " " + sProgressPercentage;
         LCDController.FirmwareUpdateProgress(sProgressPercentage);
         uiLastProgress = iProgressPercentage;
      } });
   ArduinoOTA.onError([](ota_error_t error)
                      {
      Serial.printf("Error[%u]: ", error);
      LCDController.FirmwareUpdateError();
      if (error == OTA_AUTH_ERROR) Serial.println("Auth Failed");
      else if (error == OTA_BEGIN_ERROR) Serial.println("Begin Failed");
      else if (error == OTA_CONNECT_ERROR) Serial.println("Connect Failed");
      else if (error == OTA_RECEIVE_ERROR) Serial.println("Receive Failed");
      else if (error == OTA_END_ERROR) Serial.println("End Failed"); });
   ArduinoOTA.begin();
   mdnsServerSetup();
#endif
   // log_i("Setup running on core %d", xPortGetCoreID());

   iLaserOnTime = atoi(SettingsManager.getSetting("LaserOnTimer").c_str());
   log_i("Configured laser ON time: %is", iLaserOnTime);

   log_w("ESP log level %i", CORE_DEBUG_LEVEL);
}

void loop()
{
   // Exclude handling of those services in loop while race is running
   if (RaceHandler.RaceState == RaceHandler.STOPPED || RaceHandler.RaceState == RaceHandler.RESET)
   {
      // Handle settings manager loop
      SettingsManager.loop();

#ifdef WiFiON
      // Handle OTA update if incoming
      ArduinoOTA.handle();
      /*if (bCheckWsClinetStatus)
      {
         bCheckWsClinetStatus = false;
         log_i("IP to check: %s", ipTocheck.toString().c_str());
         WebHandler.disconnectWsClient(ipTocheck);
      }*/
#endif

      // Handle GPS
      GPSHandler.loop();

      // Handle battery sensor main processing
      BatterySensor.CheckBatteryVoltage();

      // Check SD card slot (card inserted / removed)
      // SDcardController.CheckSDcardSlot(iSDdetectPin);
   }

   // Check for serial events
   serialEvent();
   // Handle serial console commands

   if (bSerialStringComplete)
      HandleSerialCommands();

   // Handle remote control and buttons states
   HandleRemoteAndButtons();

   // Handle LCD processing
   LCDController.Main();

#ifdef WiFiON
   // Handle WebSocket server
   WebHandler.loop();
#endif
}

void serialEvent()
{
   // Listen on serial port
   while (Serial.available() > 0)
   {
      char cInChar = Serial.read(); // Read a character
                                    // Check if buffer contains complete serial message, terminated by newline (\n)
      if (cInChar == '\n')
      {
         // Serial message in buffer is complete, null terminate it and store it for further handling
         bSerialStringComplete = true;
         log_d("SERIAL received: '%s'", strSerialData.c_str());
         strSerialData += '\0'; // Null terminate the string
         break;
      }
      strSerialData += cInChar; // Store it
   }
}

/// <summary>
///   These are wrapper functions which are necessary because it's not allowed to use a class member function directly as an ISR
/// </summary>
void IRAM_ATTR Sensor1Wrapper()
{
   RaceHandler.TriggerSensor1(&spinlock);
}

/// <summary>
///   These are wrapper functions which are necessary because it's not allowed to use a class member function directly as an ISR
/// </summary>
void IRAM_ATTR Sensor2Wrapper()
{
   RaceHandler.TriggerSensor2(&spinlock);
}

/// <summary>
///   Start a race.
/// </summary>
void StartRaceMain()
{
   if (RaceHandler.RaceState != RaceHandler.RESET)
      return;
   if (LightsController.bModeNAFA)
      LightsController.WarningStartSequence();
   else
      LightsController.InitiateStartSequence();
}

/// <summary>
///   Stop a race.
/// </summary>
void StopRaceMain()
{
   if (RaceHandler.RaceState == RaceHandler.STOPPED || RaceHandler.RaceState == RaceHandler.RESET)
      return;
   RaceHandler.bExecuteStopRace = true;
   LightsController.DeleteSchedules();
}

/// <summary>
///   Starts (if stopped) or stops (if started) a race. Start is only allowed if race is stopped and reset.
/// </summary>
void StartStopRace()
{
   if (RaceHandler.RaceState == RaceHandler.RESET)
      StartRaceMain();
   else // If race state is running or starting, we should stop it
      StopRaceMain();
}

/// <summary>
///   Reset race so new one can be started, reset is only allowed when race is stopped
/// </summary>
void ResetRace()
{
   if (RaceHandler.RaceState != RaceHandler.STOPPED) // Only allow reset when race is stopped first
      return;
   RaceHandler.bExecuteResetRace = true;
   LightsController.bExecuteResetLights = true;
}

#ifdef WiFiON
void WiFiEvent(arduino_event_id_t event)
{
   // Serial.printf("Wifi event %i\r\n", event);
   switch (event)
   {
   case ARDUINO_EVENT_WIFI_AP_START:
      // log_i("AP Started");
      WiFi.softAPConfig(IPGateway, IPGateway, IPSubnet);
      if (WiFi.softAPIP() != IPGateway)
      {
         log_e("I am not running on the correct IP (%s instead of %s), rebooting!", WiFi.softAPIP().toString().c_str(), IPGateway.toString().c_str());
         ESP.restart();
      }
      log_i("Ready on IP: %s, v%s", WiFi.softAPIP().toString().c_str(), APP_VER);
      break;

   case ARDUINO_EVENT_WIFI_AP_STOP:
      // log_i("AP Stopped");
      break;

   case ARDUINO_EVENT_WIFI_AP_STAIPASSIGNED:
      // log_i("IP assigned to new client");
      break;

   case ARDUINO_EVENT_WIFI_AP_STADISCONNECTED:
      // bCheckWsClinetStatus = true;
      // ipTocheck = IPAddress (192,168,20,2);
      // log_i("IP to check: %s", ipTocheck.toString().c_str());
      break;

   default:
      break;
   }
}

void ToggleWifi()
{
   if (WiFi.getMode() == WIFI_MODE_AP)
   {
      WiFi.mode(WIFI_OFF);
      LCDController.UpdateField(LCDController.WifiState, " ");
      LCDController.bExecuteLCDUpdate = true;
      log_i("WiFi OFF");
   }
   else
   {
      WiFi.mode(WIFI_AP);
      LCDController.UpdateField(LCDController.WifiState, "W");
      LCDController.bExecuteLCDUpdate = true;
      log_i("WiFi ON");
   }
}

void mdnsServerSetup()
{
   MDNS.addService("http", "tcp", 80);
   MDNS.addServiceTxt("arduino", "tcp", "app_version", APP_VER);
   MDNS.begin("flyballets");
}
#endif

void HandleSerialCommands()
{
   // Race start
   if (strSerialData == "start")
      StartRaceMain();
   // Race stop
   if (strSerialData == "stop")
      StopRaceMain();
   // Race reset button
   if (strSerialData == "reset")
      ResetRace();
   // Print time
   if (strSerialData == "time")
      log_i("System time:  %s", GPSHandler.GetLocalTimestamp());
   // Print uptime
   if (strSerialData == "uptime")
   {
      uint32_t t = (uint32_t)(millis() / 1000);
      uint8_t s = t % 60;
      t = (t - s) / 60;
      uint8_t m = t % 60;
      t = (t - m) / 60;
      uint16_t h = t;
      log_i("Up time: %i:%i:%i", h, m, s);
   }
   // Delete tag file
   if (strSerialData == "deltagfile")
      SDcardController.deleteFile(SD_MMC, "/tag.txt");
   // List files on SD card
   if (strSerialData == "list")
   {
      SDcardController.listDir(SD_MMC, "/", 0);
      SDcardController.listDir(SD_MMC, "/SENSORS_DATA", 0);
   }
   // Reboot ESP32
   if (strSerialData == "reboot")
      ESP.restart();
   // Prepare for automatic tests (used by testETS.py script)
   if (strSerialData == "preparefortesting")
      if (!Simulate)
         log_e("FAILED - Firmware's not compiled in Simulation mode");
      else
      {
         if (SettingsManager.getSetting("Accuracy3digits").equals("0"))
            RaceHandler.ToggleAccuracy();
         if (SettingsManager.getSetting("RunDirectionInverted").equals("1"))
            RaceHandler.ToggleRunDirection();
         log_i("DONE - Simulation mode active. Accuracy set to 3. Run direction: normal.");
      }
#if Simulate
   // Change Race ID (only serial command), e.g. race 1 or race 2
   if (strSerialData.startsWith("race"))
   {
      strSerialData.remove(0, 5);
      Simulator.iSimulatedRaceID = strSerialData.toInt();
      if (Simulator.iSimulatedRaceID < 0 || Simulator.iSimulatedRaceID >= NumSimulatedRaces)
      {
         Simulator.iSimulatedRaceID = 0;
      }
      // Simulator.ChangeSimulatedRaceID(iSimulatedRaceID);
      Simulator.bExecuteSimRaceChange = true;
   }
#endif
   // Dog 1 fault
   if (strSerialData == "d1f")
      RaceHandler.SetDogFault(0);
   // Dog 2 fault
   if (strSerialData == "d2f")
      RaceHandler.SetDogFault(1);
   // Dog 3 fault
   if (strSerialData == "d3f")
      RaceHandler.SetDogFault(2);
   // Dog 4 fault
   if (strSerialData == "d4f")
      RaceHandler.SetDogFault(3);
   // Toggle race direction
   if (strSerialData == "direction")
      RaceHandler.ToggleRunDirection();
   // Set explicitly number of racing dogs
   if (strSerialData.startsWith("setdogs") && RaceHandler.RaceState == RaceHandler.RESET)
   {
      strSerialData.remove(0, 8);
      uint8_t iNumberofRacingDogs = strSerialData.toInt();
      if (iNumberofRacingDogs < 1 || iNumberofRacingDogs > 4)
      {
         iNumberofRacingDogs = 4;
      }
      RaceHandler.SetNumberOfDogs(iNumberofRacingDogs);
   }
   // Toggle accuracy
   if (strSerialData == "accuracy")
      RaceHandler.ToggleAccuracy();
   // Toggle decimal separator in CSV
   if (strSerialData == "separator")
      SDcardController.ToggleDecimalSeparator();
   // Toggle between modes
   if (strSerialData == "mode")
      LightsController.ToggleStartingSequence();
   // Reruns off
   if (strSerialData == "reruns off")
      RaceHandler.ToggleRerunsOffOn(1);
   // Reruns on
   if (strSerialData == "reruns on")
      RaceHandler.ToggleRerunsOffOn(0);
   // Toggle wifi on/off
   if (strSerialData == "wifi")
      ToggleWifi();
   // Toggle wifi on/off
   if (strSerialData == "fwver")
      Serial.printf("Firmware version: %s\r\n", FW_VER);

   // Make sure this stays last in the function!
   if (strSerialData.length() > 0)
   {
      strSerialData = "";
      bSerialStringComplete = false;
   }
}

void HandleRemoteAndButtons()
{
   // Handle laser output
   digitalWrite(iLaserOutputPin, !digitalRead(iLaserTriggerPin));

   // Handle side switch button
   if (digitalRead(SideSwitch.Pin) == LOW && millis() - SideSwitch.LastTriggerTime > SideSwitch.CoolDownTime)
   {
      SideSwitch.LastTriggerTime = millis();
      ESP_LOGI(__FILE__, "Switching sides!");
      RaceHandler.ToggleRunDirection();
   }

   // byDataIn = 0;
   // digitalWrite(iLatchPin, LOW);
   // digitalWrite(iClockPin, LOW);
   // digitalWrite(iClockPin, HIGH);
   // digitalWrite(iLatchPin, HIGH);
   // for (uint8_t i = 0; i < 8; ++i)
   // {
   //    byDataIn |= digitalRead(iDataInPin) << (7 - i);
   //    digitalWrite(iClockPin, LOW);
   //    digitalWrite(iClockPin, HIGH);
   // }
   // // It's assumed that only one button can be pressed at the same time, therefore any multiple high states are treated as false read and ingored
   // if (byDataIn != 0 && byDataIn != 1 && byDataIn != 2 && byDataIn != 4 && byDataIn != 8 && byDataIn != 16 && byDataIn != 32 && byDataIn != 64 && byDataIn != 128)
   // {
   //    // log_d("Unknown buttons data --> Ignored");
   //    byDataIn = 0;
   // }
   // // Check if the switch/button changed, due to noise or pressing:
   // if (byDataIn != byLastFlickerableState)
   // {
   //    // reset the debouncing timer
   //    llLastDebounceTime = millis();
   //    // save the the last flickerable state
   //    byLastFlickerableState = byDataIn;
   // }
   // if ((byLastStadyState != byDataIn) && ((millis() - llLastDebounceTime) > DEBOUNCE_DELAY))
   // {
   //    if (byDataIn != 0)
   //    {
   //       iLastActiveBit = log2(byDataIn & -byDataIn);
   //       // log_d("byDataIn is: %i, iLastActiveBit is: %i", byDataIn, iLastActiveBit);
   //    }
   //    // whatever the reading is at, it's been there for longer than the debounce
   //    // delay, so take it as the actual current state:
   //    // if the button state has changed:
   //    if (bitRead(byLastStadyState, iLastActiveBit) == LOW && bitRead(byDataIn, iLastActiveBit) == HIGH)
   //    {
   //       llPressedTime[iLastActiveBit] = millis();
   //       // log_d("The button is pressed: %lld", llPressedTime[iLastActiveBit]);
   //    }
   //    else if (bitRead(byLastStadyState, iLastActiveBit) == HIGH && bitRead(byDataIn, iLastActiveBit) == LOW)
   //    {
   //       llReleasedTime[iLastActiveBit] = millis();
   //       // log_d("The button is released: %lld", llReleasedTime[iLastActiveBit]);
   //    }
   //    // save the the last state
   //    byLastStadyState = byDataIn;
   //    long long llPressDuration = (llReleasedTime[iLastActiveBit] - llPressedTime[iLastActiveBit]);
   //    if (llPressDuration > 0)
   //    {
   //       // Action not dependent on short/long button press
   //       if (iLastActiveBit == 1) // Race start/stop button (remote D0 output)
   //          StartStopRace();
   //       else if (iLastActiveBit == 2) // Race reset button (remote D1 output)
   //          ResetRace();
   //       // Actions for SHORT button press
   //       if (llPressDuration <= SHORT_PRESS_TIME)
   //       {
   //          log_d("%s SHORT press detected: %lldms", GetButtonString(iLastActiveBit).c_str(), llPressDuration);
   //          if (iLastActiveBit == 3) // Dog 1 fault RC button
   //             if (RaceHandler.RaceState == RaceHandler.RESET)
   //                RaceHandler.SetNumberOfDogs(1);
   //             else
   //                RaceHandler.SetDogFault(0);
   //          else if (iLastActiveBit == 6) // Dog 2 fault RC button
   //             if (RaceHandler.RaceState == RaceHandler.RESET)
   //                RaceHandler.SetNumberOfDogs(2);
   //             else
   //                RaceHandler.SetDogFault(1);
   //          else if (iLastActiveBit == 5) // Dog 3 fault RC button
   //             if (RaceHandler.RaceState == RaceHandler.RESET)
   //                RaceHandler.SetNumberOfDogs(3);
   //             else
   //                RaceHandler.SetDogFault(2);
   //          else if (iLastActiveBit == 4) // Dog 4 fault RC button
   //             if (RaceHandler.RaceState == RaceHandler.RESET)
   //                RaceHandler.SetNumberOfDogs(4);
   //             else
   //                RaceHandler.SetDogFault(3);
   //          else if (iLastActiveBit == 0 && (RaceHandler.RaceState == RaceHandler.STOPPED || RaceHandler.RaceState == RaceHandler.RESET)) // Mode button - accuracy change
   //             RaceHandler.ToggleAccuracy();
   //          else if (iLastActiveBit == 7 && !bLaserActive && (RaceHandler.RaceState == RaceHandler.STOPPED || RaceHandler.RaceState == RaceHandler.RESET)) // Laser activation
   //          {
   //             digitalWrite(iLaserOutputPin, HIGH);
   //             bLaserActive = true;
   //             log_i("Turn Laser ON.");
   //          }
   //       }
   //       // Actions for LONG button press
   //       else if (llPressDuration > SHORT_PRESS_TIME)
   //       {
   //          log_d("%s LONG press detected: %lldms", GetButtonString(iLastActiveBit).c_str(), llPressDuration);
   //          if (iLastActiveBit == 3) // Dog 1 fault RC button - toggling reruns off/on
   //             RaceHandler.ToggleRerunsOffOn(2);
   //          else if (iLastActiveBit == 6 && RaceHandler.RaceState == RaceHandler.RESET) // Dog 2 fault RC button - toggling starting sequence NAFA / FCI
   //             LightsController.ToggleStartingSequence();
   //          else if (iLastActiveBit == 0) // Mode button - side switch
   //             RaceHandler.ToggleRunDirection();
   //          else if (iLastActiveBit == 7 && RaceHandler.RaceState == RaceHandler.RESET) // Laster button - Wifi Off
   //             ToggleWifi();
   //       }
   //    }
   // }
   // // Laser deativation
   // if ((bLaserActive) && ((millis() - llReleasedTime[7] > iLaserOnTime * 1000) || RaceHandler.RaceState == RaceHandler.STARTING || RaceHandler.RaceState == RaceHandler.RUNNING))
   // {
   //    digitalWrite(iLaserOutputPin, LOW);
   //    bLaserActive = false;
   //    log_i("Turn Laser OFF.");
   // }
}

/// <summary>
///   Gets pressed button string for consol printing.
/// </summary>
String GetButtonString(uint8_t _iActiveBit)
{
   String strButton;
   switch (_iActiveBit)
   {
   case 0:
      strButton = "Mode button";
      break;
   case 1:
      strButton = "Remote 1: start/stop";
      break;
   case 2:
      strButton = "Remote 2: reset";
      break;
   case 3:
      strButton = "Remote 3: dog 1 fault";
      break;
   case 6:
      strButton = "Remote 6: dog 4 fault";
      break;
   case 5:
      strButton = "Remote 5: dog 3 fault";
      break;
   case 4:
      strButton = "Remote 4: dog 2 fault";
      break;
   case 7:
      strButton = "Laser trigger";
      break;
   default:
      strButton = "Unknown --> Ingored";
      break;
   }

   return strButton;
}

void Core1Race(void *parameter)
{
#if Simulate
   Simulator.init();
#endif
   RaceHandler.init(iS1Pin, iS2Pin);
   for (;;)
   {
#if Simulate
      Simulator.Main();
#endif
      RaceHandler.Main();
      vTaskDelay(1 / portTICK_PERIOD_MS);
   }
}

void Core1Lights(void *parameter)
{
   LightsController.init(&LightsStrip);
   for (;;)
   {
      LightsController.Main();
      vTaskDelay(1 / portTICK_PERIOD_MS);
   }
}

/*void Core1LCD(void *parameter)
{
   LCDController.init(&lcd, &lcd2);
   for (;;)
   {
      LCDController.Main();
      vTaskDelay(1 / portTICK_PERIOD_MS);
   }
}*/