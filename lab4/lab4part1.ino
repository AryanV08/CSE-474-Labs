// Filename: Project1.ino
// Authors: David Montiel, Aryran Verma 
// Date: 11/18/2025
// Description: This file implements a STRF scheduler that works with freeRTOS to manage the 
// schedluling of 3 different tasks 

// ========== Includes ==========
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include <LiquidCrystal_I2C.h>

// ========== Macros ==========
#define ledGpio 2
#define delay 500

// ========== Global Variables ==========
// Execution times (in ticks, converted from ms)
const TickType_t ledTaskExecutionTime = 500 / portTICK_PERIOD_MS;      // 500ms allowed runtime
const TickType_t counterTaskExecutionTime = 2000 / portTICK_PERIOD_MS; // 2 seconds allowed runtime
const TickType_t alphabetTaskExecutionTime = 13000 / portTICK_PERIOD_MS; // 13 seconds allowed runtime

// Remaining execution times
volatile TickType_t remainingLedTime = ledTaskExecutionTime;
volatile TickType_t remainingCounterTime = counterTaskExecutionTime;
volatile TickType_t remainingAlphabetTime = alphabetTaskExecutionTime;

// Next time a task is allowed to be considered for scheduling.
// This prevents tasks from running continuously without respecting their slice.
volatile uint32_t nextLedWakeup;
volatile uint32_t nextCounterWakeup;
volatile uint32_t nextAlphabetWakeup;

LiquidCrystal_I2C lcd(0x27, 16, 2);

// FreeRTOS task handles
TaskHandle_t ledHandle;
TaskHandle_t counterHandle;
TaskHandle_t alphabetHandle;
TaskHandle_t schedulerHandle;

// ========== Function Prototypes ==========
void ledTask(void *arg);
void counterTask(void *arg);
void alphabetTask(void *arg);
TaskHandle_t findShortestTask();
void scheduleTasks(void *arg);

// Name: ledTask
// Description: Toggles the LED every cycle, updates its remaining execution time,
// schedules its next wakeup timestamp, and then suspends itself so the
// scheduler can decide when to resume it based on SRTF logic
void ledTask(void *arg) {
  pinMode(ledGpio, OUTPUT);
  bool state = false;

  while (1) {
    state = !state;                   
    digitalWrite(ledGpio, state);     // flip LED on/off

    // Subtract the time slice from our remaining runtime
    if (remainingLedTime > delay){
      remainingLedTime -= delay;      
    } else {
      remainingLedTime = 0;           // task finished its total allotted time
    }

    uint32_t current = millis();
    nextLedWakeup = current + delay;    // do not consider this task again until 500ms passes

    vTaskSuspend(NULL); // suspend yourself; scheduler will wake you later
  }
}

// Name: counterTask
// Description: Displays an incrementing count on the LCD, wraps back to 1 after 20,
// updates this task’s remaining execution time, schedules its next wakeup timestamp,
// and suspends itself so the scheduler can resume it when it becomes the shortest remaining time task
void counterTask(void *arg) {
  int value = 1;

  while (1) {
    lcd.clear();
    lcd.print("Count: ");
    lcd.print(value);

    // Simple wrap-around counter
    if (value >= 20) {
      value = 1;
    } else {
      value++;
    }

    // Subtract our time slice
    if (remainingCounterTime > 100){
      remainingCounterTime -= 100;
    } else {
      remainingCounterTime = 0;
    }

    uint32_t current = millis();
    nextCounterWakeup = current + 100; // eligible again after 100ms

    vTaskSuspend(NULL); // scheduler will resume when appropriate
  }
}

// Name: alphabetTask
// Description: Prints letters A–Z in sequence over Serial, wraps back to 'A' and
// starts a new line after 'Z', updates its remaining execution time, schedules its next wakeup timestamp,
// and suspends itself so the scheduler can resume it when it has the shortest remaining time
void alphabetTask(void *arg) {
  char letter = 'A';

  while (1) {
    Serial.print(letter);
    Serial.print(", ");

    // wrap-around alphabet sequence
    letter++;
    if (letter > 'Z') {
      letter = 'A';
      Serial.println(); // start next alphabet cycle on a new line
    }

    // subtract our time slice
    if (remainingAlphabetTime > delay){
      remainingAlphabetTime -= delay;     
    } else {
      remainingAlphabetTime = 0;
    }

    uint32_t current = millis();
    nextAlphabetWakeup = current + 500; // eligible for scheduling after 500ms

    vTaskSuspend(NULL); // self-suspend until scheduler wakes us
  }
}

// Name: findShortestTask
// Description: Checks which task is eligible to run (remaining time > 0 and past
// its next wakeup time), then returns the handle of the task with the smallest remaining execution time
TaskHandle_t findShortestTask() {
  TickType_t minTime = portMAX_DELAY;  // start with a very large value
  TaskHandle_t shortest = NULL;
  uint32_t currentTime = millis();

  // check LED task eligibility + shortest time
  if (remainingLedTime > 0 && currentTime >= nextLedWakeup && remainingLedTime < minTime) {
    minTime = remainingLedTime;
    shortest = ledHandle;
  }
  // check counter task
  if (remainingCounterTime > 0 && currentTime >= nextCounterWakeup && remainingCounterTime < minTime) {
    minTime = remainingCounterTime;
    shortest = counterHandle;
  }
  // check alphabet task
  if (remainingAlphabetTime > 0 && currentTime >= nextAlphabetWakeup && remainingAlphabetTime < minTime) {
    minTime = remainingAlphabetTime;
    shortest = alphabetHandle;
  }

  return shortest; // NULL if nothing is eligible
}

// Name: scheduleTasks
// Description: Implements the SRTF scheduler by selecting the task with the
// shortest remaining time, suspending all tasks, and resuming only
// the eligible shortest one. It also resets tasks that finish and
// inserts a small delay to let the chosen task run 
void scheduleTasks(void *arg) {
  while (1) {

    // Pick the task with the smallest remaining time
    TaskHandle_t shortest = findShortestTask();

    // Suspend all tasks every iteration to enforce strict control
    vTaskSuspend(ledHandle);
    vTaskSuspend(counterHandle);
    vTaskSuspend(alphabetHandle);

    // Resume only the shortest-time-remaining task
    if (shortest != NULL) {
      vTaskResume(shortest);
    }

    // If any task reached zero, reset its remaining time 
    if (remainingLedTime == 0)       remainingLedTime = ledTaskExecutionTime;
    if (remainingCounterTime == 0)   remainingCounterTime = counterTaskExecutionTime;
    if (remainingAlphabetTime == 0)  remainingAlphabetTime = alphabetTaskExecutionTime;

    vTaskDelay(1); // give resumed task a tiny timeslice before the next scheduling cycle
  }
}

void setup() {
  Serial.begin(115200);
  lcd.init();
  lcd.backlight();

  // Create tasks on core 0
  xTaskCreatePinnedToCore(ledTask,        "LED Task",      2048, NULL, 1, &ledHandle,       0);
  xTaskCreatePinnedToCore(counterTask,    "Counter Task",  2048, NULL, 1, &counterHandle,   0);
  xTaskCreatePinnedToCore(alphabetTask,   "Alphabet Task", 2048, NULL, 1, &alphabetHandle,  0);
  xTaskCreatePinnedToCore(scheduleTasks,  "Scheduler",     2048, NULL, 4, &schedulerHandle, 0);
}

void loop() {}
