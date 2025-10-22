// Filename: Lab2Part1
// Author: Aryan Verma and David Montiel
// Date: 10/20/2025
// Description: This file compares the execution speed of Arduino library function vs direct register access


// =================== Includes =======================
#include "driver/gpio.h"
#include "soc/io_mux_reg.h"
#include "soc/gpio_reg.h"
#include "soc/gpio_periph.h"

// ==================== Macro ========================
#define GPIO_PIN 5 // GPIO5

void setup() {
 // Set a pin as output
 Serial.begin(115200);

 // Mark pin 5 as output with Arduino's library function
 pinMode(GPIO_PIN, OUTPUT);
 // Pin 5 is set to be a general purpose input/output pin (direct register access method)
 PIN_FUNC_SELECT(GPIO_PIN_MUX_REG[GPIO_PIN], PIN_FUNC_GPIO);

// Mark pin 5 as output with direct register access
*((volatile uint32_t *)GPIO_ENABLE_REG) |= (1<<GPIO_PIN);
}
void loop() {
 // ================> TODO:
 // For 1000 repetitions:
 //		Measure time to:
 //        		- Turn pin's output to HIGH
 //        		- Turn pin's output to LOW
 // Print out total time to the serial monitor
 // 1 second delay

   unsigned long start_time_library = micros();
  // Measures the time for digitalWrite() to change the output voltage for 1000 repetitions
   for(int i = 0; i < 1000; i++) {
    digitalWrite(GPIO_PIN, HIGH);
    digitalWrite(GPIO_PIN, LOW);
  }
  unsigned long end_time_library = micros();

  unsigned long start_time_register = micros();
  // Measures the time using direct register access to change the output voltage for 1000 repetitions
  for(int i = 0; i < 1000; i++) {
    *((volatile uint32_t *)GPIO_OUT_REG) |= (1<<GPIO_PIN);
    *((volatile uint32_t *)GPIO_OUT_REG) &= ~(1<<GPIO_PIN);
  }
  unsigned long end_time_register = micros();


  Serial.print("Total microseconds for direct register access: ");
  Serial.println(end_time_register - start_time_register);
  Serial.print("Total microseconds for arduino library function: ");
  Serial.println(end_time_library - start_time_library);

  delay(1000);
}
