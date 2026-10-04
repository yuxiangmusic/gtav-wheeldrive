// WheelDrive - minimal DirectInput steering wheel support for GTA V (Legacy/Enhanced) via Script Hook V.
// Reads the wheel with DirectInput, feeds vehicle controls through SET_CONTROL_VALUE_NEXT_FRAME,
// and drives a speed-dependent centering spring + damper for force feedback.

#define DIRECTINPUT_VERSION 0x0800
#include <windows.h>
#include <dinput.h>
#include <shlobj.h>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cstdint>
#include <type_traits>

// ---------------------------------------------------------------- Script Hook V imports
// ScriptHookV.dll exports MSVC-mangled C++ names; bind to them explicitly.
#define SHV_IMPORT(decl, sym) __declspec(dllimport) decl __asm__(sym)
extern "C" {
SHV_IMPORT(void shvScriptRegister(HMODULE, void (*)()), "?scriptRegister@@YAXPEAUHINSTANCE__@@P6AXXZ@Z");
SHV_IMPORT(void shvScriptUnregister(HMODULE), "?scriptUnregister@@YAXPEAUHINSTANCE__@@@Z");
SHV_IMPORT(void shvScriptWait(DWORD), "?scriptWait@@YAXK@Z");
SHV_IMPORT(void shvNativeInit(uint64_t), "?nativeInit@@YAX_K@Z");
SHV_IMPORT(void shvNativePush64(uint64_t), "?nativePush64@@YAX_K@Z");
SHV_IMPORT(uint64_t* shvNativeCall(), "?nativeCall@@YAPEA_KXZ");
}

static void push(int v) { shvNativePush64((uint64_t)(int64_t)v); }
static void push(bool v) { shvNativePush64(v ? 1 : 0); }
static void push(float v) { uint64_t u = 0; memcpy(&u, &v, sizeof v); shvNativePush64(u); }
static void push(const char* v) { shvNativePush64((uint64_t)(uintptr_t)v); }

template <typename R, typename... A>
static R invoke(uint64_t hash, A... args) {
    shvNativeInit(hash);
    (push(args), ...);
    uint64_t* r = shvNativeCall();
    if constexpr (!std::is_void_v<R>) {
        R out;
        memcpy(&out, r, sizeof(R));
        return out;
    }
}

namespace N {
int PlayerPedId() { return invoke<int>(0xD80958FC74E988A6); }
bool IsPedInAnyVehicle(int ped) { return invoke<bool>(0x997ABD671D25CA0B, ped, false); }
int VehiclePedIsIn(int ped) { return invoke<int>(0x9A9112A0FE9A4713, ped, false); }
int PedInSeat(int veh, int seat) { return invoke<int>(0xBB40DD2270B65366, veh, seat, false); }
float EntitySpeed(int ent) { return invoke<float>(0xD5037BA82E12416F, ent); }
void SetControl(int control, float value) { invoke<bool>(0xE8A25867FBA3B05E, 0, control, value); }
void SetSteerBias(int veh, float v) { invoke<void>(0x42A8EC77D5150CBE, veh, v); }
void DisableControl(int control) { invoke<void>(0xFE99B66D079CF6BC, 0, control, true); }
float DisabledControlNormal(int control) { return invoke<float>(0x11E65974A982637C, 0, control); }
float EntityHeading(int ent) { return invoke<float>(0xE83D4F9BA2A38914, ent); }
float FrameTime() { return invoke<float>(0x15C40837039FFAF7); }
bool UsingKeyboard() { return invoke<bool>(0xA571D46727E2B718, 0); }
int EntityModel(int ent) { return invoke<int>(0x9F47B058362C84B5, ent); }
const char* ModelDisplayName(int model) { return invoke<const char*>(0xB215AAC32D25D019, model); }
struct Vec3 { float x; uint32_t _px; float y; uint32_t _py; float z; uint32_t _pz; };
Vec3 SpeedVector(int ent) { return invoke<Vec3>(0x9A8D700A51CB7B0D, ent, true); }  // relative: x right, y forward
Vec3 RotationVelocity(int ent) { return invoke<Vec3>(0x213B91045D09B983, ent); }
int VehicleClass(int veh) { return invoke<int>(0x29439776AAA00A62, veh); }
float EntityPitch(int ent) { return invoke<float>(0xD45DC2893621E1FE, ent); }
bool OnAllWheels(int veh) { return invoke<bool>(0xB104CD1BABF302E2, veh); }
bool Collided(int ent) { return invoke<bool>(0x8BAD02F0368D9E14, ent); }
int CurrentGear(int veh) { return invoke<int>(0x56185A25D45A0DCD, veh); }
float GameThrottle(int veh) { return invoke<float>(0x92D96892FC06AF22, veh); }
float MaxAcceleration(int veh) { return invoke<float>(0x5DD35C8D074E57AE, veh); }
float MaxBraking(int veh) { return invoke<float>(0xAD7E85FC227197C4, veh); }
float MaxTraction(int veh) { return invoke<float>(0xA132FB5370554DB0, veh); }
float EstimatedMaxSpeed(int veh) { return invoke<float>(0x53AF99BAA671CA47, veh); }
void DrawText(const char* s, float x, float y, float scale = 0.3f) {
    invoke<void>(0x66E0276CC5F6B9DA, 0);                 // SET_TEXT_FONT
    invoke<void>(0x07C837F9A01C34C9, scale, scale);      // SET_TEXT_SCALE
    invoke<void>(0xBE6B23FFA53FB442, 255, 255, 255, 255); // SET_TEXT_COLOUR
    invoke<void>(0x2513DFB0FB8400FE);                    // SET_TEXT_OUTLINE
    invoke<void>(0x25FBB336DF1804CB, "STRING");          // BEGIN_TEXT_COMMAND_DISPLAY_TEXT
    invoke<void>(0x6C188BE134E074AA, s);                 // ADD_TEXT_COMPONENT_SUBSTRING_PLAYER_NAME
    invoke<void>(0xCD015E5BB0D96A57, x, y, 0);           // END_TEXT_COMMAND_DISPLAY_TEXT
}
}  // namespace N

