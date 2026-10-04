# Calibration method

How WheelDrive's steering and pedal settings were derived, and how to improve them. The loop is:

1. Drive in game. The plugin logs telemetry.
2. Run `tools/analyze.py`. It compares the measured response with real-car targets.
3. Adjust the suggested settings in `WheelDrive.ini`.
4. Drive again to verify.

Each step uses only telemetry the plugin records. No game memory is read.

## 1. Telemetry

Every session writes `Documents\WheelDrive\telemetry.csv`. The previous two sessions are kept as
`telemetry.1.csv` and `telemetry.2.csv`. Rows are written at 10 Hz while the player is in the driver's
seat. A `# vehicle NAME (model …) | key=value …` line starts each new vehicle, and lists the active steering
settings plus the game's static handling stats (`max_accel`, `max_braking`, `max_traction`, `est_max_speed`,
`gears`).

Sign convention: **positive = right** for all angle and yaw columns. Speeds are in m/s.

| Column | Meaning |
|---|---|
| `tick_ms`, `frame_dt` | Windows tick count, game frame time (s) |
| `raw_axis` | Raw steering axis, 0–65535 |
| `wheel_deg` | Physical wheel angle from center (deg), using `WheelRangeDeg` |
| `steer_linear` | Wheel position normalized by `SteerLockDeg`, before gain |
| `steer_out` | Value sent to `SET_VEHICLE_STEER_BIAS` (fraction of the car's steering lock) |
| `target_tire_deg_at40` | `steer_out` × 40°: the tire angle GTA *should* apply if the lock is 40° |
| `measured_tire_deg` | Effective front-wheel angle from yaw rate: `atan(yaw_rad/s × 2.7 m / speed)` |
| `speed_mps`, `fwd_mps`, `lat_mps` | Total, forward and lateral speed (vehicle frame) |
| `body_slip_deg` | `atan(lat / |fwd|)`: how sideways the car is moving |
| `yaw_dps`, `rotvel_z` | Yaw rate from heading change (deg/s), the game's rotation velocity (raw) |
| `game_steer_ctl` | What GTA reads on its own steering control. Nonzero means keyboard/gamepad (or the wheel seen as a gamepad). |
| `using_kb`, `kb_override` | Keyboard is the active input. The keyboard took steering this frame. |
| `bias_applied` | Direct steering was applied this frame |
| `throttle`, `brake` | Processed pedal positions 0–1 (deadzone and invert applied) |
| `reverse` | Reverse selected (button) |
| `raw_throttle`, `raw_brake` | Raw pedal axes |
| `game_throttle` | `_GET_VEHICLE_THROTTLE`: the throttle GTA actually applies (−1…1) |
| `accel_mps2` | Forward acceleration, smoothed per frame (EMA 0.25) |
| `accel_flat_mps2` | Same, with gravity along the slope removed (`+ 9.81·sin(pitch)`). **Use this one.** |
| `pitch_deg`, `gear`, `on_wheels`, `collided` | Slope, current gear, all wheels on ground, touching anything |
| `accel_ctl_out`, `brake_ctl_out` | Accelerate/brake control values the plugin sent to GTA |
| `pedal_mode` | `PedalMode` in effect |

**Filtering** (`analyze.py` does this): drop rows that are off the ground, collided, keyboard-controlled
or in reverse. For pedal analysis also drop rows with |wheel| ≥ 30° (cornering scrubs speed). For steering,
use only *steady* rows, where the wheel stayed within 4° over ±2 rows, so the yaw rate has settled.

## 2. Steering

**Actuation.** `SET_VEHICLE_STEER_BIAS(veh, −steer_out)` locks the steering angle for one frame. It's
called every frame, with −1 = full right and 1 = full left as a fraction of the car's `fSteeringLock`. The
game's own steering control is disabled while this is active, which also bypasses GTA's stick deadzone,
smoothing, speed-based steering reduction and auto-countersteer.

```
steer_out = (wheel_deg / (SteerLockDeg/2)) ^ SteerGamma × SteerGain × SteerSpeedGainCurve(km/h)
```

**Real-car reference.** A sedan with steering ratio `R` (default 15:1 = `SteerLockDeg` 1200 / 2 / 40°)
under steady-state cornering with a linear understeer gradient:

```
real_tire_deg(effective) = (wheel_deg / R) / (1 + (v / v_ch)^2),   v_ch ≈ 22.5 m/s (81 km/h)
```

`measured_tire_deg` is the kinematic (no-slip) angle the car is effectively turning at. That makes it
directly comparable to the left-hand side.

**Findings.**
- GTA turned cars about 4.3× harder than this reference for the same `steer_out`. That's
  `SteerGain = 0.23`, roughly flat from 30 to 160 km/h.
- At low speed GTA over-rotates even more: about 1.66× at 7–14 km/h, falling to 1.0 by 45 km/h. That's
  `SteerSpeedGainCurve`. Without it, 90° intersection turns took too little wheel.

**Refit.** In the report, `measured/real` per speed band should be ~1.0 (±0.08). Multiply the
`SteerSpeedGainCurve` point(s) in a band by `1/ratio` (the tool prints this). If every band is off by a
similar factor, change `SteerGain` instead.

**Caveats.** Per-car `fSteeringLock` isn't readable, so the 40° assumption makes the effective ratio
vary slightly by car (a 35° car comes out around 17:1). The wheelbase is assumed to be 2.7 m. Above about 110
km/h the samples are sparse and slip dominates.

## 3. Pedals

### Measured GTA longitudinal model

From raw pedal sweeps (pedal → control 1:1). Values are for the Buffalo. Weaker cars scale down.

- **Throttle:** a deadzone of about 0.25. Above it, `accel ≈ −hold(v) + GameThrottleSlope × fade(v) ×
  (ctl − GameThrottleZero)`, with slope 11.76 and zero 0.271. `fade(v)` drops in steps at gear changes
  (`GameThrottleFade`).
- **Hold:** a control value just past the deadzone (`GameHold` 0.26) cancels GTA's engine braking.
  Drag remains (`GameHoldDecelCurve`).
- **Lift-off:** with no input, GTA applies about 3.2–3.9 m/s² of engine braking, rising with speed
  (`GameEngineBrakeCurve`). That's far above a real car's 0–1 m/s².
- **Brake:** the same kind of deadzone, then steep: `decel ≈ 39.37 × ctl − 6.21`
  (`GameBrakeSlope`/`GameBrakeOffset`). It exceeds 1 g at about 50% control.
- **Stop:** holding the brake at a standstill makes GTA reverse.

### Real-car targets

| Situation | Target | Source |
|---|---|---|
| Part throttle | `pedal × MaxAccel(4.5) × RealAccelCurve(v)` (sedan, 0–100 km/h ≈ 8 s) | typical sedan figures |
| Lift-off coasting | `RealCoastCurve`: 0.3 → 0.55 (60 km/h) → 0.75 (90 km/h) m/s² | engine braking + drag + rolling: 0–1 m/s² |
| Braking | `RealCoast + BrakeMaxDecel(9.0) × pedal`. 25% ≈ 2.75, 50% ≈ 5, 75% ≈ 7.25, 100% ≈ 9.5 m/s² | mild 2.2 / moderate 4.4 / harsh 6.3 / ABS max 9–10 m/s² |

### Actuation (`mapPedals` / `actuate` in `src/WheelDrive.cpp`)

- **Throttle:** feedforward `ctl = zero + (target + hold) / (slope × fade)`. The **kickdown** term blends
  toward 1.0 as `pedal^KickdownExp`, so full pedal always equals the keyboard's full throttle. That keeps
  each car's own power and top speed. Don't cap full pedal.
- **Coasting and light braking** (target decel < GTA's lift-off decel): **closed loop** on
  `accel_flat`. An integrator (`CoastKi`) corrects a commanded acceleration, which is realized as:
  - light throttle (when the target decel is less than GTA's drag),
  - or a per-frame sigma-delta duty between *hold* and *lift-off*, which the car's inertia averages,
  - or the brake control.

  It's stable in simulation with large model errors and settles in 2–3 s.
- **Firm braking:** feedforward `ctl = GameBrakeOffset + decel / GameBrakeSlope`.
- **At a stop with the brake held:** handbrake (holds the car).
- **Reverse:** the gas drives the brake control and the brake drives the accelerate control while rolling
  backwards. At a stop, the handbrake holds the car.

### Refit

- **Part throttle** `measured/target` per speed band ~1.0: multiply `GameThrottleFade` points by the
  ratio (the tool prints the new curve). Fade varies by car, so fit on a typical sedan.
- **Full pedal:** `ctl` must read 1.00 at every speed.
- **Coast:** measured within ~0.1 m/s² of the target. The loop handles drift. If it's consistently off,
  check `GameHoldDecelCurve`/`GameEngineBrakeCurve`, or raise `CoastKi` (it may oscillate above ~2).
- **Brake** `measured/target` ~1.0 per pedal band: use the printed regression for `GameBrakeSlope` /
  `GameBrakeOffset`.
- **Reverse:** max forward speed while braking should stay ~0.

## 4. Running a calibration pass

1. Drive a normal sedan (the Buffalo or Taxi are the references) with logging on. 15+ minutes of mixed
   driving covers most bands. For pedals add: steady 25/50/75/100% throttle from a stop, light/medium/hard
   braking from about 80 km/h, and coasting from about 80 km/h.
2. `python3 tools/analyze.py telemetry.csv --ini <the WheelDrive.ini that was active>`. Suggestions are
   relative to the config in effect while driving, so pass the player's ini, not a newer one.
3. Apply changes to `WheelDrive.ini` (the repo copy is the shipped default, built into the plugin). Press
   F10 in game to test without restarting.
4. Re-drive and re-run until every band is within about ±8%. Bands with fewer than about 50 samples are
   noise.

## 5. Known gaps / ideas

- Steering ratio per car: read `fSteeringLock` from memory (fragile across game updates), or estimate it
  per model from telemetry and store a per-model table.
- Pedal model per car: `max_accel`/`max_braking` from the vehicle header could scale `GameThrottleSlope`
  and `GameBrakeSlope` per car.
- Force feedback is a speed-scaled centering spring plus damper. Self-aligning torque could be
  approximated from `body_slip_deg` and lateral acceleration.
- Low-speed steering below about 7 km/h is extrapolated (sparse data).
- Motorcycles use a fixed stick mapping by design (handlebars have no real wheel equivalent). Exclude them
  from calibration.
