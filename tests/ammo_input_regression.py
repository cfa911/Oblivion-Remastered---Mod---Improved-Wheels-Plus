"""IWP-043 / IWP-044: the open Ammo shortcut's input rules (src/ammo/AmmoInput.h) and their wiring.

- RB equips the pointed arrows like A and the game never sees it (IWP-044: RB cast a spell).
- The wheel starts on the worn arrows, else on the first arrow (owner choice 2026-11-20), so J / Enter always equip.
- Mouse movement aims like the right stick (owner choice 2026-11-20; the game shows no cursor in gameplay).
- The shortcut does not open while the wheel file is still loading (it opened empty right after a load).
"""
from pathlib import Path
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]
failures = []

program = r'''
#include "ammo/AmmoInput.h"
#include <cmath>
#include <cstdio>
using namespace ammoinput;
int bad = 0;
void check(bool c, const char* m) { if (!c) { std::printf("FAIL: %s\n", m); ++bad; } }
int main() {
    const std::uint16_t button = 0x0004;   // D-pad left as the shortcut's button
    check(ActionFor(kB, button, false) == Action::kCancel, "B cancels");
    check(ActionFor(kA, button, false) == Action::kEquip, "A equips");
    check(ActionFor(button, button, false) == Action::kEquip, "the shortcut's button equips");
    check(ActionFor(0, button, true) == Action::kEquip, "the keyboard key equips");
    check(ActionFor(0, button, false) == Action::kNone, "nothing pressed does nothing");
    check(ActionFor(kRB, button, false) == Action::kEquip, "RB equips like A (IWP-044)");
    check((TakenWhileOpen() & kRB) != 0, "the game never sees RB while the shortcut is open (IWP-044)");
    check((TakenWhileOpen() & (kDpad | kA | kB)) == (kDpad | kA | kB), "D-pad, A and B stay the wheel's");

    const std::uint32_t ids[8]{ 0x10, 0x20, 0x30, 0, 0, 0, 0, 0 };
    check(StartPointed(ids, 8, 0x20) == 1, "starts on the worn arrows");
    check(StartPointed(ids, 8, 0x99) == 0, "worn arrows not on the wheel: starts on the first arrow");
    check(StartPointed(ids, 8, 0) == 0, "no arrows worn: starts on the first arrow");
    const std::uint32_t none[8]{};
    check(StartPointed(none, 8, 0x20) == -1, "an empty wheel points at nothing");

    MouseAim aim;
    double x = 0, y = 0;
    check(!aim.Feed(2, 1, x, y), "a small mouse move does not aim yet");
    check(aim.Feed(30, 0, x, y) && x > 0.9 && std::fabs(y) < 0.2, "moving right aims right");
    bool up = false;
    for (int i = 0; i < 6; ++i) up = aim.Feed(0, 25, x, y);
    check(up && y > 0.9, "moving up afterwards turns the aim up (the pull is bounded)");
    check(std::hypot(aim.x, aim.y) <= 60.0, "the gathered movement stays bounded");
    aim.Reset();
    check(!aim.Feed(1, 1, x, y), "a reset starts from rest");
    if (bad) return 1;
    std::puts("PASS: Ammo shortcut input rules");
}
'''

with tempfile.TemporaryDirectory(prefix="iwp-ammo-input-") as folder:
    tmp = Path(folder)
    (tmp / "test.cpp").write_text(program, encoding="utf-8")
    (tmp / "build.bat").write_text(
        '@echo off\ncall F:\\modbuild\\tools\\msvc\\setup_x64.bat >nul\n'
        f'cl /nologo /std:c++latest /EHsc /I "{root / "src"}" test.cpp /Fe:test.exe >build.txt || (type build.txt & exit /b 2)\n',
        encoding="utf-8")
    if subprocess.run(["cmd", "/c", str(tmp / "build.bat")], cwd=tmp).returncode:
        raise SystemExit("compile failed (not a behavioural result)")
    if subprocess.run([str(tmp / "test.exe")]).returncode:
        failures.append("behaviour checks above")

ammo_cpp = (root / "src/ammo/Ammo.cpp").read_text(encoding="utf-8")
keyboard = (root / "src/input/Keyboard.cpp").read_text(encoding="utf-8")
wheels_h = (root / "src/wheels/Wheels.h").read_text(encoding="utf-8")
if "bool Loaded()" not in wheels_h or "wheels::Loaded()" not in ammo_cpp:
    failures.append("the shortcut waits for the wheel file (wheels::Loaded) before it opens")
if "ammo::MouseMove(" not in keyboard or "GetInputMouseDelta" not in keyboard:
    failures.append("Keyboard.cpp feeds the mouse movement (GetInputMouseDelta) to ammo::MouseMove while the shortcut is open")
if "void MouseMove(" not in ammo_cpp:
    failures.append("Ammo.cpp aims with the mouse movement (MouseMove)")

for f in failures:
    print("FAIL:", f)
raise SystemExit(1 if failures else 0)
