// Filename: Lab2Part2
// Authors: Aryan Verma and David Montiel
// Date: 10/20/2025
// Description: The file directly interacts with the ESP32's timer registers to control an LED

// =================== Includes ==================
#include "driver/gpio.h"
#include "soc/io_mux_reg.h"
#include "soc/gpio_reg.h"
#include "soc/gpio_periph.h"
#include "soc/timer_group_reg.h"

// ================ Macros ==================
#define GPIO_PIN 5
#define LED_TOGGLE_INTERVAL 2000000
#define TIMER_INCREMENT_MODE (1<<30) 
#define TIMER_ENABLE (1 << 31)
#define TIMER_DIVIDER_VALUE 80

void setup() {
  // Set GPIO_PIN function to GPIO using MUX macro
  PIN_FUNC_SELECT(GPIO_PIN_MUX_REG[GPIO_PIN], PIN_FUNC_GPIO);

  // Enable GPIO_PIN as output
  *((volatile uint32_t *)GPIO_ENABLE_REG) |= (1 << GPIO_PIN) ;

  // Configure timer
  uint32_t timer_config = (TIMER_DIVIDER_VALUE << 13);

  // Clock divider
  timer_config |= TIMER_INCREMENT_MODE;

  // Set increment mode and enable timer
  timer_config |= TIMER_ENABLE;

  // Write config to timer register
  *((volatile uint32_t *)TIMG_T0CONFIG_REG(0)) = timer_config;

  // Trigger a timer update to load settings
  *((volatile uint32_t *)TIMG_T0UPDATE_REG(0)) = 1;
}

void loop() {
  // Track last toggle time
  static uint32_t last_toggle_time = 0;

  // Read current timer value
  uint32_t current_time = 0;
  current_time = *((volatile uint32_t *)TIMG_T0LO_REG((0)));

  // Check if toggle interval has passed
  if ((current_time - last_toggle_time) >= LED_TOGGLE_INTERVAL) {
    // TODO: Read current GPIO output state
    uint32_t gpio_out = 0;
    gpio_out = *((volatile uint32_t *)GPIO_OUT_REG);

    // Toggle GPIO_PIN using XOR
    *((volatile uint32_t *)GPIO_OUT_REG) = gpio_out ^ (1 << 5);

    // Update last_toggle_time
    last_toggle_time = current_time;
  }

  // Refresh timer counter value
  *((volatile uint32_t *)TIMG_T0UPDATE_REG(0)) = 1;
}
