# WheelDrive for GTA V

Free steering wheel support for **GTA V Enhanced** (and Legacy) story mode, calibrated to drive like a real car.

WheelDrive is a small Script Hook V plugin. It reads your wheel and pedals directly through DirectInput and
drives the game with them. It doesn't emulate a controller. Steering, throttle and brakes are mapped so the
car responds the way a real sedan would, using telemetry measured in game.

Developed and tested with a **Logitech G29**. Other DirectInput wheels should work after adjusting the axis
and button numbers in the config.

## Features

- **1:1 steering angle.** The wheel sets the car's front-wheel angle directly every frame, with no stick
  deadzone or game smoothing. The default ratio is about 15:1 (a typical sedan, 900° lock-to-lock).
- **Real-car steering response.** GTA turns cars much harder than real ones for the same wheel angle.
  WheelDrive corrects this from measured yaw-rate data, including GTA's extra over-rotation at low speed, so
  a 90° intersection turn takes a realistic amount of wheel.
- **Real-car pedals.**
  - GTA's ~25% trigger deadzone is removed.
  - Part throttle follows a real sedan's acceleration. Flooring it blends up to the car's full power
    ("kickdown"), so every car keeps its own top speed and character.
  - Brakes are linear up to about 0.9 g at full pedal.
  - Lift-off coasting is about 0.3–1.0 m/s², like a real car, instead of GTA's strong engine braking. This
    is closed-loop on the measured deceleration.
  - Holding the brake at a stop holds the car instead of reversing.
- **Reverse and forward buttons,** like selecting R and D on a gearbox.
- **Keyboard still works.** Hold a steering key and it takes over. Release it and the wheel is back.
- **Force feedback:** a speed-dependent centering spring plus damping, capped at a configurable maximum force.
- **Live tuning:** F9 shows raw axes, buttons and output values. F10 reloads the config in game.
- **Telemetry:** per-session CSV logs (last 3 kept) of input, steering angle, yaw rate, acceleration and
  more, for calibration.

## Requirements

- GTA V Enhanced or Legacy (story mode only)
- [Script Hook V](http://www.dev-c.com/gtav/scripthookv/) with its ASI loader (`xinput1_4.dll` for Enhanced,
  `dinput8.dll` for Legacy)
- A DirectInput steering wheel (tested: Logitech G29 with G HUB)

## Install

1. Install Script Hook V. From its archive's `bin` folder, copy `ScriptHookV.dll` and the ASI loader into
   the game folder (the one containing `GTA5_Enhanced.exe`). Copy the files themselves, not the `bin`
   folder.
2. Download `WheelDrive.asi` from [Releases](../../releases) and put it in the same folder.
3. Recommended:
   - In Steam, set **Properties → Controller → Disable Steam Input** for GTA V, so the wheel isn't also
     seen as a gamepad.
   - In G HUB, set the operating range to **900°**, and turn the **Centering Spring off** and damping to 0.
     WheelDrive provides its own force feedback.
4. Launch story mode with BattlEye disabled (for example the `-nobattleye` launch option).

On first launch WheelDrive creates `Documents\WheelDrive\WheelDrive.ini` with the calibrated defaults. Your
edits there are kept and never overwritten.

## Default controls (Logitech G29)

| Wheel input | Action |
|---|---|
| Wheel / gas / brake | Steering, throttle, brake |
| Button 15 | Reverse |
| Button 14 | Forward |
| Button 0 (Cross) | Handbrake |
| Button 1 (Square) | Headlights |
| Button 2 (Circle) | Look behind |
| Button 3 (Triangle) | Exit vehicle |
| Button 9 | Change camera |
| Button 23 | Horn |
| F9 / F10 (keyboard) | Toggle overlay / reload config |

Use the F9 overlay to find your wheel's button numbers, then edit `[Buttons]` in the ini.

## Configuration

Every setting is commented in [`WheelDrive.ini`](WheelDrive.ini), which is also the source of the defaults
built into the plugin. The ones you're most likely to change:

| Setting | What it does |
|---|---|
| `DeviceName` | Part of your wheel's name, used to pick the device |
| `SteerAxis`, `ThrottleAxis`, `BrakeAxis`, `*Invert` | Axis mapping for other wheels and pedal sets |
| `WheelRangeDeg` | Must match the rotation set in your wheel software |
| `SteerLockDeg` | Steering ratio. Lower = quicker steering (1200 ≈ 15:1, 990 ≈ 12.4:1) |
| `MaxAccel`, `BrakeMaxDecel`, `KickdownExp` | Pedal feel |
| `[FFB]` | Force feedback strength and cap (`MaxForce` in %) |

The `Game*` values in `[Pedals]` describe GTA's measured response. Leave them alone unless you're
recalibrating.

Config and logs live in `Documents\WheelDrive\` (or `OneDrive\Documents\WheelDrive\`) rather than the game
folder, because GTA V Enhanced locks files in its own folder while running.

## How the calibration was done

Each session writes `telemetry.csv` (10 Hz). It records wheel angle, the steering value sent, the vehicle's
yaw rate, forward and lateral speed, slope-corrected acceleration, the pedals, the throttle value GTA
actually applies, gear, and more.

- **Steering:** the effective front-wheel angle is computed from yaw rate and speed. It's compared with a
  15:1 sedan using a standard understeer model (characteristic speed ≈ 81 km/h). The result is the 0.23 gain
  plus a low-speed correction curve.
- **Pedals:** GTA's acceleration was measured against throttle and brake input across speed bands, then
  fitted (deadzones, slopes, throttle fade per speed, engine braking, drag). The pedals are mapped onto
  published real-car figures: mild braking ≈ 2.2 m/s², moderate ≈ 4.4, harsh ≈ 6.3, ABS maximum ≈ 9–10, and
  lift-off coasting 0–1.

The calibration was measured mainly on the Bravado Buffalo and the Taxi. Other cars keep their own power,
grip and steering lock, so they still feel different, as they should.

The full method, models and telemetry format are in [CALIBRATION.md](CALIBRATION.md). To check or refine
the calibration from your own driving, run `python3 tools/analyze.py telemetry.csv --ini WheelDrive.ini`.

## Building

WheelDrive is plain C++20 against the Win32/DirectInput API, cross-compiled with
[llvm-mingw](https://github.com/mstorsjo/llvm-mingw). It doesn't need the Script Hook V SDK. The import
library is generated from [`src/ScriptHookV.def`](src/ScriptHookV.def).

```sh
export LLVM_MINGW=/path/to/llvm-mingw        # or put its bin/ on PATH
./build.sh                                   # -> build/WheelDrive.asi
GTA5_DIR="/path/to/Grand Theft Auto V Enhanced" ./build.sh install
```

Pushing a `v*` tag makes GitHub Actions build the plugin and attach `WheelDrive.asi` to a release.

## Limitations

- Tested only with a Logitech G29.
- Force feedback is a centering spring and damper, not tire physics.
- GTA has no API for the real steering angle or each car's steering lock, so the ratio assumes GTA's typical
  ~40° lock.
- Above about 85 km/h some cars can't match a real sedan's part-throttle acceleration, because GTA's
  engines fade with speed. Full pedal still gives the car's full power.
- Motorcycles get a simple fixed mapping. There's no real handlebar-to-wheel equivalent.
- Story mode only (Script Hook V doesn't run online).

## Disclaimer

Not affiliated with Rockstar Games, Take-Two Interactive, Logitech or Script Hook V. Written from scratch.
It isn't based on or derived from any other wheel mod. Use at your own risk, in single player only.

## License

[MIT](LICENSE)
