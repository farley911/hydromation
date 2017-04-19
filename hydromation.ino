#include <EEPROM.h>
#include <TimeLib.h>
#include <SoftwareSerial.h>                             // we have to include the SoftwareSerial library, or else we can't use it
#include <Adafruit_GFX.h>                               // Core graphics library
#include <SPI.h>
#include "Adafruit_HX8357.h"
#include "TouchScreen.h"

#define rx 3                                            // define what pin rx is going to be
#define tx 2                                            // define what pin tx is going to be
#define SlopeValueAddress 0     // (slope of the ph probe)store at the beginning of the EEPROM. The slope is a float number,occupies 4 bytes.
#define InterceptValueAddress (SlopeValueAddress+4) 
#define EEPROM_write(address, p) {int i = 0; byte *pp = (byte*)&(p);for(; i < sizeof(p); i++) EEPROM.write(address+i, pp[i]);}
#define EEPROM_read(address, p)  {int i = 0; byte *pp = (byte*)&(p);for(; i < sizeof(p); i++) pp[i]=EEPROM.read(address+i);}

// These are the four touchscreen analog pins
#define YP A5  // must be an analog pin, use "An" notation!
#define XM A2  // must be an analog pin, use "An" notation!
#define YM A3   // can be a digital pin
#define XP A4   // can be a digital pin

// This is calibration data for the raw touch data to the screen coordinates
#define TS_MINX 110
#define TS_MINY 80
#define TS_MAXX 900
#define TS_MAXY 940

#define MINPRESSURE 1
#define MAXPRESSURE 1000

// The display uses hardware SPI, plus #9 & #10
#define TFT_RST -1  // dont use a reset pin, tie to arduino RST if you like
#define TFT_DC 9
#define TFT_CS 10

#define TEAL 0x3FFD
#define WHITE HX8357_WHITE
#define BLACK 0x0000

Adafruit_HX8357 tft = Adafruit_HX8357(TFT_CS, TFT_DC, TFT_RST);

// For better pressure precision, we need to know the resistance
// between X+ and X- Use any multimeter to read it
// For the one we're using, its 300 ohms across the X plate
TouchScreen ts = TouchScreen(XP, YP, XM, YM, 300);

const int phSensor = 0;
const int phUp = A1;
const int phDown = 8;
const int partA = 7;
const int partB = 6;
const int supp1 = 5;
const int supp2 = 4;
const int supp3 = 1;
const int fiveMinutes = 300;
const char version[6] = "1.3.0";

int currentScreen = 1;
long ecTimeout = 43200; // 12 hours
int phTimeout = 3600; // 1 hour
TSPoint p;
SoftwareSerial ecSerial(tx, rx);                        // define how the soft serial port is going to work
boolean isPumpInUse = false;
float lastPh = 0.0;
time_t lastPhCheck;
float lastEc = 0.0;
time_t lastEcCheck;
time_t phWaitTime;
String ecSensorString = "";                             // a string to hold the data from the Atlas Scientific product
boolean isEcStringComplete = false;                     // have we received all the data from the Atlas Scientific product
time_t ecWaitTime;
boolean shouldAddPartAB = false;
boolean shouldAddSupp1 = false;
boolean shouldAddSupp2 = false;
boolean shouldAddSupp3 = false;
float targetEc = 0.0;
float ecTolerance = 0.0;
boolean isEcProbeAsleep = true;
float ecCollection[7];
int ecIndex = 0;
boolean isCheckingEc = false;
boolean isReadingEc = false;
boolean isCheckingPh = false;
float slopeValue;
float interceptValue;
int currentMinute;
boolean isSettingDateTime = false;
int setDateMonth;
int setDateDay;
int setDateYear;
int setTimeHour;
int setTimeMinute;
boolean isPurgingPump = false;
boolean isFlushingPh = false;
int nutrientRatios[4][2] = {{0, 0}, {0,0}, {0,0}, {0,0}};

void setup() {
  pinMode(phUp, OUTPUT);
  pinMode(phDown, OUTPUT);
  pinMode(partA, OUTPUT);
  pinMode(partB, OUTPUT);
  pinMode(supp1, OUTPUT);
  pinMode(supp2, OUTPUT);
  pinMode(supp2, OUTPUT);
  setTime(18, 0, 0, 1, 1, 2017);
  ecSerial.begin(9600);                                   // set baud rate for the software serial port to 9600
  ecSensorString.reserve(30);                             // set aside some bytes for receiving data from Atlas Scientific product
  ecSerial.print("SLEEP\r");                              // ensures the EC probe is awake incase the system shut down while it was sleeping.
  readCharacteristicValues();                             //read the slope and intercept of the ph probe
  tft.begin(HX8357D);
  tft.setRotation(3);
  clearScreen();
}

void loop() {
  configureTouch();

  if (!isReadingEc) {
    switch (currentScreen) {
      case 1:
        displayHomeScreen();
        break;
      case 2:
        displayConfigScreen();
        break;
      case 3:
        displayConfigScreen2();
        break;
      case 4:
        displayConfigScreen3();
        break;
      case 5:
        if (!isSettingDateTime) {
          isSettingDateTime = true;
          storeDateTime();
        }
        displaySetTimeScreen();
        break;
      case 6:
        if (!isSettingDateTime) {
          isSettingDateTime = true;
          storeDateTime();
        }
        displaySetDateScreen();
        break;
      case 7:
        displayAdjustNutrientsScreen();
        break;
      case 8:
        displayPumpPurgeScreen();
        break;
      case 9:
        displayFlushScreen();
        break;
      case 10:
        displayEnablePumpsScreen();
        break;
      case 11:
        displayAdjustSchedulesScreen();
        break;
      case 12:
        displayNutrientRatioScreenPage1();
        break;
      case 13:
        displayNutrientRatioScreenPage2();
        break;
    }
  }
  
  if (!isPumpInUse) {
    if (((!lastEcCheck || now() - lastEcCheck >= ecTimeout || isCheckingEc) && !ecWaitTime && !isCheckingPh) || (ecWaitTime && now() - ecWaitTime >= fiveMinutes)) { // Check EC every 12 hours, wait 5 minutes after adjusting EC before verifiying results and adjusting.
      checkEc();
    }
  }
  
  if (!isPumpInUse) {
    if (((!lastPhCheck || now() - lastPhCheck >= phTimeout || isCheckingPh) && !phWaitTime && !isCheckingEc) || (phWaitTime && now() - phWaitTime >= fiveMinutes)) { // Check PH once an hour, wait 5 minutes after adjusting pH before verifying results and adjusting.
      checkPh();
    }
  }
}

