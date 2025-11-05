// Filename: lab3part2.ino
// Author: David Montiel, Aryan Verma
// Date: 11/03/25
// Description: This file implements 4 tasks, blinking an led, counting on an lcd
// playing different frequencies on a buzzer with a 1-1 correspondence to an led, and printing the alphabet on the serial monitor
// all these tasks are then managed by a prority scheduler and executed accordingly 


// ========== Includes ==========
#include <stdio.h>
#include <string.h>
#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#include "driver/gpio.h"
#include "soc/io_mux_reg.h"
#include "soc/gpio_reg.h"
#include "soc/gpio_periph.h"
#include "soc/timer_group_reg.h"

// ========== Macros ==========
#define numTasks 4
#define task1Gpio 21
#define task3LedGpio 15
#define buzzerGpio 36
#define individualSegmentTime 1000

// ========== Type Definitions ==========
typedef enum {
    READY,  // aka ready
    FINISHED
} TaskState;

typedef struct {
    void (*task_function_pointer)(void);  // Task function pointer, no arguments, no return
    char* name;
    TaskState state;
    uint32_t priority;
} TCB;

// ========== Global Variables ==========
TCB Tasklist[numTasks];               // store all tasks
LiquidCrystal_I2C lcd(0x27, 16, 2);   // Initialize the LCD

// ========== Function Prototypes ==========
void ledBlinkerTask();
void lcdCounter();
void alphabetPrinter();
void musicPlayer();
void initTasks();
void priorityScheduler();

// Name : ledBlinkerTask 
// Description : This function blinks an led on and off 8 times in one second intervals
void ledBlinkerTask() {
    bool ledOn = false;  
    for (int i = 0; i < 16; i++) {
        ledOn = !ledOn;
        digitalWrite(task1Gpio, ledOn);
        delay(1000);
    }
    digitalWrite(task1Gpio, LOW);  // make sure ends off
}

// Name: lcdCounter
// Description: This function counts from 1 to 10 displaying each digit on an external LCD display
void lcdCounter() {
    for (int i = 1; i < 11; i++) {
        lcd.clear();
        lcd.print("Count: ");
        lcd.print(i);
        delay(1000);
    }
}

// Name: alphabetPrinter 
// Description: This function just prints the letters A-Z to serial monitor 
void alphabetPrinter() {
    for (char letter = 'A'; letter <= 'Z'; letter++) {
        if(letter != 'Z'){
        Serial.print(letter);
        Serial.print(", ");    
        }else{
            // Z case 
            Serial.print(letter);
        }

    }
    Serial.println();  // move to a new line after printing all
}

