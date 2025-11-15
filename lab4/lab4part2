// File: lab4part2
// Authors: Aryan Verma and David Montiel
// Date: 11/11/25
// Description: This file utilizes the dual core architecture of the ESP32 to 
//              capture and process real time sensor dataz, with binary semaphores
//              to synchronize tas


// ================ Includes ===================
#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#include <Arduino.h>
#include <math.h> 

// ============= Macros =======================
#define ledPin 19
#define photoresistorPin 1
#define windowSize 5
// LCD setup
LiquidCrystal_I2C lcd(0x27, 16, 2);

// Semaphore for synchronization

// ================= Global Variables =================
SemaphoreHandle_t lightDataSemaphore;

// Light level variables
int lightLevel = 0;
int sma = 0;
int smaArray[windowSize] = {0};
int smaIndex = 0;

const int LIGHT_THRESHOLD_HIGH = 3800;
const int LIGHT_THRESHOLD_LOW = 300;

void setup() {
  // Initialize serial communication
  Serial.begin(115200);
  
  // Initialize LCD
  lcd.init();
  lcd.backlight();
  
  // Initialize pin
  pinMode(ledPin, OUTPUT);
  
  // Create binary semaphore
  lightDataSemaphore = xSemaphoreCreateBinary();


  // Unlocks semaphore to allow tasks to proceed, turns it to 1 to set it as default is 0
  xSemaphoreGive(lightDataSemaphore);
  
  // Create tasks 
  xTaskCreatePinnedToCore(vTaskDetector, "Light Detector", 2048, NULL, 1, NULL, 0);
  xTaskCreatePinnedToCore(vTaskLCD, "LCD Display", 2048, NULL, 1, NULL, 0);
  // Assigned to core 1
  xTaskCreatePinnedToCore(vTaskAnomalyAlarm, "Anomaly Alarm", 2048, NULL, 1, NULL, 1);
  xTaskCreatePinnedToCore(vTaskPrimeCalculation, "Prime Calculation", 2048, NULL, 1, NULL, 1);
}

void vTaskDetector(void *args) {  
  while (1) {
    // Need to wait for semaphore to ensure this is the only taks accessing the shared data
    if (xSemaphoreTake(lightDataSemaphore, portMAX_DELAY) == pdTRUE) {
    lightLevel = analogRead(photoresistorPin);
    // Updates SMA array with new light level
    smaArray[smaIndex] = lightLevel;
    
    // Recalculates SMA
    sma = 0;
    for (int i = 0; i < windowSize; i++) {
      sma += smaArray[i];
    }
    // Takes average of sma array to get final SMA value
    sma /= windowSize;
    
    // Wraps around so that window size is maintained
    smaIndex = (smaIndex + 1) % windowSize;

    // Signals that data is ready for other tasks to use.
    xSemaphoreGive(lightDataSemaphore);
    }
    // Shortest delay as need light levels for other tasks more quickly
    vTaskDelay(100 / portTICK_PERIOD_MS);
  }
}

// Name: vTaskLCD
// Description: If data has changed, LCD will be updated with the new light level
// and SMA.
void vTaskLCD(void *args) {
  
  while (1) {
    // Need to wait for semaphore to ensure this is the only taks accessing the shared data
    if (xSemaphoreTake(lightDataSemaphore, portMAX_DELAY) == pdTRUE) {
      lcd.clear();
      lcd.setCursor(0, 0);
      lcd.print("Light: ");
      lcd.print(lightLevel);
      lcd.setCursor(0, 1);
      lcd.print("SMA: ");
      lcd.print(sma);

      xSemaphoreGive(lightDataSemaphore);
    }
    // Longer delay since LCD only needs to update when data changes so not
    // as frequent as other tasks.
    vTaskDelay(500 / portTICK_PERIOD_MS);
  }
}

// Name: vTaskAnomalyAlarm
// Description: If SMA indicates a light anomally where it's outside
// the defined thresholds, the LED will flash three times with a 2-second
// pause in-between
void vTaskAnomalyAlarm(void *args) {
  while (1) {
    // Need to wait for semaphore to ensure this is the only taks accessing the shared data
    if (xSemaphoreTake(lightDataSemaphore, portMAX_DELAY) == pdTRUE) {
      if (sma > LIGHT_THRESHOLD_HIGH || sma < LIGHT_THRESHOLD_LOW) {
        for (int i = 0; i < 3; i++) {
          digitalWrite(ledPin, HIGH);
          vTaskDelay(1000 / portTICK_PERIOD_MS);
          digitalWrite(ledPin, LOW);
          vTaskDelay(1000 / portTICK_PERIOD_MS);
        }
      }
      xSemaphoreGive(lightDataSemaphore);
    }
    // Delay to reduce CPU Load (checks every second for anomalies)
    vTaskDelay(1000 / portTICK_PERIOD_MS);
  }
}

// Name: vTaskPrimeCalculation
// Description: Calculates all prime numbers from 2 up to a limit of 5000
// When a prime is discovered, it is printed to the serial monitor
void vTaskPrimeCalculation(void *args) {
  while (1) {
    for (int i = 2; i <= 5000; i++) {
      bool prime = true;
      for (int j = 2; j <= sqrt(i); j++) {
        if (i % j == 0) {
          prime = false;
          break;
        }
      }
      if (prime) {
        Serial.println(i);
        vTaskDelay(50 / portTICK_PERIOD_MS);  // Delay so numbers aren't so quickly printed
      }
    }
    vTaskDelay(1000 / portTICK_PERIOD_MS);
  }
}

void loop() {

}


