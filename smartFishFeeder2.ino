#include "thingProperties.h"
#include <OneWire.h>
#include <DallasTemperature.h>
#include <Servo.h>
#include <LiquidCrystal_I2C.h>
#include <Wire.h>
#include <RTClib.h> 
#include <IRremote.hpp> 

// --- PIN DEFINITIONS ---
#define TDS_PIN       A0
#define PH_PIN        A1
#define TURBIDITY_PIN A2  
#define TEMP_PIN      4   
#define TRIG_PIN      2   
#define ECHO_PIN      3   
#define SERVO_PIN     9
#define RELAY_PIN     6   
#define RED_PIN       5
#define GREEN_PIN     11
#define BLUE_PIN      12
#define IR_RECEIVE_PIN 8  
#define BUZZER_PIN    7   

// --- IR HEX CODES ---
const uint32_t IR_FEED_NOW    = 0xBA45FF00; // Button 1: Manual Feed
const uint32_t IR_CHECK_WATER = 0xB946FF00; // Button 2: Check Water
const uint32_t IR_DISPENSE_PH = 0xE31CFF00; // Button OK: Dispense pH

// --- OBJECTS ---
OneWire oneWire(TEMP_PIN);
DallasTemperature sensors(&oneWire);
Servo feederServo;
LiquidCrystal_I2C lcd(0x27, 16, 2); 
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

  sensors.begin();
  
  lcd.init();
  lcd.backlight();
  lcd.setCursor(0, 0);
  lcd.print("VeriVox Booting.");

  if (!myRTC.begin()) {
    Serial.println("Couldn't find RTC module!");
  } 

//  myRTC.adjust(DateTime(F(__DATE__), F(__TIME__)));
  
  // Initialize Pins
  pinMode(RED_PIN, OUTPUT);
  pinMode(GREEN_PIN, OUTPUT);
  pinMode(BLUE_PIN, OUTPUT);
  pinMode(TRIG_PIN, OUTPUT);
  pinMode(ECHO_PIN, INPUT);
  
  // Initialize Buzzer
  pinMode(BUZZER_PIN, OUTPUT);
  digitalWrite(BUZZER_PIN, LOW); // Start off
  
  // ACTIVE-HIGH RELAY FIX (MD-102 Isolation)
  digitalWrite(RELAY_PIN, LOW);  // 1. Force OFF internally first
  pinMode(RELAY_PIN, OUTPUT);    // 2. Open pin as output

  // Initialize IR Receiver
  IrReceiver.begin(IR_RECEIVE_PIN, DISABLE_LED_FEEDBACK);

  setRGB(0, 0, 255); // Booting Color (Blue)

  // Startup dual-beep 
  beepBuzzer(150); 
  delay(100); 
  beepBuzzer(150);
}

void loop() {
  ArduinoCloud.update();
  
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
  // 1. Fetch Temperature (TDS needs it for compensation)
  sensors.requestTemperatures();
  float tempC = sensors.getTempCByIndex(0);
  if(tempC != -127.00) cloudTemperature = tempC;

  // 2. pH: 10-Sample Moving Average (3.3V Logic)
  long phTotal = 0;
  for(int i = 0; i < 10; i++) {
    phTotal += analogRead(PH_PIN);
    delay(10); 
  }
  float phAverage = phTotal / 10.0;
  float phVoltage = (phAverage / 1024.0) * 3.3; 
  cloudPH = (3.5 * phVoltage) + PH_OFFSET;
  cloudPH = constrain(cloudPH, 0.0, 14.0); 

  // 3. TDS: Temperature Compensated Polynomial (3.3V Logic)
  float tdsVoltage = (analogRead(TDS_PIN) / 1024.0) * 3.3;
  float compCoeff = 1.0 + 0.02 * (cloudTemperature - 25.0);
  float compVolt = tdsVoltage / compCoeff;
  cloudTDS = (133.42 * pow(compVolt, 3) - 255.86 * pow(compVolt, 2) + 857.39 * compVolt) * 0.5;

  // 4. Turbidity (Mapped to NTU scale)
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

  // Triggers the dashboard alert when food hits 20% or lower
  cloudFoodLowAlert = (cloudFoodLevel <= 20);
}

void checkWaterQuality() {
  // 1. CRITICAL LOCKOUT: pH is unsafe (Blocks Feeding)
  if (cloudPH < 6.5 || cloudPH > 8.5) {
    cloudPHAlert = true;
    cloudWaterWarning = true;
    cloudIsWaterSafe = false; // STOPS FEEDER
    setRGB(255, 0, 0);        // Solid RED LED
  } 
  // 2. MAINTENANCE WARNING: High TDS or Turbidity (Allows Feeding)
  else if (cloudTDS > TDS_WARNING_THRESHOLD || cloudTurbidity > TURBIDITY_WARNING_THRESHOLD) {
    cloudPHAlert = false;
    cloudWaterWarning = true; 
    cloudIsWaterSafe = true;  // STILL FEEDS
    setRGB(255, 128, 0);      // Solid ORANGE LED for cleaning alert
  } 
  // 3. NORMAL / SAFE STATE (Allows Feeding)
  else {
    cloudPHAlert = false;
    cloudWaterWarning = false;
    cloudIsWaterSafe = true;  // STILL FEEDS
    
    // Check food level to set LED indicator
    if (cloudFoodLevel <= 10) {
      setRGB(255, 0, 0);      // RED LED for empty hopper lockout
    } else if (cloudFoodLowAlert) {
      setRGB(255, 255, 0);    // YELLOW LED for 20% low food warning
    } else {
      setRGB(0, 255, 0);      // GREEN LED (Perfect conditions)
    }
  }
}

