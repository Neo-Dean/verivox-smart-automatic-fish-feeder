#include "thingProperties.h"
// 1. Core Communication (Must go first!)
#include <Wire.h>

// 2. LCD Screen Libraries
#include <hd44780.h>
#include <hd44780ioClass/hd44780_I2Cexp.h>

// 3. Sensors and Actuators
#include <OneWire.h>
#include <DallasTemperature.h>
#include <RTClib.h>
#include <IRremote.hpp>

// --- PIN DEFINITIONS ---
#define TDS_PIN       A0
#define PH_PIN        A1
#define TURBIDITY_PIN A2
#define TEMP_PIN      4
#define TRIG_PIN      2
#define ECHO_PIN      3
#define SERVO_PIN     9   // Sends HIGH trigger to Uno Pin 2
#define ONLINE_STATUS_PIN 7 // Sends Cloud status to Uno Pin 3
#define RELAY_PIN     6
#define IR_RECEIVE_PIN 8
#define BUZZER_PIN    10

// --- IR HEX CODES ---
const uint32_t IR_FEED_NOW    = 0xBA45FF00; // Button 1: Manual Feed
const uint32_t IR_CHECK_WATER = 0xB946FF00; // Button 2: Check Water
const uint32_t IR_DISPENSE_PH = 0xE31CFF00; // Button OK: Dispense pH
const uint32_t IR_UP_ARROW    = 0xE718FF00; // Arrow Up: Scroll Menu Back
const uint32_t IR_DOWN_ARROW  = 0xAD52FF00; // Arrow Down: Scroll Menu Forward

// --- MENU TRACKER ---
int currentMenuIndex = 0;
const int MAX_MENU_PAGES = 4; // 0=pH, 1=Temp, 2=TDS, 3=Turbidity, 4=Food

// --- OBJECTS ---
OneWire oneWire(TEMP_PIN);
DallasTemperature sensors(&oneWire);
hd44780_I2Cexp lcd; 
RTC_DS3231 myRTC;

// --- TIMERS & TRACKERS ---
unsigned long previousMillis = 0;
const long interval = 2000;
int lastFedHour = -1;

// --- CALIBRATION & THRESHOLDS ---
const int HOPPER_FULL_CM = 4;
const int HOPPER_EMPTY_CM = 15;
const float PH_OFFSET = 0.00;
const float TDS_WARNING_THRESHOLD = 400.0; // Maintenance alert threshold (ppm)
const int TURBIDITY_WARNING_THRESHOLD = 30; // Maintenance alert threshold (NTU)

void setup() {

  pinMode(LED_BUILTIN, OUTPUT);
  digitalWrite(LED_BUILTIN, HIGH);
  Serial.begin(9600);
  delay(1500);

  initProperties();
  ArduinoCloud.begin(ArduinoIoTPreferredConnection);
  setDebugMessageLevel(2);

  // --- DASHBOARD UI LABELS ---
  cloudHeaderMain = "VERIVOX";
  cloudLabelActions = "CONTROL CENTER";
  cloudLabelVitals = "REAL-TIME MONITORING";
  cloudLabelAnalytics = "SYSTEM LOGS & HISTORY";
  cloudSystemOnline = true; // Turns on the dashboard LED widget

  // Initialize Defaults for Sliders in case of offline boot
  if (userMorningHour == 0) userMorningHour = 7;
  if (userEveningHour == 0) userEveningHour = 19;
  
  // Force the slider to 400ms if it gets wiped
  if (userFeedDuration == 0) userFeedDuration = 400; 

  sensors.begin();

  lcd.begin(16, 2);
  lcd.backlight();
  lcd.setCursor(0, 0);
  lcd.print("VeriVox Booting.");

  if (!myRTC.begin()) {
    Serial.println("Couldn't find RTC module!");
  }

  // Initialize Communication Pins to Uno
  pinMode(SERVO_PIN, OUTPUT);
  digitalWrite(SERVO_PIN, LOW); 
  
  pinMode(ONLINE_STATUS_PIN, OUTPUT);
  digitalWrite(ONLINE_STATUS_PIN, LOW); 

  // Initialize Sensors
  pinMode(TRIG_PIN, OUTPUT);
  pinMode(ECHO_PIN, INPUT);
  
  // Initialize Buzzer
  pinMode(BUZZER_PIN, OUTPUT);
  digitalWrite(BUZZER_PIN, LOW); // Start off

  // ACTIVE-HIGH RELAY 
  pinMode(RELAY_PIN, OUTPUT);
  digitalWrite(RELAY_PIN, LOW); // Force the Nano to stay quiet (0V) at boot

  // Initialize IR Receiver
  IrReceiver.begin(IR_RECEIVE_PIN, DISABLE_LED_FEEDBACK);

  setRGB(0, 0, 255); // Triggers dummy function

  // Startup dual-beep
  beepBuzzer(150);
  delay(100);
  beepBuzzer(150);
}

