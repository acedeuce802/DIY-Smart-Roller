/*
   A BLE turbo trainer controller for Zwift

   Program receives environment date from the Zwift game (via BLE) and adjusts the resistance of a turbo trainer using a stepper motor
   Uses an ESP32 and includes an LCD display for feedback.

   Copyright 2020 Peter Everett
   v1.0 Aug 2020 - Initial version

   This work is licensed the GNU General Public License v3 (GPL-3)
   Modified for Proform TDF bike generation 2 by Greg Masik April 1, 2021. Removed stepper motor and LCD display
   April 28, modified to allow for zwift half grade for negative grades. May 10, 2021 added I2C LCD display for grade.
   January 5, 2022- Added case 0x01 to allow for reset request from Zwift after Dec 2021 update.

   Modified by Adam Watson January 3rd, 2024:
     Only kept BLE interface for FTMS from source code.  
     Added case 0x05 for ERG mode control for ESP32 to receive target power
     Added hall sensor input to calculate RPM of cycling rollers and smooth with an averaged array
     Added servo control, to control magnetic resistance to cycling rollers
     Added bilinear interpolation for inputs speed and target power, and output servo position

*/

//Tested with Xiao ESP32-C3

#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEUtils.h>
#include <BLE2902.h>
#include <Servo.h>

//Pin Designations
const int servoPin = 10; //Servo pin
int sensPin = 8;  //Hall sensor pin for speed input

//Random variables
int power = 0;

//Servo stuff
int pos = 0;
int newPos;
int diffPos;
uint32_t prevMs;
Servo myservo = Servo();

//RPM stuff
unsigned long whileMillis;
unsigned long millisValue = 0;

int Speed = 0;
const int Wheel_circumference = 85;  //Roller diameter, or whatever the pickup is on

bool MagRead_Last_state;
bool MagRead_Current_state;
unsigned long MagRead_Last_state_Change = 0;

int MagRead_Last_state_isonMove_Millis_Gap = Wheel_circumference * 36;

bool isonMove = false;

//RPM Smoothing  https://academy.programmingelectronics.com/tutorial-23-smoothing-data-old-version/
const int numReadings = 5;     //Higher number = more reading smoothed, less response

int readings[numReadings];      // the readings from the analog input
int readIndex = 0;              // the index of the current reading
int total = 0;                  // the running total
int speedAverage = 0;           // the average

//Simulation bounds and variables
const int upperInclineClamp = 10;
const int lowerInclineClamp = -5;  //changed from -10

float gradeFloat = 0.0;
int_least16_t grade = 0;

int roundedGrade = 0;
int currentBikeGrade = 0;

// https://forum.arduino.cc/t/bilinear-interpolation/438590/7
// Create bilinear interpolation for inputs speed and target power to output servo position
// Those arrays needs to be sorted in ascending order
// Need at least 2 points

double X[] = { 0, 1000, 1200, 1400, 1600, 2000, 2200, 3000 };  //X axis Data for testing
double Y[] = { 0, 100, 150, 200, 250, 300, 400, 600 };         //Y axis Data for testing

const int Xcount = sizeof(X) / sizeof(X[0]);
const int Ycount = sizeof(Y) / sizeof(Y[0]);

// points in 3D space (X[x], Y[y], Z[x][y])
double Z[Xcount][Ycount] = {  //Z Data for testing
  { 1000, 1000, 1000, 1000, 1000, 1000, 1000, 1000 },
  { 1000, 1459, 1790, 1844, 1844, 1844, 1844, 1844 },
  { 1000, 1366, 1595, 1824, 1844, 1844, 1844, 1844 },
  { 1000, 1000, 1483, 1661, 1840, 1844, 1844, 1844 },
  { 1000, 1000, 1000, 1556, 1702, 1844, 1844, 1844 },
  { 1000, 1000, 1000, 1000, 1525, 1637, 1844, 1844 },
  { 1000, 1000, 1000, 1000, 1463, 1558, 1750, 1844 },
  { 1000, 1000, 1000, 1000, 1284, 1352, 1487, 1758 }
};


