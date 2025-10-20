// Filename: Lab2Part4
// Authors: Aryan Verma and David Montiel
// Date: 10/20/2025
// Description: The file uses a photresistor to control a buzzer depending on the ambienet light level.

#include "driver/gpio.h"
#include "soc/io_mux_reg.h"
#include "soc/gpio_reg.h"
#include "soc/gpio_periph.h"
#include "soc/timer_group_reg.h"


// =================== Macros ====================

#define led_gpio 21
#define p_resistor 1 // taking in input, lower value = higher brightness
#define light_threshhold 3000 // if light goes below this threshhold start the buzzer sequence


// timer settings
#define buzzer_segment 5000000 // in order for this to run the other 2 tones have to have run + the time it took to run so 15 seconds 5 second buzz each, wrap back around
#define total_time 15000000


#define TIMER_INCREMENT_MODE 1<<30 // flip bit 30 which corresponds to increment in register to 1, count up
#define TIMER_ENABLE 1<<31 //flip bit 31 which corresponds to enableing the timer




void setup() {
  // put your setup code here, to run once:

  //setup p_resistor as gpio
  PIN_FUNC_SELECT(GPIO_PIN_MUX_REG[p_resistor],PIN_FUNC_GPIO);

  //set as input
  *((volatile uint32_t *)GPIO_ENABLE_REG) &= ~(1<<p_resistor);// auto comes cleared but for good practice
 
  //setup timer
  uint32_t timer_config=0; // mock 32 bit register for settings
 
  // clock divider
   timer_config |=80<<13;
  //setup increment mode and enable timer    
   timer_config |= (TIMER_INCREMENT_MODE|TIMER_ENABLE);


  // write settings to physical timer @ respective address
   *((volatile uint32_t *)TIMG_T0CONFIG_REG(0)) = timer_config;  // timer 0 group 0


  // update to configure settings and have a baseline time to go off of in lo register
    *((volatile uint32_t *)TIMG_T0UPDATE_REG(0)) = 1;


  Serial.begin(115200);
}


void loop() {
  Serial.print(analogRead(p_resistor));
  Serial.printf("\n");


      uint32_t sensor = analogRead(p_resistor);
      // trackers, need to keep last time between loop() iterations thus static
      //update snapshot once per loop :
      *((volatile uint32_t *)TIMG_T0UPDATE_REG(0)) = 1;
      uint32_t current_time=0;
      current_time= *((volatile uint32_t * )TIMG_T0LO_REG(0)); //get last time snapshot


      static uint32_t sequence_start_time=0;


      static boolean run_sequence = false; // make static so we can wait aka for delay


      //current goal replace delay(5000) using hardware time differences
      if(sensor>3000 && ! run_sequence){
        run_sequence=true;
        sequence_start_time = current_time;
        Serial.printf("Sequence has started \n: ");
      }


      if(run_sequence){
         uint32_t elapsed = current_time - sequence_start_time; // initially 0
         
        if(elapsed< buzzer_segment){
         
          Serial.println("Threshold met. Playing 500 Hz tone.");
         
          ledcAttach(led_gpio,500,12);
         
          ledcWrite(led_gpio,2048); //50% frequency duty cycle  changes loudness/intensity  


       
        }else if (elapsed < 2 * buzzer_segment){
            Serial.println("Threshold met. Playing 1000 Hz tone.");
           
            ledcAttach(led_gpio,1000,12);
           
            ledcWrite(led_gpio,3000); //change the duty cycle


        }else if (elapsed < 3 * buzzer_segment){
            Serial.println("Threshold met. Playing 2000 Hz tone.");
           
            ledcAttach(led_gpio,2000,12);
           
            ledcWrite(led_gpio,4000);// swtich duty cycles to tell them apart




        }else{
          // this means were over the 15 second sequence stop the buzzer and reset flag
          ledcWrite(led_gpio, 0);
          run_sequence=false;
          Serial.printf("Sequence is over\n");
        }
      }
}