// Name: musicPlayer 
// Description: This function plays a melody of increasing frequency via an external buzzer. Then parralel to the buzzer melody changing a corresponding led increases
// in brightness to display the buzzers "notes" 
void musicPlayer() {
    uint32_t sequenceStart = 0;  
    boolean runSequence = true;
    uint32_t count = 0;
    ledcAttach(task3LedGpio, 5000, 12);

    // run each buzzer tone for 1 seconds
    while (runSequence) {
        uint32_t currentTime = millis();

        // first iteration
        if (count == 0) {
            sequenceStart = currentTime;
        }

        uint32_t elapsed = currentTime - sequenceStart;  // total ms since the start of current tone sequence

        // ledcWriteTone
        if (elapsed < individualSegmentTime) {
            // 500 hz work dimness up to bring sarting off super dim  5% duty cyle
            ledcWriteTone(buzzerGpio, 500);
            delay(10);  // short pause to allow hardware to stabilize tone wont work without
            ledcWrite(task3LedGpio, 200);
            count++;  // just added for the first if as long as >0 all i need reset later with boolean
        } else if (elapsed < 2 * individualSegmentTime) {
            ledcWriteTone(buzzerGpio, 600);
            delay(10);
            ledcWrite(task3LedGpio, 400);  
        } else if (elapsed < 3 * individualSegmentTime) {
            ledcWriteTone(buzzerGpio, 700);
            delay(10);
            ledcWrite(task3LedGpio, 600);  
        } else if (elapsed < 4 * individualSegmentTime) {
            ledcWriteTone(buzzerGpio, 800);
            delay(10);
            ledcWrite(task3LedGpio, 800);
        } else if (elapsed < 5 * individualSegmentTime) {
            ledcWriteTone(buzzerGpio, 900);
            delay(10);
            ledcWrite(task3LedGpio, 1000);    
        } else if (elapsed < 6 * individualSegmentTime) {
            ledcWriteTone(buzzerGpio, 1000);
            delay(10);
            ledcWrite(task3LedGpio, 1500);    
        } else if (elapsed < 7 * individualSegmentTime) {
            ledcWriteTone(buzzerGpio, 1100);
            delay(10);
            ledcWrite(task3LedGpio, 2000);    
        } else if (elapsed < 8 * individualSegmentTime) {
            ledcWriteTone(buzzerGpio, 1200);
            delay(10);
            ledcWrite(task3LedGpio, 3000);    
        } else if (elapsed < 9 * individualSegmentTime) {
            ledcWriteTone(buzzerGpio, 1300);
            delay(10);
            ledcWrite(task3LedGpio, 3500);  
        } else if (elapsed < 10 * individualSegmentTime) { 
            ledcWriteTone(buzzerGpio, 1500);
            delay(10);
            ledcWrite(task3LedGpio, 4095);    
        } else {  // sequence finished
            runSequence = false;  // all tones have played sequence over stop
            count = 0;
            ledcWrite(task3LedGpio, 0);
            ledcWrite(buzzerGpio, 0);
        }
    }
}

// Name: initTasks
// Description: Intializes tasks with proper intial state to be run by our scheduler 
void initTasks() {
    Tasklist[0] = (TCB){.task_function_pointer = ledBlinkerTask, .name = "Led Blinker", .state = READY, .priority = 1};
    Tasklist[1] = (TCB){.task_function_pointer = lcdCounter, .name = "LCD Counter", .state = READY, .priority = 2};
    Tasklist[2] = (TCB){.task_function_pointer = musicPlayer, .name = "Music Player", .state = READY, .priority = 3};
    Tasklist[3] = (TCB){.task_function_pointer = alphabetPrinter, .name = "Alphabet Printer", .state = READY, .priority = 4};
}

// Name: priorityScheduler
// Description: This is a priority scheduler that runs tasks based on their respective priorities, 1 being highest priority
// after it has run all tasks it increases their priority by one (wrap around when needed) then sets them back to ready and runs the respective
// new order of priorities (lowest priority task becomes highest priority)
void priorityScheduler() {
    int highestIdx = -1;

    // find highest-priority READY task
    for (int i = 0; i < numTasks; i++) {
        if (Tasklist[i].state == READY) {
            // short circut or for first iteration, dont want to access -1
            if (highestIdx == -1 || Tasklist[i].priority < Tasklist[highestIdx].priority) {
                highestIdx = i;  // pick smaller number (higher priority)
            }
        }
    }

    // if not -1 that means at least one task was ready because thats the only change to highestidx
    if (highestIdx != -1) {
        // run it
        Tasklist[highestIdx].task_function_pointer();
        Tasklist[highestIdx].state = FINISHED;
        Serial.printf("%s: %u\n", Tasklist[highestIdx].name, Tasklist[highestIdx].priority);
    } else {
        // no READY tasks ie highestidx stays -1  everyone must be FINISHED reset & increase priori
        for (int i = 0; i < numTasks; i++) {
            Tasklist[i].state = READY;
            Tasklist[i].priority = Tasklist[i].priority % 4 + 1;  // reset priorities cyclically
        }
    }
}

void setup() {
    // put your setup code here, to run once:
    Serial.begin(115200);
    ledcAttach(buzzerGpio, 500, 12);
    pinMode(task1Gpio, OUTPUT);
    Wire.begin();
    lcd.init();
    lcd.backlight();
    delay(2);
    initTasks();
}

void loop() {
    // put your main code here, to run repeatedly:
    priorityScheduler();
}
