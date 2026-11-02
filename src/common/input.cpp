#include "input.h"

#include "GameValues.h"
#include "GlobalConstants.h"

#include <atomic>

extern CGameValues game_values;

extern int joystickcount;
extern SDL_Joystick** joysticks;

namespace {
// Players store a joystick by position (0..count-1); SDL3 events carry the
// joystick's instance ID. 0 is never a valid instance ID.
SDL_JoystickID joystickIdForDevice(int device)
{
    if (device < 0 || device >= joystickcount || !joysticks || !joysticks[device])
        return 0;
    return SDL_GetJoystickID(joysticks[device]);
}

// Intentional cast from -1 as a keyboard sentinel
constexpr SDL_JoystickID KEYBOARD_DEVICE_ID = static_cast<SDL_JoystickID>(DEVICE_KEYBOARD);
std::atomic<Uint32> padEventType {0};
} // namespace

void CPlayerInput::RegisterPadEvents()
{
    if (padEventType.load() == 0) {
        Uint32 type = SDL_RegisterEvents(1); // 0 when no event types are left
        if (type != 0)
            padEventType.store(type);
    }
}

bool CPlayerInput::PushPadEvent(short iPlayer, short iButton, bool fDown)
{
    Uint32 type = padEventType.load();
    if (type == 0 || iPlayer < 0 || iPlayer >= MAX_PLAYERS)
        return false;

    SDL_Event event;
    SDL_zero(event);
    event.type = type;
    event.user.code = (iPlayer << 8) | (iButton & 0xFF);
    event.user.data1 = (void*)(intptr_t)(fDown ? 1 : 0);
    return SDL_PushEvent(&event);
}

bool CPlayerInput::IsPadEvent(const SDL_Event& event)
{
    Uint32 type = padEventType.load();
    return type != 0 && event.type == type;
}

short CPlayerInput::PadKeyIndex(short iButton, short iGameState)
{
    if (iGameState == 1) {
        //up, down, left, right, select, cancel, random, fast scroll
        switch (iButton) {
            case PAD_UP: return 0;
            case PAD_DOWN: return 1;
            case PAD_LEFT: return 2;
            case PAD_RIGHT: return 3;
            case PAD_A: case PAD_START: return 4;
            case PAD_B: case PAD_SELECT: return 5;
            case PAD_X: return 6;
            case PAD_Y: case PAD_L: case PAD_R: return 7;
        }
    } else {
        //left, right, jump, down, turbo, powerup, start, cancel
        //Up does nothing in a match so a tilted stick cannot jump; A jumps.
        switch (iButton) {
            case PAD_LEFT: return 0;
            case PAD_RIGHT: return 1;
            case PAD_A: return 2;
            case PAD_DOWN: return 3;
            case PAD_B: case PAD_X: return 4;
            case PAD_Y: case PAD_L: case PAD_R: return 5;
            case PAD_START: return 6;
            case PAD_SELECT: return 7;
        }
    }
    return -1;
}

void CPlayerInput::PadButton(short iPlayer, short iButton, bool fDown, short iGameState)
{
    if (iPlayer < 0 || iPlayer >= MAX_PLAYERS || iButton < 0 || iButton >= PAD_BUTTONS)
        return;

    padAttached[iPlayer] = true;
    COutputControl* outputControl = &outputControls[iPlayer];
    short* held = padHeld[iPlayer];

    short iKey;
    if (fDown) {
        if (held[iButton] >= 0)
            return; //already down

        iKey = PadKeyIndex(iButton, iGameState);
        held[iButton] = iKey;
        padHeldFrames[iPlayer][iButton] = 0;
        if (iKey < 0)
            return;

        //Ignore input for cpu controlled players (same rule as keyboard input)
        if (iGameState == 0 && game_values.playercontrol[iPlayer] != 1 && iKey < 6)
            return;

        if (!outputControl->keys[iKey].fDown)
            outputControl->keys[iKey].fPressed = true;
        outputControl->keys[iKey].fDown = true;
    } else {
        //Release the key this button pressed, even if the screen changed meanwhile
        iKey = held[iButton];
        held[iButton] = -1;
        if (iKey < 0)
            return;

        //Another held button (e.g. B and X both run) keeps the key down
        for (short b = 0; b < PAD_BUTTONS; b++)
            if (held[b] == iKey)
                return;

        outputControl->keys[iKey].fDown = false;
    }
}