//BLE stuff
BLEServer* pServer = NULL;
BLECharacteristic* pIndoorBike = NULL;
BLECharacteristic* pFeature = NULL;
BLECharacteristic* pControlPoint = NULL;
BLECharacteristic* pStatus = NULL;

BLEAdvertisementData advert;
BLEAdvertisementData scan_response;
BLEAdvertising* pAdvertising;

bool deviceConnected = false;
bool oldDeviceConnected = false;
int value = 0;  //This is the value sent as "nothing".  We need to send something for some of the charactistics or it won't work.

#define FTMSDEVICE_FTMS_UUID "00001826-0000-1000-8000-00805F9B34FB"
#define FTMSDEVICE_INDOOR_BIKE_CHAR_UUID "00002AD2-0000-1000-8000-00805F9B34FB"
//#define FTMSDEVICE_RESISTANCE_RANGE_CHAR_UUID "00002AD6-0000-1000-8000-00805F9B34FB"
//#define FTMSDEVICE_POWER_RANGE_CHAR_UUID "00002AD8-0000-1000-8000-00805F9B34FB"
#define FTMSDEVICE_FTMS_FEATURE_CHAR_UUID "00002ACC-0000-1000-8000-00805F9B34FB"
#define FTMSDEVICE_FTMS_CONTROL_POINT_CHAR_UUID "00002AD9-0000-1000-8000-00805F9B34FB"
#define FTMSDEVICE_FTMS_STATUS_CHAR_UUID "00002ADA-0000-1000-8000-00805F9B34FB"

const uint16_t FTMSDEVICE_INDOOR_BIKE_CHARDef = 0b0000000001000100;  // flags for indoor bike data characteristics - power and cadence
uint16_t speedOut = 100;
int16_t powerOut = 100;
uint8_t FTMSDEVICE_INDOOR_BIKE_CHARData[8] = {  // values for setup - little endian order
  (uint8_t)(FTMSDEVICE_INDOOR_BIKE_CHARDef & 0xff),
  (uint8_t)(FTMSDEVICE_INDOOR_BIKE_CHARDef >> 8),
  (uint8_t)(speedOut & 0xff),
  (uint8_t)(speedOut >> 8),
  (uint8_t)(speedOut & 0xff),
  (uint8_t)(speedOut >> 8),
  0x64,
  0
};

//response/acknowledgement to send to the client after writing to control point
uint8_t replyDs[3] = { 0x80, 0x00, 0x01 };  // set up replyDS array with 3 byte values for control point resp to zwift

class MyServerCallbacks : public BLEServerCallbacks {
  void onConnect(BLEServer* pServer) {
    deviceConnected = true;
  };

  void onDisconnect(BLEServer* pServer) {
    deviceConnected = false;
  }
};

