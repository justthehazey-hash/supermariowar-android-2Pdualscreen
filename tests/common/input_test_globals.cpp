// The input tests only need the game values object; defining it here keeps the
// test binary from pulling in the rest of the game's globals (graphics, maps...).
#include "GameValues.h"

CGameValues game_values;
SDL_Joystick** joysticks = nullptr;
int joystickcount = 0;
