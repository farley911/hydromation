# Hydromation

###### Version: 1.4.1

Hydromation is an Arduino based automation system for maintaining and monitoring a RDWC (Recirculating Deep Water Culture) hydroponics system. The system actively monitors the pH and PPM (EC) of a nutrient rich solution. If the measurements go outside of a configured range then the system can activate a series of 7 pumps to adjust the nutrients and pH.

## Configuration

pH is maintained within a range of 5.65 and 5.95 (hard-coded).

Adjust the pH meters accuracy by setting the `Offset` property in the code.

PPM (EC) can be configured in the app under Configuration > Adjust Nutrients
* PPM: Sets the target value for the nutrient concentration. 
* Tolerance: Sets the acceptable tolerance for the nutrient concentration to vary before adjusting the levels (Default: 50.0).

Pumps can be enabled/disabled under Configuration > Enable/Disable Pumps

## Change Log

### 1.4.1 [Jan 26th 2020]

Adjusts pH `Offset` value to **-5.78**

### 1.4.0 [Dec 25th 2019]

Adjusts pH `Offset` value to **-2.24**