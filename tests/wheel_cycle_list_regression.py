"""IMP-IWP-005: [Wheel] sWheelCycle chooses which wheels LB / D-pad / L3 / R3 / Q / E rotate through, and in which
order; the Ammo-only shortcut is one setting, [AmmoWheel] uButton (controller) + sButton (keyboard, F8), and no
key or button opens the main wheel on its Ammo page any more (owner decisions, 2026-11-19).

Compiles the real ParseWheelCycle, Cycle, SwitchWheel, SetActive, RadialShown and DirectWheelButtons with a stubbed
game boundary. The stubs carry every settings field old and new code use, so unchanged code compiles and fails on
behaviour rather than on setup.
"""
from pathlib import Path
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]
failures = []


def function(path, signature):
    source = (root / path).read_text(encoding="utf-8")
    if signature not in source:
        return None
    start = source.index(signature)
    opening = source.index("{", start)
    depth = 1
    end = opening + 1
    while depth:
        depth += (source[end] == "{") - (source[end] == "}")
        end += 1
    return source[start:end]


def build_and_run(name, program):
    with tempfile.TemporaryDirectory(prefix="iwp-cycle-") as folder:
        tmp = Path(folder)
        (tmp / "test.cpp").write_text(program, encoding="utf-8")
        (tmp / "build.bat").write_text(
            '@echo off\ncall F:\\modbuild\\tools\\msvc\\setup_x64.bat >nul\n'
            'cl /nologo /std:c++latest /EHsc test.cpp /Fe:test.exe >build.txt || (type build.txt & exit /b 2)\n',
            encoding="utf-8")
        if subprocess.run(["cmd", "/c", str(tmp / "build.bat")], cwd=tmp).returncode:
            raise SystemExit(f"{name}: compile failed (not a behavioural result)")
        if subprocess.run([str(tmp / "test.exe")]).returncode:
            failures.append(f"{name}: behaviour checks above")


# --- A: the INI list -------------------------------------------------------------------------------------------
parse = function("src/core/Settings.cpp", "int ParseWheelCycle(")
if parse is None:
    failures.append("Settings.cpp has ParseWheelCycle")
else:
    build_and_run("parse", r'''
#include <cstdint>
#include <cstdio>
#include <cwctype>
#include <string>
#include <string_view>
''' + parse + r'''
int bad = 0;
void check(std::wstring_view t, int n, int a = -1, int b = -1, int c = -1) {
    std::uint8_t o[3]{9, 9, 9};
    const int got = ParseWheelCycle(t, o);
    const int want[3]{a, b, c};
    bool ok = got == n;
    for (int i = 0; ok && i < n; ++i) ok = o[i] == want[i];
    if (!ok) { std::printf("FAIL: parse %ls -> %d\n", std::wstring(t).c_str(), got); ++bad; }
}
int main() {
    check(L"Inventory,Magic,Ammo", 3, 0, 1, 2);
    check(L"Magic, Inventory", 2, 1, 0);
    check(L" ammo ,EQUIPMENT, Ammo", 2, 2, 0);
    check(L"Magic", 1, 1);
    check(L"", 0);
    check(L"Spells", 0);
    if (bad) return 1;
    std::puts("PASS: sWheelCycle parsing");
}
''')