void loop() {
  ArduinoCloud.update();

  // Tell Uno our cloud status
  if (ArduinoCloud.connected()) {
    digitalWrite(ONLINE_STATUS_PIN, HIGH);
  } else {
    digitalWrite(ONLINE_STATUS_PIN, LOW);
  }

  checkIRRemote();

  unsigned long currentMillis = millis();
  if (currentMillis - previousMillis >= interval) {
    previousMillis = currentMillis;
    readAndSyncSensors();
    checkFoodLevel();
    updateLCD();
    checkOfflineSchedule();
  }
}

// --- CORE SENSOR FUNCTIONS ---

void readAndSyncSensors() {
  sensors.requestTemperatures();
  float tempC = sensors.getTempCByIndex(0);
  if (tempC != -127.00) cloudTemperature = tempC;

  long phTotal = 0;
  for (int i = 0; i < 10; i++) {
    phTotal += analogRead(PH_PIN);
    delay(10);
  }
  float phAverage = phTotal / 10.0;
  float phVoltage = (phAverage / 1024.0) * 3.3;
  cloudPH = (3.5 * phVoltage) + PH_OFFSET;
  cloudPH = constrain(cloudPH, 0.0, 14.0);

  float tdsVoltage = (analogRead(TDS_PIN) / 1024.0) * 3.3;
  float compCoeff = 1.0 + 0.02 * (cloudTemperature - 25.0);
  float compVolt = tdsVoltage / compCoeff;
  cloudTDS = (133.42 * pow(compVolt, 3) - 255.86 * pow(compVolt, 2) + 857.39 * compVolt) * 0.5;

  int rawTurbidity = analogRead(TURBIDITY_PIN);
  cloudTurbidity = map(rawTurbidity, 0, 1023, 0, 100);

  checkWaterQuality();
}

void checkFoodLevel() {
  digitalWrite(TRIG_PIN, LOW);
  delayMicroseconds(2);
  digitalWrite(TRIG_PIN, HIGH);
  delayMicroseconds(10);
  digitalWrite(TRIG_PIN, LOW);

  long duration = pulseIn(ECHO_PIN, HIGH, 30000);
  if (duration == 0) duration = (HOPPER_EMPTY_CM * 2) / 0.034;

  long distance = duration * 0.034 / 2;
  int percentage = map(distance, HOPPER_FULL_CM, HOPPER_EMPTY_CM, 100, 0);
  cloudFoodLevel = constrain(percentage, 0, 100);

  cloudFoodLowAlert = (cloudFoodLevel <= 20);
}

void checkWaterQuality() {
  if (cloudPH < 6.5 || cloudPH > 8.5) {
    cloudPHAlert = true;
    cloudWaterWarning = true;
    cloudIsWaterSafe = false; 
    setRGB(255, 0, 0);        
  }
  else if (cloudTDS > TDS_WARNING_THRESHOLD || cloudTurbidity > TURBIDITY_WARNING_THRESHOLD) {
    cloudPHAlert = false;
    cloudWaterWarning = true;
    cloudIsWaterSafe = true;  
    setRGB(255, 128, 0);      
  }
  else {
    cloudPHAlert = false;
    cloudWaterWarning = false;
    cloudIsWaterSafe = true;  

    if (cloudFoodLevel <= 10) {
      setRGB(255, 0, 0);      
    } else if (cloudFoodLowAlert) {
      setRGB(255, 255, 0);    
    } else {
      setRGB(0, 255, 0);      
    }
  }
}

