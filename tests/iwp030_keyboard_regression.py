"""Exercise the actual select-key block from Keyboard.cpp with stubbed game input."""
import pathlib
import subprocess
import tempfile

from source_paths import KEYBOARD

root = pathlib.Path(__file__).resolve().parents[1]
source = KEYBOARD.read_text(encoding="utf-8")
end = source.index("\t\t// the right stick: four keys")
start = source.rfind("\t\tif (menus::AnyOpen() && !radial) press(k.select, XINPUT_GAMEPAD_BACK);", 0, end)
if start < 0:
    start = source.rfind("\t\t// IWP-030 select-key handling", 0, end)
assert start >= 0
block = source[start:end]
program = r"""
#include <cstdio>
constexpr int XINPUT_GAMEPAD_BACK = 32;
namespace menus {
enum class Menu { kNone, kInventory, kMagic };
Menu current = Menu::kInventory;
bool open = true;
bool AnyOpen() { return open; }
}
namespace settings { struct Values { int favouriteButton = 32; }; Values values; Values& Get() { return values; } }
namespace logger { void info(const char*) {} }
namespace rows { unsigned item = 7; unsigned HighlightedPlayerItemInContainer() { return item; } }
namespace wheels {
int favourites = 0, items = 0;
void Favourite(menus::Menu) { ++favourites; }
void ToggleItem(unsigned) { ++items; }
}
struct Key { int select = 'F'; } k;
struct Pad { int wButtons = 0; } a_pad;
bool radial = false, down = false, typing = false, any = false;
bool Down(int) { return down; }
bool Printable(int) { return true; }
bool Typing() { return typing; }
void press(int, int button) { if (down && !typing) { a_pad.wButtons |= button; any = true; } }
void tick() {
auto context = menus::current;
""" + block + r"""
}
int main() {
down = true; tick();
if (a_pad.wButtons || wheels::favourites != 1) {
  std::printf("FAIL: F must favourite once without injecting controller input (buttons=%d favourites=%d)\n",
              a_pad.wButtons, wheels::favourites); return 1;
}
tick();
if (wheels::favourites != 1) { std::puts("FAIL: held key repeats"); return 1; }
down = false; tick(); down = true; tick();
if (wheels::favourites != 2) { std::puts("FAIL: second press"); return 1; }
down = false; tick(); typing = true; down = true; tick();
if (wheels::favourites != 2 || a_pad.wButtons) { std::puts("FAIL: typing"); return 1; }
down = false; tick(); typing = false; menus::current = menus::Menu::kMagic; down = true; tick();
if (wheels::favourites != 3 || a_pad.wButtons) { std::puts("FAIL: magic"); return 1; }
down = false; tick(); menus::current = menus::Menu::kNone; down = true; tick();
if (wheels::items != 1 || a_pad.wButtons) { std::puts("FAIL: own container item"); return 1; }
down = false; tick(); rows::item = 0; down = true; tick();
if (a_pad.wButtons != XINPUT_GAMEPAD_BACK) { std::puts("FAIL: other menu back fallback"); return 1; }
down = false; tick(); a_pad.wButtons = XINPUT_GAMEPAD_BACK; menus::current = menus::Menu::kInventory; down = true; tick();
if (a_pad.wButtons != XINPUT_GAMEPAD_BACK || wheels::favourites != 4) { std::puts("FAIL: preserve physical pad input"); return 1; }
down = false; tick(); a_pad.wButtons = 0; settings::values.favouriteButton = 0; down = true; tick();
if (a_pad.wButtons != XINPUT_GAMEPAD_BACK || wheels::favourites != 4) { std::puts("FAIL: customized controller binding"); return 1; }
std::puts("PASS: keyboard favourite is edge-triggered without virtual controller input");
}
"""
with tempfile.TemporaryDirectory() as tmp:
    tmp = pathlib.Path(tmp)
    cpp = tmp / "iwp030.cpp"
    cpp.write_text(program, encoding="utf-8")
    setup = r"F:\modbuild\tools\msvc\setup_x64.bat"
    command = f'call "{setup}" >nul && cl /nologo /EHsc /std:c++17 "{cpp}" /Fe:"{tmp / "iwp030.exe"}" /Fo:"{tmp / "iwp030.obj"}"'
    batch = tmp / "build.bat"
    batch.write_text("@echo off\n" + command + "\n", encoding="utf-8")
    subprocess.run(["cmd", "/c", str(batch)], check=True, cwd=tmp)
    subprocess.run([str(tmp / "iwp030.exe")], check=True)
