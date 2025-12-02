#include <Arduino.h> 
#include <MD_MAX72xx.h>
#include <LiquidCrystal_I2C.h>
#include <ESP32Servo.h>
#include "esp_timer.h"

// -------------------------------
// Hardware Pins
// -------------------------------
#define MATRIX_DIN  10
#define MATRIX_CLK  11
#define MATRIX_CS   12

#define SERVO_PIN   4
#define UART_RX_PIN 16
#define UART_TX_PIN 17

// -------------------------------
// Board dimensions
// -------------------------------
static const int BOARD_WIDTH  = 8;
static const int BOARD_HEIGHT = 16;

// -------------------------------
// FreeRTOS Task Configuration
// -------------------------------
#define CORE_DISPLAY     0
#define CORE_IO_HAPTIC   1

#define TASK_STACK_SIZE  4096

#define PRIO_UART        3
#define PRIO_MATRIX      2
#define PRIO_HAPTIC      3
#define PRIO_LCD         1

// -------------------------------
// LED Matrix (2 modules chained vertically)
// -------------------------------
#define NUM_MATRICES 2

MD_MAX72XX mx = MD_MAX72XX(
    MD_MAX72XX::PAROLA_HW,
    MATRIX_DIN,
    MATRIX_CLK,
    MATRIX_CS,
    NUM_MATRICES
);

// -------------------------------
// LCD
// -------------------------------
LiquidCrystal_I2C lcd(0x27, 16, 2);

// -------------------------------
// Servo
// -------------------------------
Servo hapticServo;

// -------------------------------
// Queues
// -------------------------------
QueueHandle_t displayQueue;
QueueHandle_t lcdQueue;
QueueHandle_t hapticQueue;

// -------------------------------
// Data Structures
// -------------------------------
struct BoardPacket {
  uint8_t rows[BOARD_HEIGHT];
};

struct LCDPacket {
  uint16_t score;
  uint8_t nextPiece;
};

struct HapticPacket {
  uint8_t eventType;
};

// -------------------------------
// Timer Globals
// -------------------------------
esp_timer_handle_t displayTimer;
TaskHandle_t matrixTaskHandle = NULL;

// -------------------------------
// Timer ISR
// -------------------------------
void IRAM_ATTR onDisplayTimer(void *arg) {
  if (matrixTaskHandle != NULL) {
    BaseType_t high = pdFALSE;
    vTaskNotifyGiveFromISR(matrixTaskHandle, &high);
    portYIELD_FROM_ISR(high);
  }
}

// -------------------------------
// MATRIX TASK (Core 0)
// -------------------------------
void taskMatrix(void *pv) {
  matrixTaskHandle = xTaskGetCurrentTaskHandle();

  BoardPacket board;

  while (1) {
    ulTaskNotifyTake(pdTRUE, portMAX_DELAY);

    if (xQueueReceive(displayQueue, &board, 0) == pdTRUE) {

      mx.clear();

      // FIXED MAPPING FOR YOUR VERSION OF MD_MAX72XX:
      //
      // globalRow = logicalRow % 8
      // globalCol = logicalCol + 8*(logicalRow/8)
      //
      // No device index needed — MD_MAX72xx routes columns automatically.
      for (int logicalRow = 0; logicalRow < BOARD_HEIGHT; logicalRow++) {
        uint8_t rowBits = board.rows[logicalRow];

        int globalRow = logicalRow % 8;
        int deviceIndex = logicalRow / 8;
        int colOffset = deviceIndex * 8;  // 0 for top, 8 for bottom

        for (int logicalCol = 0; logicalCol < BOARD_WIDTH; logicalCol++) {
          bool pixel = (rowBits >> logicalCol) & 0x01;

          int globalCol = logicalCol + colOffset;

          // VALID call signature for your library:
          //   setPoint(row, col, state)
          mx.setPoint(globalRow, globalCol, pixel);
        }
      }
    }
  }
}

String getPieceName(uint8_t id) {
  switch (id) {
    case 0: return "I";
    case 1: return "O";
    case 2: return "T";
    case 3: return "S";
    case 4: return "Z";
    case 5: return "J";
    case 6: return "L";
    default: return "?";
  }
}

// -------------------------------
// LCD Task
// -------------------------------
void taskLCD(void *pv) {
  LCDPacket pkt;
  while (1) {
    if (xQueueReceive(lcdQueue, &pkt, portMAX_DELAY)) {
      lcd.clear();
      lcd.setCursor(0, 0);
      lcd.print("Score: ");
      lcd.print(pkt.score);

      lcd.setCursor(0, 1);
      lcd.print("Next: ");
      lcd.print(getPieceName(pkt.nextPiece));
    }
    vTaskDelay(pdMS_TO_TICKS(150));
  }
}

