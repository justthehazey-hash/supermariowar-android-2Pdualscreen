#include "doctest.h"

#include "GameValues.h"
#include "input.h"

extern CGameValues game_values;

namespace {
constexpr short MENU = 1;
constexpr short GAME = 0;

struct Fixture {
    CPlayerInput input;
    CInputPlayerControl keyboard[4];
    Fixture() {
        for (short p = 0; p < 4; p++) {
            keyboard[p].iDevice = DEVICE_KEYBOARD;
            input.inputControls[p] = &keyboard[p];
            game_values.playercontrol[p] = 1;
        }
    }
    const CKeyState& key(short player, short index) { return input.outputControls[player].keys[index]; }
};
}

TEST_CASE("Controller buttons drive menu keys on menu screens")
{
    Fixture f;
    f.input.PadButton(2, PAD_A, true, MENU);
    CHECK(f.input.outputControls[2].menu_select.fPressed);
    CHECK(f.input.outputControls[2].menu_select.fDown);
    f.input.PadButton(2, PAD_A, false, MENU);
    CHECK_FALSE(f.input.outputControls[2].menu_select.fDown);

    f.input.PadButton(3, PAD_UP, true, MENU);
    CHECK(f.input.outputControls[3].menu_up.fPressed);
    f.input.PadButton(3, PAD_B, true, MENU);
    CHECK(f.input.outputControls[3].menu_cancel.fPressed);
    f.input.PadButton(3, PAD_X, true, MENU);
    CHECK(f.input.outputControls[3].menu_random.fPressed);
    f.input.PadButton(3, PAD_L, true, MENU);
    CHECK(f.input.outputControls[3].menu_scrollfast.fDown);
    f.input.PadButton(3, PAD_START, true, MENU);
    CHECK(f.input.outputControls[3].menu_select.fPressed);
}

TEST_CASE("Controller buttons drive game keys in a match")
{
    Fixture f;
    f.input.PadButton(1, PAD_A, true, GAME);
    CHECK(f.input.outputControls[1].game_jump.fDown);
    f.input.PadButton(1, PAD_LEFT, true, GAME);
    CHECK(f.input.outputControls[1].game_left.fDown);
    f.input.PadButton(1, PAD_DOWN, true, GAME);
    CHECK(f.input.outputControls[1].game_down.fDown);
    f.input.PadButton(1, PAD_Y, true, GAME);
    CHECK(f.input.outputControls[1].game_powerup.fDown);
    f.input.PadButton(3, PAD_START, true, GAME);
    CHECK(f.input.outputControls[3].game_start.fPressed); // players 3-4 can pause too
    f.input.PadButton(3, PAD_SELECT, true, GAME);
    CHECK(f.input.outputControls[3].game_cancel.fPressed);

    // Up does nothing in a match (A is jump), so a tilted stick never jumps.
    f.input.PadButton(0, PAD_UP, true, GAME);
    for (short k = 0; k < NUM_KEYS; k++)
        CHECK_FALSE(f.key(0, k).fDown);
}

TEST_CASE("Two buttons on the same key keep it held until both are released")
{
    Fixture f;
    f.input.PadButton(0, PAD_B, true, GAME);
    f.input.PadButton(0, PAD_X, true, GAME);
    f.input.PadButton(0, PAD_B, false, GAME);
    CHECK(f.input.outputControls[0].game_turbo.fDown);
    f.input.PadButton(0, PAD_X, false, GAME);
    CHECK_FALSE(f.input.outputControls[0].game_turbo.fDown);
}

TEST_CASE("A release clears the key it pressed even after the screen changed")
{
    Fixture f;
    f.input.PadButton(0, PAD_A, true, MENU);        // menu_select (index 4)
    CHECK(f.key(0, 4).fDown);
    f.input.PadButton(0, PAD_A, false, GAME);       // in game A is jump (index 2)
    CHECK_FALSE(f.key(0, 4).fDown);
}