void setup() {

  Serial.begin(115200);
  pinMode(sensPin, INPUT);
  MagRead_Last_state = digitalRead(sensPin);

  Serial.println("Starting initialisation routine");

  //Setup BLE
  Serial.println("Creating BLE server...");
  BLEDevice::init("ESP32");

  // Create the BLE Server
  pServer = BLEDevice::createServer();
  pServer->setCallbacks(new MyServerCallbacks());

  // Create the BLE Service
  Serial.println("Define service...");
  BLEService* pService = pServer->createService(FTMSDEVICE_FTMS_UUID);

  // Create BLE Characteristics
  Serial.println("Define characteristics");
  pIndoorBike = pService->createCharacteristic(FTMSDEVICE_INDOOR_BIKE_CHAR_UUID, BLECharacteristic::PROPERTY_NOTIFY);
  pIndoorBike->addDescriptor(new BLE2902());
  pControlPoint = pService->createCharacteristic(FTMSDEVICE_FTMS_CONTROL_POINT_CHAR_UUID, BLECharacteristic::PROPERTY_INDICATE | BLECharacteristic::PROPERTY_WRITE);
  pControlPoint->addDescriptor(new BLE2902());
  pFeature = pService->createCharacteristic(FTMSDEVICE_FTMS_FEATURE_CHAR_UUID, BLECharacteristic::PROPERTY_READ);
  pFeature->addDescriptor(new BLE2902());
  pStatus = pService->createCharacteristic(FTMSDEVICE_FTMS_STATUS_CHAR_UUID, BLECharacteristic::PROPERTY_NOTIFY);
  pStatus->addDescriptor(new BLE2902());

  // Start the service
  Serial.println("Staring BLE service...");
  pService->start();

  // Start advertising
  Serial.println("Define the advertiser...");
  pAdvertising = BLEDevice::getAdvertising();

  pAdvertising->setScanResponse(true);
  pAdvertising->addServiceUUID(FTMSDEVICE_FTMS_UUID);
  pAdvertising->setMinPreferred(0x06);  // set value to 0x00 to not advertise this parameter
  Serial.println("Starting advertiser...");
  BLEDevice::startAdvertising();
  Serial.println("Waiting a client connection to notify...");

  Serial.println("Waiting for");

  Serial.println("connection...");
}

/*
  The main loop which runs continuously.
  Checks whether something is connected
  - Receive bike simulation data, adjust the trainer and send acknowledgement back
*/


