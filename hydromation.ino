#include <EEPROM.h>
#include <TimeLib.h>
#include <SoftwareSerial.h>                             // we have to include the SoftwareSerial library, or else we can't use it
#include <Adafruit_GFX.h>                               // Core graphics library
#include <SPI.h>
#include "Adafruit_HX8357.h"
#include "TouchScreen.h"

#define rx 3                                            // define what pin rx is going to be
#define tx 2                                            // define what pin tx is going to be
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

#define Offset -0.62            //deviation compensate // Adjust this to calibrate 7.0 pH. Adjust 4.01 using the knob on the jack. // Increase to decrease pH ??
#define samplingInterval 20
#define ArrayLength  40    //times of collection
int pHArray[ArrayLength];   //Store the average value of the sensor feedback

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
const char version[6] = "1.3.7";

int pHArrayIndex=0;
int currentScreen = 1;
long ecTimeout = 43200; // 12 hours
int phTimeout = 3600; // 1 hour
TSPoint p;
SoftwareSerial ecSerial(rx, tx);                        // define how the soft serial port is going to work
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
int pumpRuntime = 5000;

void setup() {
  pinMode(phUp, OUTPUT);
  pinMode(phDown, OUTPUT);
  pinMode(partA, OUTPUT);
  pinMode(partB, OUTPUT);
  pinMode(supp1, OUTPUT);
  pinMode(supp2, OUTPUT);
  pinMode(supp3, OUTPUT);
  setTime(18, 0, 0, 18, 5, 2021);
  ecSerial.begin(9600);                                   // set baud rate for the software serial port to 9600
  ecSensorString.reserve(30);                             // set aside some bytes for receiving data from Atlas Scientific product
  ecSerial.print("SLEEP\r");                              // ensures the EC probe is awake incase the system shut down while it was sleeping.
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
  if (isTouchingPoint(290, 330, 90, 130)) {
    targetEc = targetEc + 0.2;
    clearScreen();
  }

  // Increase Tolerance
  if (isTouchingPoint(25, 65, 90, 130)) {
    ecTolerance = ecTolerance + 0.02;
    clearScreen();
  }
  
  // Decrease PPM
  if (isTouchingPoint(290, 330, 170, 210)) {
    if (targetEc >= 0.2) {
      targetEc = targetEc - 0.2;
    } else {
      targetEc = 0.0;
    }
    clearScreen();
  }

  // Decrease Tolerance
  if (isTouchingPoint(25, 65, 170, 210)) {
    if (ecTolerance >= 0.02) {
      ecTolerance = ecTolerance - 0.02;
    } else {
      ecTolerance = 0.0;
    }
    clearScreen();
  }

  // Adjust Ratios Button
  if (isTouchingPoint(50, 215, 200, 250)) {
    currentScreen = 12;
    clearScreen();
  }

  // Back button
  if (isTouchingPoint(264, 386, 200, 250)) {
    currentScreen = 2;
    clearScreen();
  }
}

void addAdjustScheduleActions() {
  // increase pH interval
  if (isTouchingPoint(317, 357, 80, 120)) {
    clearScreen();
    phTimeout = phTimeout + 3600;  
  }
  
  // increase EC interval
  if (isTouchingPoint(107, 147, 80, 120)) {
    clearScreen();
    ecTimeout = ecTimeout + 3600;  
  }
  
  // decrease pH interval
  if (isTouchingPoint(317, 357, 185, 225) && phTimeout > 3600) {
    clearScreen();
    phTimeout = phTimeout - 3600;
  }

  // decrease EC interval
  if (isTouchingPoint(107, 147, 185, 225) && ecTimeout > 3600) {
    clearScreen();
    ecTimeout = ecTimeout - 3600;
  }

  // Back button
  if (isTouchingPoint(160, 285, 250, 300)) {
    clearScreen();
    currentScreen = 4;
  }
}

