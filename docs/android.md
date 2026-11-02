# Android: players and controls

Up to four controllers can play at once: the Portal's built-in controls and any
Bluetooth/USB gamepads. Nothing needs setting up in the in-game **Controls** menu; leave
every player on **Keyboard** there. Touch controls keep working alongside controllers.

## Joining

- Press any button (or push a stick) on a controller and it becomes the next free player.
- On the main menu, set the **Players** row so everyone playing is **Player** (human):
  highlight the row, press A, choose a player with Left/Right, change it with Up/Down,
  press A when done. The game remembers this.
- The Portal's built-in controls rank after Bluetooth pads. When docked to a TV,
  the Bluetooth pads become Players 1, 2, ... even if the Portal's buttons were pressed
  first, and the built-in controls step aside when all four players are taken.
- Hold **L + R** together for one second (outside a match) to make that controller
  Player 1; whoever had Player 1 swaps to your old number.
- If a controller turns off or disconnects, its player number is kept for it and it gets
  the same number back when it reconnects (unless someone else took it meanwhile).

## Buttons

| Button                 | Menus                     | Match                |
|------------------------|---------------------------|----------------------|
| D-pad / left stick     | Navigate                  | Move / duck (Down)   |
| A                      | Confirm / ready up        | Jump                 |
| B                      | Back                      | Run / fire           |
| X                      | Random                    | Run / fire           |
| Y, L1/R1, L2/R2        | Fast scroll               | Use item             |
| Start                  | Confirm                   | Pause                |
| Select / Back          | Back                      | Exit game dialog     |

- Up does nothing during a match, so a tilted stick never jumps; A is jump.
- Every controller can drive the shared menus, ready up and continue on team select,
  pause, and use the exit dialog (B closes it).
- B on the main menu highlights **Exit** instead of quitting, so quitting needs a
  deliberate A on Exit.
- Without a controller, Player 1 can use the on-screen touch controls. They hide while
  Player 1 uses a controller; touching the screen brings them back.

## How it works

`PadManager.java` assigns controllers to players and turns Android key/axis events into
abstract buttons. `GameActivity` sends them to the game through JNI as SDL user events,
and `CPlayerInput::PadButton` (`src/common/input.cpp`) maps them per screen. They never
pass through the key bindings, so they work no matter how the Controls menu is set.

Tests: `android/test-touch.sh` (assignment rules) and `tests/common/test_input_pad.cpp`
(native mapping).

Built-in controls are recognised by Android reporting them as an internal device. If a
handheld reports its built-in pad as external, it is simply treated like a Bluetooth pad.

## CRT filter

Options -> Graphics -> **CRT Filter** draws the game through a RetroArch CRT shader:
**CRT Lottes** (curved screen, shadow mask, scanlines, bloom) or **CRT EasyMode** (flat,
sharp scanlines and mask). It switches immediately and is saved with the other settings.
Off draws exactly as before. The shaders come unmodified from libretro/glsl-shaders
(crt-lottes: public domain, Timothy Lottes; crt-easymode: GPL, EasyMode) and run as a final
OpenGL ES pass (`src/common/gfx/CrtFilter.cpp`).