void CPlayerInput::PadDetach(short iPlayer)
{
    if (iPlayer < 0 || iPlayer >= MAX_PLAYERS)
        return;

    for (short b = 0; b < PAD_BUTTONS; b++)
        if (padHeld[iPlayer][b] >= 0)
            PadButton(iPlayer, b, false, 0);

    padAttached[iPlayer] = false;
}

bool CPlayerInput::UsesSharedKeyboard(short iPlayer) const
{
    if (iPlayer <= 0 || iPlayer >= MAX_PLAYERS)
        return false;
    if (padAttached[iPlayer])
        return false;
    return inputControls[iPlayer] && inputControls[iPlayer]->iDevice == DEVICE_KEYBOARD;
}

CPlayerInput::CPlayerInput()
{
    for (int iPlayer = 0; iPlayer < MAX_PLAYERS; iPlayer++) {
        for (int iKey = 0; iKey < NUM_KEYS; iKey++) {
			outputControls[iPlayer].keys[iKey].fPressed = false;
			outputControls[iPlayer].keys[iKey].fDown = false;
		}
	}

    for (int iPlayer = 0; iPlayer < MAX_PLAYERS; iPlayer++) {
        padAttached[iPlayer] = false;
        for (int b = 0; b < PAD_BUTTONS; b++) {
            padHeld[iPlayer][b] = -1;
            padHeldFrames[iPlayer][b] = 0;
        }
    }

    iPressedKey = 0;
}

//Pass in 0 for game and 1 for menu
//Clear old button pushed states
void CPlayerInput::ClearPressedKeys(short iGameState)
{
    for (int iPlayer = 0; iPlayer < MAX_PLAYERS; iPlayer++) {
        COutputControl* outputControl = &outputControls[iPlayer];
        for (int iKey = 0; iKey < NUM_KEYS; iKey++) {
            outputControl->keys[iKey].fPressed = false;
        }
    }

    //Menus: a held controller direction repeats, like a held keyboard key
    //(first repeat after ~0.4s, then ~10 per second at the normal frame rate)
    if (iGameState == 1) {
        const short kRepeatDelay = 18, kRepeatInterval = 6;
        for (int iPlayer = 0; iPlayer < MAX_PLAYERS; iPlayer++) {
            for (short b = PAD_UP; b <= PAD_RIGHT; b++) {
                const short iKey = padHeld[iPlayer][b];
                if (iKey < 0)
                    continue;
                short& frames = padHeldFrames[iPlayer][b];
                if (frames < kRepeatDelay + kRepeatInterval)
                    frames++;
                if (frames >= kRepeatDelay + kRepeatInterval) {
                    frames = kRepeatDelay;
                    if (outputControls[iPlayer].keys[iKey].fDown)
                        outputControls[iPlayer].keys[iKey].fPressed = true;
                }
            }
        }
    }

    iPressedKey = 0;
}

void CPlayerInput::ClearGameActionKeys()
{
    for (int iPlayer = 0; iPlayer < MAX_PLAYERS; iPlayer++) {
		COutputControl * outputControl = &outputControls[iPlayer];

		// 0-3: direction keys, 4: turbo
        for (int iKey = 5; iKey < NUM_KEYS; iKey++) {
			outputControl->keys[iKey].fPressed = false;
			outputControl->keys[iKey].fDown = false;
		}
	}

	iPressedKey = 0;
}

//Clear all button pushed and down states
//Call this when switching from menu to game
void CPlayerInput::ResetKeys()
{
    for (int iPlayer = 0; iPlayer < MAX_PLAYERS; iPlayer++) {
        for (int iKey = 0; iKey < NUM_KEYS; iKey++) {
			outputControls[iPlayer].keys[iKey].fPressed = false;
			outputControls[iPlayer].keys[iKey].fDown = false;
		}
	}

	iPressedKey = 0;
}