# --- B: rotation, reopen and own buttons --------------------------------------------------------------------------
cycle = function("src/wheels/Wheels.cpp", "std::vector<Wheel> Cycle()") or ""
build_and_run("rotation", r'''
#include <Windows.h>
#include <atomic>
#include <cstdint>
#include <cstdio>
#include <vector>
ULONGLONG g_now = 100000;
ULONGLONG Tick() { return g_now; }
#define GetTickCount64 Tick
namespace settings {
struct Values { bool ammoWheel = true; bool ammoInCycle = true; bool rememberWheel = true;
                std::uint8_t cycle[3]{0, 1, 2}; int cycleCount = 3; } values;
Values& Get() { return values; }
}
namespace quickkeys { bool RadialOpen() { return true; } }
namespace logger { template<class... T> void info(T...) {} template<class... T> void warn(T...) {} }
namespace wheels {
enum class Wheel { kEquipment, kMagic, kAmmo };
std::atomic<int> g_active{0};
ULONGLONG g_keepUntil = 0, g_ammoChosenUntil = 0, g_chosenUntil = 0;
int g_chosenWheel = -1;
Wheel Active() { return static_cast<Wheel>(g_active.load()); }
const char* Name(Wheel) { return "wheel"; }
bool EnsureLoaded() { return true; }
void ShowMagic(bool) {}
''' + cycle + "\n" + function("src/wheels/Wheels.cpp", "void SwitchWheel(int a_dir)") + "\n"
    + function("src/wheels/Wheels.cpp", "void KeepActive()") + "\n"
    + function("src/wheels/Wheels.cpp", "void SetActive(Wheel a_wheel)") + "\n"
    + function("src/wheels/Wheels.cpp", "void RadialShown()") + r'''
}
int bad = 0;
void check(bool c, const char* m) { if (!c) { std::printf("FAIL: %s\n", m); ++bad; } }
using wheels::Wheel;
void list(std::initializer_list<int> l) { int i = 0; for (int w : l) settings::values.cycle[i++] = static_cast<std::uint8_t>(w); settings::values.cycleCount = i; }
void at(Wheel w) { wheels::g_active = static_cast<int>(w); }
void plainOpen() { g_now += 5000; wheels::RadialShown(); }
int main() {
    using namespace wheels;
    auto& s = settings::values;
    at(Wheel::kEquipment); SwitchWheel(+1); SwitchWheel(+1);
    check(Active() == Wheel::kAmmo, "default list: Inventory -> Magic -> Ammo");
    SwitchWheel(+1);
    check(Active() == Wheel::kEquipment, "default list wraps to Inventory");

    list({0, 1});
    at(Wheel::kEquipment); SwitchWheel(+1); SwitchWheel(+1);
    check(Active() == Wheel::kEquipment, "Inventory,Magic: Ammo skipped");
    at(Wheel::kAmmo); SwitchWheel(+1);
    check(Active() == Wheel::kEquipment, "outside the list: right -> first");
    at(Wheel::kAmmo); SwitchWheel(-1);
    check(Active() == Wheel::kMagic, "outside the list: left -> last");

    list({1, 2});   // Inventory out
    at(Wheel::kMagic); SwitchWheel(+1);
    check(Active() == Wheel::kAmmo, "Magic,Ammo: Magic -> Ammo");
    SwitchWheel(+1);
    check(Active() == Wheel::kMagic, "Magic,Ammo: Ammo -> Magic (Inventory skipped)");
    at(Wheel::kEquipment); plainOpen();
    check(Active() == Wheel::kMagic, "Magic,Ammo: a plain reopen left on Inventory lands on Magic");
    at(Wheel::kEquipment); g_now += 5000; KeepActive(); RadialShown();
    check(Active() == Wheel::kMagic, "Magic,Ammo: keyboard R reopen lands on Magic");
    at(Wheel::kMagic); g_now += 5000; SetActive(Wheel::kEquipment); RadialShown();
    check(Active() == Wheel::kEquipment, "Magic,Ammo: Inventory's own button (F6) still opens Inventory");

    list({2, 1});   // order: Ammo first
    at(Wheel::kAmmo); SwitchWheel(+1);
    check(Active() == Wheel::kMagic, "Ammo,Magic order: Ammo -> Magic");
    s.rememberWheel = false;
    at(Wheel::kMagic); plainOpen();
    check(Active() == Wheel::kAmmo, "bRememberWheel=0: a plain open starts on the list's first wheel");
    s.rememberWheel = true;

    s.ammoWheel = false; list({2});
    at(Wheel::kAmmo); plainOpen();
    check(Active() == Wheel::kEquipment, "list of only a disabled Ammo falls back to Inventory");
    SwitchWheel(+1);
    check(Active() == Wheel::kEquipment, "a one-wheel rotation stays put");
    if (bad) return 1;
    std::puts("PASS: sWheelCycle rotation, order, reopen and own buttons");
}
''')