void addAdjustNutrientActions() {
  // Increase PPM
  if (p.y >= 290 && p.y <= 330 && p.x >= 90 && p.x <= 130 && isTouchingScreen()) {
    targetEc = targetEc + 0.2;
    clearScreen();
  }

  // Increase Tolerance
  if (p.y >= 25 && p.y <= 65 && p.x >= 90 && p.x <= 130 && isTouchingScreen()) {
    ecTolerance = ecTolerance + 0.02;
    clearScreen();
  }
  
  // Decrease PPM
  if (p.y >= 290 && p.y <= 330 && p.x >= 170 && p.x <= 210 && isTouchingScreen()) {
    if (targetEc >= 0.2) {
      targetEc = targetEc - 0.2;
    } else {
      targetEc = 0.0;
    }
    clearScreen();
  }

  // Decrease Tolerance
  if (p.y >= 25 && p.y <= 65 && p.x >= 170 && p.x <= 210 && isTouchingScreen()) {
    if (ecTolerance >= 0.02) {
      ecTolerance = ecTolerance - 0.02;
    } else {
      ecTolerance = 0.0;
    }
    clearScreen();
  }

  // Adjust Ratios Button
  if (p.y >= 70 && p.y <= 195 && p.x >= 250 && p.x <= 300 && isTouchingScreen()) {
    currentScreen = 12;
    clearScreen();
  }

  // Back button
  if (p.y >= 250 && p.y <= 415 && p.x >= 250 && p.x <= 300 && isTouchingScreen()) {
    currentScreen = 2;
    clearScreen();
  }
}

void addAdjustScheduleActions() {
  // increase pH interval
  if (p.y >= 317 && p.y <= 357 && p.x >= 80 && p.x <= 120 && isTouchingScreen()) {
    clearScreen();
    phTimeout = phTimeout + 3600;  
  }
  
  // increase EC interval
  if (p.y >= 107 && p.y <= 147 && p.x >= 80 && p.x <= 120 && isTouchingScreen()) {
    clearScreen();
    ecTimeout = ecTimeout + 3600;  
  }
  
  // decrease pH interval
  if (p.y >= 317 && p.y <= 357 && p.x >= 185 && p.x <= 225 && isTouchingScreen() && phTimeout > 3600) {
    clearScreen();
    phTimeout = phTimeout - 3600;
  }

  // decrease EC interval
  if (p.y >= 107 && p.y <= 147 && p.x >= 185 && p.x <= 225 && isTouchingScreen() && ecTimeout > 3600) {
    clearScreen();
    ecTimeout = ecTimeout - 3600;
  }

  // Back button
  if (p.y >= 160 && p.y <= 285 && p.x >= 250 && p.x <= 300 && isTouchingScreen()) {
    clearScreen();
    currentScreen = 4;
  }
}

void  addConfigScreenActions() {
  // navigate to home
  if (p.y >= 330 && p.y <= 430 && p.x >= 70 && p.x <= 120 && isTouchingScreen()) {
    clearScreen();
    currentScreen = 1;
  }

  // Adjust nutrients
  if (p.y >= 180 && p.y <= 430 && p.x >= 130 && p.x <= 180 && isTouchingScreen()) {
    clearScreen();
    currentScreen = 7;
  }

  // Enable/Disable pumps
  if (p.y >= 130 && p.y <= 430 && p.x >= 190 && p.x <= 240 && isTouchingScreen()) {
    clearScreen();
    currentScreen = 10;
  }
  
  // navigate from config page 1 to 2
  if (p.y >= 225 && p.y <= 430 && p.x >= 250 && p.x <= 300 && isTouchingScreen()) {
    clearScreen();
    currentScreen = 3;
  }
}

void addConfigScreen2Actions() {
  // navigate from config page 2 to 1
  if (p.y >= 210 && p.y <= 430 && p.x >= 70 && p.x <= 120 && isTouchingScreen()) {
    clearScreen();
    currentScreen = 2;
  }

  // Navigate to Purge Pumps Screen
  if (p.y >= 170 && p.y <= 430 && p.x >= 130 && p.x <= 180 && isTouchingScreen()) {
    clearScreen();
    currentScreen = 8;
  }

  // Flush system
  if (p.y >= 315 && p.y <= 430 && p.x >= 190 && p.x <= 240 && isTouchingScreen()) {
    clearScreen();
    currentScreen = 9;
  }

  // Navigate to config page 3
  if (p.y >= 215 && p.y <= 430 && p.x >= 250 && p.x <= 300 && isTouchingScreen()) {
    clearScreen();
    currentScreen = 4;
  }
}

void addConfigScreen3Actions() {
  // navigate from config page 3 to 2
  if (p.y >= 210 && p.y <= 430 && p.x >= 70 && p.x <= 120 && isTouchingScreen()) {
    clearScreen();
    currentScreen = 3;
  }

  // Set Time
  if (p.y >= 270 && p.y <= 430 && p.x >= 130 && p.x <= 180 && isTouchingScreen()) {
    clearScreen();
    currentScreen = 5;
  }
  
  // Set Date
  if (p.y >= 270 && p.y <= 430 && p.x >= 190 && p.x <= 240 && isTouchingScreen()) {
    clearScreen();
    currentScreen = 6;
  }

  // Adjust schedules
  if (p.y >= 270 && p.y <= 430 && p.x >= 250 && p.x <= 300 && isTouchingScreen()) {
    clearScreen();
    currentScreen = 11;
  }
}

