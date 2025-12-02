// Filename: ESP32-B_BACKEND_TETRIS.ino
// Author: David Montiel 
// Date: 11/2/25
// Description: This file handles all the backend logic of our ESP-32 based tetris game 

// ====== Includes ======
#include <Arduino.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "esp_system.h"   // for esp_random()

// ====== Macros ======
#define UART_RX_PIN 16
#define UART_TX_PIN 17
#define BUTTON_1 5 // left
#define BUTTON_2 6 // up
#define BUTTON_3 45 // down
#define BUTTON_4 2 // right

// ====== Globals and Global Types ======

//board dimensions for stacked display
static const int BOARD_WIDTH  = 8;   // columns
static const int BOARD_HEIGHT = 16;  // rows (two 8x8 matrices)

enum ButtonCode : uint8_t {
  BTN_NONE = 0,
  BTN_LEFT,
  BTN_RIGHT,
  BTN_ROTATE,
  BTN_DROP
};

enum HapticCode : uint8_t {
  HAPTIC_NONE = 0,
  HAPTIC_SINGLE_LINE,
  HAPTIC_DOUBLE_LINE,
  HAPTIC_TRIPLE_LINE,
  HAPTIC_TETRIS
};

// uart protocol constants to match frontend esp
const uint8_t PKT_HEADER      = 0xAA;
const uint8_t PKT_TYPE_BOARD  = 0x01;
const uint8_t PKT_TYPE_LCD    = 0x02;
const uint8_t PKT_TYPE_HAPTIC = 0x03;

// 8x16 board each byte = one row 1 bit per cell
struct GameState {
  uint8_t  board[BOARD_HEIGHT];  // 16 rows
  uint16_t score;
  uint8_t  nextPiece;   // 0–6 for the different tetrominoes
  HapticCode haptic;
};

// packet to send to frontend esp
struct DisplayPacket {
  uint8_t type;                 
  uint16_t score;               
  uint8_t nextPiece;            // 0–6
  uint8_t board[BOARD_HEIGHT];  // 16 rows
  uint8_t haptic;               // HapticCode
};

//tetris pieces
const uint8_t NUM_PIECES = 7;

// TETROMINOES[pieceType][rotation][row]
const uint8_t TETROMINOES[NUM_PIECES][4][4] = {
  // I piece
  {
    {0b0000, 0b1111, 0b0000, 0b0000},
    {0b0010, 0b0010, 0b0010, 0b0010},
    {0b0000, 0b1111, 0b0000, 0b0000},
    {0b0010, 0b0010, 0b0010, 0b0010}
  },
  // O piece
  {
    {0b0110, 0b0110, 0b0000, 0b0000},
    {0b0110, 0b0110, 0b0000, 0b0000},
    {0b0110, 0b0110, 0b0000, 0b0000},
    {0b0110, 0b0110, 0b0000, 0b0000}
  },
  // T piece
  {
    {0b0100, 0b1110, 0b0000, 0b0000},
    {0b0100, 0b0110, 0b0100, 0b0000},
    {0b0000, 0b1110, 0b0100, 0b0000},
    {0b0100, 0b1100, 0b0100, 0b0000}
  },
  // S piece
  {
    {0b0110, 0b1100, 0b0000, 0b0000},
    {0b1000, 0b1100, 0b0100, 0b0000},
    {0b0110, 0b1100, 0b0000, 0b0000},
    {0b1000, 0b1100, 0b0100, 0b0000}
  },
  // Z piece
  {
    {0b1100, 0b0110, 0b0000, 0b0000},
    {0b0100, 0b1100, 0b1000, 0b0000},
    {0b1100, 0b0110, 0b0000, 0b0000},
    {0b0100, 0b1100, 0b1000, 0b0000}
  },
  // J piece
  {
    {0b1000, 0b1110, 0b0000, 0b0000},
    {0b0110, 0b0100, 0b0100, 0b0000},
    {0b0000, 0b1110, 0b0010, 0b0000},
    {0b0100, 0b0100, 0b1100, 0b0000}
  },
  // L piece
  {
    {0b0010, 0b1110, 0b0000, 0b0000},
    {0b0100, 0b0100, 0b0110, 0b0000},
    {0b0000, 0b1110, 0b1000, 0b0000},
    {0b1100, 0b0100, 0b0100, 0b0000}
  }
};

// active falling piece
struct ActivePiece {
  uint8_t type;   // 0..6 7 total pieces
  uint8_t rot;    // 0..3 aka 1 of 4 rotations
  int8_t x;       // board x (0..7)
  int8_t y;       // board y (0..15) top of 4x4 box
};


QueueHandle_t inputQueue;    // buttonCode
QueueHandle_t displayQueue;  // displayPacket

