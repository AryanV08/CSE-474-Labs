// ====== Includes ======
#include <Arduino.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "esp_system.h"   // for esp_random()

// ====== Game definitions ======
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

// 8x8 board: each byte = one row, 1 bit per cell
struct GameState {
  uint8_t board[8];
  uint16_t score;
  uint8_t nextPiece;   // 0–6 for the different tetrominoes
  HapticCode haptic;
};

// Packet to send to ESP32-A
struct DisplayPacket {
  uint8_t type;        // ex 0x01 = GAME_UPDATE
  uint16_t score;      // little-endian
  uint8_t nextPiece;   // 0–6
  uint8_t board[8];    // row 0..7, bit 0 = leftmost column
  uint8_t haptic;      // HapticCode
};

// ====== Tetris pieces ======
// Pieces stored as 4x4 bitmasks: each uint8_t is a row, low 4 bits used.
const uint8_t NUM_PIECES = 7;

// TETROMINOES[pieceType][rotation][row]
const uint8_t TETROMINOES[NUM_PIECES][4][4] = {
  // I piece aka #### straight line 
  {
    {0b0000, 
    0b1111, 
    0b0000, 
    0b0000}, // rot 0 aka ####
   
    {0b0010, // rot 1 aka #
    0b0010, //            #
    0b0010, //            #
    0b0010},//.           #

    {0b0000, 0b1111, 0b0000, 0b0000}, // rot 2 same as 0 
    {0b0010, 0b0010, 0b0010, 0b0010}  // rot 3 same as 1 because of symetry 
  },
  // O piece aka 2x2 square 
  // ##
  // ##
  {
    {0b0110, 
    0b0110, 
    0b0000, 
    0b0000}, 
    // every rotation is the same its just a square 
    {0b0110, 0b0110, 0b0000, 0b0000},
    {0b0110, 0b0110, 0b0000, 0b0000},
    {0b0110, 0b0110, 0b0000, 0b0000}
  },
  // T piece self explanitory 
  // ###
  //  #
  {
    {0b0100, 
     0b1110, 
     0b0000, 
     0b0000} // upside down T and likewise 
     ,
    {0b0100, 0b0110, 0b0100, 0b0000},
    {0b0000, 0b1110, 0b0100, 0b0000},
    {0b0100, 0b1100, 0b0100, 0b0000}
  },
  // S piece 
  //  ##
  // ##
  {
    {0b0110, 0b1100, 0b0000, 0b0000},
    {0b1000, 0b1100, 0b0100, 0b0000},
    {0b0110, 0b1100, 0b0000, 0b0000},
    {0b1000, 0b1100, 0b0100, 0b0000}
  },
  // Z piece opisite of S 
                      //  ## 
                        // ##
  {
    {0b1100, 0b0110, 0b0000, 0b0000},
    {0b0100, 0b1100, 0b1000, 0b0000},
    {0b1100, 0b0110, 0b0000, 0b0000},
    {0b0100, 0b1100, 0b1000, 0b0000}
  },
  // J piece aka 
    // #
  // ###
  {
    {0b1000, 0b1110, 0b0000, 0b0000},
    {0b0110, 0b0100, 0b0100, 0b0000},
    {0b0000, 0b1110, 0b0010, 0b0000},
    {0b0100, 0b0100, 0b1100, 0b0000}
  },
  // L piece
  // #
  // ###
  {
    {0b0010, 0b1110, 0b0000, 0b0000},
    {0b0100, 0b0100, 0b0110, 0b0000},
    {0b0000, 0b1110, 0b1000, 0b0000},
    {0b1100, 0b0100, 0b0100, 0b0000}
  }
};

// Active falling piece
struct ActivePiece {
  uint8_t type;   // 0..6 7 total pieces 
  uint8_t rot;    // 0..3 aka 1 of 4 rotations 
  int8_t x;       // board x (0..7)
  int8_t y;       // board y (0..7), top of 4x4 box
};

// ====== Queues ======
QueueHandle_t inputQueue;    // ButtonCode
QueueHandle_t displayQueue;  // DisplayPacket

