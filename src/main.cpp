/*
Copyright 2021 Kulverstukas

This file is part of airsoft-bomb.

airsoft-bomb is free software: you can redistribute it and/or modify it
under the terms of the GNU General Public License as published by the
Free Software Foundation, either version 3 of the License,
or (at your option) any later version.
airsoft-bomb is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY
or FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for more details.
You should have received a copy of the GNU General Public License along with
airsoft-bomb. If not, see <https://www.gnu.org/licenses/>.
*/

#include <Arduino.h>
#include <WString.h>
#include <Keypad.h>
#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#include <LcdBarGraphI2C.h>
#include <menu.cpp>

/* set this to false to skip compiling battery checking functionality */
#define CHECK_BATTERY false

#define PROJECT_VERSION "1.3"
#define BUZZER_PIN 5
#define T1_BTN_PIN 6
#define T2_BTN_PIN 7
#define SIREN_PIN 8
#define KEYPAD_ROWS 4
#define KEYPAD_COLS 4
#define LCD_COLS 16
#define LCD_ROWS 2
#define BEEP_TONE 1500
#define KEYPAD_LONG_PRESS_TIME 10000
#define TEAM_SWITCH_TIME 5000
#define BOMB_DEFUSE_TIME 10000 // used if defusing with buttons
#define BOMB_ARM_TIME 5000 // used if arming with buttons
#define SIREN_DURATION_START_GAME 8000
#define SIREN_DURATION_END_GAME 12000
#define SIREN_DELAY_TIME 5000
#if CHECK_BATTERY
  #define CELL_PIN 17
  #define CELL_LED 2
  #define MAX_VOLTAGE 4.35 // such value is needed to correctly calculate the actual voltage
#endif

char keys[KEYPAD_ROWS][KEYPAD_COLS] = {
  {'1','2','3', 'a'},
  {'4','5','6', 'b'},
  {'7','8','9', 'c'},
  {'*','0','#', 'd'}
};
byte rowPins[KEYPAD_ROWS] = {12, 11, 10, 9};
byte colPins[KEYPAD_COLS] = {16, 15, 14, 13};
Keypad kpd = Keypad(makeKeymap(keys), rowPins, colPins, KEYPAD_ROWS, KEYPAD_COLS);
bool timerStarted;
bool dominationStarted;
bool zoneControlStarted;
bool defusalStarted;
bool printedLine; // used to prevent refresh of the first line when counting pre-game time
bool showScore; // used to indicate that pre-game time has finished and domination score can now be shown
bool isDisarmed;
bool isDisarming; // used to prevent screen update and switch to progress bar printing in multiple modes
bool isArmed;
bool isArming;
bool isInScoreScreen;
bool useDefusalCode; // should the mode be played with code or not
bool ignoreBtn; // used in defusal mode to check if arming button was released after the bomb was planted
#if CHECK_BATTERY
  bool lowBattery;
#endif
bool teamScoreSwitcher[2];
byte badCodeCounter;
unsigned long startedMillis;
unsigned long currMillisLoop; // this is only used in loop()
unsigned long currMillisDefusal; // only for defusal mode
unsigned long lastMillis; // for timekeeping, to know when to execute a block of code
unsigned long lastBeepMillis; // to know when last time beep happened
unsigned long sirenStartedMillis;
unsigned long timerMillis[2]; // holds delay and game times
unsigned long defusalMillis[2]; // holds delay and game times
unsigned int dominationScore[2];
char defusalCode[MAX_CODE_LEN+1];
int mainMenuLineIdx;

// LCD initialization
LiquidCrystal_I2C lcd(0x27, LCD_COLS, LCD_ROWS);
LcdBarGraphI2C lbg(&lcd, LCD_COLS, 0, 1);

// menu initialization. It's built in menu.cpp file
LiquidMenu mainMenu(lcd);

void playKeypress(char key) {
    noTone(BUZZER_PIN);
    switch (key) {
      case 'c':
        tone(BUZZER_PIN, 1400, 100);
        break;
      case 'd':
        tone(BUZZER_PIN, 400, 100);
        break;
      default:
        tone(BUZZER_PIN, 1000, 100);
    }
}

