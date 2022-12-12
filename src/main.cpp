#include <Arduino.h>
#include "SevSeg.h"
#include <Wire.h> 
SevSeg sevseg; 

const int down_pin = 51;
const int up_pin = 52;
const int trigger_pin = 50;

//------------------------------------------------------------------------------
// Main Timer - This updates all the pin values 
//------------------------------------------------------------------------------
uint32_t lastInterupt = 0;
uint32_t lastRefresh = 0;
uint32_t value = 0;
uint32_t pin_states[3] = { 0, 0, 0 }; // down, up, trigger
uint32_t pin_time[3] = { 0, 0, 0 }; // down, up, trigger

bool doAction(int i) {
 return pin_states[i] == 0 ||
    (pin_states[i] == 1 && millis() - pin_time[i] > 500) || // first
    (pin_states[i] > 1 && millis() - pin_time[i] > 10); // fast
}

byte currentGroup = 0x00;

ISR(TIMER2_COMPA_vect)
{
  // flashing when selected
  if (millis() - pin_time[2] < 100) sevseg.blank();
  else if (millis() - pin_time[2] < 200) sevseg.setNumber(value);
  else if (millis() - pin_time[2] < 300) sevseg.blank();
  else if (millis() - pin_time[2] < 400) sevseg.setNumber(value);
  else if (millis() - pin_time[2] < 500) sevseg.blank();
  else sevseg.setNumber(value);

  sevseg.refreshDisplay(); 
}

bool reset = false;
void ReadSwitches()
{
  // read switches

  // DOWN
  bool down = digitalRead(down_pin) == 0;
  int i = 0;
  if (down) {
    if (doAction(i)) {
      value--;
      if (value < 0) {
        value = 255;
        if (currentGroup > 0) currentGroup--;
        Serial.println("Changing group to: " + (String)currentGroup);

        PORTA = currentGroup;
        PORTC = 0b00011100;      // sound address + rw = low, with wden low
        delayMicroseconds(30);
      }
      sevseg.setNumber(value);
      pin_states[i]++;
      pin_time[i] = millis();
    }
  } else {
    pin_states[i] = 0;
    pin_time[i] = 0;
  }

  // UP
  bool up = digitalRead(up_pin) == 0;
  i = 1;
  if (up) {
    if (doAction(i)) {
      value++;
      Serial.println("Changing value to: " + (String)value);
      if (value > 255) {
        value = 0;
        currentGroup++;
        Serial.println("Changing group to: " + (String)currentGroup);
    
        PORTA = currentGroup;
        PORTC = 0b00011100;      // sound address + rw = low, with wden low
        delayMicroseconds(30);
      }
      sevseg.setNumber(value);
      pin_states[i]++;
      pin_time[i] = millis();
    }
  } else {
    pin_states[i] = 0;
    pin_time[i] = 0;
  }

  // TRIGGER
  bool trigger = digitalRead(trigger_pin) == 0;
  i = 2;
  if (trigger) {
    if (pin_states[i] == 0) {
      // trigger sound command!
      Serial.println("Triggering value: " + (String)value);

      delayMicroseconds(900);

      // bank address
      PORTA = 0x7a;      
      PORTC = 0b01011100;      // sound address + rw = low, with wden high
      delayMicroseconds(20);
      PORTC = 0b00011100;      // sound address + rw = low, with wden low
      delayMicroseconds(30);
      PORTC = 0b01011100;      // sound address + rw = low, with wden high
      delayMicroseconds(500);

      // sound address
      PORTA = value;      
      PORTC = 0b01011100;      // sound address + rw = low, with wden high
      delayMicroseconds(20);
      PORTC = 0b00011100;      // sound address + rw = low, with wden low
      delayMicroseconds(30);
      PORTC = 0b01011100;      // sound address + rw = low, with wden high

      pin_states[i] = 1;
      pin_time[i] = millis();
      reset = false;
      
    } else {
      if (millis() - pin_time[i] >= 2000 && !reset) {
        // reset
        Serial.println("Reset!");
        PORTC = 0b00011101;      // sound address + rw = low, with wden low
        delayMicroseconds(30);
        PORTC = 0b01011101;      // sound address + rw = low, with wden high
        reset = true;
      }
    }
  } else {
    // OFF
    pin_states[i] = 0;
  }
}