void addEnablePumpsActions() {
  if (p.y >= 255 && p.y <= 345 && p.x >= 80 && p.x <= 130 && isTouchingScreen()) {
    clearScreen();
    shouldAddPartAB = !shouldAddPartAB;
  }
  if (p.y >= 35 && p.y <= 125 && p.x >= 80 && p.x <= 130 && isTouchingScreen()) {
    clearScreen();
    shouldAddSupp2 = !shouldAddSupp2;
  }
  if (p.y >= 255 && p.y <= 345 && p.x >= 155 && p.x <= 205 && isTouchingScreen()) {
    clearScreen();
    shouldAddSupp1 = !shouldAddSupp1;
  }
  if (p.y >= 35 && p.y <= 125 && p.x >= 155 && p.x <= 205 && isTouchingScreen()) {
    clearScreen();
    shouldAddSupp3 = !shouldAddSupp3;
  }
  if (p.y >= 160 && p.y <= 285 && p.x >= 250 && p.x <= 300 && isTouchingScreen()) {
    clearScreen();
    currentScreen = 2;
  }
}

void addFlushActions() {
  // Back button
  if (p.y >= 285 && p.y <= 410 && p.x >= 140 && p.x <= 190 && isTouchingScreen()) {
    clearScreen();
    currentScreen = 3;
  }

  // Soups on button
  if (p.y >= 30 && p.y <= 230 && p.x >= 140 && p.x <= 190 && isTouchingScreen()) {
    clearScreen();
    currentScreen = 1;
    isFlushingPh = true;
    checkPh();
  }
}

void addHomeScreenActions() {
  if (currentMinute != minute()) {
    currentMinute = minute();
    clearScreen();
  }
  
  // Navigate to Configuration Screen
  if (p.y >= 135 && p.y <= 340 && p.x >= 255 && p.x <= 305 && isTouchingScreen()) {
    clearScreen();
    currentScreen = 2; // Display the configuration screen
  }
}

void addNutrientRatiosActionsPage1() {
  // increase A/B concentration
  if (p.y >= 293 && p.y <= 333 && p.x >= 80 && p.x <= 120 && isTouchingScreen()) {
    clearScreen();
    nutrientRatios[0][0] = nutrientRatios[0][0] + 1;  
  }
  
  // increase Supp 1 concentration
  if (p.y >= 123 && p.y <= 163 && p.x >= 80 && p.x <= 120 && isTouchingScreen()) {
    clearScreen();
    nutrientRatios[1][0] = nutrientRatios[1][0] + 1;  
  }
  
  // decrease A/B concentration
  if (p.y >= 293 && p.y <= 333 && p.x >= 185 && p.x <= 225 && isTouchingScreen() && nutrientRatios[0][0] > 0) {
    clearScreen();
    nutrientRatios[0][0] = nutrientRatios[0][0] - 1;
  }

  // decrease Supp 1 concentration
  if (p.y >= 123 && p.y <= 163 && p.x >= 185 && p.x <= 225 && isTouchingScreen && nutrientRatios[1][0] > 0) {
    clearScreen();
    nutrientRatios[1][0] = nutrientRatios[1][0] - 1;
  }

  // Back button
  if (p.y >= 265 && p.y <= 390 && p.x >= 250 && p.x <= 300 && isTouchingScreen()) { // TODO: Update touch location
    determineSupplementRatios();
    clearScreen();
    currentScreen = 7;
  }

  // More button
  if (p.y >= 100 && p.y <= 225 && p.x >= 250 && p.x <= 300 && isTouchingScreen()) { // TODO: Update touch location
    clearScreen();
    currentScreen = 13;
  }
}

void addNutrientRatiosActionsPage2() {
  // increase Supp 2 concentration
  if (p.y >= 288 && p.y <= 328 && p.x >= 80 && p.x <= 120 && isTouchingScreen()) {
    clearScreen();
    nutrientRatios[2][0] = nutrientRatios[2][0] + 1;  
  }
  
  // increase Supp 3 concentration
  if (p.y >= 118 && p.y <= 163 && p.x >= 80 && p.x <= 120 && isTouchingScreen()) {
    clearScreen();
    nutrientRatios[3][0] = nutrientRatios[3][0] + 1;  
  }
  
  // decrease supp 2 concentration
  if (p.y >= 288 && p.y <= 328 && p.x >= 185 && p.x <= 225 && isTouchingScreen() && nutrientRatios[2][0] > 0) {
    clearScreen();
    nutrientRatios[2][0] = nutrientRatios[2][0] - 1;
  }

  // decrease Supp 3 concentration
  if (p.y >= 118 && p.y <= 163 && p.x >= 185 && p.x <= 225 && isTouchingScreen() && nutrientRatios[3][0] > 0) {
    clearScreen();
    nutrientRatios[3][0] = nutrientRatios[3][0] - 1;
  }

  // Back button
  if (p.y >= 180 && p.y <= 305 && p.x >= 250 && p.x <= 300 && isTouchingScreen()) {
    clearScreen();
    currentScreen = 12;
  }
}