// GTA control ids
enum {
    CtlNextCamera = 0, CtlVehMoveLR = 59, CtlVehAccelerate = 71, CtlVehBrake = 72,
    CtlVehHeadlight = 74, CtlVehExit = 75, CtlVehHandbrake = 76, CtlVehLookBehind = 79, CtlVehHorn = 86,
};

// ---------------------------------------------------------------- logging / config
static char g_dir[MAX_PATH];
static char g_ini[MAX_PATH];
static FILE* g_log;
static FILE* g_steerLog;  // CSV of wheel input vs measured vehicle response, for calibration

static void logf(const char* fmt, ...) {
    if (!g_log) return;
    SYSTEMTIME t; GetLocalTime(&t);
    fprintf(g_log, "[%02d:%02d:%02d] ", t.wHour, t.wMinute, t.wSecond);
    va_list ap; va_start(ap, fmt); vfprintf(g_log, fmt, ap); va_end(ap);
    fputc('\n', g_log);
    fflush(g_log);
}

// Default settings, generated at build time from WheelDrive.ini (the single source of defaults).
#include "default_ini.h"

// Piecewise-linear curve over speed in km/h, parsed from "x:y,x:y,..." (x ascending).
struct Curve {
    float x[16], y[16];
    int n;
    float at(float kmh) const {
        if (n == 0) return 0;
        if (kmh <= x[0]) return y[0];
        for (int i = 1; i < n; i++)
            if (kmh <= x[i]) return y[i - 1] + (y[i] - y[i - 1]) * (kmh - x[i - 1]) / (x[i] - x[i - 1]);
        return y[n - 1];
    }
};

static Curve parseCurve(const char* text) {
    Curve c{};
    char buf[256];
    strncpy(buf, text, sizeof buf - 1);
    buf[sizeof buf - 1] = 0;
    for (char* t = strtok(buf, ", "); t && c.n < 16; t = strtok(nullptr, ", ")) {
        char* colon = strchr(t, ':');
        if (!colon) continue;
        c.x[c.n] = (float)atof(t);
        c.y[c.n] = (float)atof(colon + 1);
        c.n++;
    }
    return c;
}

struct ButtonList {
    int ids[8];
    int count;
};

struct Config {
    char device[64];
    int steerAxis, throttleAxis, brakeAxis;
    bool steerInv, throttleInv, brakeInv;
    float wheelRange, steerLock, deadzone, steerAntiDz, steerGamma, steerSpeedRef, steerGain;
    Curve steerSpeedGain;
    int steerMode;
    int btnHandbrake, btnHeadlight, btnLookBehind, btnExit, btnHorn, btnCamera;
    ButtonList btnReverse, btnForward;
    int pedalMode;
    float maxAccel, brakeMaxDecel;
    float gThrZero, gThrSlope, gHold, gBrkOffset, gBrkSlope, coastKi, kickdownExp;
    Curve realAccel, realCoast, gThrFade, gHoldDecel, gEngineBrake;
    bool ffb;
    int gain, springBase, springPerMps, springMax, damper, maxForce;
} cfg;

static const char* kAxisNames[] = {"X", "Y", "Z", "RX", "RY", "RZ", "S0", "S1"};
static const DWORD kAxisOffsets[] = {DIJOFS_X, DIJOFS_Y, DIJOFS_Z, DIJOFS_RX, DIJOFS_RY, DIJOFS_RZ,
                                     DIJOFS_SLIDER(0), DIJOFS_SLIDER(1)};

static int axisIndex(const char* name, int def) {
    for (int i = 0; i < 8; i++)
        if (_stricmp(name, kAxisNames[i]) == 0) return i;
    return def;
}

// Adds a key with its default value if an older ini doesn't have it yet.
static void iniEnsure(const char* sec, const char* key, const char* def) {
    char buf[8];
    if (!GetPrivateProfileStringA(sec, key, "", buf, sizeof buf, g_ini)) WritePrivateProfileStringA(sec, key, def, g_ini);
}
static int iniInt(const char* sec, const char* key, int def) { return GetPrivateProfileIntA(sec, key, def, g_ini); }
static float iniFloat(const char* sec, const char* key, float def) {
    char buf[32];
    GetPrivateProfileStringA(sec, key, "", buf, sizeof buf, g_ini);
    return buf[0] ? (float)atof(buf) : def;
}
static int iniAxis(const char* key, const char* def) {
    char buf[8];
    GetPrivateProfileStringA("Wheel", key, def, buf, sizeof buf, g_ini);
    return axisIndex(buf, axisIndex(def, 0));
}

static ButtonList iniButtons(const char* key, const char* def) {
    char buf[64];
    GetPrivateProfileStringA("Buttons", key, def, buf, sizeof buf, g_ini);
    ButtonList list{};
    for (char* t = strtok(buf, ", "); t && list.count < 8; t = strtok(nullptr, ", ")) list.ids[list.count++] = atoi(t);
    return list;
}

