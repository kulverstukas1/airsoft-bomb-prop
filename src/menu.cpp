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

#include <LiquidMenu.h>

#define MAX_USER_INPUT_LEN 3
#define MAX_CODE_LEN 6

const char DELAY_STR[] PROGMEM = "Delay min: ";
const char GAME_STR[] PROGMEM = "Game  min: ";
const char CODE_STR[] PROGMEM = "Code: ";
const char BOMB_STR[] PROGMEM = "Bomb  min: ";
const char DEFUSAL_STR[] PROGMEM = "Defusal";
const char DOMINATION_STR[] PROGMEM = "Domination";
const char TIMER_STR[] PROGMEM = "Timer";
const char START_STR[] PROGMEM = "START";

char userInputDelayStr[MAX_USER_INPUT_LEN+1];
char* userInputDelayPtr = userInputDelayStr;
char userInputGameStr[MAX_USER_INPUT_LEN+1];
char* userInputGamePtr = userInputGameStr;
char userInputBombStr[MAX_USER_INPUT_LEN+1];
char* userInputBombPtr = userInputBombStr;
char userInputCodeStr[MAX_CODE_LEN+1];
char* userInputCodePtr = userInputCodeStr;
byte userInputCount = 0;
byte userCodeInputCount = 0;

LiquidLine defusalLine(1, 0, DEFUSAL_STR);
LiquidLine dominationLine(1, 1, DOMINATION_STR);
LiquidLine timerLine(1, 1, TIMER_STR);
LiquidScreen mainScreen(defusalLine, dominationLine, timerLine);

LiquidLine startLine(1, 1, START_STR);

LiquidLine defusalDelayTime(1, 0, DELAY_STR, userInputDelayPtr);
LiquidLine defusalBombTime(1, 1, BOMB_STR, userInputBombPtr);
LiquidLine defusalBombCode(1, 1, CODE_STR, userInputCodePtr);
LiquidScreen defusalScreen(defusalDelayTime, defusalBombTime, defusalBombCode, startLine);

LiquidLine timerDelayTime(1, 0, DELAY_STR, userInputDelayPtr);
LiquidLine timerGameTime(1, 1, GAME_STR, userInputGamePtr);
LiquidScreen timerScreen(timerDelayTime, timerGameTime, startLine);

void setupScreens() {
    mainScreen.set_displayLineCount(2);
    defusalScreen.set_displayLineCount(2);
    timerScreen.set_displayLineCount(2);
}

void setupProgmems() {
    defusalLine.set_asProgmem(1);
    dominationLine.set_asProgmem(1);
    timerLine.set_asProgmem(1);
    startLine.set_asProgmem(1);
    defusalDelayTime.set_asProgmem(1);
    defusalBombTime.set_asProgmem(1);
    defusalBombCode.set_asProgmem(1);
    timerDelayTime.set_asProgmem(1);
    timerGameTime.set_asProgmem(1);
}