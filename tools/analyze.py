#!/usr/bin/env python3
"""Calibration report for WheelDrive telemetry.

Compares how the car actually responded in game (telemetry.csv) against the real-car targets in a
WheelDrive.ini, per vehicle, and suggests new values for the calibration settings.
See CALIBRATION.md for the method behind every number printed here.

    python3 tools/analyze.py telemetry.csv [--ini WheelDrive.ini] [--min-rows 800]

--ini should be the config that was active while driving (the player's Documents\\WheelDrive\\WheelDrive.ini),
since suggestions are relative to the values in effect. Defaults to the repo's WheelDrive.ini.
"""
import argparse
import configparser
import math
import statistics
from collections import defaultdict
from pathlib import Path

GTA_STEER_LOCK_DEG = 40.0  # typical handling.meta fSteeringLock; GTA exposes no per-car value
WHEELBASE_M = 2.7          # used by the plugin for measured_tire_deg
REAL_VCH_MPS = 22.5        # real sedan understeer characteristic speed (~81 km/h)
MIN_SAMPLES = 6

SPEED_BANDS_MPS = [(2, 4), (4, 6), (6, 8), (8, 11), (11, 15), (15, 22), (22, 30), (30, 45)]
PEDAL_BANDS_MPS = [(1, 8), (8, 14), (14, 20), (20, 26), (26, 35), (35, 50)]


class Curve:
    """Piecewise-linear speed_kmh:value curve, same semantics as the plugin."""

    def __init__(self, text):
        self.pts = [tuple(float(x) for x in p.split(":")) for p in text.split(",") if ":" in p]

    def at(self, kmh):
        pts = self.pts
        if kmh <= pts[0][0]:
            return pts[0][1]
        for (x0, y0), (x1, y1) in zip(pts, pts[1:]):
            if kmh <= x1:
                return y0 + (y1 - y0) * (kmh - x0) / (x1 - x0)
        return pts[-1][1]

    def scaled(self, factors):
        """New curve text with each point multiplied by the factor of the band containing it.
        The slowest band also covers points down to 0 km/h; points above the fastest measured band keep
        their value (no data there)."""
        if factors:
            factors = [(0.0, factors[0][1], factors[0][2])] + factors[1:]
        out = []
        for x, y in self.pts:
            f = next((f for lo, hi, f in factors if lo <= x < hi), 1.0)
            out.append(f"{x:g}:{y * f:.2f}".rstrip("0").rstrip("."))
        return ",".join(out)


def load_ini(path):
    cp = configparser.ConfigParser(inline_comment_prefixes=(";",))
    cp.optionxform = str
    cp.read(path, encoding="utf-8-sig")
    g = lambda s, k, d: cp.get(s, k, fallback=d)
    return {
        "steer_lock": float(g("Wheel", "SteerLockDeg", "1200")),
        "steer_gain": float(g("Wheel", "SteerGain", "0.23")),
        "steer_speed_gain": Curve(g("Wheel", "SteerSpeedGainCurve", "0:1")),
        "max_accel": float(g("Pedals", "MaxAccel", "4.5")),
        "brake_max": float(g("Pedals", "BrakeMaxDecel", "9.0")),
        "real_accel": Curve(g("Pedals", "RealAccelCurve", "0:1")),
        "real_coast": Curve(g("Pedals", "RealCoastCurve", "0:0.5")),
        "fade": Curve(g("Pedals", "GameThrottleFade", "0:1")),
        "brake_offset": float(g("Pedals", "GameBrakeOffset", "0.1577")),
        "brake_slope": float(g("Pedals", "GameBrakeSlope", "39.37")),
    }


def load_telemetry(path):
    """Rows grouped by vehicle. '# vehicle NAME ...' lines start a new vehicle segment."""
    by_vehicle = defaultdict(list)
    vehicle = "UNKNOWN"
    with open(path, encoding="utf-8", errors="replace") as f:
        header = f.readline().strip().split(",")
        for line in f:
            if line.startswith("#"):
                parts = line.split()
                vehicle = parts[2] if len(parts) > 2 else "UNKNOWN"
                continue
            values = line.strip().split(",")
            if len(values) != len(header):
                continue
            try:
                by_vehicle[vehicle].append(dict(zip(header, map(float, values))))
            except ValueError:
                continue
    return by_vehicle


def clean(r):
    """Driving forward, on all wheels, no collision, wheel (not keyboard) in control."""
    return (r.get("on_wheels", 1) == 1 and r.get("collided", 0) == 0 and r["kb_override"] == 0
            and r["reverse"] == 0)


def med(values):
    return statistics.median(values) if len(values) >= MIN_SAMPLES else None


def fmt(value, n, digits=2):
    return f"{value:6.{digits}f} (n={n})" if value is not None else f"     -  (n={n})"


def steady_steering(rows):
    """Rows where the wheel was held roughly still (+-2 rows within 4 deg): yaw has settled."""
    out = []
    for i in range(2, len(rows) - 2):
        window = [rows[j]["wheel_deg"] for j in range(i - 2, i + 3)]
        if max(window) - min(window) < 4:
            out.append(rows[i])
    return out


