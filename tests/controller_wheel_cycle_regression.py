"""Exercise the real gameplay wheel rules and wheel order with a stubbed game boundary."""
from pathlib import Path
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]


def function(path, signature):
    source = (root / path).read_text(encoding="utf-8")
    start = source.index(signature)
    opening = source.index("{", start)
    depth = 1
    end = opening + 1
    while depth:
        depth += (source[end] == "{") - (source[end] == "}")
        end += 1
    return source[start:end]


program = r'''
#include <Windows.h>
#include <Xinput.h>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <vector>
using Clock = std::chrono::steady_clock;
namespace settings {
struct Values { bool ammoWheel = true; WORD wheelButton = 0; } values;
Values& Get() { return values; }
}
namespace quickkeys {
bool radial = true;
bool RadialOpen() { return radial; }
int OpenCount() { return 1; }
int PointedSlot() { return 0; }
void Repoint(int) {}
}
namespace logger { template<class... T> void info(T...) {} }
namespace wheels {
enum class Wheel { kEquipment, kMagic, kAmmo };
std::atomic<int> g_active{0};
Wheel Active() { return static_cast<Wheel>(g_active.load()); }
const char* Name(Wheel) { return "wheel"; }
bool EnsureLoaded() { return true; }
void ShowMagic(bool) {}
''' + function("src/wheels/Wheels.cpp", "void SwitchWheel(int a_dir)") + r'''
}
namespace keyboard {
bool MouseOwnsRadial() { return false; }
bool ScriptRunning() { return false; }
}
namespace radialselection { int LiveStickKey(SHORT, SHORT) { return -1; } }
struct Read {
    XINPUT_GAMEPAD& pad;
    WORD raw, pressed, released;
    bool ltPressed, rtPressed;
    Clock::time_point now;
    WORD& out;
};
int g_stayUsedSlot = 3, g_liveStickKey = -1;
bool g_leftToGame = false, g_closingMagic = false;
struct CloseEpoch { bool Superseded(bool, int, bool) { return false; } } g_closeEpoch;
void AbandonClose(const char*) {}
void SnapToFilled(XINPUT_GAMEPAD&) {}
void RestSnap(XINPUT_GAMEPAD&, Clock::time_point) {}
void WheelButton(Read&, WORD, bool) {}
void RadialChoices(Read&, WORD) {}
''' + function("src/input/Pad.cpp", "void PointRadial(Read& r)") + "\n" + function(
    "src/input/Pad.cpp", "void GameplayRules(Read& r)"
) + r'''
bool check(bool condition, const char* message) {
    if (!condition) std::printf("FAIL: %s\n", message);
    return condition;
}
WORD apply(WORD pressed, WORD raw = 0) {
    XINPUT_GAMEPAD pad{};
    WORD out = raw | pressed | XINPUT_GAMEPAD_Y;
    Read read{pad, static_cast<WORD>(raw | pressed), pressed, 0, false, false, Clock::now(), out};
    GameplayRules(read);
    return out;
}
int main() {
    using wheels::Wheel;
    const WORD thumbs = XINPUT_GAMEPAD_LEFT_THUMB | XINPUT_GAMEPAD_RIGHT_THUMB;
    wheels::g_active = 0;
    apply(XINPUT_GAMEPAD_RIGHT_THUMB);
    if (!check(wheels::Active() == Wheel::kMagic, "R3: Inventory -> Magic")) return 1;
    if (!check(g_stayUsedSlot == -1, "switch clears remembered used slot")) return 1;
    apply(XINPUT_GAMEPAD_RIGHT_THUMB);
    if (!check(wheels::Active() == Wheel::kAmmo, "R3: Magic -> Ammo")) return 1;
    apply(XINPUT_GAMEPAD_LEFT_THUMB);
    if (!check(wheels::Active() == Wheel::kMagic, "L3: Ammo -> Magic")) return 1;
    apply(XINPUT_GAMEPAD_LEFT_THUMB);
    if (!check(wheels::Active() == Wheel::kEquipment, "L3: Magic -> Inventory")) return 1;
    apply(XINPUT_GAMEPAD_LEFT_THUMB);
    if (!check(wheels::Active() == Wheel::kAmmo, "L3 wraps to Ammo")) return 1;
    apply(XINPUT_GAMEPAD_RIGHT_THUMB);
    if (!check(wheels::Active() == Wheel::kEquipment, "R3 wraps to Inventory")) return 1;
    apply(XINPUT_GAMEPAD_DPAD_RIGHT);
    if (!check(wheels::Active() == Wheel::kMagic, "D-pad right still works")) return 1;
    apply(XINPUT_GAMEPAD_DPAD_LEFT);
    if (!check(wheels::Active() == Wheel::kEquipment, "D-pad left still works")) return 1;
    apply(XINPUT_GAMEPAD_DPAD_RIGHT | XINPUT_GAMEPAD_RIGHT_THUMB);
    if (!check(wheels::Active() == Wheel::kMagic, "same-direction simultaneous buttons switch once")) return 1;
    WORD out = apply(0, thumbs);
    if (!check(wheels::Active() == Wheel::kMagic, "held clicks do not repeat")) return 1;
    if (!check(!(out & thumbs) && (out & XINPUT_GAMEPAD_Y), "consume clicks while preserving unrelated buttons")) return 1;
    settings::values.ammoWheel = false;
    apply(XINPUT_GAMEPAD_RIGHT_THUMB);
    if (!check(wheels::Active() == Wheel::kEquipment, "disabled Ammo is skipped")) return 1;
    quickkeys::radial = false;
    out = apply(XINPUT_GAMEPAD_RIGHT_THUMB);
    if (!check(wheels::Active() == Wheel::kEquipment && (out & XINPUT_GAMEPAD_RIGHT_THUMB), "closed wheel leaves normal controls alone")) return 1;
    quickkeys::radial = true;
    g_closingMagic = true;
    apply(XINPUT_GAMEPAD_RIGHT_THUMB);
    if (!check(wheels::Active() == Wheel::kEquipment, "no switching while closing")) return 1;
    std::puts("PASS: gameplay stick cycling, D-pad, wrapping, hold, consumption and closed/closing wheel");
}
'''

with tempfile.TemporaryDirectory(prefix="iwp-controller-cycle-") as folder:
    tmp = Path(folder)
    (tmp / "test.cpp").write_text(program, encoding="utf-8")
    batch = tmp / "build.bat"
    batch.write_text(
        '@echo off\ncall F:\\modbuild\\tools\\msvc\\setup_x64.bat\n'
        'cl /nologo /std:c++latest /EHsc test.cpp /Fe:test.exe\n', encoding="utf-8"
    )
    subprocess.run(["cmd", "/c", str(batch)], check=True, cwd=tmp)
    subprocess.run([str(tmp / "test.exe")], check=True)
