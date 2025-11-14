#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "driver/gpio.h"
#include <LiquidCrystal_I2C.h>

#define ledGpio 2
// Total times for tasks
const TickType_t ledTaskExecutionTime = 500 / portTICK_PERIOD_MS;      // 500 ms
const TickType_t counterTaskExecutionTime = 2000 / portTICK_PERIOD_MS; // 2 seconds
const TickType_t alphabetTaskExecutionTime = 13000 / portTICK_PERIOD_MS; // 13 seconds
// Remaining Execution Times
volatile TickType_t remainingLedTime = ledTaskExecutionTime;
volatile TickType_t remainingCounterTime = counterTaskExecutionTime;
volatile TickType_t remainingAlphabetTime = alphabetTaskExecutionTime;

//Initalize the LED 
LiquidCrystal_I2C lcd(0x27, 16, 2); 

// GLOBAL handles (must be global)
TaskHandle_t ledHandle;
TaskHandle_t counterHandle;
TaskHandle_t alphabetHandle;
TaskHandle_t schedulerHandle;

void ledTask(void *arg) {
  pinMode(ledGpio, OUTPUT);
  bool state=false;
  while(1){
  TickType_t before = xTaskGetTickCount();

  state = !state;
  digitalWrite(ledGpio, state);
  TickType_t after = xTaskGetTickCount();
  remainingLedTime -= (after - before);   
  vTaskDelay(500/ portTICK_PERIOD_MS);// let other low higher remaining tasks run
  }

}


void counterTask(void *arg) {
 // TODO: Print out an incrementing counter to your LCD, and 
 //       update remaining time for this 
 int value =1; 
 while(1){
  TickType_t before = xTaskGetTickCount();


    lcd.clear();
    lcd.print("Count: ");
    lcd.print(value);

    //increment counter for next time
    value++;
    if (value > 20) {
        value = 1; 
    }

  TickType_t after = xTaskGetTickCount();
  remainingCounterTime -= (after - before);  
  vTaskDelay(500/ portTICK_PERIOD_MS);

 }
}


void alphabetTask(void *arg) {
 // TODO: Print out the alphabet to Serial, and update remaining
 //       time for this 
 char letter = 'A';

 while(1){
  TickType_t before = xTaskGetTickCount();

    //one letter at a time
    Serial.print(letter);
    Serial.print(", ");

    //move to next letter
    letter++;
    if (letter > 'Z') {
        letter = 'A';
        Serial.println(); // new line after finishing Z
    }

  TickType_t after = xTaskGetTickCount();
  remainingAlphabetTime -= (after - before);   
  vTaskDelay(500/ portTICK_PERIOD_MS);

 }
}


// finds shortest task based on remaining time
TaskHandle_t findShortestTask(void){
  TickType_t minTime= remainingLedTime; //baseline shortest, otheres may be shorter but good baseline
  TaskHandle_t shortest = ledHandle; 

  if(remainingCounterTime < minTime){
    minTime = remainingCounterTime; 
    shortest = counterHandle; 
  }

  if (remainingAlphabetTime < minTime) {
    minTime = remainingAlphabetTime;
    shortest = alphabetHandle;
  }

  return shortest;
}



//prioirity 4 highest 
void scheduleTasks(void *arg) {
   // TODO: Implement SRTF scheduling logic. This function should select the task with 
   //       the shortest remaining time and run it. Once a task completes it should 
   //       reset its remaining time.

   //all start suspended theoretically 
   vTaskSuspend(ledHandle);
   vTaskSuspend(counterHandle);
   vTaskSuspend(alphabetHandle);

   TaskHandle_t currentTask = NULL; 

  while(1){

    TaskHandle_t shortestRemainingTask = findShortestTask();

    if(shortestRemainingTask != currentTask){ // works based on first because led is shortest to start 

      if(currentTask != NULL){//prevents suspending null
        vTaskSuspend(currentTask);
      }

      //only 1 will resume aka the shortest goes back to ready 
      vTaskResume(shortestRemainingTask);
      currentTask = shortestRemainingTask; 

      if (currentTask == ledHandle && remainingLedTime == 0) {
            remainingLedTime = ledTaskExecutionTime;  // reset
      }

      if (currentTask == counterHandle && remainingCounterTime == 0) {
            remainingCounterTime = counterTaskExecutionTime;
      }

      if (currentTask == alphabetHandle && remainingAlphabetTime == 0) {
            remainingAlphabetTime = alphabetTaskExecutionTime;
      }

    }

    // let the shortest task run by giving up control for one tick 
    vTaskDelay(1);  // 1ms tick of scheduler

  }

}


void setup() {
   // TODO: Create 4 tasks and pin them to core 0:
   //          1. A scheduler that handles the scheduling of the other three tasks
   //          2. Blink an LED
   //          3. Print a counter to the LCD
   //          4. Print the alphabet to Serial

  Serial.begin(115200);
  lcd.init();
  lcd.backlight();
  delay(2);

  //pin tasks to core 
  xTaskCreatePinnedToCore(scheduleTasks, "Scheduler",2048, NULL,4,&schedulerHandle,0); 

  // all other tasks have the same priority, thus chosen based soley off shortest remaining time 
  xTaskCreatePinnedToCore(ledTask, "Led Task", 1024, NULL,1, &ledHandle,0);
  xTaskCreatePinnedToCore(counterTask, "Counter Task", 2048, NULL,1, &counterHandle,0);
  xTaskCreatePinnedToCore(alphabetTask, "Alphabet Task", 2048, NULL,1, &alphabetHandle,0);

}

void loop() {}
