"""IWP-043: while the Ammo shortcut is open the left mouse button is taken off the game's gameplay mappings (it attacked
after the click), and put back exactly when it closes (owner choice 2026-11-20: unbind while open).

Compiles src/input/KeySuspend.h against a fake mapping array; checks the wiring in Ammo.cpp / Controls.cpp.
"""
from pathlib import Path
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]
failures = []

program = r'''
#include "input/KeySuspend.h"
#include <cstdio>
#include <cstring>
struct Entry { std::uint64_t triggers; std::uint64_t key; std::uint64_t details; std::uint64_t action; };   // key at offset 8
int bad = 0;
void check(bool c, const char* m) { if (!c) { std::printf("FAIL: %s\n", m); ++bad; } }
int main() {
    Entry e[4]{ { 11, 0x77, 0xD1, 1 }, { 12, 0x55, 0xD2, 2 }, { 13, 0x77, 0xD3, 3 }, { 14, 0x66, 0xD4, 4 } };
    auto* data = reinterpret_cast<std::uint8_t*>(e);
    const auto saved = keysuspend::Suspend(data, 4, sizeof(Entry), 8, [](const std::uint8_t* a_entry) {
        std::uint64_t n; std::memcpy(&n, a_entry + 8, 8); return n == 0x77; });
    check(saved.size() == 2, "both entries on the key are taken off");
    check(e[0].key == 0 && e[2].key == 0, "their key name is empty (None)");
    check(e[1].key == 0x55 && e[3].key == 0x66, "other keys stay");
    check(e[0].triggers == 11 && e[0].details == 0xD1 && e[0].action == 1, "the rest of the entry is untouched");
    e[2].key = 0x99;   // something else rebound it meanwhile: left alone
    const int back = keysuspend::Restore(data, 4, sizeof(Entry), 8, saved);
    check(back == 1 && e[0].key == 0x77, "the saved key name comes back");
    check(e[2].key == 0x99, "an entry changed meanwhile is not overwritten");
    check(keysuspend::Restore(data, 1, sizeof(Entry), 8, saved) == 0, "an index past a shrunk array is skipped");
    if (bad) return 1;
    std::puts("PASS: key suspend / restore");
}
'''

with tempfile.TemporaryDirectory(prefix="iwp-keysuspend-") as folder:
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
controls_cpp = (root / "src/input/Controls.cpp").read_text(encoding="utf-8")
controls_h = (root / "src/input/Controls.h").read_text(encoding="utf-8")
if "void SuspendGameMouseButton(bool" not in controls_h or "keysuspend::Suspend(" not in controls_cpp:
    failures.append("Controls has SuspendGameMouseButton built on keysuspend")
if ammo_cpp.count("controls::SuspendGameMouseButton(") < 2:
    failures.append("Ammo.cpp suspends the left button on open and restores it on close")

for f in failures:
    print("FAIL:", f)
raise SystemExit(1 if failures else 0)