// function that executes whenever data is received from master 
// this function is registered as an event, see setup()

void i2c_receive(int numBytes) 
{
  Serial.println("==> Received i2C");

  uint8_t result[] = "      ";
  int index = 0;
  
  while (Wire.available()) {
    int c = Wire.read();
    //Serial.print((String)c + " ");
    result[index] = c;
    index++;
  }
  Serial.println();

  if (index != 6 || result[0] != 255 || result[5] != 255) return;

  // Check Event
  int sound_id = (int)result[4];
  if (sound_id == 0) return;

  Serial.println("==> Received Sound Event");
  Serial.println("Source: " + (String)(int)result[1]);
  Serial.println("Sound: " + (String)sound_id);
}

void setup() {

  Serial.begin(115200);

  pinMode(down_pin, INPUT_PULLUP);
  pinMode(up_pin, INPUT_PULLUP);
  pinMode(trigger_pin, INPUT_PULLUP);

  byte numDigits = 3;
  
  
  // For JLC one: 11  7  4  2  1 10  5  3
  //              45 49 41 39 38 46 42 40
  // digits:      12  9  8
  //              44 47 41
  byte digitPins[] = { 44, 47, 48 };
  byte segmentPins[] = {45, 49, 41, 39, 38, 46, 42, 40 };
  bool resistorsOnSegments = true;
  bool updateWithDelays = false; // Default 'false' is Recommended
  bool leadingZeros = false; // Use 'true' if you'd like to keep the leading zeros
  bool disableDecPoint = true; // Use 'true' if your decimal point doesn't exist or isn't connected. Then, you only need to specify 7 segmentPins[]

  byte hardwareConfig = COMMON_ANODE; 
  sevseg.begin(hardwareConfig, numDigits, digitPins, segmentPins, resistorsOnSegments, updateWithDelays, leadingZeros, disableDecPoint);
  sevseg.setBrightness(255);

  // TIMER 2 for interrupt frequency 6024.096385542169 Hz:
  cli(); // stop interrupts
  TCCR2A = 0; // set entire TCCR2A register to 0
  TCCR2B = 0; // same for TCCR2B
  TCNT2  = 0; // initialize counter value to 0
  // set compare match register for 6024.096385542169 Hz increments
  OCR2A = 82; // = 16000000 / (32 * 6024.096385542169) - 1 (must be <256)
  // turn on CTC mode
  TCCR2B |= (1 << WGM21);
  // Set CS22, CS21 and CS20 bits for 32 prescaler
  TCCR2B |= (0 << CS22) | (1 << CS21) | (1 << CS20);
  // enable timer compare interrupt
  TIMSK2 |= (1 << OCIE2A);
  sei(); // allow interrupts

  sevseg.setNumber(value);

  // set gpio mode
  DDRC = 255; // switch port c completely to output
  DDRA = 255; // switch port a completely to output
  PORTA = 255; // high
  PORTC = 255; // high

  Serial.begin(115200);

  Serial.println();
  Serial.println("-----------------------");
  Serial.println("Stumblor PinPoint v1.0 TEST");
  Serial.println("-----------------------");

  // I2C
  Wire.begin(2);                // join i2c bus with address #2 
  Wire.onReceive(i2c_receive);

  // RESET BOARD
  PORTC = 0b00011101;      // sound address + rw = low, with wden low
  delayMicroseconds(30);
  PORTC = 0b01011101;      // sound address + rw = low, with wden high
}

void loop() {
  ReadSwitches();
}