static void loadConfig() {
    if (GetFileAttributesA(g_ini) == INVALID_FILE_ATTRIBUTES) {
        if (FILE* f = fopen(g_ini, "w")) { fputs(kDefaultIni, f); fclose(f); }
    }
    iniEnsure("Wheel", "SteerAntiDeadzone", "0.2");
    iniEnsure("Wheel", "SteerGamma", "1.0");
    iniEnsure("Wheel", "SteerSpeedRef", "0");
    iniEnsure("Wheel", "SteerGain", "0.23");
    iniEnsure("Buttons", "Reverse", "15");
    iniEnsure("Buttons", "Forward", "14");
    GetPrivateProfileStringA("Wheel", "DeviceName", "G29", cfg.device, sizeof cfg.device, g_ini);
    cfg.steerAxis = iniAxis("SteerAxis", "X");
    cfg.throttleAxis = iniAxis("ThrottleAxis", "Y");
    cfg.brakeAxis = iniAxis("BrakeAxis", "RZ");
    cfg.steerInv = iniInt("Wheel", "SteerInvert", 0);
    cfg.throttleInv = iniInt("Wheel", "ThrottleInvert", 1);
    cfg.brakeInv = iniInt("Wheel", "BrakeInvert", 1);
    cfg.wheelRange = iniFloat("Wheel", "WheelRangeDeg", 900);
    cfg.steerLock = iniFloat("Wheel", "SteerLockDeg", 1200);
    cfg.deadzone = iniFloat("Wheel", "PedalDeadzone", 0.03f);
    cfg.steerMode = iniInt("Wheel", "SteerMode", 1);
    cfg.steerAntiDz = iniFloat("Wheel", "SteerAntiDeadzone", 0.2f);
    cfg.steerGamma = iniFloat("Wheel", "SteerGamma", 1.0f);
    cfg.steerSpeedRef = iniFloat("Wheel", "SteerSpeedRef", 0.0f);
    cfg.steerGain = iniFloat("Wheel", "SteerGain", 0.23f);
    iniEnsure("Wheel", "SteerSpeedGainCurve", "0:0.6,14:0.6,18:0.66,25:0.78,35:0.91,45:1");
    {
        char buf[256];
        GetPrivateProfileStringA("Wheel", "SteerSpeedGainCurve", "", buf, sizeof buf, g_ini);
        cfg.steerSpeedGain = parseCurve(buf);
        if (cfg.steerSpeedGain.n == 0) cfg.steerSpeedGain = parseCurve("0:1");
    }
    if (cfg.steerAntiDz < 0) cfg.steerAntiDz = 0;
    if (cfg.steerAntiDz > 0.5f) cfg.steerAntiDz = 0.5f;
    if (cfg.steerGamma < 0.2f) cfg.steerGamma = 0.2f;
    cfg.btnReverse = iniButtons("Reverse", "15");
    cfg.btnForward = iniButtons("Forward", "14");
    cfg.btnHandbrake = iniInt("Buttons", "Handbrake", 0);
    cfg.btnHeadlight = iniInt("Buttons", "Headlight", 1);
    cfg.btnLookBehind = iniInt("Buttons", "LookBehind", 2);
    cfg.btnExit = iniInt("Buttons", "ExitVehicle", 3);
    cfg.btnHorn = iniInt("Buttons", "Horn", 23);
    cfg.btnCamera = iniInt("Buttons", "Camera", 9);
    static const char* pedalKeys[][2] = {
        {"PedalMode", "1"}, {"MaxAccel", "4.5"}, {"BrakeMaxDecel", "9.0"},
        {"RealAccelCurve", "0:1,30:0.96,50:0.8,80:0.58,100:0.47,130:0.33,160:0.2"},
        {"RealCoastCurve", "0:0.3,30:0.4,60:0.55,90:0.75,120:1.0,160:1.4"},
        {"GameThrottleZero", "0.271"}, {"GameThrottleSlope", "11.76"},
        {"GameThrottleFade", "0:1.1,15:1.1,20:1.24,32:1.25,36:0.78,45:0.68,55:0.74,65:0.8,78:0.8,85:0.49,100:0.45,115:0.4,130:0.3,160:0.2"},
        {"GameHold", "0.26"}, {"GameHoldDecelCurve", "0:0,50:0.1,80:0.3,110:0.5,130:0.8"},
        {"GameEngineBrakeCurve", "0:3.2,20:3.2,30:3.28,43:3.37,57:3.54,72:3.67,86:3.89,120:4.4"},
        {"GameBrakeOffset", "0.1577"}, {"GameBrakeSlope", "39.37"}, {"CoastKi", "1.0"}, {"KickdownExp", "4"}};
    for (auto& k : pedalKeys) iniEnsure("Pedals", k[0], k[1]);
    auto iniCurve = [](const char* key) {
        char buf[256];
        GetPrivateProfileStringA("Pedals", key, "", buf, sizeof buf, g_ini);
        return parseCurve(buf);
    };
    cfg.pedalMode = iniInt("Pedals", "PedalMode", 1);
    cfg.maxAccel = iniFloat("Pedals", "MaxAccel", 4.5f);
    cfg.brakeMaxDecel = iniFloat("Pedals", "BrakeMaxDecel", 9.0f);
    cfg.realAccel = iniCurve("RealAccelCurve");
    cfg.realCoast = iniCurve("RealCoastCurve");
    cfg.gThrZero = iniFloat("Pedals", "GameThrottleZero", 0.271f);
    cfg.gThrSlope = iniFloat("Pedals", "GameThrottleSlope", 11.76f);
    cfg.gThrFade = iniCurve("GameThrottleFade");
    cfg.gHold = iniFloat("Pedals", "GameHold", 0.26f);
    cfg.gHoldDecel = iniCurve("GameHoldDecelCurve");
    cfg.gEngineBrake = iniCurve("GameEngineBrakeCurve");
    cfg.gBrkOffset = iniFloat("Pedals", "GameBrakeOffset", 0.1577f);
    cfg.gBrkSlope = iniFloat("Pedals", "GameBrakeSlope", 39.37f);
    cfg.coastKi = iniFloat("Pedals", "CoastKi", 1.0f);
    cfg.kickdownExp = fmaxf(iniFloat("Pedals", "KickdownExp", 4.0f), 0.5f);
    if (cfg.gThrSlope < 0.1f) cfg.gThrSlope = 0.1f;
    if (cfg.gBrkSlope < 0.1f) cfg.gBrkSlope = 0.1f;
    cfg.ffb = iniInt("FFB", "Enable", 1);
    cfg.gain = iniInt("FFB", "Gain", 80);
    cfg.springBase = iniInt("FFB", "SpringBase", 0);
    cfg.springPerMps = iniInt("FFB", "SpringPerMps", 80);
    cfg.springMax = iniInt("FFB", "SpringMax", 2000);
    cfg.damper = iniInt("FFB", "Damper", 400);
    iniEnsure("FFB", "MaxForce", "20");
    cfg.maxForce = iniInt("FFB", "MaxForce", 20);
    if (cfg.maxForce < 0) cfg.maxForce = 0;
    if (cfg.maxForce > 100) cfg.maxForce = 100;
    if (cfg.steerLock < 30) cfg.steerLock = 30;
    logf("config: device='%s' steer=%s throttle=%s brake=%s range=%.0f lock=%.0f antidz=%.2f gamma=%.2f mode=%d "
         "reverse_buttons=%d ffb=%d",
         cfg.device, kAxisNames[cfg.steerAxis], kAxisNames[cfg.throttleAxis], kAxisNames[cfg.brakeAxis],
         cfg.wheelRange, cfg.steerLock, cfg.steerAntiDz, cfg.steerGamma, cfg.steerMode, cfg.btnReverse.count,
         (int)cfg.ffb);
}