// -------------------------------
// Servo Haptics
// -------------------------------
void playHaptic(uint8_t eventType) {
  switch(eventType) {
    case 1:
      hapticServo.write(70);
      vTaskDelay(pdMS_TO_TICKS(120));
      hapticServo.write(90);
      break;

    case 2:
      for (int i = 0; i < 2; i++) {
        hapticServo.write(50);                        // stronger downward pulse
        vTaskDelay(pdMS_TO_TICKS(160));              // allow servo to reach the angle
        hapticServo.write(130);                       // strong upward rebound
        vTaskDelay(pdMS_TO_TICKS(160));
        hapticServo.write(90);                        // reset to center
        vTaskDelay(pdMS_TO_TICKS(220));               // breathing room before next pulse
      }
      break;


    case 3:
      hapticServo.write(50);
      vTaskDelay(pdMS_TO_TICKS(220));
      hapticServo.write(130);
      vTaskDelay(pdMS_TO_TICKS(220));
      hapticServo.write(90);
      break;

    case 4:
      hapticServo.write(40);
      vTaskDelay(pdMS_TO_TICKS(220));
      hapticServo.write(140);
      vTaskDelay(pdMS_TO_TICKS(220));
      hapticServo.write(90);
      break;
  }
}

// -------------------------------
// HAPTIC TASK
// -------------------------------
void taskHaptic(void *pv) {
  HapticPacket pkt;
  while (1) {
    if (xQueueReceive(hapticQueue, &pkt, portMAX_DELAY))
      playHaptic(pkt.eventType);
  }
}

// -------------------------------
// UART TASK
// -------------------------------
void taskUART(void *pv) {
  while (1) {
    if (Serial2.available()) {

      uint8_t header = Serial2.read();
      if (header != 0xAA) continue;

      uint8_t type = Serial2.read();

      if (type == 0x01) {
        BoardPacket pkt;
        for (int i = 0; i < BOARD_HEIGHT; i++)
          pkt.rows[i] = Serial2.read();
        xQueueSend(displayQueue, &pkt, 0);
      }
      else if (type == 0x02) {
        LCDPacket pkt;
        pkt.score = (Serial2.read() << 8) | Serial2.read();
        pkt.nextPiece = Serial2.read();
        xQueueSend(lcdQueue, &pkt, 0);
      }
      else if (type == 0x03) {
        HapticPacket pkt;
        pkt.eventType = Serial2.read();
        xQueueSend(hapticQueue, &pkt, 0);
      }
    }
    vTaskDelay(1);
  }
}

// -------------------------------
// SETUP
// -------------------------------
void setup() {
  Serial.begin(115200);

  mx.begin();
  mx.clear();

  lcd.init();
  lcd.backlight();

  hapticServo.setPeriodHertz(50);
  hapticServo.attach(SERVO_PIN, 500, 2400);

  Serial2.begin(115200, SERIAL_8N1, UART_RX_PIN, UART_TX_PIN);

  displayQueue = xQueueCreate(4, sizeof(BoardPacket));
  lcdQueue     = xQueueCreate(4, sizeof(LCDPacket));
  hapticQueue  = xQueueCreate(4, sizeof(HapticPacket));

  xTaskCreatePinnedToCore(taskMatrix, "matrix", TASK_STACK_SIZE, NULL, PRIO_MATRIX, NULL, CORE_DISPLAY);
  xTaskCreatePinnedToCore(taskLCD,    "lcd",    TASK_STACK_SIZE, NULL, PRIO_LCD,    NULL, CORE_DISPLAY);
  xTaskCreatePinnedToCore(taskUART,   "uart",   TASK_STACK_SIZE, NULL, PRIO_UART,   NULL, CORE_IO_HAPTIC);
  xTaskCreatePinnedToCore(taskHaptic, "haptic", TASK_STACK_SIZE, NULL, PRIO_HAPTIC, NULL, CORE_IO_HAPTIC);

  const esp_timer_create_args_t displayTimerArgs = {
    .callback = &onDisplayTimer,
    .arg = NULL,
    .dispatch_method = ESP_TIMER_TASK,
    .name = "matrix_timer"
  };

  esp_timer_create(&displayTimerArgs, &displayTimer);
  esp_timer_start_periodic(displayTimer, 10000); // 10 ms

  Serial.println("ESP32-A READY (8x16 vertical stack)");
}

void loop() {}
