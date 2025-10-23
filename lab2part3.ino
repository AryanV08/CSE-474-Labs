// Filename: Lab2Part3
// Authors: Aryan Verma and David Montiel
// Date: 10/20/2025
// Description: The file uses a photoresistor to control the brightness of an LED.

// ====================== Includes =================
#include "driver/gpio.h"
#include "soc/io_mux_reg.h"
#include "soc/gpio_reg.h"
#include "soc/gpio_periph.h"
#include "soc/timer_group_reg.h"

// =================== Macros ====================

#define led_gpio 21
#define p_resistor 1 // taking in input, lower value = higher brightness
#define TIMER_INCREMENT_MODE 1<<30 
#define TIMER_ENABLE 1<<31 
#define DELAY 1500000

void setup() {
Serial.begin(115200);

 PIN_FUNC_SELECT(GPIO_PIN_MUX_REG[p_resistor],PIN_FUNC_GPIO);

 *((volatile uint32_t *)GPIO_ENABLE_REG) &= ~(1<<p_resistor);// auto comes cleared but for good practice

//prep the pin to output
 ledcAttach(led_gpio, 5000, 12); // pin 21 , 5000 0n/off cycles per second, 12 bits precision aka brightness levels 4095 whcih is coincidentally the max value of the p_resistor, can make 1-1 correspondence with the brightness

//Timer Configuration for delay 

//mock 32 bit register all zeroed out 
 uint32_t timer_config1 = 0;

 timer_config1 |= 80<<13; //divide the mhz by 80 to get 1mhz so were in ms 

  // Set increment mode and enable timer
 timer_config1 |= (TIMER_INCREMENT_MODE|TIMER_ENABLE);

  //  Write the config to timer register
   *((volatile uint32_t *)TIMG_T0CONFIG_REG(0)) = timer_config1;

  // Trigger a timer update to load settings
   *((volatile uint32_t *)TIMG_T0UPDATE_REG(0)) = 1;



}
//added 1.5 second delay between sensor readings to notice led change
void loop() {
  
  uint32_t current_time=0; 

  //get a snapshot of current time 
  *((volatile uint32_t *)TIMG_T0UPDATE_REG(0)) = 1;

  current_time= *((volatile uint32_t *) TIMG_T0LO_REG(0)); // get the most recent time from register 
  
  static uint32_t last_led_update = 0; // must remain between loop calls 

//update LED brightness only after the delay interval has passed to avoid rapid sensor updates  
  if(current_time - last_led_update >= DELAY){
  Serial.print(analogRead(p_resistor));
  Serial.printf("\n");
        
    uint32_t sensor = analogRead(p_resistor);
    uint32_t duty   = 4095 - sensor;  // invert the relationship less light = brighter led
   
    //make the pin actually send out the PWM signal with the ON time depending on the duty cycle calculated via DUTY value 
    ledcWrite(led_gpio, duty); 
  
    last_led_update = current_time; //update to keep loop going 

  }
}
   
    