// ---------------------------------------------------------------- DirectInput wheel
static IDirectInput8A* g_di;
static IDirectInputDevice8A* g_dev;
static IDirectInputEffect* g_spring;
static IDirectInputEffect* g_damper;
static bool g_exclusive;
static char g_devName[MAX_PATH];

struct EnumCtx { GUID guid; bool found; bool foundFF; char name[MAX_PATH]; };

static BOOL CALLBACK enumDevice(const DIDEVICEINSTANCEA* inst, void* p) {
    auto* ctx = (EnumCtx*)p;
    logf("found device: '%s'", inst->tszProductName);
    if (cfg.device[0] && strstr(inst->tszProductName, cfg.device)) {
        ctx->guid = inst->guidInstance; ctx->found = true;
        strncpy(ctx->name, inst->tszProductName, sizeof ctx->name - 1);
        return DIENUM_STOP;
    }
    if (!ctx->found) {
        ctx->guid = inst->guidInstance; ctx->found = true;
        strncpy(ctx->name, inst->tszProductName, sizeof ctx->name - 1);
    }
    return DIENUM_CONTINUE;
}

static BOOL CALLBACK setAxisRange(const DIDEVICEOBJECTINSTANCEA* obj, void* p) {
    DIPROPRANGE r{};
    r.diph.dwSize = sizeof r; r.diph.dwHeaderSize = sizeof r.diph;
    r.diph.dwHow = DIPH_BYID; r.diph.dwObj = obj->dwType;
    r.lMin = 0; r.lMax = 65535;
    ((IDirectInputDevice8A*)p)->SetProperty(DIPROP_RANGE, &r.diph);
    return DIENUM_CONTINUE;
}

static HWND g_gameWnd;
static BOOL CALLBACK findGameWindow(HWND w, LPARAM) {
    DWORD pid; GetWindowThreadProcessId(w, &pid);
    if (pid == GetCurrentProcessId() && IsWindowVisible(w) && !GetWindow(w, GW_OWNER)) { g_gameWnd = w; return FALSE; }
    return TRUE;
}

static DICONDITION g_cond;
static LONG g_axisOfs;
static DIEFFECT makeCondEffect(LONG* dir) {
    DIEFFECT e{};
    e.dwSize = sizeof e;
    e.dwFlags = DIEFF_CARTESIAN | DIEFF_OBJECTOFFSETS;
    e.dwDuration = INFINITE;
    e.dwGain = DI_FFNOMINALMAX;
    e.dwTriggerButton = DIEB_NOTRIGGER;
    e.cAxes = 1;
    e.rgdwAxes = (DWORD*)&g_axisOfs;
    e.rglDirection = dir;
    e.cbTypeSpecificParams = sizeof g_cond;
    e.lpvTypeSpecificParams = &g_cond;
    return e;
}

static void setCond(IDirectInputEffect* fx, int coef) {
    if (!fx) return;
    if (coef < 0) coef = 0;
    if (coef > 10000) coef = 10000;
    g_cond = {};
    g_cond.lPositiveCoefficient = g_cond.lNegativeCoefficient = coef;
    g_cond.dwPositiveSaturation = g_cond.dwNegativeSaturation = (DWORD)(cfg.maxForce * DI_FFNOMINALMAX / 100);
    LONG dir = 0;
    DIEFFECT e = makeCondEffect(&dir);
    fx->SetParameters(&e, DIEP_TYPESPECIFICPARAMS);
}

static void releaseWheel() {
    if (g_spring) { g_spring->Stop(); g_spring->Release(); g_spring = nullptr; }
    if (g_damper) { g_damper->Stop(); g_damper->Release(); g_damper = nullptr; }
    if (g_dev) { g_dev->Unacquire(); g_dev->Release(); g_dev = nullptr; }
}

static void createEffects() {
    if (!cfg.ffb || !g_exclusive) return;
    DIPROPDWORD p{};
    p.diph.dwSize = sizeof p; p.diph.dwHeaderSize = sizeof p.diph; p.diph.dwHow = DIPH_DEVICE;
    p.dwData = (DWORD)(cfg.gain * 100);
    g_dev->SetProperty(DIPROP_FFGAIN, &p.diph);

    g_axisOfs = (LONG)kAxisOffsets[cfg.steerAxis];
    LONG dir = 0;
    g_cond = {};
    g_cond.dwPositiveSaturation = g_cond.dwNegativeSaturation = DI_FFNOMINALMAX;
    DIEFFECT e = makeCondEffect(&dir);
    HRESULT h1 = g_dev->CreateEffect(GUID_Spring, &e, &g_spring, nullptr);
    HRESULT h2 = g_dev->CreateEffect(GUID_Damper, &e, &g_damper, nullptr);
    if (g_spring) g_spring->Start(1, 0);
    if (g_damper) g_damper->Start(1, 0);
    logf("ffb: spring=0x%08lx damper=0x%08lx", (unsigned long)h1, (unsigned long)h2);
}