//Called during game loop to read input events and see if
//configured keys were pressed.  If they were, then turn on
//key flags to be used by game logic
//iGameState == 0 for in game and 1 for menu
void CPlayerInput::Update(SDL_Event event, short iGameState)
{
    if (IsPadEvent(event)) {
        short iPlayer = (event.user.code >> 8) & 0xFF;
        short iButton = event.user.code & 0xFF;
        if (iButton == PAD_DETACH)
            PadDetach(iPlayer);
        else
            PadButton(iPlayer, iButton, event.user.data1 != nullptr, iGameState);
        return;
    }

	bool fFound = false;
    for (short iPlayer = -1; iPlayer < MAX_PLAYERS; iPlayer++) {
		CInputControl * inputControl;
		COutputControl * outputControl;
		SDL_JoystickID iDeviceID = KEYBOARD_DEVICE_ID;

		//Allow keyboard input from player 1 at all times (even when he is configured to use joystick)
        if (iPlayer == -1) {
            if (iGameState == 1 && inputControls[0]->iDevice != DEVICE_KEYBOARD) {
				inputControl = &game_values.inputConfiguration[0][0].inputGameControls[1];
				outputControl = &outputControls[0];
				iDeviceID = game_values.inputConfiguration[0][0].iDevice;
            } else {
				continue;
			}
        } else {
			if (!inputControls[iPlayer])
				continue;

			inputControl = &inputControls[iPlayer]->inputGameControls[iGameState];
			outputControl = &outputControls[iPlayer];
			iDeviceID = inputControls[iPlayer]->iDevice;
		}
		if (iDeviceID != KEYBOARD_DEVICE_ID)
			iDeviceID = joystickIdForDevice(static_cast<short>(iDeviceID));

        if (iDeviceID == KEYBOARD_DEVICE_ID) {
            if (SDL_EVENT_KEY_DOWN == event.type) {
                for (int iKey = 0; iKey < NUM_KEYS && !fFound; iKey++) {
                    if (inputControl->keys[iKey] == event.key.key) {
						fFound = true;

						//Ignore input for cpu controlled players
						if (iGameState == 0 && game_values.playercontrol[iPlayer] != 1 && iKey < 6)
							continue;

						if (!outputControl->keys[iKey].fDown)
							outputControl->keys[iKey].fPressed = true;

						outputControl->keys[iKey].fDown = true;
					}
				}

				iPressedKey = event.key.key;
            } else if (SDL_EVENT_KEY_UP == event.type) {
                for (int iKey = 0; iKey < NUM_KEYS && !fFound; iKey++) {
                    if (inputControl->keys[iKey] == event.key.key) {
						fFound = true;

						//Ignore input for cpu controlled players
						if (iGameState == 0 && game_values.playercontrol[iPlayer] != 1 && iKey < 6)
							continue;

						outputControl->keys[iKey].fDown = false;
					}
				}
            } else if (SDL_EVENT_MOUSE_MOTION == event.type) {
                for (int iKey = 0; iKey < NUM_KEYS && !fFound; iKey++) {
                    if (inputControl->keys[iKey] >= MOUSE_UP) {
						if ((inputControl->keys[iKey] == MOUSE_UP && event.motion.yrel < -MOUSE_Y_DEAD_ZONE) ||
							(inputControl->keys[iKey] == MOUSE_DOWN && event.motion.yrel > MOUSE_Y_DEAD_ZONE) ||
							(inputControl->keys[iKey] == MOUSE_LEFT && event.motion.xrel < -MOUSE_X_DEAD_ZONE) ||
							(inputControl->keys[iKey] == MOUSE_RIGHT && event.motion.xrel > MOUSE_X_DEAD_ZONE) ||
                                (inputControl->keys[iKey] >= MOUSE_BUTTON_START && (event.motion.state & SDL_BUTTON_MASK(inputControl->keys[iKey] - MOUSE_BUTTON_START)))) {
							fFound = true;

							//Ignore input for cpu controlled players
							if (iGameState == 0 && game_values.playercontrol[iPlayer] != 1 && iKey < 6)
								continue;

							if (!outputControl->keys[iKey].fDown)
								outputControl->keys[iKey].fPressed = true;

							outputControl->keys[iKey].fDown = true;
                        } else {
							//Ignore input for cpu controlled players
							if (iGameState == 0 && game_values.playercontrol[iPlayer] != 1 && iKey < 6)
								continue;

							//Mouse scroll wheel up/down events happen on same frame so ignore up event (and clear it in the ClearPressedKeys() method)
							if (inputControl->keys[iKey] == MOUSE_BUTTON_START + 4 || inputControl->keys[iKey] == MOUSE_BUTTON_START + 5)
								continue;

							outputControl->keys[iKey].fDown = false;
						}
					}
				}
            } else if (SDL_EVENT_MOUSE_BUTTON_DOWN == event.type) {
                for (int iKey = 0; iKey < NUM_KEYS && !fFound; iKey++) {
                    if (inputControl->keys[iKey] == event.button.button + MOUSE_BUTTON_START) {
						fFound = true;

						//Ignore input for cpu controlled players
						if (iGameState == 0 && game_values.playercontrol[iPlayer] != 1 && iKey < 6)
							continue;

						if (!outputControl->keys[iKey].fDown)
							outputControl->keys[iKey].fPressed = true;

						outputControl->keys[iKey].fDown = true;
					}
				}
            } else if (SDL_EVENT_MOUSE_BUTTON_UP == event.type) {
                for (int iKey = 0; iKey < NUM_KEYS && !fFound; iKey++) {
                    if (inputControl->keys[iKey] == event.button.button + MOUSE_BUTTON_START) {
						fFound = true;

						//Mouse scroll wheel up/down events happen on same frame so ignore up event (and clear it in the ClearPressedKeys() method)
						if (inputControl->keys[iKey] == MOUSE_BUTTON_START + 4 || inputControl->keys[iKey] == MOUSE_BUTTON_START + 5)
							continue;

						//Ignore input for cpu controlled players
						if (iGameState == 0 && game_values.playercontrol[iPlayer] != 1 && iKey < 6)
							continue;

						outputControl->keys[iKey].fDown = false;
					}
				}
			}
        } else {
            if (SDL_EVENT_JOYSTICK_HAT_MOTION == event.type) {
				if (iDeviceID != event.jhat.which)
					continue;

                for (int iKey = 0; iKey < NUM_KEYS; iKey++) {
                    if (inputControl->keys[iKey] >= JOY_HAT_UP && inputControl->keys[iKey] <= JOY_HAT_RIGHT) {
						if ((inputControl->keys[iKey] == JOY_HAT_UP && (event.jhat.value & SDL_HAT_UP)) ||
							(inputControl->keys[iKey] == JOY_HAT_DOWN && (event.jhat.value & SDL_HAT_DOWN)) ||
							(inputControl->keys[iKey] == JOY_HAT_LEFT && (event.jhat.value & SDL_HAT_LEFT)) ||
                                (inputControl->keys[iKey] == JOY_HAT_RIGHT && (event.jhat.value & SDL_HAT_RIGHT))) {
							fFound = true;

							//Ignore input for cpu controlled players
							if (iGameState == 0 && game_values.playercontrol[iPlayer] != 1 && iKey < 6)
								continue;

							if (!outputControl->keys[iKey].fDown)
								outputControl->keys[iKey].fPressed = true;

							outputControl->keys[iKey].fDown = true;
                        } else {
							//Ignore input for cpu controlled players
							if (iGameState == 0 && game_values.playercontrol[iPlayer] != 1 && iKey < 6)
								continue;

							outputControl->keys[iKey].fDown = false;
						}
					}
				}
            } else if (SDL_EVENT_JOYSTICK_BUTTON_DOWN == event.type) {
				if (iDeviceID != event.jbutton.which)
					continue;

                for (int iKey = 0; iKey < NUM_KEYS && !fFound; iKey++) {
                    if (inputControl->keys[iKey] == event.jbutton.button + JOY_BUTTON_START) {
						fFound = true;

						//Ignore input for cpu controlled players
						if (iGameState == 0 && game_values.playercontrol[iPlayer] != 1 && iKey < 6)
							continue;

						if (!outputControl->keys[iKey].fDown)
							outputControl->keys[iKey].fPressed = true;

						outputControl->keys[iKey].fDown = true;
					}
				}
            } else if (SDL_EVENT_JOYSTICK_BUTTON_UP == event.type) {
				if (iDeviceID != event.jbutton.which)
					continue;

                for (int iKey = 0; iKey < NUM_KEYS && !fFound; iKey++) {
                    if (inputControl->keys[iKey] == event.jbutton.button + JOY_BUTTON_START) {
						fFound = true;

						//Ignore input for cpu controlled players
						if (iGameState == 0 && game_values.playercontrol[iPlayer] != 1 && iKey < 6)
							continue;

						outputControl->keys[iKey].fDown = false;
					}
				}
            } else if (SDL_EVENT_JOYSTICK_AXIS_MOTION == event.type) {
				if (iDeviceID != event.jaxis.which)
					continue;

                for (int iKey = 0; iKey < NUM_KEYS; iKey++) {
					bool fUseJoystickInput = false;
					bool fJoystickDown = false;

                    if (event.jaxis.axis == 0 && inputControl->keys[iKey] == JOY_STICK_1_LEFT) {
						fUseJoystickInput = true;

						if (event.jaxis.value < -JOYSTICK_DEAD_ZONE)
							fJoystickDown = true;
                    } else if (event.jaxis.axis == 0 && inputControl->keys[iKey] == JOY_STICK_1_RIGHT) {
						fUseJoystickInput = true;

						if (event.jaxis.value > JOYSTICK_DEAD_ZONE)
							fJoystickDown = true;
                    } else if (event.jaxis.axis == 1 && inputControl->keys[iKey] == JOY_STICK_1_UP) {
						fUseJoystickInput = true;

						if (event.jaxis.value < -JOYSTICK_DEAD_ZONE)
							fJoystickDown = true;
                    } else if (event.jaxis.axis == 1 && inputControl->keys[iKey] == JOY_STICK_1_DOWN) {
						fUseJoystickInput = true;

						if (event.jaxis.value > JOYSTICK_DEAD_ZONE)
							fJoystickDown = true;
                    } else if (event.jaxis.axis == 2 && inputControl->keys[iKey] == JOY_STICK_2_LEFT) {
						fUseJoystickInput = true;

						if (event.jaxis.value < -JOYSTICK_DEAD_ZONE)
							fJoystickDown = true;
                    } else if (event.jaxis.axis == 2 && inputControl->keys[iKey] == JOY_STICK_2_RIGHT) {
						fUseJoystickInput = true;

						if (event.jaxis.value > JOYSTICK_DEAD_ZONE)
							fJoystickDown = true;
                    } else if (event.jaxis.axis == 3 && inputControl->keys[iKey] == JOY_STICK_2_UP) {
						fUseJoystickInput = true;

						if (event.jaxis.value < -JOYSTICK_DEAD_ZONE)
							fJoystickDown = true;
                    } else if (event.jaxis.axis == 3 && inputControl->keys[iKey] == JOY_STICK_2_DOWN) {
						fUseJoystickInput = true;

						if (event.jaxis.value > JOYSTICK_DEAD_ZONE)
							fJoystickDown = true;
					}

                    if (fUseJoystickInput) {
						//Ignore input for cpu controlled players
						if (iGameState == 0 && game_values.playercontrol[iPlayer] != 1 && iKey < 6)
							continue;

                        if (fJoystickDown) {
							fFound = true;

							if (!outputControl->keys[iKey].fDown)
								outputControl->keys[iKey].fPressed = true;

							outputControl->keys[iKey].fDown = true;
                        } else {
							outputControl->keys[iKey].fDown = false;
						}
					}
				}
			}
		}

		//This line might be causing input from some players not to be read
		//if (fFound)
			//break;
	}
}