TaskHandle_t buttonTaskHandle;
TaskHandle_t gameTaskHandle;
TaskHandle_t uartTaskHandle;

GameState gState;
ActivePiece gPiece;

//global timing state for speed control
TickType_t gCurrentTickPeriod = 0;
uint32_t   gLastSpeedUpdateMs = 0;


void setup() {
  Serial.begin(115200);        
  Serial2.begin(115200, SERIAL_8N1, UART_RX_PIN, UART_TX_PIN);

  delay(2000);
  Serial.println("\n\nESP32-B up and running");

  // button inputs with pullups
  pinMode(BUTTON_1, INPUT_PULLUP);
  pinMode(BUTTON_2, INPUT_PULLUP);
  pinMode(BUTTON_3, INPUT_PULLUP);
  pinMode(BUTTON_4, INPUT_PULLUP);

  // create queues
  inputQueue   = xQueueCreate(10, sizeof(ButtonCode));
  displayQueue = xQueueCreate(1,  sizeof(DisplayPacket)); // length 1 for xQueueOverwrite

  // initialize timing state
  gCurrentTickPeriod = pdMS_TO_TICKS(50); // start at 50 ms
  gLastSpeedUpdateMs = millis();

  setupGameState();

  // create tasks
  xTaskCreatePinnedToCore(buttonTask,     "buttonTask",   4096, NULL, 2, &buttonTaskHandle,   0);
  xTaskCreatePinnedToCore(gameTask,   "GameTask", 4096, NULL, 3, &gameTaskHandle, 1);
  xTaskCreatePinnedToCore(uartTxTask, "UartTx",   4096, NULL, 1, &uartTaskHandle, 0);
}

void loop() {
}

// Name : setupGameState 
// Description : initializes all game state fields by clearing the board, resetting
// the score and haptic status, and spawning the first active Tetris piece
void setupGameState() {
  for (int r = 0; r < BOARD_HEIGHT; r++) {
    gState.board[r] = 0;
  }
  gState.score = 0;
  gState.nextPiece = 0;
  gState.haptic = HAPTIC_NONE;

  spawnNewPiece(gState, gPiece);
}

// Name : checkCollision 
// Description : determines whether the active piece p would collide with anything in the
// given game state st by checking board boundaries and occupied cells. returns true if a
// collision is detected and false otherwise
bool checkCollision(const GameState &st, const ActivePiece &p) {
  const uint8_t* shape = TETROMINOES[p.type][p.rot];

  for (int py = 0; py < 4; py++) {
    uint8_t rowMask = shape[py];
    for (int px = 0; px < 4; px++) {
      if (rowMask & (1 << (3 - px))) { // bit from left in 4 wide shape
        int bx = p.x + px;
        int by = p.y + py;

        // outside board?
        if (bx < 0 || bx >= BOARD_WIDTH || by < 0 || by >= BOARD_HEIGHT) {
          return true;
        }

        // overlapping locked block?
        if (st.board[by] & (1 << bx)) {
          return true;
        }
      }
    }
  }
  return false;
}

// Name : lockPieceIntoBoard 
// Description : transfers the active piece p into the game board within st by writing its
// occupied 4×4 shape cells into the board bitmap, this function has no return value
void lockPieceIntoBoard(GameState &st, const ActivePiece &p) {
  const uint8_t* shape = TETROMINOES[p.type][p.rot];

  for (int py = 0; py < 4; py++) {
    uint8_t rowMask = shape[py];
    for (int px = 0; px < 4; px++) {
      if (rowMask & (1 << (3 - px))) {
        int bx = p.x + px;
        int by = p.y + py;
        if (bx >= 0 && bx < BOARD_WIDTH && by >= 0 && by < BOARD_HEIGHT) {
          st.board[by] |= (1 << bx);
        }
      }
    }
  }
}

// Name : clearFullLines 
// Description : scans the board in st for fully filled rows, removes each one, shifts the
// above rows downward, updates the score based on how many were cleared, and sets the
// appropriate haptic feedback code, this function has no return value
void clearFullLines(GameState &st) {
  int cleared = 0;

  for (int row = BOARD_HEIGHT - 1; row >= 0; row--) {
    if (st.board[row] == 0xFF) { // all 8 bits set
      cleared++;

      // shift everything above down
      for (int r = row; r > 0; r--) {
        st.board[r] = st.board[r - 1];
      }
      st.board[0] = 0x00;

      // recheck same row index after shifting
      row++;
    }
  }

  if (cleared > 0) {
    st.score += 100 * cleared;

    switch (cleared) {
      case 1: st.haptic = HAPTIC_SINGLE_LINE; break;
      case 2: st.haptic = HAPTIC_DOUBLE_LINE; break;
      case 3: st.haptic = HAPTIC_TRIPLE_LINE; break;
      case 4: st.haptic = HAPTIC_TETRIS;      break;
      default: st.haptic = HAPTIC_SINGLE_LINE; break;
    }
  } else {
    st.haptic = HAPTIC_NONE;
  }
}