static bool initWheel() {
    releaseWheel();
    if (!g_di && FAILED(DirectInput8Create(GetModuleHandleA(nullptr), DIRECTINPUT_VERSION, IID_IDirectInput8A,
                                           (void**)&g_di, nullptr))) {
        logf("DirectInput8Create failed");
        return false;
    }
    EnumCtx ctx{};
    g_di->EnumDevices(DI8DEVCLASS_GAMECTRL, enumDevice, &ctx, DIEDFL_ATTACHEDONLY);
    if (!ctx.found) { logf("no game controller found"); return false; }
    strcpy(g_devName, ctx.name);
    if (FAILED(g_di->CreateDevice(ctx.guid, &g_dev, nullptr))) { logf("CreateDevice failed"); return false; }
    g_dev->SetDataFormat(&c_dfDIJoystick2);

    EnumWindows(findGameWindow, 0);
    g_exclusive = false;
    if (g_gameWnd && cfg.ffb &&
        SUCCEEDED(g_dev->SetCooperativeLevel(g_gameWnd, DISCL_EXCLUSIVE | DISCL_BACKGROUND)))
        g_exclusive = true;
    else
        g_dev->SetCooperativeLevel(g_gameWnd, DISCL_NONEXCLUSIVE | DISCL_BACKGROUND);

    g_dev->EnumObjects(setAxisRange, g_dev, DIDFT_AXIS);
    DIPROPDWORD ac{};
    ac.diph.dwSize = sizeof ac; ac.diph.dwHeaderSize = sizeof ac.diph; ac.diph.dwHow = DIPH_DEVICE;
    ac.dwData = DIPROPAUTOCENTER_OFF;
    g_dev->SetProperty(DIPROP_AUTOCENTER, &ac.diph);

    HRESULT hr = g_dev->Acquire();
    logf("using '%s' (exclusive=%d, acquire=0x%08lx, hwnd=%p)", g_devName, (int)g_exclusive, (unsigned long)hr,
         (void*)g_gameWnd);
    createEffects();
    return true;
}

static bool pollWheel(DIJOYSTATE2& js) {
    if (!g_dev) return false;
    HRESULT hr = g_dev->Poll();
    if (FAILED(hr)) {
        g_dev->Acquire();
        g_dev->Poll();
    }
    hr = g_dev->GetDeviceState(sizeof js, &js);
    return SUCCEEDED(hr);
}

static LONG axisRaw(const DIJOYSTATE2& js, int idx) {
    switch (idx) {
        case 0: return js.lX;  case 1: return js.lY;  case 2: return js.lZ;
        case 3: return js.lRx; case 4: return js.lRy; case 5: return js.lRz;
        case 6: return js.rglSlider[0]; default: return js.rglSlider[1];
    }
}

static float pedal(const DIJOYSTATE2& js, int axis, bool inv) {
    float v = axisRaw(js, axis) / 65535.0f;
    if (inv) v = 1.0f - v;
    if (v < cfg.deadzone) return 0.0f;
    return (v - cfg.deadzone) / (1.0f - cfg.deadzone);
}

static bool btn(const DIJOYSTATE2& js, int i) { return i >= 0 && i < 128 && (js.rgbButtons[i] & 0x80); }

static bool anyBtn(const DIJOYSTATE2& js, const ButtonList& list) {
    for (int i = 0; i < list.count; i++)
        if (btn(js, list.ids[i])) return true;
    return false;
}

// Maps wheel position (-1..1) onto GTA's stick input so steering starts responding right off center.
static float shapeSteer(float s) {
    float a = fabsf(s);
    if (a < 0.002f) return 0.0f;
    float out = cfg.steerAntiDz + (1.0f - cfg.steerAntiDz) * powf(a, cfg.steerGamma);
    return copysignf(out > 1 ? 1 : out, s);
}

// ---------------------------------------------------------------- main loop
// Turns pedal positions into GTA accelerate/brake/handbrake controls so the car's longitudinal response matches
// real-car targets. GTA has a ~25% control deadzone, strong lift-off "engine braking" (~3.25 m/s^2) and very
// steep brakes, all measured; decelerations gentler than the engine braking are synthesized by alternating,
// frame by frame, between holding speed and lifting off in the right proportion.
struct PedalOut { float accel, brake; bool handbrake; };
struct PedalState { float sigma, integ; };

// Realizes a desired GTA longitudinal acceleration (m/s^2, <= ~0) without the driver's throttle:
// light throttle to cancel excess drag, frame-by-frame lift-off duty between holding and GTA's engine
// braking, or the brake control beyond that.
static PedalOut actuate(float aCmd, float kmh, float& sigma) {
    PedalOut out{0, 0, false};
    float hold = cfg.gHoldDecel.at(kmh), lift = cfg.gEngineBrake.at(kmh);
    float gain = cfg.gThrSlope * fmaxf(cfg.gThrFade.at(kmh), 0.05f);
    if (aCmd >= -hold) {
        out.accel = cfg.gThrZero + (aCmd + hold) / gain;
    } else if (aCmd >= -lift) {
        sigma += (-aCmd - hold) / (lift - hold);  // fraction of frames lifting off
        if (sigma >= 1.0f) sigma -= 1.0f;          // lift-off frame: no input -> GTA engine braking
        else out.accel = cfg.gHold;                // hold frame: just past the deadzone, no engine braking
    } else {
        out.brake = cfg.gBrkOffset - aCmd / cfg.gBrkSlope;
    }
    return out;
}