void  addConfigScreenActions() {
  // navigate to home
  if (isTouchingPoint(330, 430, 70, 120)) {
    clearScreen();
    currentScreen = 1;
  }

  // Adjust nutrients
  if (isTouchingPoint(180, 430, 130, 180)) {
    clearScreen();
    currentScreen = 7;
  }

  // Enable/Disable pumps
  if (isTouchingPoint(130, 430, 190, 240)) {
    clearScreen();
    currentScreen = 10;
  }
  
  // navigate from config page 1 to 2
  if (isTouchingPoint(225, 430, 200, 250)) {
    clearScreen();
    currentScreen = 3;
  }
}

void addConfigScreen2Actions() {
  // navigate from config page 2 to 1
  if (isTouchingPoint(210, 430, 70, 120)) {
    clearScreen();
    currentScreen = 2;
  }

  // Navigate to Purge Pumps Screen
  if (isTouchingPoint(170, 430, 130, 180)) {
    clearScreen();
    currentScreen = 8;
  }

  // Flush system
  if (isTouchingPoint(315, 430, 190, 240)) {
    clearScreen();
    currentScreen = 9;
  }

  // Navigate to config page 3
  if (isTouchingPoint(215, 430, 200, 250)) {
    clearScreen();
    currentScreen = 4;
  }
}

void addConfigScreen3Actions() {
  // navigate from config page 3 to 2
  if (isTouchingPoint(210, 430, 70, 120)) {
    clearScreen();
    currentScreen = 3;
  }

  // Set Time
  if (isTouchingPoint(270, 430, 130, 180)) {
    clearScreen();
    currentScreen = 5;
  }
  
  // Set Date
  if (isTouchingPoint(270, 430, 140, 190)) {
    clearScreen();
    currentScreen = 6;
  }
}

void addEnablePumpsActions() {
  if (isTouchingPoint(255, 345, 80, 130)) {
    clearScreen();
    shouldAddPartAB = !shouldAddPartAB;
  }
  if (isTouchingPoint(35, 125, 80, 130)) {
    clearScreen();
    shouldAddSupp1 = !shouldAddSupp1;
  }
  if (isTouchingPoint(255, 345, 155, 205)) {
    clearScreen();
    shouldAddSupp2 = !shouldAddSupp2;
  }
  if (isTouchingPoint(35, 125, 155, 205)) {
    clearScreen();
    shouldAddSupp3 = !shouldAddSupp3;
  }
  if (isTouchingPoint(160, 285, 200, 250)) {
    clearScreen();
    currentScreen = 2;
  }
}