def report(name, rows, cfg):
    ratio = cfg["steer_lock"] / 2 / GTA_STEER_LOCK_DEG
    minutes = (rows[-1]["tick_ms"] - rows[0]["tick_ms"]) / 60000
    top = max(r["speed_mps"] for r in rows) * 3.6
    print(f"\n=== {name}: {len(rows)} rows, {minutes:.1f} min, top {top:.0f} km/h ===")

    # Steering: measured effective tire angle vs a real sedan at the configured ratio.
    steer = [r for r in steady_steering(rows)
             if clean(r) and r["bias_applied"] and abs(r["wheel_deg"]) >= 3 and r["fwd_mps"] > 2]
    print(f"STEERING  measured / real-car tire angle  (1.00 = real {ratio:.1f}:1 sedan)")
    factors = []
    for lo, hi in SPEED_BANDS_MPS:
        g = [r["measured_tire_deg"] / ((r["wheel_deg"] / ratio) / (1 + (r["speed_mps"] / REAL_VCH_MPS) ** 2))
             for r in steer if lo <= r["fwd_mps"] < hi]
        m = med(g)
        print(f"  {lo*3.6:4.0f}-{hi*3.6:4.0f} km/h  {fmt(m, len(g))}")
        if m:
            factors.append((lo * 3.6, hi * 3.6, 1 / m))
    off = [f for _, _, f in factors if abs(f - 1) > 0.08]
    if off:
        print(f"  -> SteerSpeedGainCurve={cfg['steer_speed_gain'].scaled(factors)}")
        print("     (or, if every band is off by a similar factor, multiply SteerGain instead)")

    flat = [r for r in rows if clean(r) and abs(r["wheel_deg"]) < 30]

    # Part throttle vs real sedan target; the ratio rescales GameThrottleFade.
    print("THROTTLE  part pedal 15-60%: measured / target accel")
    factors = []
    for lo, hi in PEDAL_BANDS_MPS:
        g = [r["accel_flat_mps2"] / (r["throttle"] * cfg["max_accel"] * cfg["real_accel"].at(r["fwd_mps"] * 3.6))
             for r in flat if 0.15 < r["throttle"] < 0.6 and r["brake"] < 0.02 and lo <= r["fwd_mps"] < hi]
        m = med(g)
        print(f"  {lo*3.6:4.0f}-{hi*3.6:4.0f} km/h  {fmt(m, len(g))}")
        if m:
            factors.append((lo * 3.6, hi * 3.6, m))
    if any(abs(f - 1) > 0.08 for _, _, f in factors):
        print(f"  -> GameThrottleFade={cfg['fade'].scaled(factors)}")

    print("FULL PEDAL >95%: control sent (should be 1.00) and accel")
    for lo, hi in PEDAL_BANDS_MPS:
        g = [r for r in flat if r["throttle"] > 0.95 and lo <= r["fwd_mps"] < hi]
        if len(g) >= 3:
            ctl = statistics.median(r["accel_ctl_out"] for r in g)
            acc = statistics.median(r["accel_flat_mps2"] for r in g)
            print(f"  {lo*3.6:4.0f}-{hi*3.6:4.0f} km/h  ctl {ctl:.2f}  accel {acc:5.2f} m/s^2  (n={len(g)})")

    print("COAST     no pedals: measured vs target decel (m/s^2)")
    for lo, hi in PEDAL_BANDS_MPS[1:]:
        g = [-r["accel_flat_mps2"] for r in flat if r["throttle"] < 0.02 and r["brake"] < 0.02 and lo <= r["fwd_mps"] < hi]
        target = cfg["real_coast"].at((lo + hi) / 2 * 3.6)
        print(f"  {lo*3.6:4.0f}-{hi*3.6:4.0f} km/h  target {target:4.2f}  measured {fmt(med(g), len(g))}")

    print("BRAKE     measured / target decel")
    for lo, hi in [(0.03, 0.2), (0.2, 0.4), (0.4, 0.6), (0.6, 0.8), (0.8, 1.01)]:
        g = [-r["accel_flat_mps2"] / (cfg["real_coast"].at(r["fwd_mps"] * 3.6) + cfg["brake_max"] * r["brake"])
             for r in flat if lo <= r["brake"] < hi and r["throttle"] < 0.02 and r["fwd_mps"] > 3]
        print(f"  pedal {lo:.2f}-{hi:.2f}  {fmt(med(g), len(g))}")
    pts = [(r["brake_ctl_out"], -r["accel_flat_mps2"]) for r in flat
           if r["brake_ctl_out"] > 0 and r["throttle"] < 0.02 and r["fwd_mps"] > 3]
    if len(pts) >= 30:
        n = len(pts)
        sx = sum(x for x, _ in pts); sy = sum(y for _, y in pts)
        sxx = sum(x * x for x, _ in pts); sxy = sum(x * y for x, y in pts)
        k = (n * sxy - sx * sy) / (n * sxx - sx * sx)
        c = (sy - k * sx) / n
        print(f"  fit: decel = {k:.2f} * brake_ctl {c:+.2f}  (n={n})"
              f"  -> GameBrakeSlope={k:.2f} GameBrakeOffset={-c / k:.4f}"
              f"  (current {cfg['brake_slope']:.2f} / {cfg['brake_offset']:.4f})")

    rev = [r for r in rows if r["reverse"] and r["brake"] > 0.05]
    if rev:
        creep = max(r["fwd_mps"] for r in rev)
        print(f"REVERSE   braking: max forward speed {creep:.2f} m/s (should stay ~0)  (n={len(rev)})")


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("telemetry")
    ap.add_argument("--ini", default=str(Path(__file__).resolve().parent.parent / "WheelDrive.ini"))
    ap.add_argument("--min-rows", type=int, default=800, help="skip vehicles with fewer rows")
    args = ap.parse_args()

    cfg = load_ini(args.ini)
    data = load_telemetry(args.telemetry)
    print(f"telemetry: {args.telemetry}\nconfig:    {args.ini}")
    for name, rows in sorted(data.items(), key=lambda kv: -len(kv[1])):
        if len(rows) >= args.min_rows:
            report(name, rows, cfg)
        else:
            print(f"\n(skipping {name}: {len(rows)} rows)")


if __name__ == "__main__":
    main()