// Pedals -> GTA controls so the car responds like a real sedan. Throttle and firm braking are feedforward
// from measured GTA response; coasting and light braking (below GTA's own engine braking) run closed loop on
// the measured, grade-corrected acceleration, since GTA's drag varies by car and speed.
static PedalOut mapPedals(float thr, float brk, float fwdSpeed, float measAccel, float dt, PedalState& st) {
    PedalOut out{0, 0, false};
    float kmh = fwdSpeed * 3.6f;
    float hold = cfg.gHoldDecel.at(kmh);
    float gain = cfg.gThrSlope * fmaxf(cfg.gThrFade.at(kmh), 0.05f);
    if (thr > 0) {
        // Normal pedal travel follows a real sedan; toward the floor it blends up to full GTA throttle
        // (kickdown) so every car keeps its own full power and top speed.
        auto calibrated = [&](float p) { return cfg.gThrZero + (p * cfg.maxAccel * cfg.realAccel.at(kmh) + hold) / gain; };
        float c = fminf(calibrated(thr), 1.0f);
        float full = fminf(calibrated(1.0f), 1.0f);
        out.accel = c + (1.0f - full) * powf(thr, cfg.kickdownExp);
        st.integ = 0;
    } else if (brk > 0 && fwdSpeed < 0.5f) {
        out.handbrake = true;  // hold the car at a stop instead of letting GTA reverse on the brake
        st.integ = 0;
    } else if (fwdSpeed > 1.0f) {
        float decel = cfg.realCoast.at(kmh) + cfg.brakeMaxDecel * brk;
        if (decel >= cfg.gEngineBrake.at(kmh)) {
            out = actuate(-decel, kmh, st.sigma);
            st.integ = 0;
        } else {
            st.integ += cfg.coastKi * (-decel - measAccel) * dt;
            if (st.integ > 4) st.integ = 4;
            if (st.integ < -4) st.integ = -4;
            out = actuate(-decel + st.integ, kmh, st.sigma);
        }
    } else {
        st.integ = 0;
    }
    if (out.accel > 1) out.accel = 1;
    if (out.brake > 1) out.brake = 1;
    return out;
}

static void drawOverlay(const DIJOYSTATE2& js, float steer, float steerOut, float thr, float brk) {
    char line[128];
    snprintf(line, sizeof line, "WheelDrive: %s  ffb=%s", g_devName, g_spring ? "on" : "off");
    N::DrawText(line, 0.01f, 0.30f);
    snprintf(line, sizeof line, "X=%ld Y=%ld Z=%ld", js.lX, js.lY, js.lZ);
    N::DrawText(line, 0.01f, 0.33f);
    snprintf(line, sizeof line, "RX=%ld RY=%ld RZ=%ld", js.lRx, js.lRy, js.lRz);
    N::DrawText(line, 0.01f, 0.36f);
    snprintf(line, sizeof line, "S0=%ld S1=%ld", js.rglSlider[0], js.rglSlider[1]);
    N::DrawText(line, 0.01f, 0.39f);
    snprintf(line, sizeof line, "steer=%.2f -> %.2f throttle=%.2f brake=%.2f", steer, steerOut, thr, brk);
    N::DrawText(line, 0.01f, 0.42f);
    int n = snprintf(line, sizeof line, "buttons:");
    for (int i = 0; i < 32 && n < (int)sizeof line - 4; i++)
        if (btn(js, i)) n += snprintf(line + n, sizeof line - n, " %d", i);
    N::DrawText(line, 0.01f, 0.45f);
}