// Name: spawnNewPiece
// Description : initializes a new active tetris piece p in the game state st by assigning
// it a random type, resetting its rotation and position, and updating the next-piece field.
// if the new piece immediately collides with the board, the function triggers a game over
// reset by clearing the board, resetting the score, and restoring default fall speed
void spawnNewPiece(GameState &st, ActivePiece &p) {
  p.type = esp_random() % NUM_PIECES;
  p.rot  = 0;
  p.x    = 2;   // near center of 8 wide board
  p.y    = 0;   // top of 16 high board

  st.nextPiece = p.type;

  // if it collides immediately, simple game over, clear board and reset score
  if (checkCollision(st, p)) {
    for (int r = 0; r < BOARD_HEIGHT; r++) st.board[r] = 0;
    st.score = 0;
    st.haptic = HAPTIC_NONE;

    // RESET SPEED ON GAME OVER TO DEFAULT
    gCurrentTickPeriod = pdMS_TO_TICKS(50);   // back to 50 ms
    gLastSpeedUpdateMs = millis();            // restart 5 second timer
    Serial.println("Game over: speed reset to 50 ms");
  }
}

// Name : buttonTask
// Description: Continuously polls the four physical buttons, detects falling edge presses
// (HIGH to LOW), maps each to the corresponding ButtonCode (left, right, rotate,
// drop), and sends the result to inputQueue with a small debounce, runs forever
// as a FreeRTOS task and does not return
void buttonTask(void *pv) {
  (void)pv;

  Serial.println("ButtonTask started");

  // previous states for edge detection
  int prev1 = HIGH;
  int prev2 = HIGH;
  int prev3 = HIGH;
  int prev4 = HIGH;

  uint32_t lastPrint = 0;

  while (true) {
    int curr1 = digitalRead(BUTTON_1);
    int curr2 = digitalRead(BUTTON_2);
    int curr3 = digitalRead(BUTTON_3);
    int curr4 = digitalRead(BUTTON_4);

    ButtonCode code = BTN_NONE;

    // Map buttons:
    // BUTTON_1 = LEFT
    // BUTTON_4 = RIGHT
    // BUTTON_2 = ROTATE
    // BUTTON_3 = DROP
    //
    // detect HIGH to LOW 
    if (curr1 == LOW && prev1 == HIGH) {
     code = BTN_LEFT;
    } else if (curr4 == LOW && prev4 == HIGH) {
    code = BTN_RIGHT;
    } else if (curr2 == LOW && prev2 == HIGH) {
    code = BTN_ROTATE;
    } else if (curr3 == LOW && prev3 == HIGH) {
    code = BTN_DROP;
  } 

    // update previous states
    prev1 = curr1;
    prev2 = curr2;
    prev3 = curr3;
    prev4 = curr4;

    if (code != BTN_NONE) {
      xQueueSend(inputQueue, &code, 0);
      Serial.print("ButtonTask: queued code ");
      Serial.println(code);
      // small debounce so you don't spam 100 presses
      vTaskDelay(pdMS_TO_TICKS(120));
    } else {
      vTaskDelay(pdMS_TO_TICKS(10));
    }
  }
}

