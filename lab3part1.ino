// ========== Includes ==========
#include <Wire.h>
// Ensure that the LiquidCrystal I2C library by Frank de Brabander is installed in your Arduino IDE Library Manager
#include <LiquidCrystal_I2C.h>

// ========== Global Variables ==========
LiquidCrystal_I2C lcd(0x27, 16, 2);  // Initialize the LCD
bool newMessage = true;

// ========== Function Prototypes ==========
void sendCommand(uint8_t command);
void sendData(uint8_t serialData);


// Name: sendCommand 
// Description:  This is a helper function that sends instruction commands to the LCD in 4 bit mode
// this reduces repetition in the main loop by handling the control signal sequence required
// to send a command byte over the I2C interface. 
void sendCommand(uint8_t command) {
    uint8_t config = 0;
    config |= (1 << 3);  // backlight=1, bit 0 is 0 (command) bit 1 is 0 write, bit 3, 1 backlight

    // upper nibble
    Wire.beginTransmission(0x27);
    Wire.write((command & 0xF0) | config | (1 << 2));  // HI=1
    Wire.endTransmission();
    Wire.beginTransmission(0x27);
    Wire.write((command & 0xF0) | config);  // LO=0
    Wire.endTransmission();

    // lower nibble
    Wire.beginTransmission(0x27);
    Wire.write((command << 4) | config | (1 << 2));  // HI=1
    Wire.endTransmission();
    Wire.beginTransmission(0x27);
    Wire.write((command << 4) | config);  // LO=0
    Wire.endTransmission();

    delay(2);  // needed to propertly clear else glitches
}

// Name: sendData
// Description: Sends a data byte to the LCD via I2C in 4 bit mode 
// this function handles the proper sequence of enable pulses and bit shifts 
// required to transmit the upper and lower nibbles of a character or symbol 
// to be displayed on the LCD
void sendData(uint8_t serialData) {
    // get serial data upperNibble to process
    uint8_t upperNibble = (serialData & 0xF0);  // bits 7–4 stay, bits 3–0 cleared
    uint8_t serialDataConfig = 0;
    serialDataConfig |= 1;           // Bit 0 data mode
    serialDataConfig &= ~(1 << 1);   // Bit 1 write bit
    serialDataConfig |= (1 << 3);    // Bit 3  backlight

    // send upper nibble with EN high then low (bit 2 0 by default)
    Wire.beginTransmission(0x27);
    Wire.write(upperNibble | serialDataConfig | (1 << 2));  // HI = 1
    Wire.endTransmission();

    Wire.beginTransmission(0x27);
    Wire.write(upperNibble | serialDataConfig);  // LO = 0
    Wire.endTransmission();

    // lower nibble
    uint8_t lowerNibble = (serialData << 4);  // shift bits 3–0 into 7–4 position

    Wire.beginTransmission(0x27);
    Wire.write(lowerNibble | serialDataConfig | (1 << 2));  // HI = 1
    Wire.endTransmission();

    Wire.beginTransmission(0x27);
    Wire.write(lowerNibble | serialDataConfig);  // LO = 0
    Wire.endTransmission();
}


void setup() {
    Serial.begin(115200);
    Wire.begin();
    lcd.init();
    delay(2);
}

void loop() {
    // Write code that takes Serial input and displays it to the LCD. Do NOT use any
    // functions from the LiquidCrystal I2C library here.

    if (Serial.available() > 0) {
        // when new input comes in clear and then write
        // clears only after buffer confirmed empty meaning next >0 happens when new
        if (newMessage) {
            uint8_t command = 0x01;  // clear display
            sendCommand(command);
            newMessage = false;
        }

        // get data in serial buffer
        uint8_t serialData = Serial.read(); 
        sendData(serialData); 

    } else {
        // once were done processing that last serial enter next enter is a new message
        newMessage = true;
    }
}