// ====== Task handles ======
TaskHandle_t irTaskHandle;
TaskHandle_t gameTaskHandle;
TaskHandle_t uartTaskHandle;

// ====== Globals ======
GameState gState;
ActivePiece gPiece;

//===== Function Prototypes =====
void IRTask(void *pv);
void GameTask(void *pv);
void UartTxTask(void *pv);

void setupGameState();
bool checkCollision(const GameState &st, const ActivePiece &p);
void lockPieceIntoBoard(GameState &st, const ActivePiece &p);
void clearFullLines(GameState &st);
void spawnNewPiece(GameState &st, ActivePiece &p);

// ====== setup / loop ======
void setup() {
  Serial.begin(115200);        // Debug to USB serial
  Serial2.begin(115200);       // UART link to ESP32-A (set RX/TX pins in Arduino menu if needed)

  delay(2000); // Give Serial time to connect
  Serial.println("\n\nESP32-B up and running");

  // Create queues
  inputQueue   = xQueueCreate(10, sizeof(ButtonCode));
  displayQueue = xQueueCreate(1,  sizeof(DisplayPacket)); // length 1 for xQueueOverwrite

  setupGameState();

  // Create tasks
  xTaskCreatePinnedToCore(IRTask,     "IRTask",   4096, NULL, 2, &irTaskHandle,   0);
  xTaskCreatePinnedToCore(GameTask,   "GameTask", 4096, NULL, 3, &gameTaskHandle, 1);
  xTaskCreatePinnedToCore(UartTxTask, "UartTx",   4096, NULL, 1, &uartTaskHandle, 0);
}

void loop() {
  // Everything is done in tasks
}

// ====== Initialize game ======
void setupGameState() {
  for (int r = 0; r < 8; r++) {
    gState.board[r] = 0;
  }
  gState.score = 0;
  gState.nextPiece = 0;
  gState.haptic = HAPTIC_NONE;

  spawnNewPiece(gState, gPiece);
}

// ====== Collision check ======
bool checkCollision(const GameState &st, const ActivePiece &p) {
const uint8_t* shape = TETROMINOES[p.type][p.rot];


  for (int py = 0; py < 4; py++) {
    uint8_t rowMask = shape[py];
    for (int px = 0; px < 4; px++) {
      if (rowMask & (1 << (3 - px))) { // bit from left in 4-wide shape
        int bx = p.x + px;
        int by = p.y + py;

        // Outside board?
        if (bx < 0 || bx >= 8 || by < 0 || by >= 8) {
          return true;
        }

        // Overlapping locked block?
        if (st.board[by] & (1 << bx)) {
          return true;
        }
      }
    }
  }
  return false;
}

// ====== Merge active piece into board permanently ======
void lockPieceIntoBoard(GameState &st, const ActivePiece &p) {
const uint8_t* shape = TETROMINOES[p.type][p.rot];


  for (int py = 0; py < 4; py++) {
    uint8_t rowMask = shape[py];
    for (int px = 0; px < 4; px++) {
      if (rowMask & (1 << (3 - px))) {
        int bx = p.x + px;
        int by = p.y + py;
        if (bx >= 0 && bx < 8 && by >= 0 && by < 8) {
          st.board[by] |= (1 << bx);
        }
      }
    }
  }
}