void addFlushActions() {
  // Back button
  if (isTouchingPoint(285, 410, 140, 190)) {
    clearScreen();
    currentScreen = 3;
  }

  // Soups on button
  if (isTouchingPoint(30, 230, 140, 190)) {
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
  if (isTouchingPoint(255, 305, 140, 345)) {
    clearScreen();
    currentScreen = 2; // Display the configuration screen
  }
}

void addNutrientRatiosActionsPage1() {
  // increase A/B concentration
  if (isTouchingPoint(293, 333, 80, 120)) {
    clearScreen();
    nutrientRatios[0][0] = nutrientRatios[0][0] + 1;  
  }
  
  // increase Supp 1 concentration
  if (isTouchingPoint(123, 163, 80, 120)) {
    clearScreen();
    nutrientRatios[1][0] = nutrientRatios[1][0] + 1;  
  }
  
  // decrease A/B concentration
  if (isTouchingPoint(293, 333, 185, 225) && nutrientRatios[0][0] > 0) {
    clearScreen();
    nutrientRatios[0][0] = nutrientRatios[0][0] - 1;
  }

  // decrease Supp 1 concentration
  if (isTouchingPoint(123, 163, 185, 225) && nutrientRatios[1][0] > 0) {
    clearScreen();
    nutrientRatios[1][0] = nutrientRatios[1][0] - 1;
  }

  // Back button
  if (isTouchingPoint(265, 390, 200, 250)) {
    determineSupplementRatios();
    clearScreen();
    currentScreen = 7;
  }

  // More button
  if (isTouchingPoint(100, 225, 200, 250)) {
    clearScreen();
    currentScreen = 13;
  }
}

void addNutrientRatiosActionsPage2() {
  // increase Supp 2 concentration
  if (isTouchingPoint(288, 328, 80, 120)) {
    clearScreen();
    nutrientRatios[2][0] = nutrientRatios[2][0] + 1;  
  }
  
  // increase Supp 3 concentration
  if (isTouchingPoint(118, 163, 80, 120)) {
    clearScreen();
    nutrientRatios[3][0] = nutrientRatios[3][0] + 1;  
  }
  
  // decrease supp 2 concentration
  if (isTouchingPoint(288, 328, 185, 225) && nutrientRatios[2][0] > 0) {
    clearScreen();
    nutrientRatios[2][0] = nutrientRatios[2][0] - 1;
  }

  // decrease Supp 3 concentration
  if (isTouchingPoint(118, 163, 185, 225) && nutrientRatios[3][0] > 0) {
    clearScreen();
    nutrientRatios[3][0] = nutrientRatios[3][0] - 1;
  }

  // Back button
  if (isTouchingPoint(180, 305, 200, 250)) {
    clearScreen();
    currentScreen = 12;
  }
}

void addPurgeScreenActions() {
  // Back Button
  if (isTouchingPoint(405, 445, 15, 55)) {
    currentScreen = 3;
    clearScreen();
  } else if (isTouchingPoint(230, 345, 80, 130)) {
    digitalWrite(partA, HIGH);
    isPurgingPump = true;
  } else if (isTouchingPoint(230, 345, 155, 195)) {
    digitalWrite(partB, HIGH);
    isPurgingPump = true;
  } else if (isTouchingPoint(230, 345, 180, 220)) {
    digitalWrite(supp1, HIGH);
    isPurgingPump = true;
  } else if (isTouchingPoint(10, 125, 80, 130)) {
    digitalWrite(supp2, HIGH);
    isPurgingPump = true;
  } else if (isTouchingPoint(10, 125, 155, 195)) {
    digitalWrite(phUp, HIGH);
    isPurgingPump = true;
  } else if (isTouchingPoint(10, 125, 180, 220)) {
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
  if (isTouchingPoint(318, 358, 85, 125)) {
    if (setDateMonth < 12) {
      setDateMonth++;
    } else {
      setDateMonth = 1;
    }
    clearScreen();
  }

  // Increase day
  if (isTouchingPoint(198, 238, 85, 125)) {
    if (setDateDay < 31) {
      setDateDay++;
    } else {
      setDateDay = 1;
    }
    clearScreen();
  }
  
  // Increase year
  if (isTouchingPoint(83, 123, 85, 125)) {
    setDateYear++;
    clearScreen();
  }

  // Decrease Month
  if (isTouchingPoint(318, 358, 170, 210)) {
    if (setDateMonth > 1) {
      setDateMonth--;
    } else {
      setDateMonth = 12;
    }
    clearScreen();
  }

  // Decrease day
  if (isTouchingPoint(198, 238, 170, 210)) {
    if (setDateDay > 1) {
      setDateDay--;
    } else {
      setDateDay = 31;
    }
    clearScreen();
  }
  
  // Decrease year
  if (isTouchingPoint(83, 123, 170, 210)) {
    setDateYear--;
    clearScreen();
  }

  // Save date
  if (isTouchingPoint(150, 280, 190, 240)) {
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
  if (isTouchingPoint(303, 343, 85, 125)) {
    if (setTimeHour < 23) {
      setTimeHour++;
    } else {
      setTimeHour = 0;
    }
    clearScreen();
  }

  // Increase minute
  if (isTouchingPoint(183, 223, 85, 125)) {
    if (setTimeMinute < 59) {
      setTimeMinute++;
    } else {
      setTimeMinute = 0;
    }
    clearScreen();
  }

  // Decrease hour
  if (isTouchingPoint(303, 343, 170, 210)) {
    if (setTimeHour > 0) {
      setTimeHour--;
    } else {
      setTimeHour = 23;
    }
    clearScreen();
  }

  // Decrease minute
  if (isTouchingPoint(183, 223, 170, 210)) {
    if (setTimeMinute > 1) {
      setTimeMinute--;
    } else {
      setTimeMinute = 59;
    }
    clearScreen();
  }

  // Save time
  if (isTouchingPoint(150, 280, 210, 240)) {
    int _month = month();
    int _day = day();
    int _year = year();
    setTime(setTimeHour, setTimeMinute, 0, _day, _month, _year);
    currentScreen = 4;
    isSettingDateTime = false;
    clearScreen();
  }
}

double avergearray(int* arr, int number){
  int i;
  int max,min;
  double avg;
  long amount=0;
  if(number<=0){
    // Error number for the array to avraging!
    return 0;
  }
  if(number<5){   //less than 5, calculated directly statistics
    for(i=0;i<number;i++){
      amount+=arr[i];
    }
    avg = amount/number;
    return avg;
  }else{
    if(arr[0]<arr[1]){
      min = arr[0];max=arr[1];
    }
    else{
      min=arr[1];max=arr[0];
    }
    for(i=2;i<number;i++){
      if(arr[i]<min){
        amount+=min;        //arr<min
        min=arr[i];
      }else {
        if(arr[i]>max){
          amount+=max;    //arr>max
          max=arr[i];
        }else{
          amount+=arr[i]; //min<=arr<=max
        }
      }//if
    }//for
    avg = (double)amount/(number-2);
  }//if
  return avg;
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
    nutrientRatios[i][1] = ((float)nutrientRatios[i][0] / (float)nutrientRatios[highValIndex][0]) * 10;
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

int getPpm(float ec) {
  return ec * 500;
}

float getPh() {
  static float pHValue,voltage;
  do {
    pHArray[pHArrayIndex++]=analogRead(0);
  } while (pHArrayIndex < ArrayLength);
  if(pHArrayIndex==ArrayLength) pHArrayIndex=0;
  voltage = avergearray(pHArray, ArrayLength)*5.0/1024;
  pHValue = 3.5*voltage+Offset;
  return pHValue;
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
    delay(pumpRuntime * ((float)nutrientRatios[0][1] / 10));                                          // add Part A nutes
    digitalWrite(partA, LOW);
    digitalWrite(partB, HIGH);
    delay(5000 * ((float)nutrientRatios[0][1] / 10));                                          // add Part B nutes
    digitalWrite(partB, LOW);
    isPumpInUse = false;
  }
  if (shouldAddSupp1) {
    isPumpInUse = true;
    digitalWrite(supp1, HIGH);
    delay(pumpRuntime * ((float)nutrientRatios[1][1] / 10));                                          // add supplemental nutes #1
    digitalWrite(supp1, LOW);
    isPumpInUse = false;
  }
  if (shouldAddSupp2) {
    isPumpInUse = true;
    digitalWrite(supp2, HIGH);
    delay(pumpRuntime * ((float)nutrientRatios[2][1] / 10));                                          // add supplemental nutes #2
    digitalWrite(supp2, LOW);
    isPumpInUse = false;
  }
  if (shouldAddSupp3) {
    isPumpInUse = true;
    digitalWrite(supp3, HIGH);
    delay(pumpRuntime * ((float)nutrientRatios[3][1] / 10));                                         // add supplemental nutes #3
    digitalWrite(supp3, LOW);
    isPumpInUse = false;
  }
  ecWaitTime = now();
}

boolean isTouchingPoint(int yMin, int yMax, int xMin, int xMax) {
  return p.y >= yMin && p.y <= yMax && p.x >= xMin && p.x <= xMax && isTouchingScreen();
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

void storeDateTime() {
  setTimeHour = hour();
  setTimeMinute = minute();
  setDateDay = day();
  setDateMonth = month();
  setDateYear = year();
}