TEST_CASE("CPU players ignore controller movement in a match")
{
    Fixture f;
    game_values.playercontrol[2] = 2;
    f.input.PadButton(2, PAD_RIGHT, true, GAME);
    CHECK_FALSE(f.input.outputControls[2].game_right.fDown);
    f.input.PadButton(2, PAD_START, true, GAME);
    CHECK(f.input.outputControls[2].game_start.fPressed); // pause still allowed
    game_values.playercontrol[2] = 1;
}

TEST_CASE("Players with a controller may drive shared menus")
{
    Fixture f;
    CHECK_FALSE(f.input.UsesSharedKeyboard(0));
    CHECK(f.input.UsesSharedKeyboard(1));
    f.input.PadButton(1, PAD_DOWN, true, MENU);
    CHECK_FALSE(f.input.UsesSharedKeyboard(1));
    f.input.PadDetach(1);
    CHECK(f.input.UsesSharedKeyboard(1));
    CHECK_FALSE(f.input.outputControls[1].menu_down.fDown); // detach releases held buttons
}

TEST_CASE("Pad events travel through the SDL queue into Update")
{
    REQUIRE(SDL_Init(SDL_INIT_EVENTS));
    CPlayerInput::RegisterPadEvents();
    Fixture f;
    REQUIRE(CPlayerInput::PushPadEvent(3, PAD_A, true));
    SDL_Event event;
    bool seen = false;
    while (SDL_PollEvent(&event)) {
        if (CPlayerInput::IsPadEvent(event)) seen = true;
        f.input.Update(event, MENU);
    }
    CHECK(seen);
    CHECK(f.input.outputControls[3].menu_select.fPressed);

    REQUIRE(CPlayerInput::PushPadEvent(3, PAD_DETACH, false));
    while (SDL_PollEvent(&event)) f.input.Update(event, MENU);
    CHECK_FALSE(f.input.padAttached[3]);
    CHECK_FALSE(f.input.outputControls[3].menu_select.fDown);
    SDL_Quit();
}

TEST_CASE("A held direction repeats in menus, not in a match")
{
    Fixture f;
    f.input.PadButton(0, PAD_DOWN, true, MENU);
    CHECK(f.input.outputControls[0].menu_down.fPressed);
    int presses = 0, firstRepeat = -1;
    for (int frame = 1; frame <= 62; frame++) {  // one second at the normal frame rate
        f.input.ClearPressedKeys(MENU);
        if (f.input.outputControls[0].menu_down.fPressed) {
            presses++;
            if (firstRepeat < 0) firstRepeat = frame;
        }
        CHECK_FALSE(f.input.outputControls[0].menu_up.fPressed);
    }
    CHECK(firstRepeat >= 20);
    CHECK(firstRepeat <= 30);
    CHECK(presses >= 5);
    CHECK(presses <= 8);

    // Released: no more repeats, and the next hold starts the delay over.
    f.input.PadButton(0, PAD_DOWN, false, MENU);
    for (int frame = 0; frame < 40; frame++) {
        f.input.ClearPressedKeys(MENU);
        CHECK_FALSE(f.input.outputControls[0].menu_down.fPressed);
    }
    f.input.PadButton(0, PAD_DOWN, true, MENU);
    f.input.ClearPressedKeys(MENU);
    CHECK_FALSE(f.input.outputControls[0].menu_down.fPressed);

    // Buttons never repeat.
    f.input.PadButton(1, PAD_A, true, MENU);
    for (int frame = 0; frame < 62; frame++) {
        f.input.ClearPressedKeys(MENU);
        CHECK_FALSE(f.input.outputControls[1].menu_select.fPressed);
    }

    // In a match a held direction is just held.
    f.input.PadButton(2, PAD_LEFT, true, GAME);
    for (int frame = 0; frame < 62; frame++) {
        f.input.ClearPressedKeys(GAME);
        CHECK_FALSE(f.input.outputControls[2].game_left.fPressed);
        CHECK(f.input.outputControls[2].game_left.fDown);
    }
}
