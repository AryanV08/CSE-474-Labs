// File: lab3part3.ino
// Authors: Aryan Verma and David Montiel
// Date: 10/20/2025
// Description: This file uses mutliple interrupts which includes:
// a hardware timer interrupt, button interrupt, and a BLE interrupt.

// ============== Includes ==========
#include <BLEDevice.h>
#include <BLEUtils.h>
#include <BLEServer.h>
#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#include "esp_timer.h"

// =============== Macros ===============
#define BUTTON_PIN 19
#define TOGGLE_INTERVAL_US 1000000  // Timer interval set to 1,000,000 microseconds = 1 second


// Generates random Service and Characteristic UUIDs at https://www.uuidgenerator.net/
#define SERVICE_UUID        "360fdc61-78ee-472f-b5f3-931baae7dc80"
#define CHARACTERISTIC_UUID "d0261ff2-c295-478f-8943-8737b7b48c1c"

// =================== Global Variables ========================
LiquidCrystal_I2C lcd(0x27, 16, 2);
esp_timer_handle_t periodic_timer; // handle for esp_timer
volatile bool timerFlag  = false; 
volatile bool buttonFlag = false;
volatile bool bleFlag    = false;
volatile unsigned long counter = 0;
volatile bool countingActive = true;  // flag to control when counting runs
volatile unsigned long lastInterruptTime = 0;
const unsigned long debounceDelay = 200;

// Used for determining the seconds before timerISR triggers again instead of just using delay so nothing freezes
unsigned long pauseStart = 0; 
const unsigned long pauseLength = 2000;


class MyCallbacks: public BLECharacteristicCallbacks {
// Callback function 
// Name: onWrite
// Implements BLE behavior to trigger "New Message!" on LCD
  void onWrite(BLECharacteristic *pCharacteristic)  {
    bleFlag = true;
  }
};

// Name: onTimer
// Description: Interrupt Service Routine (ISR) triggered by the hardware timer
//              once per second. Sets a flag to signal the main loop to increment and display the counter.
void IRAM_ATTR onTimer(void* arg) { // 
  if (countingActive) {
    timerFlag = true;
  }
}


// Name: onButtonPress
// Description: ISR triggered by the button press.
//              Sets a flag to signal the main loop to display
//              "Button Pressed" on the LCD after a valid (debounced) press.
void IRAM_ATTR onButtonPress() {
  unsigned long currentTime = millis();
  // Handles case where button press happens after the debounce delay
  if(currentTime - lastInterruptTime > debounceDelay) {
      buttonFlag = true;
      lastInterruptTime = currentTime;
  }
}

void setup() {
  Serial.begin(115200);

  // Initialize LCD display 
  lcd.init();
  lcd.backlight();
  lcd.clear();
  lcd.print("Initializing...");

  // Setup BLE peripheral
  BLEDevice::init("MyESP32");
  BLEServer *pServer = BLEDevice::createServer();
  BLEService *pService = pServer->createService(SERVICE_UUID);
  BLECharacteristic *pCharacteristic = pService->createCharacteristic(
    CHARACTERISTIC_UUID,
    BLECharacteristic::PROPERTY_READ | BLECharacteristic::PROPERTY_WRITE
  );
  pCharacteristic->setCallbacks(new MyCallbacks());
  pService->start();
  BLEAdvertising *pAdvertising = pServer->getAdvertising();
  pAdvertising->start();

  // Configures button pin and attach interrupt, only external interrupt
  pinMode(BUTTON_PIN, INPUT_PULLUP);
  attachInterrupt(digitalPinToInterrupt(BUTTON_PIN), onButtonPress, FALLING); // 

  // Defines a configuration structure for the ESP32-S3 high-resolution timer.
  // This structure tells the system what function to call, how to call it, and what to name the timer.
  // The type `esp_timer_create_args_t` is defined in the ESP-IDF and is required when creating an `esp_timer` instance.

  const esp_timer_create_args_t timer_args = {
  // This is the function that will be called every time the timer interval elapses.
  // The function must match the signature and`in our case, this is the `onTimer()` function that sets a flag to update the counter
  // and refresh the LCD display once per second.
    .callback = &onTimer,

    // This is an optional argument that can be passed to the callback.
    // We are not using it in this case, so we set it to `nullptr`.
    .arg = nullptr,

   // Specifies how the timer ISR is dispatched.
   // We use ESP_TIMER_TASK so it’s safe to set flags and coordinate LCD updates.
    .dispatch_method = ESP_TIMER_TASK,  

    // This is a human-readable name for the timer, useful for debugging or diagnostics via tools.
    .name = "Counter_Timer"
  };

  // Call the `esp_timer_create()` function, passing in the timer configuration (`timer_args`)
  // and a pointer to our `periodic_timer` handle, which will store the timer instance.
  // This function allocates resources and prepares the timer to run.
  if (esp_timer_create(&timer_args, &periodic_timer) != ESP_OK) {
    // If the timer creation fails, we print an error and halt execution.
    Serial.println("Failed to create timer!");
    while (true);
  }
  // Now we start the timer using `esp_timer_start_periodic()`.
  // This sets the timer to fire repeatedly every TOGGLE_INTERVAL_US microseconds
  // The callback defined above (`onTimer`) will run on each interval.
  if (esp_timer_start_periodic(periodic_timer, TOGGLE_INTERVAL_US) != ESP_OK) {
    Serial.println("Failed to start timer!");
    while (true);
  }

  lcd.clear();
  lcd.print("Ready!");
}

// Makes the display reset so that it utilizes top and bottom side of the lcd screen
// and displays correct contents
void changeDisplay() {
  lcd.setCursor(0, 1);
    lcd.print("                ");  // clear bottom line
    lcd.setCursor(0, 1);
}

void loop() {
  unsigned long track = millis();

  // Handle 1-second counter update
  if (timerFlag) {
    timerFlag = false;
    counter++;
    lcd.setCursor(0, 0);
    lcd.print("Counter: ");
    lcd.print(counter);
    lcd.print("   ");  // erase leftover digits
  }

  // Handle button press message
  if (buttonFlag) {
    buttonFlag = false;
    countingActive = false;  // pause counter
    pauseStart = track;      // record pause start time
    changeDisplay();
    lcd.print("Button Pressed");
  }

  // Handle BLE message event
  if (bleFlag) {
    bleFlag = false;
    countingActive = false;
    pauseStart = track;
    changeDisplay();
    lcd.print("New Message!");
  }

  // Resume counting after 2-second pause
  if (!countingActive && (track - pauseStart) >= pauseLength) {
    countingActive = true;
    lcd.setCursor(0, 1);
    lcd.print("                "); // clear bottom line
  }
  delay(5);  // tiny LCD refresh delay
}