static void scriptMain() {
    loadConfig();
    initWheel();
    bool overlay = false, f9 = false, f10 = false;
    bool reverse = false;
    int biasVeh = 0;  // vehicle we last applied steer bias to
    int lastVeh = 0;
    float lastHeading = 0, yawRate = 0, lastFwd = 0, accel = 0;
    DWORD lastSteerLog = 0;
    int loggedVeh = 0;
    PedalState pedalState{};
    float pedalAccelOut = 0, pedalBrakeOut = 0;
    DWORD lastFfb = 0, lastRetry = GetTickCount();

    for (;;) {
        bool k9 = GetAsyncKeyState(VK_F9) & 0x8000, k10 = GetAsyncKeyState(VK_F10) & 0x8000;
        if (k9 && !f9) overlay = !overlay;
        if (k10 && !f10) { loadConfig(); initWheel(); }
        f9 = k9; f10 = k10;

        if (!g_dev && GetTickCount() - lastRetry > 5000) { lastRetry = GetTickCount(); initWheel(); }

        DIJOYSTATE2 js{};
        if (pollWheel(js)) {
            float steer = (axisRaw(js, cfg.steerAxis) - 32767.5f) / 32767.5f;
            if (cfg.steerInv) steer = -steer;
            steer *= cfg.wheelRange / cfg.steerLock;
            steer = steer < -1 ? -1 : steer > 1 ? 1 : steer;
            float thr = pedal(js, cfg.throttleAxis, cfg.throttleInv);
            float brk = pedal(js, cfg.brakeAxis, cfg.brakeInv);
            int ped = N::PlayerPedId();
            int veh = N::IsPedInAnyVehicle(ped) ? N::VehiclePedIsIn(ped) : 0;
            bool driving = veh && N::PedInSeat(veh, -1) == ped;
            // Motorcycles/bicycles steer by leaning; locking the bar angle breaks them, so use stick input.
            int vclass = driving ? N::VehicleClass(veh) : -1;
            bool directSteer = cfg.steerMode == 1 && vclass != 8 && vclass != 13;
            float speed = veh ? N::EntitySpeed(veh) : 0.0f;

            // Measure actual turning: yaw rate -> effective front-wheel angle (kinematic, wheelbase ~2.7 m).
            float heading = veh ? N::EntityHeading(veh) : 0.0f, dt = N::FrameTime();
            if (veh && veh == lastVeh && dt > 0) {
                float dh = heading - lastHeading;
                if (dh > 180) dh -= 360;
                if (dh < -180) dh += 360;
                yawRate += 0.2f * (dh / dt - yawRate);
            } else {
                yawRate = 0;
            }
            float fwd = veh ? N::SpeedVector(veh).y : 0.0f;
            if (veh && veh == lastVeh && dt > 0) accel += 0.25f * ((fwd - lastFwd) / dt - accel);
            else accel = 0;
            lastFwd = fwd;
            lastVeh = veh;
            lastHeading = heading;
            float steerOut;
            if (directSteer) {
                // Direct angle: response curve plus speed-sensitive reduction, since bypassing the game's
                // input path also bypasses its own high-speed steering reduction.
                float scale = cfg.steerSpeedRef > 0 ? 1.0f / (1.0f + speed / cfg.steerSpeedRef) : 1.0f;
                steerOut = copysignf(powf(fabsf(steer), cfg.steerGamma), steer) * scale * cfg.steerGain *
                           cfg.steerSpeedGain.at(speed * 3.6f);
                steerOut = steerOut < -1 ? -1 : steerOut > 1 ? 1 : steerOut;
            } else if (vclass == 8 || vclass == 13) {
                // Bikes have handlebars, nothing real to match: full bar at +-180 deg of wheel, via stick input.
                float bike = (axisRaw(js, cfg.steerAxis) - 32767.5f) / 32767.5f * cfg.wheelRange / 2 / 180.0f;
                if (cfg.steerInv) bike = -bike;
                steerOut = shapeSteer(bike < -1 ? -1 : bike > 1 ? 1 : bike);
            } else {
                steerOut = shapeSteer(steer);
            }

            if (biasVeh && biasVeh != (driving ? veh : 0)) { N::SetSteerBias(biasVeh, 0.0f); biasVeh = 0; }
            if (!driving || anyBtn(js, cfg.btnForward)) reverse = false;
            else if (anyBtn(js, cfg.btnReverse)) reverse = true;

            // Keyboard steering still reaches the game; while it's held, hand steering back to it.
            // Only keyboard: GTA may also read the wheel itself as a gamepad stick, which must not count.
            float gameSteer = driving ? N::DisabledControlNormal(CtlVehMoveLR) : 0.0f;
            bool usingKb = N::UsingKeyboard();
            bool otherSteer = driving && usingKb && fabsf(gameSteer) > 0.05f;

            pedalAccelOut = pedalBrakeOut = 0;
            if (driving) {
                if (directSteer && !otherSteer) {
                    // Wheel drives the steering angle directly; block the game's own steering so nothing fights it.
                    N::DisableControl(CtlVehMoveLR);
                    N::SetSteerBias(veh, -steerOut);
                    biasVeh = veh;
                } else if (directSteer) {
                    biasVeh = 0;  // not calling SET_VEHICLE_STEER_BIAS releases the lock
                }
                else if (steerOut != 0.0f) N::SetControl(CtlVehMoveLR, steerOut);
                if (cfg.pedalMode == 1 && !reverse) {
                    float accelFlat = accel + 9.81f * sinf(N::EntityPitch(veh) * 3.14159265f / 180);
                    PedalOut po = mapPedals(thr, brk, fwd, accelFlat, dt, pedalState);
                    if (po.accel > 0) N::SetControl(CtlVehAccelerate, po.accel);
                    if (po.brake > 0) N::SetControl(CtlVehBrake, po.brake);
                    if (po.handbrake) N::SetControl(CtlVehHandbrake, 1.0f);
                    pedalAccelOut = po.accel;
                    pedalBrakeOut = po.brake;
                } else if (reverse) {
                    // GTA drives backwards on the brake control and stops a reversing car with the accelerate
                    // control. Gas -> brake control; brake -> accelerate control only while still rolling
                    // backwards, then hold with the handbrake so the car never creeps forward.
                    float go = 0, stop = 0;
                    if (thr > 0) {
                        go = cfg.pedalMode == 1 ? cfg.gThrZero + cfg.maxAccel * thr / cfg.gThrSlope : thr;
                        N::SetControl(CtlVehBrake, fminf(go, 1.0f));
                    } else if (brk > 0) {
                        if (fwd < -0.5f) {
                            stop = cfg.pedalMode == 1 ? cfg.gBrkOffset + cfg.brakeMaxDecel * brk / cfg.gBrkSlope : brk;
                            N::SetControl(CtlVehAccelerate, fminf(stop, 1.0f));
                        } else {
                            N::SetControl(CtlVehHandbrake, 1.0f);
                        }
                    }
                    pedalAccelOut = stop;
                    pedalBrakeOut = go;
                } else {
                    if (thr > 0) N::SetControl(CtlVehAccelerate, thr);
                    if (brk > 0) N::SetControl(CtlVehBrake, brk);
                    pedalAccelOut = thr;
                    pedalBrakeOut = brk;
                }
                if (btn(js, cfg.btnHandbrake)) N::SetControl(CtlVehHandbrake, 1.0f);
                if (btn(js, cfg.btnHorn)) N::SetControl(CtlVehHorn, 1.0f);
                if (btn(js, cfg.btnHeadlight)) N::SetControl(CtlVehHeadlight, 1.0f);
                if (btn(js, cfg.btnLookBehind)) N::SetControl(CtlVehLookBehind, 1.0f);
            }
            if (veh) {
                if (btn(js, cfg.btnExit)) N::SetControl(CtlVehExit, 1.0f);
                if (btn(js, cfg.btnCamera)) N::SetControl(CtlNextCamera, 1.0f);
            }

            if (GetTickCount() - lastFfb > 50) {
                lastFfb = GetTickCount();
                int spring = driving ? cfg.springBase + (int)(speed * cfg.springPerMps) : cfg.springBase;
                if (spring > cfg.springMax) spring = cfg.springMax;
                setCond(g_spring, spring);
                setCond(g_damper, driving ? cfg.damper : cfg.damper / 2);
            }
            if (g_steerLog && driving && veh != loggedVeh) {
                loggedVeh = veh;
                int model = N::EntityModel(veh);
                fprintf(g_steerLog, "# vehicle %s (model 0x%08x), mode=%d lock=%.0f range=%.0f gamma=%.2f speedref=%.1f gain=%.3f"
                        " | max_accel=%.3f max_braking=%.3f max_traction=%.3f est_max_speed=%.1f gears=%d\n",
                        N::ModelDisplayName(model), (unsigned)model, cfg.steerMode, cfg.steerLock, cfg.wheelRange,
                        cfg.steerGamma, cfg.steerSpeedRef, cfg.steerGain, N::MaxAcceleration(veh), N::MaxBraking(veh),
                        N::MaxTraction(veh), N::EstimatedMaxSpeed(veh),
                        invoke<int>(0x24910C3D66BA770D, veh));
            }
            if (g_steerLog && driving && GetTickCount() - lastSteerLog >= 100) {
                lastSteerLog = GetTickCount();
                const float kPi = 3.14159265f, kWheelbase = 2.7f;
                float wheelDeg = (axisRaw(js, cfg.steerAxis) - 32767.5f) / 32767.5f * cfg.wheelRange / 2;
                N::Vec3 v = N::SpeedVector(veh), rv = N::RotationVelocity(veh);
                // Positive = right for all angle/rate columns.
                float yaw = -yawRate;
                float effDeg = speed > 1.0f ? atanf(yaw * kPi / 180 * kWheelbase / speed) * 180 / kPi : 0.0f;
                float slipDeg = fabsf(v.y) > 1.0f ? atanf(v.x / fabsf(v.y)) * 180 / kPi : 0.0f;
                fprintf(g_steerLog,
                        "%lu,%.4f,%ld,%.2f,%.4f,%.4f,%.2f,%.2f,%.2f,%.2f,%.2f,%.2f,%.2f,%.3f,%.4f,%d,%d,%d,%.3f,%.3f,%d,"
                        "%ld,%ld,%.3f,%.3f,%.3f,%.2f,%d,%d,%d,%.3f,%.3f,%d\n",
                        (unsigned long)lastSteerLog, dt, axisRaw(js, cfg.steerAxis), wheelDeg, steer, steerOut,
                        steerOut * 40.0f, effDeg, speed, v.y, v.x, slipDeg, yaw, -rv.z, gameSteer, (int)usingKb,
                        (int)otherSteer, (int)(directSteer && !otherSteer), thr, brk, (int)reverse,
                        axisRaw(js, cfg.throttleAxis), axisRaw(js, cfg.brakeAxis), N::GameThrottle(veh), accel,
                        // grade-corrected: remove gravity component along the slope (pitch > 0 = nose up)
                        accel + 9.81f * sinf(N::EntityPitch(veh) * kPi / 180), N::EntityPitch(veh),
                        N::CurrentGear(veh), (int)N::OnAllWheels(veh), (int)N::Collided(veh), pedalAccelOut,
                        pedalBrakeOut, cfg.pedalMode);
                fflush(g_steerLog);
            }
            if (overlay) drawOverlay(js, steer, steerOut, thr, brk);
        } else if (overlay) {
            N::DrawText("WheelDrive: no wheel (F10 to retry)", 0.01f, 0.30f);
        }
        shvScriptWait(0);
    }
}

