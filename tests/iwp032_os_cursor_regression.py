"""IWP-032: with the assign panel open the game's GetMousePosition stays frozen while the Windows cursor moves
(synchronized log, 2026-10-28). Hover and click on the panel must follow the Windows cursor.
Runs the production hover and click blocks of src/input/Keyboard.cpp with stubbed game calls."""
import pathlib
import subprocess
import tempfile

from source_paths import KEYBOARD

root = pathlib.Path(__file__).resolve().parents[1]
source = KEYBOARD.read_text(encoding="utf-8")


def cut(start, end):
    i = source.index(start)
    return source[i:source.index(end, i) + len(end)]


hover = cut("\t\t// IWP-032 panel hover start", "\t\t// IWP-032 panel hover end")
click = cut("\t\t{   // the left mouse button on a cell of the open assign panel", "\t\t\ts_lmb = now;\n\t\t}")

program = r"""
#include <cmath>
#include <cstdio>
#include <string>
using ULONGLONG = unsigned long long;
#define VK_LBUTTON 1
ULONGLONG tick_ = 1000;
ULONGLONG GetTickCount64() { return tick_; }
namespace logger { template<class... T> void info(const char*, T...) {} template<class... T> void warn(const char*, T...) {} }
namespace menus { enum class Menu { kNone, kInventory, kMagic }; Menu Active() { return Menu::kInventory; } }
using WORD = unsigned short;
using SHORT = short;
struct POINT { long x, y; };
struct XINPUT_GAMEPAD { WORD wButtons; unsigned char bLeftTrigger, bRightTrigger; SHORT sThumbLX, sThumbLY, sThumbRX, sThumbRY; };
#define XINPUT_GAMEPAD_DPAD_UP 0x0001
#define XINPUT_GAMEPAD_DPAD_DOWN 0x0002
#define XINPUT_GAMEPAD_DPAD_LEFT 0x0004
#define XINPUT_GAMEPAD_DPAD_RIGHT 0x0008
#define XINPUT_GAMEPAD_LEFT_SHOULDER 0x0100
#define XINPUT_GAMEPAD_RIGHT_SHOULDER 0x0200
#define XINPUT_GAMEPAD_LEFT_THUMB_DEADZONE 7849
#define XINPUT_GAMEPAD_RIGHT_THUMB_DEADZONE 8689
XINPUT_GAMEPAD a_pad{};
bool panel = true, msg = false, lmb = false;
double gameX = 0, gameY = 0;   // frozen, as measured
double osX = 0, osY = 0;       // follows the visible cursor
int selected = 0, repoints = 0, assigned = -1;
// cell 1 sits at x=100, cell 3 at x=300; anything else misses
int CellAt(double x, double, std::string* = nullptr) { return x == 100 ? 0 : x == 300 ? 2 : -1; }
bool MouseUnits(double& x, double& y) { x = gameX; y = gameY; return true; }
bool CursorUnits(double& x, double& y) { x = osX; y = osY; return true; }
bool GetCursorPos(POINT* p) { p->x = (long)osX; p->y = (long)osY; return true; }
int CellUnderMouse(std::string* a = nullptr) { return CellAt(gameX, gameY, a); }
bool SearchLightBarUnderMouse() { return false; }
bool Down(int) { return lmb; }
bool PanelUp(menus::Menu) { return panel; }
struct Script { enum Kind { kNone, kClick } kind; int a; int b; bool c; ULONGLONG t; } g_script{ Script::kNone, 0, 0, false, 0 };
namespace quickkeys {
int PointedSlot() { return selected + 1; }
bool GameMessageUp() { return msg; }
bool RadialOpen() { return false; }
void Repoint(int n) { selected = n; ++repoints; }
}
namespace wheels { void AssignHighlighted(menus::Menu, int n) { assigned = n; } }
void hover() {
""" + hover + r"""
}
void click() {
""" + click + r"""
}
void step(void (*f)()) { tick_ += 60; f(); }
int main() {
osX = 50; step(hover);                       // the panel opens with the cursor somewhere
if (selected != 0) { std::puts("FAIL: opening the panel must not move the selection"); return 8; }
osX = 300; step(hover);
if (selected != 2) { std::puts("FAIL: hovering cell 3 must select it (the game mouse position is frozen)"); return 1; }
if (assigned != -1) { std::puts("FAIL: hover must not assign"); return 2; }
selected = 7; step(hover);                    // the game re-points slot 8 from its frozen mouse angle (owner recording)
if (selected != 2) { std::puts("FAIL: a resting cursor on cell 3 must win over the game's snap back to slot 8"); return 9; }
a_pad.sThumbRX = 20000; selected = 5; step(hover); a_pad.sThumbRX = 0;   // the controller stick moves the selection
step(hover);
if (selected != 5) { std::puts("FAIL: a stationary cursor must not take the selection back from the controller"); return 3; }
a_pad.wButtons = XINPUT_GAMEPAD_RIGHT_SHOULDER; selected = 6; step(hover); a_pad.wButtons = 0; step(hover);
if (selected != 6) { std::puts("FAIL: shoulder (or Q/E) stepping must keep its selection"); return 10; }
osX = 100; step(hover);
if (selected != 0) { std::puts("FAIL: moving to cell 1 must select it"); return 4; }
msg = true; osX = 300; step(hover);
if (selected != 0) { std::puts("FAIL: a game message must block hover"); return 5; }
msg = false; osX = 100; step(hover);
panel = false; step(hover); panel = true; selected = 3; step(hover);
if (selected != 3) { std::puts("FAIL: reopening the panel must not restore an old hover"); return 11; }
selected = 0; osX = 300; lmb = true; step(click); lmb = false; step(click);
if (assigned != 2) { std::printf("FAIL: clicking cell 3 assigned slot %d, not 3\n", assigned + 1); return 6; }
assigned = -1; selected = 4; osX = 999; lmb = true; step(click); lmb = false; step(click);
if (assigned != 4) { std::puts("FAIL: a click off the cells keeps assigning the selected slot"); return 7; }
std::puts("PASS: panel hover and click follow the Windows cursor; controller selection is kept");
}
"""
with tempfile.TemporaryDirectory() as tmp:
    tmp = pathlib.Path(tmp)
    cpp = tmp / "iwp032os.cpp"
    cpp.write_text(program, encoding="utf-8")
    batch = tmp / "build.bat"
    batch.write_text('@echo off\ncall F:\\modbuild\\tools\\msvc\\setup_x64.bat >nul && '
                     f'cl /nologo /EHsc /std:c++20 "{cpp}" /Fe:"{tmp / "t.exe"}" /Fo:"{tmp / "t.obj"}" >nul\n',
                     encoding="utf-8")
    subprocess.run(["cmd", "/c", str(batch)], check=True, cwd=tmp)
    raise SystemExit(subprocess.run([str(tmp / "t.exe")]).returncode)