void useSiren(bool start) {
  if (start) {
    sirenStartedMillis = millis();
    digitalWrite(SIREN_PIN, HIGH);
  } else {
    sirenStartedMillis = 0;
    digitalWrite(SIREN_PIN, LOW);
  }
}

void printToLcd(bool clear, byte col, byte row, const __FlashStringHelper* text) {
  if (clear) lcd.clear();
  lcd.setCursor(col, row);
  lcd.print(text);
}

void drawProgress(int progress, int howLong) {
  lbg.drawValue(progress, howLong);
}

void printTime(unsigned long millis, byte col, byte row) {
  int mins = (millis / 1000L) / 60;
  int secs = (millis / 1000L) % 60;
  lcd.setCursor(col, row);
  if (mins < 10) {
    lcd.print('0');
  }
  lcd.print(mins, DEC);
  lcd.print(':');
  if (secs < 10) {
    lcd.print('0');
  }
  lcd.print(secs, DEC);
  // with this we clear whatever was left after minutes went from 100 to 99
  lcd.print(F("  "));
}

void printDefusalCode(byte col, byte row) {
  lcd.setCursor(col, row);
  lcd.print(defusalCode);
}

// used to clear user input when a button is pressed on a certain line
void resetUserInput() {
  if (mainMenu.get_currentScreen() == &timerScreen) {
    if (mainMenu.get_focusedLine() == 0) {
      userInputDelayStr[0] = '\0';
    } else if (mainMenu.get_focusedLine() == 1) {
      userInputGameStr[0] = '\0';
    }
  } else if (mainMenu.get_currentScreen() == &defusalScreen) {
    if (mainMenu.get_focusedLine() == 0) {
      userInputDelayStr[0] = '\0';
    } else if (mainMenu.get_focusedLine() == 1) {
      userInputBombStr[0] = '\0';
    } else if (mainMenu.get_focusedLine() == 2) {
      memset(userInputCodeStr, 0, sizeof(userInputCodeStr));
      userCodeInputCount = 0;
    }
  }
  userInputCount = 0;
}

// used to clear user input every time a user comes to a screen
void resetAllInput() {
  userInputDelayStr[0] = '\0';
  userInputGameStr[0] = '\0';
  userInputBombStr[0] = '\0';
  userInputCodeStr[0] = '\0';
  defusalCode[0] = '\0';
  userInputCount = 0;
}

void resetInputPos() {
  userInputCount = 0;
  userCodeInputCount = 0;
}

void resetCodeInput() {
  memset(defusalCode, 0, sizeof(defusalCode));
  userCodeInputCount = 0;
  if (isArmed) lcd.setCursor(7, 0);
  else lcd.setCursor(10, 0);
  lcd.print(F("      "));
}

void stopGames() {
  timerStarted = false;
  dominationStarted = false;
  zoneControlStarted = false;
  defusalStarted = false;
}

bool isInGame() {
  return (timerStarted || dominationStarted || zoneControlStarted || defusalStarted);
}

void verifyDefusalCode() {
  bool codeOk = true;
  for (byte i = 0; i < MAX_CODE_LEN; i++) {
    if (defusalCode[i] != userInputCodeStr[i]) {
      codeOk = false;
      break;
    }
  }
  if (isArmed) {
    if (codeOk) {
      isDisarmed = true;
      isArmed = false;
      printToLcd(true, 4, 0, F("DISARMED"));
      printToLcd(false, 0, 1, F("TIME LEFT:"));
      printTime(defusalMillis[1]-currMillisDefusal, 10, 1);
      defusalStarted = false;
      delay(SIREN_DELAY_TIME);
      useSiren(true); // disarmed with code, so end the game
    } else {
      printToLcd(false, 0, 0, F("    BAD CODE    "));
      delay(1000);
      switch (badCodeCounter) { // for bad codes add some penalties
        case 0:
          defusalMillis[1] = (defusalMillis[1]-currMillisDefusal) / 2; // first time cut the time in half
          startedMillis = millis();
          break;
        case 1:
          if ((defusalMillis[1]-currMillisDefusal) > 15000) {
            defusalMillis[1] = 15000; // second time reduce it to 15 secs
            startedMillis = millis();
          }
          break;
        case 2: // third time bomb goes off
          defusalMillis[1] = 0;
          break;
      }
      badCodeCounter++;
      resetCodeInput();
      printedLine = false;
    }
  } else {
    if (codeOk) {
      isArmed = true;
      resetCodeInput();
      lcd.clear();
      startedMillis = millis();
    } else {
      lcd.setCursor(0, 0);
      lcd.print(F("    BAD CODE    "));
      delay(1500);
      resetCodeInput();
    }
    printedLine = false;
  }
}