void addPurgeScreenActions() {
  // Back Button
  if (p.y >= 405 && p.y <= 445 && p.x >= 15 && p.x <= 55 && isTouchingScreen()) {
    currentScreen = 3;
    clearScreen();
  } else if (p.y >= 230 && p.y <= 345 && p.x >= 80 && p.x <= 130 && isTouchingScreen()) {
    digitalWrite(partA, HIGH);
    isPurgingPump = true;
  } else if (p.y >= 230 && p.y <= 345 && p.x >= 155 && p.x <= 195 && isTouchingScreen()) {
    digitalWrite(partB, HIGH);
    isPurgingPump = true;
  } else if (p.y >= 230 && p.y <= 345 && p.x >= 230 && p.x <= 270 && isTouchingScreen()) {
    digitalWrite(supp1, HIGH);
    isPurgingPump = true;
  } else if (p.y >= 10 && p.y <= 125 && p.x >= 80 && p.x <= 130 && isTouchingScreen()) {
    digitalWrite(supp2, HIGH);
    isPurgingPump = true;
  } else if (p.y >= 10 && p.y <= 125 && p.x >= 155 && p.x <= 195 && isTouchingScreen()) {
    digitalWrite(phUp, HIGH);
    isPurgingPump = true;
  } else if (p.y >= 10 && p.y <= 125 && p.x >= 230 && p.x <= 270 && isTouchingScreen()) {
    digitalWrite(phDown, HIGH);
    isPurgingPump = true;
  } else if (isPurgingPump) {
    digitalWrite(partA, LOW);
    digitalWrite(partB, LOW);
    digitalWrite(supp1, LOW);
    digitalWrite(supp2, LOW);
    digitalWrite(phUp, LOW);
    digitalWrite(phDown, LOW);
    isPurgingPump = false;
  }
}

void addSetDateActions() {
  // Increase Month
  if (p.y >= 318 && p.y <= 358 && p.x >= 85 && p.x <= 125 && isTouchingScreen()) {
    if (setDateMonth < 12) {
      setDateMonth++;
    } else {
      setDateMonth = 1;
    }
    clearScreen();
  }

  // Increase day
  if (p.y >= 198 && p.y <= 238 && p.x >= 85 && p.x <= 125 && isTouchingScreen()) {
    if (setDateDay < 31) {
      setDateDay++;
    } else {
      setDateDay = 1;
    }
    clearScreen();
  }
  
  // Increase year
  if (p.y >= 83 && p.y <= 123 && p.x >= 85 && p.x <= 125 && isTouchingScreen()) {
    setDateYear++;
    clearScreen();
  }

  // Decrease Month
  if (p.y >= 318 && p.y <= 358 && p.x >= 170 && p.x <= 210 && isTouchingScreen()) {
    if (setDateMonth > 1) {
      setDateMonth--;
    } else {
      setDateMonth = 12;
    }
    clearScreen();
  }

  // Decrease day
  if (p.y >= 198 && p.y <= 238 && p.x >= 170 && p.x <= 210 && isTouchingScreen()) {
    if (setDateDay > 1) {
      setDateDay--;
    } else {
      setDateDay = 31;
    }
    clearScreen();
  }
  
  // Decrease year
  if (p.y >= 83 && p.y <= 123 && p.x >= 170 && p.x <= 210 && isTouchingScreen()) {
    setDateYear--;
    clearScreen();
  }

  // Save date
  if (p.y >= 150 && p.y <= 280 && p.x >= 240 && p.x <= 290 && isTouchingScreen()) {
    int _hour = hour();
    int _minute = minute();
    setTime(_hour, _minute, 0, setDateDay, setDateMonth, setDateYear);
    currentScreen = 4;
    isSettingDateTime = false;
    clearScreen();
  }
}

void addSetTimeScreenActions() {
  // Increase hour
  if (p.y >= 303 && p.y <= 343 && p.x >= 85 && p.x <= 125 && isTouchingScreen()) {
    if (setTimeHour < 23) {
      setTimeHour++;
    } else {
      setTimeHour = 0;
    }
    clearScreen();
  }

  // Increase minute
  if (p.y >= 183 && p.y <= 223 && p.x >= 85 && p.x <= 125 && isTouchingScreen()) {
    if (setTimeMinute < 59) {
      setTimeMinute++;
    } else {
      setTimeMinute = 0;
    }
    clearScreen();
  }

  // Decrease hour
  if (p.y >= 303 && p.y <= 343 && p.x >= 170 && p.x <= 210 && isTouchingScreen()) {
    if (setTimeHour > 0) {
      setTimeHour--;
    } else {
      setTimeHour = 23;
    }
    clearScreen();
  }

  // Decrease minute
  if (p.y >= 183 && p.y <= 223 && p.x >= 170 && p.x <= 210 && isTouchingScreen()) {
    if (setTimeMinute > 1) {
      setTimeMinute--;
    } else {
      setTimeMinute = 59;
    }
    clearScreen();
  }

  // Save time
  if (p.y >= 150 && p.y <= 280 && p.x >= 240 && p.x <= 290 && isTouchingScreen()) {
    int _month = month();
    int _day = day();
    int _year = year();
    setTime(setTimeHour, setTimeMinute, 0, _day, _month, _year);
    currentScreen = 4;
    isSettingDateTime = false;
    clearScreen();
  }
}

void checkEc() {
  if (!isReadingEc) {
    isReadingEc = true;
  }
  if (!isCheckingEc) {
    lastEcCheck = now();
    isCheckingEc = true;
  }
  if (isEcProbeAsleep) {
    ecSerial.print("WAKE\r");
    isEcProbeAsleep = false;
  }
  if (!isEcStringComplete) {
    if (ecSerial.available() > 0) {                       // if we see that the Atlas Scientific product has sent a character
      char inchar = (char)ecSerial.read();                // get the char we just received
      ecSensorString += inchar;                           // add the char to the var called sensorstring
      if (inchar == '\r') {                               // if the incoming character is a <CR>
        isEcStringComplete = true;                        // set the flag
      }
    }
  } else if (ecIndex < 7) {
    if (isdigit(ecSensorString[0]) == true) { 
      float ec = atof(parseEcString());
      ecCollection[ecIndex++] = ec;
    }
    ecSensorString = "";
    isEcStringComplete = false;
  } else if (ecIndex == 7) {                              // the probe seems to provide unreliable results for the first 4 requests so I use the 7th to add a buffer and help ensure accurate readings.
    ecIndex = 0;
    lastEc = ecCollection[6];
    clearScreen();
    if (lastEc < targetEc - ecTolerance) {
      ecSerial.print("SLEEP\r");
      isEcProbeAsleep = true;
      increaseEc();
    } else {
      ecWaitTime = 0;
      isCheckingEc = false;
      ecSerial.print("SLEEP\r");
      isEcProbeAsleep = true;
      clearScreen();
    }
    isReadingEc = false;
  }
}