# --- C: the old [Wheel] uAmmoWheelButton no longer opens the main wheel on Ammo ---------------------------------------
build_and_run("direct buttons", r'''
#include <Windows.h>
#include <Xinput.h>
#include <array>
#include <cstdio>
#include <utility>
namespace settings {
struct Values { int wheelButton = 256, equipmentWheelButton = 0, magicWheelButton = 0, ammoWheelButton = 0; bool ammoWheel = true; } values;
Values& Get() { return values; }
}
namespace menus { enum class Menu { kNone, kInventory }; Menu Active() { return Menu::kNone; } bool AnyOpen() { return false; } }
namespace quickkeys { bool RadialOpen() { return false; } }
namespace wheels { enum class Wheel { kEquipment, kMagic, kAmmo }; int setTo = -1;
    Wheel Active() { return Wheel::kEquipment; } void SetActive(Wheel w) { setTo = static_cast<int>(w); } }
int g_stayUsedSlot = 0;
''' + function("src/input/Pad.cpp", "void DirectWheelButtons(XINPUT_GAMEPAD& a_pad)") + r'''
int bad = 0;
void check(bool c, const char* m) { if (!c) { std::printf("FAIL: %s\n", m); ++bad; } }
WORD press(WORD b) { XINPUT_GAMEPAD p{}; p.wButtons = b; DirectWheelButtons(p); return p.wButtons; }
int main() {
    settings::values.ammoWheelButton = XINPUT_GAMEPAD_DPAD_RIGHT;
    WORD out = press(XINPUT_GAMEPAD_DPAD_RIGHT);
    check(wheels::setTo != 2, "the old uAmmoWheelButton does not make the main wheel's Ammo page active");
    check(!(out & 256), "the old uAmmoWheelButton does not hold the main wheel button");
    press(0);
    settings::values.equipmentWheelButton = XINPUT_GAMEPAD_DPAD_UP;
    out = press(XINPUT_GAMEPAD_DPAD_UP);
    check(wheels::setTo == 0 && (out & 256) && !(out & XINPUT_GAMEPAD_DPAD_UP), "Inventory's own button still opens the main wheel");
    if (bad) return 1;
    std::puts("PASS: direct wheel buttons");
}
''')

# --- D: the keyboard F8 path, the shortcut's request, and the INI ------------------------------------------------
keyboard = (root / "src/input/Keyboard.cpp").read_text(encoding="utf-8")
ammo_cpp = (root / "src/ammo/Ammo.cpp").read_text(encoding="utf-8")
settings_cpp = (root / "src/core/Settings.cpp").read_text(encoding="utf-8")
ini = (root / "dist/OblivionRemastered/Binaries/Win64/OBSE/Plugins/ImprovedWheelMenu.ini").read_text(encoding="utf-8")
if "ammo::RequestToggle()" not in keyboard:
    failures.append("Keyboard.cpp: sAmmoWheel (F8) asks the Ammo-only shortcut")
if "void RequestToggle()" not in ammo_cpp:
    failures.append("Ammo.cpp: RequestToggle opens / equips like uButton")
if 'L"sWheelCycle"' not in settings_cpp or 'L"bInWheelCycle"' in settings_cpp:
    failures.append("Settings.cpp reads [Wheel] sWheelCycle (and no longer bInWheelCycle)")
if 'L"AmmoWheel", L"sButton"' not in settings_cpp:
    failures.append("Settings.cpp reads [AmmoWheel] sButton (the shortcut's keyboard key)")
section = lambda name, nxt: ini[ini.index(f"\n[{name}]\n"):ini.index(f"\n[{nxt}]\n")]
if "\nsWheelCycle=Inventory,Magic,Ammo" not in section("Wheel", "Controller") or "bInWheelCycle" in ini:
    failures.append("shipped INI: sWheelCycle=Inventory,Magic,Ammo in [Wheel], no bInWheelCycle")
if "\nuAmmoWheelButton=" in ini or "\nsAmmoWheel=" in ini:
    failures.append("shipped INI: no uAmmoWheelButton / sAmmoWheel lines (merged into [AmmoWheel])")

# IWP-042: the keyboard shortcuts sit on keys the game binds nowhere (V, B, J), never on F6 / F7 / F8 (game menu keys)
settings_h = (root / "src/core/Settings.h").read_text(encoding="utf-8")
if "equipmentWheel = 'V', magicWheel = 'B'" not in settings_h or "ammoWheel = 'J'" not in settings_h:
    failures.append("Settings.h: keyboard defaults V (Inventory wheel), B (Magic wheel), J (Ammo shortcut)")
if "\nsEquipmentWheel=V" not in ini or "\nsMagicWheel=B" not in ini or "\nsButton=J" not in section("AmmoWheel", "Keyboard"):
    failures.append("shipped INI: sEquipmentWheel=V, sMagicWheel=B, [AmmoWheel] sButton=J")
if 'L"sAmmoWheel"' in settings_cpp:
    failures.append("Settings.cpp no longer reads [Keyboard] sAmmoWheel (an old F8 there opened the Options menu)")

for f in failures:
    print("FAIL:", f)
raise SystemExit(1 if failures else 0)