void updateLCD() {
  lcd.clear();
  
  // Always print the system name centered on the top row
  lcd.setCursor(4, 0);
  lcd.print("VERIVOX");
  
  // Switch the bottom row based on the current remote selection
  lcd.setCursor(0, 1);
  switch(currentMenuIndex) {
    case 0:
      lcd.print("pH Level: "); 
      lcd.print(cloudPH, 1);
      break;
    case 1:
      lcd.print("Temp: "); 
      lcd.print(cloudTemperature, 1); 
      lcd.print(" C");
      break;
    case 2:
      lcd.print("TDS: "); 
      lcd.print(cloudTDS, 0); 
      lcd.print(" ppm");
      break;
    case 3:
      lcd.print("Turbidity: "); 
      lcd.print(cloudTurbidity); 
      lcd.print("%");
      break;
    case 4:
      lcd.print("Food Lvl: "); 
      lcd.print(cloudFoodLevel); 
      lcd.print("%");
      break;
  }
}

void beepBuzzer(int duration) {
  digitalWrite(BUZZER_PIN, HIGH);
  delay(duration);
  digitalWrite(BUZZER_PIN, LOW);
}

// --- SCHEDULING & ACTUATOR FUNCTIONS ---

void checkOfflineSchedule() {
  DateTime now = myRTC.now();

  int morningFeed = (userMorningHour != 0) ? userMorningHour : 7;
  int eveningFeed = (userEveningHour != 0) ? userEveningHour : 19;

  if (now.hour() == morningFeed && now.minute() == 0 && lastFedHour != morningFeed) {
    if (!cloudIsWaterSafe) {
      cloudLastFeedingStatus = "SKIPPED (pH Unsafe)";
      beepBuzzer(500); delay(100); beepBuzzer(500);
    } else if (cloudFoodLevel <= 10) {
      cloudLastFeedingStatus = "SKIPPED (Hopper Empty!)";
      beepBuzzer(500); delay(100); beepBuzzer(500);
    } else {
      executeMotorSequence();
      cloudFeedingCount++;
      cloudLastFeedingStatus = cloudWaterWarning ? "Fed Morning (CLEAN TANK!)" : (cloudFoodLowAlert ? "Fed Morning (REFILL FOOD!)" : "Success (Morning)");
    }
    lastFedHour = morningFeed;
  }

  if (now.hour() == eveningFeed && now.minute() == 0 && lastFedHour != eveningFeed) {
    if (!cloudIsWaterSafe) {
      cloudLastFeedingStatus = "SKIPPED (pH Unsafe)";
      beepBuzzer(500); delay(100); beepBuzzer(500);
    } else if (cloudFoodLevel <= 10) {
      cloudLastFeedingStatus = "SKIPPED (Hopper Empty!)";
      beepBuzzer(500); delay(100); beepBuzzer(500);
    } else {
      executeMotorSequence();
      cloudFeedingCount++;
      cloudLastFeedingStatus = cloudWaterWarning ? "Fed Evening (CLEAN TANK!)" : (cloudFoodLowAlert ? "Fed Evening (REFILL FOOD!)" : "Success (Evening)");
    }
    lastFedHour = eveningFeed;
  }

  if (now.hour() != morningFeed && now.hour() != eveningFeed) {
    lastFedHour = -1;
  }
}

void executeMotorSequence() {
  setRGB(0, 0, 255); 
  beepBuzzer(200); 
  
  // If the slider is set to 0, default to a quick 400ms flick
  int spinTime = (userFeedDuration > 0) ? userFeedDuration : 400; 
  
  // 1. Send the HIGH trigger to the Uno to start spinning the auger
  digitalWrite(SERVO_PIN, HIGH); 
  
  // 2. Wait for the designated duration
  delay(spinTime); 
  
  // 3. Send the LOW trigger to the Uno to stop the motor
  digitalWrite(SERVO_PIN, LOW);  
  
  checkWaterQuality(); 
}

void dispensePHTreatment() {
  Serial.println("Activating pH Pump...");
  lcd.clear();
  lcd.print("Pumping pH Fix..");

  setRGB(255, 0, 255);

  // --- TRIGGER THE UNO BRIDGE ---
  digitalWrite(RELAY_PIN, HIGH); // Send 3.3V to Uno Pin 4 to turn pump ON
  delay(2000);
  digitalWrite(RELAY_PIN, LOW);  // Drop to 0V so Uno turns pump OFF

  cloudPHTreatmentCount++;
  cloudLastPHTreatment = "Today";
  updateLCD();

  checkWaterQuality();
}

// --- IR REMOTE FUNCTIONS ---