void checkPh() {
  if (!isEcProbeAsleep) {
    ecSerial.print("SLEEP\r");
    isEcProbeAsleep = true;
  }
  lastPh = getPh();
  clearScreen();
  if (!isCheckingPh) {
    isCheckingPh = true;
    lastPhCheck = now();
  }
  if (lastPh <= 5.65) {
    increasePh();
  } else if (lastPh > 5.95) {
    decreasePh();
  } else {
    phWaitTime = 0;
    isCheckingPh = false;
    if (isFlushingPh) {
      isFlushingPh = false;
      checkEc();
    }
    clearScreen();
  }
}

void clearScreen() {
  tft.fillScreen(HX8357_BLACK);
}

void configureTouch() {
  // Retrieve a point  
  p = ts.getPoint();

  // Scale from ~0->1000 to tft.width using the calibration #'s
  p.x = map(p.x, TS_MINX, TS_MAXX, 0, tft.height());
  p.y = map(p.y, TS_MINY, TS_MAXY, 0, tft.width());
}

int convert24HourTo12Hour(char* ampm, int _hour) {
  if (_hour < 12) {
    if (_hour == 0) {
      _hour = 12;
    }
    strcpy(ampm, "AM");
  } else {
    if (_hour > 12) {
      _hour = _hour - 12;
    }
    strcpy(ampm, "PM");
  }
  return _hour;
}

void decreasePh() {
  isPumpInUse = true;
  digitalWrite(phDown, HIGH);
  delay(1000);                                           // add 1ml of pH down solution.
  digitalWrite(phDown, LOW);
  isPumpInUse = false;
  phWaitTime = now();
}

void determineSupplementRatios() {
  int highValIndex = 0;

  for (int i = 1; i <= 3; i++) {
    if (nutrientRatios[highValIndex][0] < nutrientRatios[i][0]) {
      highValIndex = i;
    }
  }
  nutrientRatios[highValIndex][1] = 1;
  for (int i = 0; i <= 3; i++) {
    nutrientRatios[i][1] = (nutrientRatios[i][0] / nutrientRatios[highValIndex][0]) * 10;
  }
}

void displayAdjustNutrientsScreen() {
  displayHeader();

  drawUpButton(130, 90);
  drawUpButton(400, 90);

  // PPM
  tft.setTextColor(WHITE);
  tft.setCursor(45, 140);
  tft.print("PPM:");
  tft.setCursor(125, 140);
  tft.setTextColor(TEAL);
  tft.print(getPpm(targetEc));

  // Tolerance
  tft.setTextColor(WHITE);
  tft.setCursor(205, 140);
  tft.print("Tolerance:");
  tft.setCursor(395, 140);
  tft.setTextColor(TEAL);
  tft.print(getPpm(ecTolerance));

  drawDownButton(130, 170);
  drawDownButton(400, 170);
  
//  drawSaveButton();
  char back[ ] = "Back";
  drawButton(70, 250, 125, back);
  char ratios[ ] = "Ratios";
  drawButton(250, 250, 165, ratios);
  
  addAdjustNutrientActions();
}

void displayAdjustSchedulesScreen() {
  displayHeader();

  drawUpButton(103, 80);
  drawUpButton(313, 80);

  tft.setCursor(45, 140);
  tft.setTextColor(WHITE);
  tft.print("pH:");
  tft.setTextColor(TEAL);
  tft.setCursor(105, 140);
  tft.print(phTimeout / 3600);
  tft.setCursor(150, 145);
  tft.setTextSize(2);
  tft.print("hour(s)");

  tft.setTextSize(3);
  tft.setTextColor(WHITE);
  tft.setCursor(255, 140);
  tft.print("EC:");
  tft.setTextColor(TEAL);
  tft.setCursor(315, 140);
  tft.print(ecTimeout / 3600);
  tft.setCursor(360, 145);
  tft.setTextSize(2);
  tft.print("hour(s)");

  drawDownButton(103, 185);
  drawDownButton(313, 185);

  tft.setTextSize(3);
  char backText[ ] = "Back";
  drawButton(175, 250, 125, backText);

  addAdjustScheduleActions();
}

void displayConfigScreen() {
  displayHeader();
  tft.setTextSize(2);
  displayHomeButton(30, 70);

  // Adjust Nutrients
  char selectNutesText[ ] = "Adjust Nutrients";
  drawButton(30, 130, 250, selectNutesText);
  
  // Enable/Disable Pumps
  char togglePumpText[ ] = "Enable/Disable Pumps";
  drawButton(30, 190, 300, togglePumpText);

  // More Settings
  char moreText[ ] = "More Settings";
  drawButton(30, 250, 215, moreText);
  
  addConfigScreenActions();
}

void displayConfigScreen2() {
  displayHeader();
  tft.setTextSize(2);
  
  // Previous Page
  char previousText[ ] = "Previous Page";
  drawButton(30, 70, 220, previousText);

  // Purge Pump Lines
  char purgeText[ ] = "Purge Pump Lines";
  drawButton(30, 130, 260, purgeText);
  
  // Flush System
  char flushText[ ] = "Flush";
  drawButton(30, 190, 115, flushText);
  
  // More Settings
  char moreText[ ] = "More Settings";
  drawButton(30, 250, 215, moreText);

  addConfigScreen2Actions();
}

void displayConfigScreen3() {
  displayHeader();
  tft.setTextSize(2);
  
  // Previous Page
  char previousText[ ] = "Previous Page";
  drawButton(30, 70, 220, previousText);
  
  // Set time
  char timeText[ ] = "Set Time";
  drawButton(30, 130, 160, timeText);
  
  // Set date
  char dateText[ ] = "Set Date";
  drawButton(30, 190, 160, dateText);

  // Adjust schedule
  char scheduleText[ ] = "Adjust Schedules";
  drawButton(30, 250, 250, scheduleText);

  addConfigScreen3Actions();
}