unsigned int getWaitTimeForBeep(unsigned long totalBombMillis, unsigned long passedMillis) {
  /*
    For simplicity's sake I used my own percentages the way I thought it would be good.
    If one wants to make it exactly like (or at least very close to) how a CSGO bomb beeps,
    then you can follow this article: https://blog.woutergritter.me/2020/07/21/how-i-got-the-csgo-bomb-beep-pattern/
    My beep pattern is like so:
      Game time is 100% - beep every 10 secs.
      When there's 60% left - beep very 5 secs.
      When there's 40% left - beep every 3 sec.
      When there's 20% left - beep every 1 sec.
      When there's 10% left - beep 5 times/sec.
  */

 if (totalBombMillis <= 15000) return 200; // if bomb time was reduced to 15 secs by second bad code
 byte percPassed = 100 - ((passedMillis * 100) / totalBombMillis);
 if ((percPassed <= 100) && (percPassed > 60)) return 10000; // every 10 secs
 else if ((percPassed <= 60) && (percPassed > 40)) return 5000; // every 5 secs
 else if ((percPassed <= 40) && (percPassed > 20)) return 3000; // every 3 sec
 else if ((percPassed <= 20) && (percPassed > 10)) return 1000; // every 1 sec
 else return 200; // 5 times / sec
}

#if CHECK_BATTERY
float fmap(float x, float in_min, float in_max, float out_min, float out_max) {
  return (x - in_min) * (out_max - out_min) / (in_max - in_min) + out_min;
}
#endif

#if CHECK_BATTERY
float getBatteryVolts() {
  int cellInput = analogRead(CELL_PIN);
  float cellVoltage = fmap(cellInput, 0, 1023, 0.0, MAX_VOLTAGE);
  // Serial.print("Cell voltage: ");
  // Serial.println(cellVoltage);
  // delay(500);
  return cellVoltage;
}
#endif