void updateLCD() {
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("pH:"); lcd.print(cloudPH, 1);
  lcd.print(" T:"); lcd.print(cloudTemperature, 1);
  lcd.setCursor(0, 1);
  lcd.print("TDS:"); lcd.print(cloudTDS);
  lcd.print(" Fd:"); lcd.print(cloudFoodLevel); lcd.print("%");
}

void setRGB(int r, int g, int b) {
  analogWrite(RED_PIN, r);
  analogWrite(GREEN_PIN, g);
  analogWrite(BLUE_PIN, b);
}

void beepBuzzer(int duration) {
  digitalWrite(BUZZER_PIN, HIGH); 
  delay(duration);
  digitalWrite(BUZZER_PIN, LOW);  
}

// --- SCHEDULING & ACTUATOR FUNCTIONS ---

void checkOfflineSchedule() {
  DateTime now = myRTC.now();
  
  // Backup defaults if sliders are not initialized properly
  int morningFeed = (userMorningHour != 0) ? userMorningHour : 7;
  int eveningFeed = (userEveningHour != 0) ? userEveningHour : 19;

  // MORNING FEED (Based on UI Slider or 7AM Default)
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

  // EVENING FEED (Based on UI Slider or 7PM Default)
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

  // Reset the tracker dynamically when the hour passes
  if (now.hour() != morningFeed && now.hour() != eveningFeed) {
    lastFedHour = -1;
  }
}

void executeMotorSequence() {
  setRGB(0, 0, 255); 
  
  // --- SUCCESS BEEP ---
  beepBuzzer(200); 
  
  // Suspend IR to prevent motor electrical noise interference
  IrReceiver.stop(); 
  
  feederServo.attach(SERVO_PIN);
  feederServo.write(180);  
  delay(1000);             
  feederServo.write(90);   
  delay(500);              
  feederServo.detach();
  
  // Resume IR
  IrReceiver.begin(IR_RECEIVE_PIN, DISABLE_LED_FEEDBACK);
  
  checkWaterQuality(); 
}

void dispensePHTreatment() {
  Serial.println("Activating pH Pump...");
  lcd.clear();
  lcd.print("Pumping pH Fix..");

  // Set LED to Solid Purple for Chemical Dosing
  setRGB(255, 0, 255); 

  // ACTIVE-HIGH RELAY LOGIC
  digitalWrite(RELAY_PIN, HIGH); 
  delay(2000);                   
  digitalWrite(RELAY_PIN, LOW);  
  
  cloudPHTreatmentCount++;
  cloudLastPHTreatment = "Today";
  updateLCD(); 

  // Force a sensor read to update the LED back to Red/Green based on water state
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
      lcd.print("Checking...");
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

    IrReceiver.resume(); 
  }
}

void performManualFeedingWithIR() {
  if (!cloudIsWaterSafe) {
    beepBuzzer(500); delay(100); beepBuzzer(500);
    lcd.clear();
    lcd.print("Feed Blocked!");
    lcd.setCursor(0,1);
    lcd.print("pH Unsafe!");
    delay(2000);
    updateLCD();
    
  } else if (cloudFoodLevel <= 10) {
    beepBuzzer(500); delay(100); beepBuzzer(500);
    lcd.clear();
    lcd.print("Feed Blocked!");
    lcd.setCursor(0,1);
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
      // LOCKOUT 1: Water is toxic
      cloudLastFeedingStatus = "SKIPPED (pH Unsafe)";
      beepBuzzer(500); delay(100); beepBuzzer(500);
      
    } else if (cloudFoodLevel <= 10) {
      // LOCKOUT 2: Food is at 10% or below
      cloudLastFeedingStatus = "SKIPPED (Hopper Empty!)";
      beepBuzzer(500); delay(100); beepBuzzer(500);
      
    } else {
      // SAFE TO FEED (Food is > 10%)
      executeMotorSequence();
      cloudFeedingCount++;
      
      // Check for non-critical warnings (like 20% food or dirty water)
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

void onCloudPHTreatmentConfirmedChange() {
  if(cloudPHTreatmentConfirmed) {
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

// Sliders will naturally update the variables in the main loop
void onUserMorningHourChange() { }
void onUserEveningHourChange() { }