void loop() {
  char windStr[6];   //6 spot array 1 byte each -wind speed
  char gradeStr[6];  //6 spot array 1 byte each - grade
  char rStr[4];      //4spot array 1 byte each  - rolling res
  char wStr[4];      //4 spot array 1 byte each - wind coef
  // notify changed value
  if (deviceConnected) {
    //indoor bike char values
    uint8_t bikeData[19] = {};  //set all the bytes to zero .  If we're only transmitting power, there's no point transmitting beyond byte 18
    bikeData[0] = 0x01;         // flags for Indoor bike-set bit 6 low of byte 0 to say power data not present
    //bit 1 set high for inst speed NOT present
    // bikeData[15] = 0x00;  //set bytes? 15 and 16 for the power (note that this is a signed int16 no power (0)
    // bikeData[16] = 0x00;
    uint8_t flags = bikeData[0];  //set to 0x01
    uint8_t dataToSend[1] = { flags };
    pIndoorBike->setValue(dataToSend, 1);
    pIndoorBike->notify();  //notify zwift

    //configure machine feature characteristic 8 fields- only set indoor bike simulation bit(field 5)
    uint8_t feature[8] = { 0x00, 0x00, 0x00, 0x00, 0x0C, 0x20, 0x00, 0x00 };  // 2^13 = bike simulation bit set byte 5.
    pFeature->setValue(feature, 8);                                           //send data to feature char- 8 locations from feature array
    pStatus->setValue(value);                                                 //send data to status char- value =0
    pStatus->notify();                                                        //notify zwift

    //Get the data written to the control point
    std::string rxValue = pControlPoint->getValue();
    if (rxValue.length() == 0) {
      Serial.println("No data received...");
    } else {
      uint8_t value[3] = { 0x80, (uint8_t)rxValue[0], 0x02 };  // confirmation data, default - 0x02 - Op "Code not supported", no app cares
      switch (rxValue[0]) {
        case 0x00:  //Request control from zwift
          //reply with 0x80, 0x00, 0x01 to say OK
          replyDs[1] = rxValue[0];
          pControlPoint->setValue(replyDs, 3);
          pControlPoint->indicate();
          Serial.println("case 0");
          break;

        case 0x01:  //reset
          //reply with 0x80, 0x01, 0x01 to say OK
          replyDs[1] = rxValue[0];
          pControlPoint->setValue(replyDs, 3);
          pControlPoint->indicate();
          Serial.println("case 1");
          break;

        case 0x05:
          {
            Serial.println("case 5");
            int16_t power = rxValue[1] + 256 * rxValue[2];
            

            FTMSDEVICE_INDOOR_BIKE_CHARData[6] = (uint8_t)(constrain(power, 0, 4000) & 0xff);
            FTMSDEVICE_INDOOR_BIKE_CHARData[7] = (uint8_t)(constrain(power, 0, 4000) >> 8);  // power value, constrained to avoid negative values, although the specification allows for a sint16

            // Send back power if rollers are moving, apps won't PID target power if they don't receive "trainer power"
            // Since we don't have "trainer power", spoof power=target power
            // App will now adjust target power based on difference between value from power meter, and spoofed power
            // If we spam power back all the time, app won't pause
            if (Speed > 100) 
            {
              pIndoorBike->setValue(FTMSDEVICE_INDOOR_BIKE_CHARData, 8);  // values sent
              pIndoorBike->notify();
            }
            
            //Serial.println("End of case 5");
          }
          break;

        case 0x07:  //Start/resume
          //reply with 0x80, 0x07, 0x01 to say OK
          replyDs[1] = rxValue[0];
          pControlPoint->setValue(replyDs, 3);
          pControlPoint->indicate();
          Serial.println("case 7");
          break;

        case 0x11:  //receive simulation parameters
          Serial.println("case 11");
          /*
            In the case of Zwift:
            Wind is always constant, even with an aero boost (you just go faster)
            When trainer difficulty is set to maximum, the grade is as per the BLE spec (500 = 5%)
            Rolling resistance only ever seems to be 0 or 255.  255 occurs on mud/off road sections but not always (short sections only, like a patch of sand?)
            Wind coefficient doesn't change during a ride, even with an aero boost.  It may change depending on bike used in the game.
            Note: only using grade for TDF bike in 1% increments*/

          int16_t wind = rxValue[2] << 8;  //register changed from [1] in turbotrainer original
          wind |= rxValue[1];              //register changed from [0]in turbotrainer original

          uint8_t Rres = rxValue[5];  //register changed from [4]in turbotrainer original
          uint8_t Wres = rxValue[6];  //register changed from [5]in turbotrainer original

          grade = rxValue[4] << 8;
          grade |= rxValue[3];

          gradeFloat = grade / 100.0;

          if (gradeFloat < 0) {
            gradeFloat = (gradeFloat * 2);
          }

          // Serial.print("gradeFloat= ");
          // Serial.println(gradeFloat);
          //Serial.print("wind speed= ");
          //Serial.println(wind);
          //Serial.print("Rolling res= ");
          //Serial.println(Rres);
          //Serial.print("Wind Res= ");
          //Serial.println(Wres);

          roundedGrade = round(gradeFloat);

          //  Serial.print("rounded grade = ");
          // Serial.println(roundedGrade);

          if (roundedGrade >= upperInclineClamp) {
            roundedGrade = upperInclineClamp;
          }
          if (roundedGrade <= lowerInclineClamp) {
            roundedGrade = lowerInclineClamp;
          }
          if (roundedGrade == currentBikeGrade) {
            delay(275);  //provides a delay for  the no change condition- same as other for uniformity
          }

          //delay(1000);
          // Serial.println("One Second Delay");
          break;
      }
    }
    while (millis() - whileMillis < 500 ) //After 0x05 breaks, run control code for 500ms without respamming BLE bus
    {
      rpmRead();
      setServo();
    }
    whileMillis = millis() ;
  }
  // disconnecting
  if (!deviceConnected && oldDeviceConnected) {
    delay(300);                   // give the bluetooth stack the chance to get things ready
    pServer->startAdvertising();  // restart advertising
    //Serial.println("Nothing connected, start advertising");
    oldDeviceConnected = deviceConnected;
  }
  // connecting
  if (deviceConnected && !oldDeviceConnected) {
    // do stuff here on connecting
    oldDeviceConnected = deviceConnected;
  }
}

