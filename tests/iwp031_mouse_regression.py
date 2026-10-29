"""Exercise the real menu/radial click block with stubbed game functions."""
import pathlib
import subprocess
import tempfile

from source_paths import KEYBOARD

root = pathlib.Path(__file__).resolve().parents[1]
source = KEYBOARD.read_text(encoding="utf-8")
start = source.index("\t\t{   // the left mouse button on a cell")
end = source.index("\t\tif (ammo::Open())", start)
block = source[start:end]
program = r"""
#include <cstdio>
constexpr int VK_LBUTTON = 1;
namespace menus {
enum class Menu { kNone, kInventory, kMagic };
Menu current = Menu::kInventory;
Menu Active() { return current; }
}
namespace quickkeys {
bool message = false, radial = false;
int pointed = 3;
bool RadialOpen() { return radial; }
bool GameMessageUp() { return message; }
int PointedSlot() { return pointed; }
}
namespace wheels {
int calls = 0, slot = -1;
menus::Menu menu;
void AssignHighlighted(menus::Menu m, int s) { ++calls; menu = m; slot = s; }
}
namespace logger { template<class... T> void info(const char*, T...) {} }
struct Script { enum Kind { kNone, kAssign, kClick } kind = kNone; int slot; int phase; bool opened; unsigned long long at; } g_script{};
bool down = false, panel = true, search = false;
bool Down(int) { return down; }
bool PanelUp(menus::Menu) { return panel; }
bool SearchLightBarUnderMouse() { return search; }
unsigned long long GetTickCount64() { return 10; }
bool CursorUnits(double& x, double& y) { x = y = 0; return true; }   // off the cells: the selected slot is used
int CellAt(double, double) { return -1; }
void tick() {
""" + block + r"""
}
int main() {
down = true; tick();
if (wheels::calls != 1 || wheels::slot != 2 || g_script.kind != Script::kNone) {
 std::printf("FAIL: Inventory click must assign directly without virtual A (calls=%d script=%d)\n", wheels::calls, g_script.kind); return 1;
}
tick(); if (wheels::calls != 1) return 2;
down = false; tick(); menus::current = menus::Menu::kMagic; down = true; tick();
if (wheels::calls != 2 || wheels::menu != menus::Menu::kMagic) return 3;
down = false; tick(); quickkeys::message = true; down = true; tick();
if (wheels::calls != 2) return 4;
down = false; tick(); quickkeys::message = false; search = true; down = true; tick();
if (wheels::calls != 2) return 5;
down = false; tick(); search = false; menus::current = menus::Menu::kNone; quickkeys::radial = true; down = true; tick();
if (g_script.kind != Script::kClick || wheels::calls != 2) return 6;
std::puts("PASS: menu clicks assign directly; gameplay radial and guards preserved");
}
"""
with tempfile.TemporaryDirectory() as tmp:
    tmp = pathlib.Path(tmp)
    cpp = tmp / "iwp031.cpp"
    cpp.write_text(program, encoding="utf-8")
    batch = tmp / "build.bat"
    batch.write_text('@echo off\ncall F:\\modbuild\\tools\\msvc\\setup_x64.bat >nul && '
                     f'cl /nologo /EHsc /std:c++17 "{cpp}" /Fe:"{tmp / "iwp031.exe"}" /Fo:"{tmp / "iwp031.obj"}"\n',
                     encoding="utf-8")
    subprocess.run(["cmd", "/c", str(batch)], check=True, cwd=tmp)
    subprocess.run([str(tmp / "iwp031.exe")], check=True)