// Name: gameTask
// Description : runs the main tetris game loop by applying gravity, handling player input,
// speeding up over time, locking and spawning pieces, clearing lines, and packaging the
// current board state into a display packet each tick this FreeRTOS task runs indefinitely
// and does not return
void gameTask(void *pv) {
  (void)pv;

  // timing/speed control 
  const TickType_t baseTickPeriod = pdMS_TO_TICKS(50);   // initial 50ms tick (20 Hz)
  const TickType_t minTickPeriod  = pdMS_TO_TICKS(15);   // cap at 66 Hz
  const TickType_t tickDecrement  = pdMS_TO_TICKS(5);    // speed up step
  const uint32_t   speedIntervalMs = 5000;               // every 5 seconds

  const int gravityTicks = 10;  // piece moves down every 10 ticks

  ButtonCode input = BTN_NONE;
  TickType_t wakeTime = xTaskGetTickCount();
  int tickCounter = 0;

  while (true) {
    //  possibly speed up the game
    uint32_t nowMs = millis();
    if (nowMs - gLastSpeedUpdateMs >= speedIntervalMs) {
      gLastSpeedUpdateMs = nowMs;

      if (gCurrentTickPeriod > minTickPeriod + tickDecrement) {
        gCurrentTickPeriod -= tickDecrement;
      } else {
        gCurrentTickPeriod = minTickPeriod;
      }

      Serial.print("Speed up! New tick period (ms): ");
      Serial.println(gCurrentTickPeriod * portTICK_PERIOD_MS);
    }

    // wait for next tick using the current period 
    vTaskDelayUntil(&wakeTime, gCurrentTickPeriod);

    //  read input from queue 
    if (xQueueReceive(inputQueue, &input, 0) == pdTRUE) {
      ActivePiece trial = gPiece;

      if (input == BTN_LEFT) {
        trial.x--;
        if (!checkCollision(gState, trial)) {
          gPiece.x--;
        }
      } else if (input == BTN_RIGHT) {
        trial.x++;
        if (!checkCollision(gState, trial)) {
          gPiece.x++;
        }
      } else if (input == BTN_ROTATE) {
        trial.rot = (trial.rot + 1) & 0x03;
        if (!checkCollision(gState, trial)) {
          gPiece.rot = trial.rot;
        }
      } else if (input == BTN_DROP) {
        // hard drop move down until collide
        trial = gPiece;
        while (true) {
          trial.y++;
          if (checkCollision(gState, trial)) {
            trial.y--;
            gPiece.y = trial.y;
            break;
          }
        }

        // lock piece, clear lines, spawn new one
        lockPieceIntoBoard(gState, gPiece);
        clearFullLines(gState);
        spawnNewPiece(gState, gPiece);
      }
    } else {
      input = BTN_NONE;
    }

    //  gravity
    tickCounter++;
    if (tickCounter >= gravityTicks) {
      tickCounter = 0;

      ActivePiece trial = gPiece;
      trial.y++;

      if (!checkCollision(gState, trial)) {
        gPiece.y++;
      } else {
        // we hit something, lock current piece and spawn new
        lockPieceIntoBoard(gState, gPiece);
        clearFullLines(gState);
        spawnNewPiece(gState, gPiece);
      }
    }

    // build display board 
    uint8_t displayBoard[BOARD_HEIGHT];
    for (int r = 0; r < BOARD_HEIGHT; r++) {
      displayBoard[r] = gState.board[r];
    }

    const uint8_t* shape = TETROMINOES[gPiece.type][gPiece.rot];
    for (int py = 0; py < 4; py++) {
      uint8_t rowMask = shape[py];
      for (int px = 0; px < 4; px++) {
        if (rowMask & (1 << (3 - px))) {
          int bx = gPiece.x + px;
          int by = gPiece.y + py;
          if (bx >= 0 && bx < BOARD_WIDTH && by >= 0 && by < BOARD_HEIGHT) {
            displayBoard[by] |= (1 << bx);
          }
        }
      }
    }

    // pack and send to display queue 
    DisplayPacket pkt;
    pkt.type      = 0x01;
    pkt.score     = gState.score;
    pkt.nextPiece = gState.nextPiece;
    pkt.haptic    = gState.haptic;
    for (int r = 0; r < BOARD_HEIGHT; r++) {
      pkt.board[r] = displayBoard[r];
    }

    xQueueOverwrite(displayQueue, &pkt);
  }
}

// Name: uartTxTask
// Description : waits for display packets from the game logic then transmits the board,
// score/LCD data, and any haptic events over UART in the expected packet format for the
// frontend esp32, runs indefinitely as a dedicated UART transmit task

void uartTxTask(void *pv) {
  (void)pv;
  DisplayPacket pkt;

  while (true) {
    if (xQueueReceive(displayQueue, &pkt, portMAX_DELAY) == pdTRUE) {
      // send board packet in expected format: 0xAA, 0x01, 16 bytes of rows
      Serial2.write(PKT_HEADER);
      Serial2.write(PKT_TYPE_BOARD);
      for (int r = 0; r < BOARD_HEIGHT; r++) {
        Serial2.write(pkt.board[r]);
      }

      // send lcd packet: 0xAA, 0x02, score_hi, score_lo, nextPiece
      Serial2.write(PKT_HEADER);
      Serial2.write(PKT_TYPE_LCD);
      uint8_t score_hi = (pkt.score >> 8) & 0xFF;
      uint8_t score_lo = pkt.score        & 0xFF;
      Serial2.write(score_hi);
      Serial2.write(score_lo);
      Serial2.write(pkt.nextPiece);

      //  send haptic packet : 0xAA, 0x03, eventType
      if (pkt.haptic != HAPTIC_NONE) {
        Serial2.write(PKT_HEADER);
        Serial2.write(PKT_TYPE_HAPTIC);
        Serial2.write((uint8_t)pkt.haptic);
        gState.haptic = HAPTIC_NONE;
      }
    }
  }
}