void rpmRead() {
  MagRead_Current_state = !digitalRead(sensPin);

  if (MagRead_Last_state_Change + MagRead_Last_state_isonMove_Millis_Gap < millis()) {
    Speed = 0;
    isonMove = false;
  }

  if (MagRead_Current_state != MagRead_Last_state) {
    millisValue = millis();

    if (MagRead_Current_state == HIGH) {
      if (isonMove) {
        //Speed = float(Wheel_circumference) / float(millisValue - MagRead_Last_state_Change);
        Speed = 60000 / float(millisValue - MagRead_Last_state_Change);
      } else {
        isonMove = true;
      }
      MagRead_Last_state_Change = millisValue;
    }
    MagRead_Last_state = MagRead_Current_state;
  }

  total = total - readings[readIndex];
  readings[readIndex] = Speed; // read Speed into array
  total = total + readings[readIndex]; // add the reading to the total:
  readIndex = readIndex + 1; // advance to the next position in the array:

  if (readIndex >= numReadings) // if we're at the end of the array, go back to beginning
  { 
    readIndex = 0;
  }
  speedAverage = total / numReadings; // calculate the average:

}

void setServo()
{
  newPos = bilinearXY(speedAverage, power);  //get servo position from bilinear interpolated array

  diffPos = newPos - pos;  //calc position differential, to have if/then step the servo by predertimined amount rather than jump to new position

  if (newPos > pos && diffPos > 10) 
    {
      pos = pos + 5;
    } else if (newPos < pos && diffPos < 10) {
      pos = pos - 5;
    } else {
      pos = newPos;
    }
    
    myservo.writeMicroseconds(servoPin, pos); //Write slowly stepped value to servo, uses writeMicroseconds rather than writeServo for finer control
    Serial.print("Pos =");
    Serial.println(pos);
};

double bilinearXY(int x, int y) {
  int xIndex, yIndex;

  if ((x < X[0]) || (x > X[Xcount - 1])) {
    Serial.println(F("x not in range"));
    return -1;  // arbitrary...
  }

  if ((y < Y[0]) || (y > Y[Ycount - 1])) {
    Serial.println(F("y not in range"));
    return -1;  // arbitrary...
  }

  for (int i = Xcount - 2; i >= 0; --i)
    if (x >= X[i]) {
      xIndex = i;
      break;
    }

  for (int i = Ycount - 2; i >= 0; --i)
    if (y >= Y[i]) {
      yIndex = i;
      break;
    }

  Serial.print(F("X:"));
  Serial.print(x);
  Serial.print(F(" in ["));
  Serial.print(X[xIndex]);
  Serial.print(F(","));
  Serial.print(X[xIndex + 1]);
  Serial.print(F("] and Y:"));
  Serial.print(y);
  Serial.print(F(" in ["));
  Serial.print(Y[yIndex]);
  Serial.print(F(","));
  Serial.print(Y[yIndex + 1]);
  Serial.println(F("]"));

  // https://en.wikipedia.org/wiki/Bilinear_interpolation
  // Q11 = (x1, y1), Q12 = (x1, y2), Q21 = (x2, y1), and Q22 = (x2, y2)
  double x1, y1, x2, y2;
  double fQ11, fQ12, fQ21, fQ22;
  double fxy1, fxy2, fxy;

  x1 = X[xIndex];
  x2 = X[xIndex + 1];
  y1 = Y[yIndex];
  y2 = Y[yIndex + 1];

  fQ11 = Z[xIndex][yIndex];
  fQ12 = Z[xIndex][yIndex + 1];
  fQ21 = Z[xIndex + 1][yIndex];
  fQ22 = Z[xIndex + 1][yIndex + 1];

  fxy1 = ((x2 - x) / (x2 - x1)) * fQ11 + ((x - x1) / (x2 - x1)) * fQ21;
  fxy2 = ((x2 - x) / (x2 - x1)) * fQ12 + ((x - x1) / (x2 - x1)) * fQ22;

  fxy = ((y2 - y) / (y2 - y1)) * fxy1 + ((y - y1) / (y2 - y1)) * fxy2;

  return fxy;
}