// ====== Clear full lines, update score & haptic ======
void clearFullLines(GameState &st) {
  int cleared = 0;

  for (int row = 7; row >= 0; row--) {
    if (st.board[row] == 0xFF) { // all 8 bits set
      cleared++;

      // Shift everything above down
      for (int r = row; r > 0; r--) {
        st.board[r] = st.board[r - 1];
      }
      st.board[0] = 0x00;

      // Re-check same row index after shifting
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

// ====== Spawn new piece at top center ======
void spawnNewPiece(GameState &st, ActivePiece &p) {
  p.type = esp_random() % NUM_PIECES;
  p.rot  = 0;
  p.x    = 2;   // near center of 8-wide board
  p.y    = 0;

  st.nextPiece = p.type;

  // If it collides immediately, simple "game over": clear board & reset score
  if (checkCollision(st, p)) {
    for (int r = 0; r < 8; r++) st.board[r] = 0;
    st.score = 0;
    st.haptic = HAPTIC_NONE;
  }
}

// ====== IR Task (currently WASD over Serial) ======
void IRTask(void *pv) {
  (void)pv;

  while (true) {
    if (Serial.available() > 0) {
      char c = Serial.read();
      ButtonCode code = BTN_NONE;

      switch (c) {
        case 'a': code = BTN_LEFT;   break;
        case 'd': code = BTN_RIGHT;  break;
        case 'w': code = BTN_ROTATE; break;
        case 's': code = BTN_DROP;   break;
      }

      if (code != BTN_NONE) {
        xQueueSend(inputQueue, &code, 0);
        Serial.print("IRTask: queued code ");
        Serial.println(code);
      }
    }

    vTaskDelay(pdMS_TO_TICKS(10)); // don't hog CPU
  }
}

// ====== Game Task (Tetris logic) ======
void GameTask(void *pv) {
  (void)pv;
  const TickType_t tickPeriod = pdMS_TO_TICKS(50); // 20 Hz main tick
  const int gravityTicks = 10;  // piece moves down every 10 ticks (~0.5s)

  ButtonCode input = BTN_NONE;
  TickType_t wakeTime = xTaskGetTickCount();

  int tickCounter = 0;

  while (true) {
    vTaskDelayUntil(&wakeTime, tickPeriod);

    // ---- 1. Read input from queue ----
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
        // Hard drop: move down until collide
        trial = gPiece;
        while (true) {
          trial.y++;
          if (checkCollision(gState, trial)) {
            trial.y--;
            gPiece.y = trial.y;
            break;
          }
        }

        // Lock piece, clear lines, spawn new one
        lockPieceIntoBoard(gState, gPiece);
        clearFullLines(gState);
        spawnNewPiece(gState, gPiece);
      }
    } else {
      input = BTN_NONE;
    }

    // ---- 2. Gravity ----
    tickCounter++;
    if (tickCounter >= gravityTicks) {
      tickCounter = 0;

      ActivePiece trial = gPiece;
      trial.y++;

      if (!checkCollision(gState, trial)) {
        gPiece.y++;
      } else {
        // We hit something: lock current piece and spawn new
        lockPieceIntoBoard(gState, gPiece);
        clearFullLines(gState);
        spawnNewPiece(gState, gPiece);
      }
    }
    uint8_t displayBoard[8];
    // Build a display view: board + active piece overlaid
    for (int r = 0; r < 8; r++) {
      displayBoard[r] = gState.board[r];
    }
const uint8_t* shape = TETROMINOES[gPiece.type][gPiece.rot];
    for (int py = 0; py < 4; py++) {
      uint8_t rowMask = shape[py];
      for (int px = 0; px < 4; px++) {
        if (rowMask & (1 << (3 - px))) {
          int bx = gPiece.x + px;
          int by = gPiece.y + py;
          if (bx >= 0 && bx < 8 && by >= 0 && by < 8) {
            displayBoard[by] |= (1 << bx);
          }
        }
      }
    }

    // ---- 4. Pack & send to display queue ----
    DisplayPacket pkt;
    pkt.type      = 0x01;
    pkt.score     = gState.score;
    pkt.nextPiece = gState.nextPiece;
    pkt.haptic    = gState.haptic;
    for (int r = 0; r < 8; r++) {
      pkt.board[r] = displayBoard[r];
    }

    xQueueOverwrite(displayQueue, &pkt);
  }
}

// ====== UART TX Task ======
void UartTxTask(void *pv) {
  (void)pv;
  DisplayPacket pkt;

  while (true) {
    if (xQueueReceive(displayQueue, &pkt, portMAX_DELAY) == pdTRUE) {
      // Send raw bytes over Serial2
      Serial2.write((uint8_t *)&pkt, sizeof(pkt));

      // Debug
      Serial.print("Sent packet, score=");
      Serial.println(pkt.score);
    }
  }
}
