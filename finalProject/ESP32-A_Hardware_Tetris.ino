#include <Arduino.h>
#include <MD_MAX72xx.h>
#include <LiquidCrystal_I2C.h>
#include <ESP32Servo.h>

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
// LED Matrix Driver
// -------------------------------
MD_MAX72XX mx = MD_MAX72XX(
    MD_MAX72XX::PAROLA_HW,
    MATRIX_DIN,
    MATRIX_CLK,
    MATRIX_CS
);

// -------------------------------
// LCD Screen
// -------------------------------
LiquidCrystal_I2C lcd(0x27, 16, 2);

// -------------------------------
// Servo Object
// -------------------------------
Servo hapticServo;

// -------------------------------
// FreeRTOS Queues
// -------------------------------
QueueHandle_t displayQueue;
QueueHandle_t lcdQueue;
QueueHandle_t hapticQueue;

// -------------------------------
// Data Structures
// -------------------------------
struct BoardPacket {
  uint8_t rows[8];
};

struct LCDPacket {
  uint16_t score;
  uint8_t nextPiece;
};

struct HapticPacket {
  uint8_t eventType; 
};

// -------------------------------
// LED Matrix Task (Core 0)
// -------------------------------
void taskMatrix(void *pv) {
  BoardPacket board;
  const TickType_t delayTicks = pdMS_TO_TICKS(10); // 100 Hz

  while (1) {
    if (xQueueReceive(displayQueue, &board, 0) == pdTRUE) {
      mx.clear();
      for (int r = 0; r < 8; r++) {
        for (int c = 0; c < 8; c++) {
          bool pixel = (board.rows[r] >> c) & 1;
          mx.setPoint(r, c, pixel);
        }
      }
    }
    vTaskDelay(delayTicks);
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
// LCD Task (Core 0)
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
  }
}

// -------------------------------
// Servo Haptic Function
// -------------------------------
void playHaptic(uint8_t eventType) {
  switch(eventType) {

    // Single line clear: short tap
    case 1:
      hapticServo.write(70);
      vTaskDelay(pdMS_TO_TICKS(120));
      hapticServo.write(90);
      break;

    // Double line clear: double tap
    case 2:
      for (int i = 0; i < 2; i++) {
        hapticServo.write(60);
        vTaskDelay(pdMS_TO_TICKS(90));
        hapticServo.write(90);
        vTaskDelay(pdMS_TO_TICKS(120));
      }
      break;

    // Triple line clear: longer vibration
    case 3:
      hapticServo.write(50);
      vTaskDelay(pdMS_TO_TICKS(200));
      hapticServo.write(100);
      vTaskDelay(pdMS_TO_TICKS(200));
      hapticServo.write(90);
      break;

    // TETRIS: dramatic sweep
    case 4:
      hapticServo.write(40);  // full left
      vTaskDelay(pdMS_TO_TICKS(180));
      hapticServo.write(140); // full right
      vTaskDelay(pdMS_TO_TICKS(180));
      hapticServo.write(90);  // center
      break;

    default:
      break;
  }
}


// -------------------------------
// Haptic Task (Core 1)
// -------------------------------
void taskHaptic(void *pv) {
  HapticPacket pkt;
  while (1) {
    if (xQueueReceive(hapticQueue, &pkt, portMAX_DELAY)) {
      playHaptic(pkt.eventType);
    }
  }
}

// -------------------------------
// UART Task (Core 1)
// -------------------------------
void taskUART(void *pv) {
  while (1) {
    if (Serial2.available()) {
      uint8_t header = Serial2.read();
      if (header != 0xAA) continue;

      uint8_t type = Serial2.read();

      if (type == 0x01) {  
        BoardPacket pkt;
        for (int i = 0; i < 8; i++)
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
// TEST TASK (Core 1) — REMOVE LATER
// -------------------------------
void taskTest(void *pv) {

  // A few example Tetris boards (8x8) to simulate gameplay
  uint8_t frames[][8] = {
    // Empty board
    {0,0,0,0,0,0,0,0},

    // Falling I-piece (vertical)
    {0b00011000,
     0b00011000,
     0b00011000,
     0b00011000,
     0,0,0,0},

    // Falling T-piece
    {0b00000000,
     0b00010000,
     0b00111000,
     0b00000000,
     0,0,0,0},

    // Board with one line filled
    {0b11111111,
     0b00000000,
     0b00011000,
     0b00011000,
     0,0,0,0},

    // Board after line clear
    {0,0,0,0,0,0,0,0}
  };

  int numFrames = sizeof(frames)/sizeof(frames[0]);

  int score = 0;
  int nextPiece = 2; // T-piece

  while (1) {

    for (int i = 0; i < numFrames; i++) {

      // SEND MATRIX FRAME
      BoardPacket bp;
      memcpy(bp.rows, frames[i], 8);
      xQueueSend(displayQueue, &bp, 0);

      // UPDATE LCD (fake score, fake next piece)
      LCDPacket lp = { score, nextPiece };
      xQueueSend(lcdQueue, &lp, 0);

      // HAPTIC: If a line is full, simulate line clear
      if (frames[i][0] == 0xFF) {
        HapticPacket hp = { 1 }; // single line clear
        xQueueSend(hapticQueue, &hp, 0);
        score += 100;
      }

      vTaskDelay(pdMS_TO_TICKS(700)); // simulate ~1 frame per 0.7 sec
    }

    nextPiece = (nextPiece + 1) % 7; // cycle through pieces
  }
}


// -------------------------------
// Setup
// -------------------------------
void setup() {
  Serial.begin(115200);

  // LED matrix
  mx.begin();
  mx.clear();

  // LCD
  lcd.init();
  lcd.backlight();

  // Servo
  hapticServo.setPeriodHertz(50);
  hapticServo.attach(SERVO_PIN, 500, 2400);

  // UART
  Serial2.begin(115200, SERIAL_8N1, UART_RX_PIN, UART_TX_PIN);

  // Queues
  displayQueue = xQueueCreate(4, sizeof(BoardPacket));
  lcdQueue     = xQueueCreate(4, sizeof(LCDPacket));
  hapticQueue  = xQueueCreate(4, sizeof(HapticPacket));

  // Tasks
  xTaskCreatePinnedToCore(taskMatrix, "matrix", 4096, NULL, 2, NULL, 0);
  xTaskCreatePinnedToCore(taskLCD,    "lcd",    4096, NULL, 1, NULL, 0);
  xTaskCreatePinnedToCore(taskUART,   "uart",   4096, NULL, 3, NULL, 1);
  xTaskCreatePinnedToCore(taskHaptic, "haptic", 4096, NULL, 2, NULL, 1);

  // TEST TASKS  REMOVE later
  xTaskCreatePinnedToCore(taskTest,   "test",   4096, NULL, 1, NULL, 1);
  // xTaskCreatePinnedToCore(taskHapticTest, "hTest", 4096, NULL, 1, NULL, 1);

}

void loop() {}