BOOL APIENTRY DllMain(HMODULE mod, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) {
        // Keep files outside the game folder: DirectStorage on Enhanced locks files in the game directory.
        if (SUCCEEDED(SHGetFolderPathA(nullptr, CSIDL_PERSONAL, nullptr, 0, g_dir))) {
            strcat(g_dir, "\\WheelDrive");
            CreateDirectoryA(g_dir, nullptr);
            snprintf(g_ini, sizeof g_ini, "%s\\WheelDrive.ini", g_dir);
            char logPath[MAX_PATH];
            snprintf(logPath, sizeof logPath, "%s\\WheelDrive.log", g_dir);
            g_log = fopen(logPath, "w");
            // Keep the last 3 sessions: telemetry.csv (current), telemetry.1.csv, telemetry.2.csv (oldest).
            char older[MAX_PATH], newer[MAX_PATH];
            for (int i = 2; i >= 1; i--) {
                snprintf(older, sizeof older, "%s\\telemetry.%d.csv", g_dir, i);
                if (i == 1) snprintf(newer, sizeof newer, "%s\\telemetry.csv", g_dir);
                else snprintf(newer, sizeof newer, "%s\\telemetry.%d.csv", g_dir, i - 1);
                MoveFileExA(newer, older, MOVEFILE_REPLACE_EXISTING);
            }
            snprintf(logPath, sizeof logPath, "%s\\telemetry.csv", g_dir);
            g_steerLog = fopen(logPath, "w");
            if (g_steerLog)
                fputs("tick_ms,frame_dt,raw_axis,wheel_deg,steer_linear,steer_out,target_tire_deg_at40,"
                      "measured_tire_deg,speed_mps,fwd_mps,lat_mps,body_slip_deg,yaw_dps,rotvel_z,"
                      "game_steer_ctl,using_kb,kb_override,bias_applied,throttle,brake,reverse,"
                      "raw_throttle,raw_brake,game_throttle,accel_mps2,accel_flat_mps2,pitch_deg,gear,on_wheels,"
                      "collided,accel_ctl_out,brake_ctl_out,pedal_mode\n",
                      g_steerLog);
        }
        logf("WheelDrive loaded");
        shvScriptRegister(mod, scriptMain);
    } else if (reason == DLL_PROCESS_DETACH) {
        shvScriptUnregister(mod);
        releaseWheel();
        if (g_di) { g_di->Release(); g_di = nullptr; }
        if (g_log) fclose(g_log);
        if (g_steerLog) fclose(g_steerLog);
    }
    return TRUE;
}