void checkIRRemote() {
  if (IrReceiver.decode()) {
    uint32_t receivedCode = IrReceiver.decodedIRData.decodedRawData;

    if (receivedCode == IR_FEED_NOW) {
      performManualFeedingWithIR();
    }
    else if (receivedCode == IR_CHECK_WATER) {
      lcd.clear();
      lcd.setCursor(2, 0);
      lcd.print("Scanning");
      lcd.setCursor(2, 1);
      lcd.print("Sensors...");
      delay(500);
      readAndSyncSensors();
      updateLCD();
    }
    else if (receivedCode == IR_DISPENSE_PH) {
      if (cloudPHAlert) {
        dispensePHTreatment();
      } else {
        lcd.clear();
        lcd.print("pH is Normal!");
        delay(1500);
        updateLCD();
      }
    }
    // --- SCROLLING LOGIC ---
    else if (receivedCode == IR_UP_ARROW) {
      currentMenuIndex--; // Go to previous sensor
      if (currentMenuIndex < 0) currentMenuIndex = MAX_MENU_PAGES; // Wrap around to end
      updateLCD();
      beepBuzzer(50); // Tiny click sound when scrolling
    }
    else if (receivedCode == IR_DOWN_ARROW) {
      currentMenuIndex++; // Go to next sensor
      if (currentMenuIndex > MAX_MENU_PAGES) currentMenuIndex = 0; // Wrap around to start
      updateLCD();
      beepBuzzer(50); 
    }

    IrReceiver.resume();
  }
}

void performManualFeedingWithIR() {
  if (!cloudIsWaterSafe) {
    beepBuzzer(500); delay(100); beepBuzzer(500);
    lcd.clear();
    lcd.print("Feed Blocked!");
    lcd.setCursor(0, 1);
    lcd.print("pH Unsafe!");
    delay(2000);
    updateLCD();

  } else if (cloudFoodLevel <= 10) {
    beepBuzzer(500); delay(100); beepBuzzer(500);
    lcd.clear();
    lcd.print("Feed Blocked!");
    lcd.setCursor(0, 1);
    lcd.print("Hopper Empty!");
    delay(2000);
    updateLCD();

  } else {
    executeMotorSequence();
    cloudFeedingCount++;

    if (cloudWaterWarning) {
      cloudLastFeedingStatus = "IR Fed (CLEAN TANK!)";
    } else if (cloudFoodLowAlert) {
      cloudLastFeedingStatus = "IR Fed (REFILL FOOD!)";
    } else {
      cloudLastFeedingStatus = "Success (IR Remote)";
    }
  }
}

// --- CLOUD CALLBACK FUNCTIONS ---

void onCloudManualFeedChange() {
  if (cloudManualFeed) {

    if (!cloudIsWaterSafe) {
      cloudLastFeedingStatus = "SKIPPED (pH Unsafe)";
      beepBuzzer(500); delay(100); beepBuzzer(500);

    } else if (cloudFoodLevel <= 10) {
      cloudLastFeedingStatus = "SKIPPED (Hopper Empty!)";
      beepBuzzer(500); delay(100); beepBuzzer(500);

    } else {
      executeMotorSequence();
      cloudFeedingCount++;

      if (cloudWaterWarning) {
        cloudLastFeedingStatus = "Fed (CLEAN TANK!)";
      } else if (cloudFoodLowAlert) {
        cloudLastFeedingStatus = "Fed (REFILL FOOD!)";
      } else {
        cloudLastFeedingStatus = "Success (Manual)";
      }
    }

    cloudManualFeed = false;
  }
}

// MATCHED EXACTLY TO THINGPROPERTIES.H EXPECTATIONS
void onCloudPhtreatmentConfirmedChange() {
  if (cloudPHTreatmentConfirmed) {
    dispensePHTreatment();
    cloudPHTreatmentConfirmed = false;
  }
}

void onCloudRefillHopperChange() {
  if (cloudRefillHopper) {
    cloudFoodLowAlert = false;
    cloudRefillHopper = false;
  }
}

void onUserMorningHourChange() { }
void onUserEveningHourChange() { }  

void onUserFeedDurationChange() { }

// --- THE DUMMY RGB FUNCTION ---
// Keeps old sensor code from crashing!
void setRGB(int r, int g, int b) {
  // Intentionally left blank. The Uno handles the lights now!
}