void displayEnablePumpsScreen() {
  char offText[ ] = "OFF";
  char onText[ ] = "ON";
  
  displayHeader();
  
  tft.setTextSize(2);
  tft.setTextColor(WHITE);
  tft.setCursor(60, 100);
  char partABText[ ] = "A/B";
  tft.print(partABText);
  if (shouldAddPartAB) {
    drawButton(115, 80, 85, onText);
  } else {
    drawButton(115, 80, 90, offText);
  }
  
  tft.setTextColor(WHITE);
  tft.setCursor(30, 175);
  char supp2Text[ ] = "Supp 2";
  tft.print(supp2Text);
  if (shouldAddSupp2) {
    drawButton(115, 155, 85, onText);
  } else {
    drawButton(115, 155, 90, offText);
  }
  
  tft.setTextColor(WHITE);
  tft.setCursor(250, 100);
  char supp1Text[ ] = "Supp 1";
  tft.print(supp1Text);
  if (shouldAddSupp1) {
    drawButton(335, 80, 85, onText);
  } else {
    drawButton(335, 80, 90, offText);
  }
  
  tft.setTextColor(WHITE);
  tft.setCursor(250, 175);
  char supp3Text[ ] = "Supp 3";
  tft.print(supp3Text);
  if (shouldAddSupp3) {
    drawButton(335, 155, 85, onText);
  } else {
    drawButton(335, 155, 90, offText);
  }
  
  tft.setTextColor(TEAL);
  tft.setTextSize(3);
  char backText[ ] = "Back";
  drawButton(175, 250, 125, backText);

  addEnablePumpsActions();
}

void displayFlushScreen() {
  displayHeader();
  tft.setTextColor(WHITE);
  char back[ ] = "Back";
  drawButton(50, 140, 125, back);
  char soupsOn[ ] = "Soups On";
  drawButton(230, 140, 200, soupsOn);
  addFlushActions();
}

void displayHeader() {
  // App Heading
  tft.setCursor(70, 20);
  tft.setTextSize(3);
  tft.setTextColor(TEAL);
  tft.println("eFarley Hydromation");
}

void displayHomeButton(int x, int y) {
  char homeText[5] = "Home";
  drawButton(x, y, 105, homeText);
}

void displayHomeScreen() {
  displayHeader();
  
  // Date
  tft.setCursor(165, 55);
  tft.setTextSize(2);
  tft.setTextColor(WHITE);
  tft.print(lookupMonthName(month()));
  tft.print(" ");
  tft.print(day());
  tft.print(" ");
  tft.println(year());

  // Time
  tft.setCursor(185, 80);
  tft.setTextSize(3);
  char ampm[3];
  int _hour = hour();
  _hour = convert24HourTo12Hour(ampm, _hour);
  tft.print(_hour);
  tft.print(":");
  int _minute = minute();
  if (_minute < 10) {
    tft.print("0");
  }
  tft.print(_minute);
  tft.print(" ");
  tft.println(ampm);

  // pH
  tft.setCursor(30, 120);
  tft.setTextSize(2);
  tft.print("Last pH check on: ");
  tft.print(lookupMonthName(month(lastPhCheck))[0]);
  tft.print(lookupMonthName(month(lastPhCheck))[1]);
  tft.print(lookupMonthName(month(lastPhCheck))[2]);
  tft.print(" ");
  tft.print(day(lastPhCheck));
  tft.print(" @ ");
  char lastAmpm[3];
  int lastHour = hour(lastPhCheck);
  int lastMinute = minute(lastPhCheck);
  lastHour = convert24HourTo12Hour(lastAmpm, lastHour);
  tft.print(lastHour);
  tft.print(":");
  if (lastMinute < 10) {
    tft.print("0");
  }
  tft.print(lastMinute);
  tft.print(" ");
  tft.println(lastAmpm);
  tft.setCursor(30, 145);
  tft.print("pH: ");
  tft.setTextColor(TEAL);
  tft.print(lastPh);
  if (isCheckingPh) {
    tft.print(" Adjusting...");
  }
  tft.println("");

  // PPM
  tft.setCursor(30, 180);
  tft.setTextColor(WHITE);
  tft.setTextSize(2);
  tft.print("Last EC check on: ");
  tft.print(lookupMonthName(month(lastEcCheck))[0]);
  tft.print(lookupMonthName(month(lastEcCheck))[1]);
  tft.print(lookupMonthName(month(lastEcCheck))[2]);
  tft.print(" ");
  tft.print(day(lastEcCheck));
  tft.print(" @ ");
  lastHour = hour(lastEcCheck);
  lastMinute = minute(lastEcCheck);
  lastHour = convert24HourTo12Hour(lastAmpm, lastHour);
  tft.print(lastHour);
  tft.print(":");
  if (lastMinute < 10) {
    tft.print("0");
  }
  tft.print(lastMinute);
  tft.print(" ");
  tft.println(lastAmpm);
  tft.setCursor(30, 205);
  tft.print("PPM: ");
  tft.setTextColor(TEAL);
  tft.print(getPpm(lastEc));
  tft.setTextColor(WHITE);
  tft.print(" (EC ");
  tft.print(lastEc);
  tft.print(")");
  if (isCheckingEc) {
    tft.setTextColor(TEAL);
    tft.print(" Adjusting...");
  }
  tft.println("");
  tft.setCursor(30, 225);
  tft.setTextSize(1);
  tft.print("Target: ");
  tft.setTextColor(TEAL);
  tft.print(getPpm(targetEc));
  tft.print(" PPM / ");
  tft.print(targetEc);
  tft.println(" EC");

  // Configure button
  char configText[ ] = "Configuration";
  tft.setTextSize(2);
  drawButton(140, 255, 205, configText);

  // version
  tft.setCursor(420, 300);
  tft.setTextSize(1);
  tft.print("v. ");
  tft.println(version);

  addHomeScreenActions();
}