#if CHECK_BATTERY
void checkBattery() {
  float cellVoltage = getBatteryVolts();
  if (cellVoltage < 3.40) {
    lowBattery = true;
    digitalWrite(CELL_LED, HIGH);
  }
}
#endif
//==============================================
void processInput(char key) {
  if (userInputCount >= MAX_USER_INPUT_LEN) userInputCount = 0;
  if (userCodeInputCount >= MAX_CODE_LEN) {
    memset(userInputCodeStr, 0, sizeof(userInputCodeStr));
    userCodeInputCount = 0;
  }
  
  if (mainMenu.get_currentScreen() == &timerScreen) {
    if (mainMenu.get_focusedLine() == 0) {
      userInputDelayStr[userInputCount] = key;
      userInputDelayStr[userInputCount+1] = '\0';
      userInputCount++;
    } else if (mainMenu.get_focusedLine() == 1) {
      userInputGameStr[userInputCount] = key;
      userInputGameStr[userInputCount+1] = '\0';
      userInputCount++;
    }
  } else if (mainMenu.get_currentScreen() == &defusalScreen) {
    if (mainMenu.get_focusedLine() == 0) {
      userInputDelayStr[userInputCount] = key;
      userInputDelayStr[userInputCount+1] = '\0';
      userInputCount++;
    } else if (mainMenu.get_focusedLine() == 1) {
      userInputBombStr[userInputCount] = key;
      userInputBombStr[userInputCount+1] = '\0';
      userInputCount++;
    } else if (mainMenu.get_focusedLine() == 2) {
      userInputCodeStr[userCodeInputCount] = key;
      userInputCodeStr[userCodeInputCount+1] = '\0';
      userCodeInputCount++;
    }
  }
}
//---------------------
void processDefusalInput(char key) {
  if (userCodeInputCount >= MAX_CODE_LEN) {
    memset(defusalCode, 0, sizeof(defusalCode));
    userCodeInputCount = 0;
    if (isArmed) lcd.setCursor(7, 0);
    else lcd.setCursor(10, 0);
    lcd.print(F("      "));
  }
  defusalCode[userCodeInputCount] = key;
  defusalCode[userCodeInputCount+1] = '\0';
  userCodeInputCount++;
}
//---------------------
void processKeypress(char key) {
  if (key != NO_KEY) {
    playKeypress(key);
    switch (key) {
      case 'a':
        if (!isInGame() && !isInScoreScreen) {
          mainMenu.switch_focus(false);
          resetInputPos();
        }
        break;
      case 'b':
        if (!isInGame() && !isInScoreScreen) {
          mainMenu.switch_focus(true);
          resetInputPos();
        }
        break;
      case 'c':
        if (!isInGame() && !isInScoreScreen) {
          if (mainMenu.get_currentScreen() == &mainScreen) {
            mainMenuLineIdx = mainMenu.get_focusedLine();
          }
          mainMenu.call_function(1);
          useSiren(false);
        }
        break;
      case 'd':
        if (!isInGame() && !isInScoreScreen) {
          mainMenu.change_screen(&mainScreen);
          mainMenu.set_focusedLine(mainMenuLineIdx);
          stopGames();
          useSiren(false);
        }
        break;
      case '*':
        if (defusalStarted && (defusalMillis[0] == 0) && useDefusalCode) resetCodeInput();
        break;
      case '#':
        if (defusalStarted && (defusalMillis[0] == 0) && useDefusalCode) verifyDefusalCode();
        break;
      default:
        if (!isInGame() && !isInScoreScreen) processInput(key);
        if (defusalStarted && (defusalMillis[0] == 0) && useDefusalCode) processDefusalInput(key);
        break;
    }
    if (!isInGame() && !isInScoreScreen) mainMenu.update();
  }
}
//---------------------
// this only fires when a game is in progress to prevent accidents
void processHoldKeypress(char key) {
  if (key != NO_KEY) {
    switch (key) {
      case 'c':
        // reset the game
        if (!isInGame() && isInScoreScreen) {
          mainMenu.call_function(1);
          useSiren(false);
        }
        break;
      case 'd':
        // go to main menu
        if (!isInGame() && isInScoreScreen) {
          mainMenu.change_screen(&mainScreen);
          mainMenu.set_focusedLine(mainMenuLineIdx);
          mainMenu.update();
          stopGames();
          useSiren(false);
          isInScoreScreen = false;
        } else {
          for (int i = 0; i < LIST_MAX; i++) {
            if ((kpd.key[i].kchar == '*') && (kpd.key[i].kstate == HOLD)) {
              mainMenu.change_screen(&mainScreen);
              mainMenu.set_focusedLine(mainMenuLineIdx);
              stopGames();
              useSiren(false);
              mainMenu.update();
              isInScoreScreen = false;
            }
          }
        }
        break;
    }
  }
}
//---------------------
void keypadEvent(KeypadEvent key) {
  switch (kpd.getState()) {
    case IDLE:
      break;

    case RELEASED:
      break;

    case HOLD:
      processHoldKeypress(key);
      break;

    case PRESSED:
      processKeypress(key);
      break;
  }
}
//==============================================
// callback function, only setup variables here
void startDefusal() {
  if (atoi(userInputBombStr) == 0) {
    printToLcd(true, 0, 0, F("*INVALID INPUT*"));
    printToLcd(false, 1, 1, F("* BOMB TIME *"));
    delay(3000);
    mainMenu.set_focusedLine(1);
    return;
  }
  // we don't swap values in this mode, only in domination and timer
  defusalMillis[0] = (atoi(userInputDelayStr) * 1000L) * 60; // delay time
  defusalMillis[1] = (atoi(userInputBombStr) * 1000L) * 60; // bomb time
  resetCodeInput();
  useDefusalCode = (userInputCodeStr[0] != '\0');
  startedMillis = 0;
  lastMillis = 0;
  badCodeCounter = 0;
  userCodeInputCount = 0;
  printedLine = false;
  isArmed = false;
  isArming = false;
  isDisarmed = false;
  isDisarming = false;
  defusalStarted = true;
  isInScoreScreen = true;
}
//---------------------
void updateDefusal() {
  if (defusalMillis[0] > 0) { // delay time was entered
    if (startedMillis == 0) startedMillis = millis();
    unsigned long currMillis = millis() - startedMillis;
    if ((millis() - lastMillis) >= 1000) { // don't need to re-draw more than once per second
      lastMillis = millis();
      if (!printedLine) {
        printToLcd(true, 1, 0, F("PREP FOR GAME"));
        printedLine = true;
      }
      if (currMillis >= defusalMillis[0]) {
        defusalMillis[0] = 0;
        startedMillis = 0;
        printedLine = false;
        useSiren(true);
      } else {
        printTime((defusalMillis[0]-currMillis), 5, 1);
      }
    }
  } else {
    currMillisDefusal = millis() - startedMillis;
    // if code is used, we need to update the screen more often
    if (((millis() - lastMillis) >= ((useDefusalCode) ? 100 : 1000)) && (useDefusalCode || (!isArming && !isDisarming))) {
      lastMillis = millis();
      if (!isArmed && !isDisarmed) {
        if (useDefusalCode) {
          if (!printedLine) {
            printToLcd(false, 0, 0, F("ARM CODE:       "));
            printedLine = true;
          }
          printDefusalCode(10, 0);
        } else {
          if (!printedLine) {
            printToLcd(false, 0, 0, F("     READY      "));
            printedLine = true;
          }
        }
        printTime(defusalMillis[1], 10, 1);
      } else if (isDisarmed) {
        printToLcd(true, 4, 0, F("DISARMED"));
        printToLcd(false, 0, 1, F("TIME LEFT:"));
        printTime(defusalMillis[1]-currMillisDefusal, 10, 1);
        defusalStarted = false;
        delay(SIREN_DELAY_TIME);
        useSiren(true); // end the game when disarmed with buttons
      } else if (isArmed) {
        if (useDefusalCode) {
          if (!printedLine) {
            printToLcd(false, 0, 0, F("ARMED: "));
            printedLine = true;
          }
          printDefusalCode(7, 0);
        } else {
          if (!printedLine) {
            printToLcd(false, 0, 0, F("     ARMED      "));
            printedLine = true;
          }
        }
        printTime(defusalMillis[1]-currMillisDefusal, 10, 1);
      }
      printToLcd(false, 0, 1, F("TIME LEFT:"));
    }
    if (isArmed && (currMillisDefusal > defusalMillis[1])) {
      printToLcd(true, 4, 0, F("EXPLODED"));
      printToLcd(false, 0, 1, F("TIME LEFT:00:00"));
      defusalStarted = false;
      delay(SIREN_DELAY_TIME);
      useSiren(true); // end the game when time runs out
    }
    if (defusalStarted && isArmed) {
      if (!useDefusalCode && (lastBeepMillis == 0)) { // skip first beep when the bomb has just been planted with buttons
        lastBeepMillis = millis();
        return;
      }
      unsigned int waitTime = getWaitTimeForBeep(defusalMillis[1], currMillisDefusal);
      if ((millis() - lastBeepMillis) > waitTime) {
        lastBeepMillis = millis();
        tone(BUZZER_PIN, BEEP_TONE, 125); // 125 millis is the same as in CSGO, apparently
      }
    }
  }
}
//---------------------
void defusal() {
  startLine.attach_function(1, startDefusal);
  resetAllInput();
  mainMenu.change_screen(&defusalScreen);
  mainMenu.set_focusedLine(0);
  mainMenu.update();
}
//==============================================
// callback function, only setup variables here
void startDomination() {
  printedLine = false;
  showScore = false;
  dominationScore[0] = 0;
  dominationScore[1] = 0;
  teamScoreSwitcher[0] = false;
  teamScoreSwitcher[1] = false;
  lastMillis = 0;
  timerMillis[0] = (atoi(userInputDelayStr) * 1000L) * 60; // this holds the time to compare to
  timerMillis[1] = (atoi(userInputGameStr) * 1000L) * 60; // this holds next time to count
  if (timerMillis[1] == 0) {
    printToLcd(true, 0, 0, F("*INVALID INPUT*"));
    printToLcd(false, 1, 1, F("* GAME TIME *"));
    delay(3000);
    mainMenu.set_focusedLine(1);
  } else {
    dominationStarted = true;
    isInScoreScreen = true;
  }
  startedMillis = millis();
}
//---------------------
void updateDomination() {
  unsigned long currMillis = millis() - startedMillis;
  if (currMillis >= timerMillis[0]) {
    if (timerMillis[1] == 0) {
      dominationStarted = false;
      printToLcd(false, 0, 0, F("DOMINATION ENDED"));
      useSiren(true); // end the game
    } else {
      if (timerMillis[0] > 0) useSiren(true); // start the game
      timerMillis[0] = timerMillis[1];
      timerMillis[1] = 0;
      showScore = true;
      lcd.clear();
      startedMillis = millis();
    }
  } else if ((millis() - lastMillis) >= 1000) {
    lastMillis = millis();
    if (!showScore) {
      if (!printedLine) {
        printToLcd(true, 1, 0, F("PREP FOR GAME"));
        printedLine = true;
      }
      printTime((timerMillis[0]-currMillis), 5, 1);
    } else {
      if (teamScoreSwitcher[0]) dominationScore[0]++;
      if (teamScoreSwitcher[1]) dominationScore[1]++;
      printToLcd(false, 0, 0, F("TIME LEFT:"));
      printTime((timerMillis[0]-currMillis), 10, 0);
      if (!isDisarming) { // only print score if progressbar isn't showing
        printToLcd(false, 0, 1, F("T1:      ")); // need to print with spaces to clear progress left-overs
        printToLcd(false, 9, 1, F("T2:    "));
        lcd.setCursor(3, 1);
        lcd.print(dominationScore[0], DEC);
        lcd.setCursor(12, 1);
        lcd.print(dominationScore[1], DEC);
      }
    }
  }
}
//---------------------
void domination() {
  // we re-use the same input interface from timer to save space, only change the callback
  startLine.attach_function(1, startDomination);
  resetAllInput();
  mainMenu.change_screen(&timerScreen);
  mainMenu.set_focusedLine(0);
  mainMenu.update();
}
//==============================================
// callback function, only setup variables here
void startZoneControl() {
  printedLine = false;
  isInScoreScreen = true;
  lastMillis = 0;
  dominationScore[0] = 0;
  dominationScore[1] = 0;
  teamScoreSwitcher[0] = false;
  teamScoreSwitcher[1] = false;
  zoneControlStarted = true;
}
//---------------------
void updateZoneControl() {
  if ((millis() - lastMillis) >= 1000) {
    lastMillis = millis();
    if (teamScoreSwitcher[0]) dominationScore[0]++;
    if (teamScoreSwitcher[1]) dominationScore[1]++;
    if (!isDisarming) { // only print score if progressbar isn't showing
      if (!printedLine) {
        printToLcd(true, 0, 0, F("TEAM 1:"));
        printToLcd(false, 9, 0, F("TEAM 2:"));
        printedLine = true;
      }
      lcd.setCursor(0, 1);
      lcd.print(dominationScore[0], DEC);
      lcd.setCursor(9, 1);
      lcd.print(dominationScore[1], DEC);
    } else {
      if (!printedLine) {
        printToLcd(false, 3, 0, F("CAPTURING"));
        printedLine = true;
      }
    }
  }
}
//---------------------
void zoneControl() {
  // empty, only keep this for pretty format
}
//==============================================
// callback function, only setup variables here
void startTimer() {
  printedLine = false;
  lastMillis = 0;
  timerMillis[0] = (atoi(userInputDelayStr) * 1000L) * 60; // this holds the time to compare to
  timerMillis[1] = (atoi(userInputGameStr) * 1000L) * 60; // this holds next time to count
  if ((timerMillis[0] == 0) || timerMillis[1] == 0) {
    printToLcd(true, 0, 0, F("*INVALID INPUT*"));
    if (timerMillis[0] == 0) {
      printToLcd(false, 1, 1, F("* DELAY TIME *"));
      mainMenu.set_focusedLine(0);
    } else if (timerMillis[1] == 0) {
      printToLcd(false, 1, 1, F("* GAME TIME *"));
      mainMenu.set_focusedLine(1);
    }
    delay(3000);
  } else {
    timerStarted = true;
    isInScoreScreen = true;
  }
  startedMillis = millis();
}
//---------------------
void updateTimer() {
  unsigned long currMillis = millis() - startedMillis;
  if (currMillis >= timerMillis[0]) {
    if (timerMillis[1] == 0) {
      timerStarted = false;
      printToLcd(true, 3, 0, F("GAME ENDED"));
      useSiren(true);
    } else {
      printToLcd(true, 2, 0, F("GAME STARTED"));
      timerMillis[0] = timerMillis[1];
      timerMillis[1] = 0;
      startedMillis = millis();
      useSiren(true);
    }
  } else if ((millis() - lastMillis) >= 1000) {
    lastMillis = millis();
    if (!printedLine) {
      printToLcd(true, 1, 0, F("PREP FOR GAME"));
      printedLine = true;
    }
    printTime((timerMillis[0]-currMillis), 5, 1);
  }
}
//---------------------
void timer() {
  // attach callback here because we use the same interface for domination
  startLine.attach_function(1, startTimer);
  resetAllInput();
  mainMenu.change_screen(&timerScreen);
  mainMenu.set_focusedLine(0);
  mainMenu.update();
}
//==============================================
void setup() {
  // Serial.begin(115200);

  pinMode(T1_BTN_PIN, INPUT_PULLUP);
  pinMode(T2_BTN_PIN, INPUT_PULLUP);
  pinMode(BUZZER_PIN, OUTPUT);
  pinMode(SIREN_PIN, OUTPUT);
  #if CHECK_BATTERY
    pinMode(CELL_PIN, INPUT);
    pinMode(CELL_LED, OUTPUT);
  #endif

  noTone(BUZZER_PIN);

  kpd.setDebounceTime(10);
  kpd.setHoldTime(KEYPAD_LONG_PRESS_TIME);
  kpd.addEventListener(keypadEvent);

  lcd.init();
  lcd.clear();
  lcd.backlight();
  delay(100);

  #if CHECK_BATTERY
    if ((digitalRead(T1_BTN_PIN) == LOW) || (digitalRead(T2_BTN_PIN) == LOW)) {
      printToLcd(false, 0, 0, F("Battery:"));
      float cellVoltage = getBatteryVolts();
      lcd.setCursor(9, 0);
      lcd.print(cellVoltage);
      delay(3000);
    }
  #endif

  setupProgmems();
  setupScreens();

  defusalLine.attach_function(1, defusal);
  dominationLine.attach_function(1, domination);
  zoneControlLine.attach_function(1, startZoneControl);
  timerLine.attach_function(1, timer);
  mainMenu.add_screen(mainScreen);

  defusalDelayTime.attach_function(1, resetUserInput);
  defusalBombTime.attach_function(1, resetUserInput);
  defusalBombCode.attach_function(1, resetUserInput);
  mainMenu.add_screen(defusalScreen);

  timerDelayTime.attach_function(1, resetUserInput);
  timerGameTime.attach_function(1, resetUserInput);
  mainMenu.add_screen(timerScreen);

  mainMenu.set_focusPosition(Position::LEFT);
  mainMenu.switch_focus(1);

  printToLcd(true, 1, 0, F("makerspace.lt"));
  printToLcd(false, 1, 1, F("Bomb prop v"));
  printToLcd(false, 12, 1, F(PROJECT_VERSION));
  delay(1500);
  mainMenu.update();
}

