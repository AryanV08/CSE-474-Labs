#include "driver/gpio.h"
#include "soc/io_mux_reg.h"
#include "soc/gpio_reg.h"
#include "soc/gpio_periph.h"

#define led_gpio 21
#define p_resistor 1 // taking in input, lower value = higher brightness


void setup() {
 // put your setup code here, to run once:


//PIN_FUNC_SELECT(GPIO_PIN_MUX_REG[led_gpio],PIN_FUNC_GPIO); // dont need to set becasue its not doing GPIO its a PWM signal that does HIGH/LOW at the given frequency in ledcAttach eg 5000 cycles per second
PIN_FUNC_SELECT(GPIO_PIN_MUX_REG[p_resistor],PIN_FUNC_GPIO);




// one output one input
//*((volatile uint32_t *)GPIO_ENABLE_REG) |= 1<<led_gpio;// left shift to correct bit set, 1 or 0 will default to 1
*((volatile uint32_t *)GPIO_ENABLE_REG) &= ~(1<<p_resistor);// auto comes cleared but for good practice




//prep the pin no output
ledcAttach(led_gpio, 5000, 12); // pin , 5000 0n/off cycles per second, 12 bits precision aka brightness levels 4095 whcih is coincidentally the max value of the p_resistor, can make 1-1 correspondence with the brightness
    // this operation does the equvalent of setting up a gpio but for a PWM signal


//get a baseline brightness


Serial.begin(115200);




}
void loop() {
 // put your main code here, to run repeatedly:
  Serial.printf("\n");
  Serial.print(analogRead(p_resistor));


    uint32_t sensor = analogRead(p_resistor);
    uint32_t duty   = 4095 - sensor;  // invert the relationship less light = brighter led
   
    //make the pin actually send out the PWM signal with the ON depending on the duty cycle  
    ledcWrite(led_gpio, duty); // the duty value is used to calculate the percentage of the time the led should be ON during the ON/OFF cycles per sec aka the frequency, in our case the frequency is 5000 on/off cycles per second
                              // if the duty value is 0 meaning its dark in a room wiht no light the duty cyucle in our code will be (4095-0 / 2^12 - 1 ) x %100 = %100 meaning our led will be on during the 5000 cycles/ sec giving peak brightness
                              // the oppisite is true if it reads a lot of light if it reads 4095 the duty cycle is 0% meaning the led will be off which is what they asked the darker the reading the brigher the led






 }