void displayNutrientRatioScreenPage1() {
  displayHeader();

  drawUpButton(123, 80);
  drawUpButton(333, 80);

  tft.setCursor(55, 140);
  tft.setTextColor(WHITE);
  tft.print("A/B");
  tft.setTextColor(TEAL);
  tft.setCursor(125, 140);
  tft.print(nutrientRatios[0][0]);
  tft.setCursor(170, 145);
  tft.setTextSize(2);
  tft.print("ml/l");

  tft.setTextSize(3);
  tft.setTextColor(WHITE);
  tft.setCursor(285, 140);
  tft.print("S1");
  tft.setTextColor(TEAL);
  tft.setCursor(335, 140);
  tft.print(nutrientRatios[1][0]);
  tft.setCursor(380, 145);
  tft.setTextSize(2);
  tft.print("ml/l");

  drawDownButton(123, 185);
  drawDownButton(333, 185);

  tft.setTextSize(3);
  char backText[ ] = "Back";
  drawButton(100, 250, 125, backText);

  char moreText[ ] = "More";
  drawButton(265, 250, 125, moreText);

  addNutrientRatiosActionsPage1();
}

void displayNutrientRatioScreenPage2() {
  displayHeader();

  drawUpButton(118, 80);
  drawUpButton(328, 80);

  tft.setCursor(65, 140);
  tft.setTextColor(WHITE);
  tft.print("S2");
  tft.setTextColor(TEAL);
  tft.setCursor(120, 140);
  tft.print(nutrientRatios[2][0]);
  tft.setCursor(165, 145);
  tft.setTextSize(2);
  tft.print("ml/l");

  tft.setTextSize(3);
  tft.setTextColor(WHITE);
  tft.setCursor(275, 140);
  tft.print("S3");
  tft.setTextColor(TEAL);
  tft.setCursor(330, 140);
  tft.print(nutrientRatios[3][0]);
  tft.setCursor(375, 145);
  tft.setTextSize(2);
  tft.print("ml/l");

  drawDownButton(118, 185);
  drawDownButton(328, 185);

  tft.setTextSize(3);
  char backText[ ] = "Back";
  drawButton(180, 250, 125, backText);
  
  addNutrientRatiosActionsPage2();
}

void displayPumpPurgeScreen() {
  // Back button
  tft.drawRoundRect(15, 15, 40, 40, 5, TEAL);
  tft.drawLine(25, 35, 35, 25, TEAL);
  tft.drawLine(25, 35, 45, 35, TEAL);
  tft.drawLine(25, 35, 35, 45, TEAL);

  tft.setTextSize(3);
  tft.setTextColor(TEAL);
  tft.setCursor(90, 20);
  tft.println("eFarley Hydromation");
  
  tft.setTextSize(2);
  tft.setTextColor(WHITE);
  tft.setCursor(30, 100);
  tft.print("Part A");
  drawPurgeButton(115, 80);
  
  tft.setTextColor(WHITE);
  tft.setCursor(30, 175);
  tft.print("Part B");
  drawPurgeButton(115, 155);
  
  tft.setTextColor(WHITE);
  tft.setCursor(30, 250);
  tft.print("Supp 1");
  drawPurgeButton(115, 230);
  
  tft.setTextColor(WHITE);
  tft.setCursor(250, 100);
  tft.print("Supp 2");
  drawPurgeButton(335, 80);
  
  tft.setTextColor(WHITE);
  tft.setCursor(250, 175);
  tft.print("pH Up");
  drawPurgeButton(335, 155);
  
  tft.setTextColor(WHITE);
  tft.setCursor(250, 250);
  tft.print("pH Dwn");
  drawPurgeButton(335, 230);

  addPurgeScreenActions();
}

void displaySetDateScreen() {
  displayHeader();

  drawUpButton(102, 85);
  drawUpButton(222, 85);
  drawUpButton(337, 85);

  tft.setTextColor(WHITE);
  tft.setCursor(105, 140);
  tft.print(setDateMonth);
  tft.setCursor(175, 140);
  tft.setTextColor(TEAL);
  tft.print("/");
  tft.setTextColor(WHITE);
  tft.setCursor(225, 140);
  tft.print(setDateDay);
  tft.setCursor(275, 140);
  tft.setTextColor(TEAL);
  tft.print("/");
  tft.setTextColor(WHITE);
  tft.setCursor(325, 140);
  tft.print(setDateYear);
  
  drawDownButton(102, 170);
  drawDownButton(222, 170);
  drawDownButton(337, 170);
  
  drawSaveButton();

  addSetDateActions();
}

void displaySetTimeScreen() {
  displayHeader();

  drawUpButton(117, 85);
  drawUpButton(237, 85);
    
  tft.setTextColor(WHITE);
  tft.setCursor(120, 138);
  int _hour = setTimeHour;
  char ampm[3];
  _hour = convert24HourTo12Hour(ampm, _hour);
  tft.print(_hour);
  tft.setCursor(190, 138);
  tft.setTextColor(TEAL);
  tft.print(":");
  tft.setTextColor(WHITE);
  tft.setCursor(240, 138);
  if (setTimeMinute < 10) {
    tft.print("0");
  }
  tft.print(setTimeMinute);
  tft.setCursor(330, 138);
  tft.print(ampm);
  
  drawDownButton(117, 170);
  drawDownButton(237, 170);

  drawSaveButton();

  addSetTimeScreenActions();
}

void drawButton(int x, int y, int width, char text[ ]) {
  tft.drawRoundRect(x, y, width, 50, 8, TEAL);
  tft.setCursor(x + 30, y + 15);
  tft.println(text);
}

void drawDownButton(int x, int y) {
  tft.drawRoundRect(x, y, 40, 40, 5, TEAL);
  tft.fillTriangle(x + 10, y + 10, x + 30, y + 10, x + 20, y + 28, TEAL);
}