void loop() {
  kpd.getKey(); // this is required to fire off attached events

  #if CHECK_BATTERY
    if (!lowBattery) checkBattery();
  #endif

  if (sirenStartedMillis > 0) { // check to see if we need to stop the siren already, yo
    if (isInGame() && (millis() - sirenStartedMillis) > SIREN_DURATION_START_GAME) useSiren(false);
    else if (!isInGame() && (millis() - sirenStartedMillis) > SIREN_DURATION_END_GAME) useSiren(false);
  }

  if ((dominationStarted && showScore) || zoneControlStarted) {
    if ((digitalRead(T1_BTN_PIN) == LOW) && !teamScoreSwitcher[0]) {
      if (currMillisLoop == 0) currMillisLoop = millis();
      int millisDiff = millis() - currMillisLoop;
      if (!isDisarming) {
        isDisarming = true;
        lcd.clear();
        lastMillis = 0; // set to 0 to show time immediately
      }
      if (zoneControlStarted) printedLine = false;
      drawProgress(millisDiff, TEAM_SWITCH_TIME);
      if (millisDiff >= TEAM_SWITCH_TIME) {
        isDisarming = false;
        lastMillis = 0; // set to 0 to show score immediately
        currMillisLoop = 0;
        teamScoreSwitcher[0] = true;
        teamScoreSwitcher[1] = false;
        tone(BUZZER_PIN, 700, 2000);
      }
    } else if ((digitalRead(T2_BTN_PIN) == LOW) && !teamScoreSwitcher[1]) {
      if (currMillisLoop == 0) currMillisLoop = millis();
      int millisDiff = millis() - currMillisLoop;
      if (!isDisarming) {
        isDisarming = true;
        lcd.clear();
        lastMillis = 0; // set to 0 to show time immediately
      }
      if (zoneControlStarted) printedLine = false;
      drawProgress(millisDiff, TEAM_SWITCH_TIME);
      if (millisDiff >= TEAM_SWITCH_TIME) {
        isDisarming = false;
        lastMillis = 0; // set to 0 to show score immediately
        currMillisLoop = 0;
        teamScoreSwitcher[0] = false;
        teamScoreSwitcher[1] = true;
        tone(BUZZER_PIN, 700, 2000);
      }
    } else { // if both buttons are not pressed
      if (isDisarming) {
        isDisarming = false;
        lastMillis = 0; // set to 0 to show score immediately
      }
      if (currMillisLoop != 0) currMillisLoop = 0;
    }
  }

  if (defusalStarted && (defusalMillis[0] == 0)) {
    if (ignoreBtn) {
      // check if button was released after planting the bomb to not start defusing immediately if someone keeps holding the button
      ignoreBtn = ((digitalRead(T1_BTN_PIN) == LOW) || (digitalRead(T2_BTN_PIN) == LOW));
    }
    // use any of two buttons to arm and defuse
    if (!ignoreBtn && !useDefusalCode && ((digitalRead(T1_BTN_PIN) == LOW) || (digitalRead(T2_BTN_PIN) == LOW))) {
      if (currMillisLoop == 0) {
        currMillisLoop = millis();
        lcd.clear();
      }
      int millisDiff = millis() - currMillisLoop;
      if (!isArmed && !isDisarmed) {
        if (!isArming) isArming = true;
        printToLcd(false, 5, 0, F("ARMING"));
        drawProgress(millisDiff, BOMB_ARM_TIME);
        printedLine = false;
        if (millisDiff >= BOMB_ARM_TIME) {
          isArmed = true;
          isArming = false;
          lcd.clear();
          tone(BUZZER_PIN, 700, 2000);
          startedMillis = millis();
          currMillisLoop = 0;
          ignoreBtn = ((digitalRead(T1_BTN_PIN) == LOW) || (digitalRead(T2_BTN_PIN) == LOW));
        }
      } else if (!isDisarmed) {
        if (!isDisarming) isDisarming = true;
        printToLcd(false, 0, 0, F("DISARMING"));
        printTime(defusalMillis[1]-currMillisDefusal, 10, 0);
        drawProgress(millisDiff, BOMB_DEFUSE_TIME);
        printedLine = false;
        if (millisDiff >= BOMB_DEFUSE_TIME) {
          isArmed = false;
          isDisarmed = true;
          isDisarming = false;
          lcd.clear();
        }
      }
    } else {
      if (isArming) isArming = false;
      if (isDisarming) isDisarming = false;
      if (currMillisLoop != 0) currMillisLoop = 0;
    }
  }

  if (timerStarted) {
    updateTimer();
  } else if (dominationStarted) {
    updateDomination();
  } else if (zoneControlStarted) {
    updateZoneControl();
  } else if (defusalStarted) {
    updateDefusal();
  }
}