void drawPurgeButton(int x, int y) {
  char purge[ 6 ] = "Purge";
  drawButton(x, y, 115, purge);
}

void drawSaveButton() {
  tft.setTextColor(TEAL);
  char save[5] = "Save";
  drawButton(180, 250, 130, save);
}

void drawUpButton(int x, int y) {
  tft.drawRoundRect(x, y, 40, 40, 5, TEAL);
  tft.fillTriangle(x + 20, y + 10, x + 30, y + 28, x + 10, y + 28, TEAL);
}

int getMedianNum(int bArray[], int iFilterLen) {        // method copied from calibration example code.
  int bTab[iFilterLen];
  for (byte i = 0; i<iFilterLen; i++) {
    bTab[i] = bArray[i];
  }
  int i, j, z, bTemp;
  for (j = 0; j < iFilterLen - 1; j++) {
    for (i = 0; i < iFilterLen - j - 1; i++) {
      if (bTab[i] > bTab[i + 1]) {
        bTemp = bTab[i];
        bTab[i] = bTab[i + 1];
        bTab[i + 1] = bTemp;
     }
    }
  }
  float sum;
  for (z = 0; z < iFilterLen; z++) {
    sum = sum + bTab[z];
  }
  bTemp = sum / iFilterLen;
  return bTemp;
}

int getPpm(float ec) {
  return ec * 500;
}

float getPh() {
  float avgValue;
  int sampleCount = 30;
  int phSamples[sampleCount];
  int temp;
  EEPROM_read(SlopeValueAddress, slopeValue);     // After calibration, the new slope and intercept should be read ,to update current value.
  EEPROM_read(InterceptValueAddress, interceptValue);
  for (int i = 0; i < sampleCount; i++) {                        // Get 10 sample values from the sensor to smooth the value
    phSamples[i] = analogRead(phSensor) / 1024.0 * 5000;
    delay(40);
  }
  avgValue = getMedianNum(phSamples, sampleCount);
  return (avgValue / 1000 * slopeValue + interceptValue) - 0.2; // pH meter seems to read roughly .2 below the actual pH so I'm adjusting it.
}

void increasePh() {
  isPumpInUse = true;
  digitalWrite(phUp, HIGH);
  delay(1500);                                          // add 1.5ml of pH up solution.
  digitalWrite(phUp, LOW);
  isPumpInUse = false;
  phWaitTime = now();
}

void increaseEc() {
  if (shouldAddPartAB) {
    isPumpInUse = true;
    digitalWrite(partA, HIGH);
    delay(5000 * (nutrientRatios[0][1] / 10));                                          // add Part A nutes
    digitalWrite(partA, LOW);
    digitalWrite(partB, HIGH);
    delay(5000 * (nutrientRatios[0][1] / 10));                                          // add Part B nutes
    digitalWrite(partB, LOW);
    isPumpInUse = false;
  }
  if (shouldAddSupp1) {
    isPumpInUse = true;
    digitalWrite(supp1, HIGH);
    delay(5000 * (nutrientRatios[1][1] / 10));                                          // add supplemental nutes #1
    digitalWrite(supp1, LOW);
    isPumpInUse = false;
  }
  if (shouldAddSupp2) {
    isPumpInUse = true;
    digitalWrite(supp2, HIGH);
    delay(5000 * (nutrientRatios[2][1] / 10));                                          // add supplemental nutes #2
    digitalWrite(supp2, LOW);
    isPumpInUse = false;
  }
  if (shouldAddSupp3) {
    isPumpInUse = true;
    digitalWrite(supp3, HIGH);
    delay(5000 * (nutrientRatios[3][1] / 10));                                         // add supplemental nutes #3
    digitalWrite(supp3, LOW);
    isPumpInUse = false;
  }
  ecWaitTime = now();
}

boolean isTouchingScreen() {
  return p.z >= MINPRESSURE && p.z <= MAXPRESSURE;
}

const char* lookupMonthName(int month) {
  switch (month) {
    case 1:
      return "January";
      break;
    case 2:
      return "Febuary";
      break;
    case 3:
      return "March";
      break;
    case 4:
      return "April";
      break;
    case 5:
      return "May";
      break;
    case 6:
      return "June";
      break;
    case 7:
      return "July";
      break;
    case 8:
      return "August";
      break;
    case 9:
      return "September";
      break;
    case 10:
      return "October";
      break;
    case 11:
      return "November";
      break;
    case 12:
      return "December";
      break;
  }
}

char* parseEcString() {
  char sensorstring_array[30];                          // we make a char array
  char *EC;                                             // char pointer used in string parsing
  ecSensorString.toCharArray(sensorstring_array, 30);   // convert the string to a char array 
  EC = strtok(sensorstring_array, ",");                 // let's pars the array at each comma
  return EC;
}

void purgePumpLines() {
  
}

void readCharacteristicValues() {
  EEPROM_read(SlopeValueAddress, slopeValue);
  EEPROM_read(InterceptValueAddress, interceptValue);
  if(EEPROM.read(SlopeValueAddress)==0xFF && EEPROM.read(SlopeValueAddress+1)==0xFF && EEPROM.read(SlopeValueAddress+2)==0xFF && EEPROM.read(SlopeValueAddress+3)==0xFF) {
    slopeValue = 3.5;                                   // If the EEPROM is new, the recommendatory slope is 3.5.
    EEPROM_write(SlopeValueAddress, slopeValue);
  }
  if(EEPROM.read(InterceptValueAddress)==0xFF && EEPROM.read(InterceptValueAddress+1)==0xFF && EEPROM.read(InterceptValueAddress+2)==0xFF && EEPROM.read(InterceptValueAddress+3)==0xFF) {
    interceptValue = 0;                                 // If the EEPROM is new, the recommendatory intercept is 0.
    EEPROM_write(InterceptValueAddress, interceptValue);
  }
}

void storeDateTime() {
  setTimeHour = hour();
  setTimeMinute = minute();
  setDateDay = day();
  setDateMonth = month();
  setDateYear = year();
}


