"""IWP-041: assigning by click lets the game assign and watches the items' quick keys. For an item held only through the base
record (no inventory entry), the game keys only its own table, so nothing arrived and the click failed until a number key had made
the entry. Such an item takes the data path the number keys use (which makes the entry). Runs Wheels.cpp's real AssignPressed.
"""
import pathlib
import re
import subprocess
import tempfile

ROOT = pathlib.Path(__file__).resolve().parents[1]
source = (ROOT / "src/wheels/Wheels.cpp").read_text(encoding="utf-8")
match = re.search(r"\n\tvoid AssignPressed\(menus::Menu a_menu, int a_slot\)\s*\{", source)
assert match
pos = source.index("{", match.start())
depth, end = 1, pos + 1
while depth:
    depth += (source[end] == "{") - (source[end] == "}")
    end += 1
assign_pressed = source[match.start():end]

program = r'''
#include <array>
#include <cstdint>
#include <cstdio>
#include <set>
#include <string>
namespace logger { template <class... A> void info(const char*, A&&...) {} }
namespace menus { enum class Menu { kNone, kInventory, kMagic }; }
namespace inventory {
constexpr int kSlots = 8;
using KeyMap = std::array<std::uint32_t, kSlots>;
std::set<std::uint32_t> entries;   // items that have an inventory entry
KeyMap Keys() { return {}; }
void ClearKey(std::uint32_t) {}
std::string NameOf(std::uint32_t) { return "item"; }
bool HasEntry(std::uint32_t a_id) { return entries.count(a_id) > 0; }
}
namespace rows { std::uint32_t highlighted = 0; std::uint32_t HighlightedItem() { return highlighted; } std::uint32_t HighlightedSpell() { return 0; } }
namespace wheels {
enum class Wheel { kEquipment, kMagic, kAmmo };
namespace detail {
constexpr int kEquip = 0, kMagic = 1;
struct Slot { std::array<std::uint32_t, 1> entries{}; };
struct Pending { bool on = false; int slot = -1; inventory::KeyMap before{}; std::uint32_t parked = 0; };
Pending g_pending;
std::array<std::array<Slot, 8>, 3> g_wheels;
int dataAssigns = 0;
bool EnsureLoaded() { return true; }
void SettleNothing() {}
void Save() {}
int IndexOf(const Slot&, std::uint32_t) { return -1; }
void RemoveAt(int, int, int, const char*) {}
void RemoveFromOtherSlots(int, int, std::uint32_t) {}
void AddTo(int, int, std::uint32_t, const char*) {}
Wheel AssignmentWheel(menus::Menu) { return Wheel::kEquipment; }
void AssignHighlighted(menus::Menu, int) { ++dataAssigns; }
''' + assign_pressed + r'''
}
}
int main() {
    using namespace wheels::detail;
    int failures = 0;
    auto check = [&](bool ok, const char* what) { std::printf("%s: %s\n", ok ? "PASS" : "FAIL", what); failures += !ok; };
    rows::highlighted = 0x27319;   // starting shirt: held only through the base record, no entry
    AssignPressed(menus::Menu::kInventory, 2);
    check(dataAssigns == 1 && !g_pending.on, "a click on an item without an inventory entry takes the number keys' data path");
    inventory::entries.insert(0x229B3); rows::highlighted = 0x229B3;   // an ordinary item
    dataAssigns = 0; g_pending = {};
    AssignPressed(menus::Menu::kInventory, 3);
    check(dataAssigns == 0 && g_pending.on && g_pending.slot == 3, "a click on an ordinary item still lets the game assign");
    return failures ? 1 : 0;
}
'''

with tempfile.TemporaryDirectory(prefix="iwp041c-") as folder:
    tmp = pathlib.Path(folder)
    (tmp / "click.cpp").write_text(program, encoding="utf-8")
    (tmp / "build.bat").write_text(
        '@echo off\ncall "F:\\modbuild\\tools\\msvc\\setup_x64.bat" >nul\n'
        f'cl /nologo /EHsc /std:c++20 "{tmp / "click.cpp"}" /Fe:"{tmp / "click.exe"}" /Fo:"{tmp / "click.obj"}"\n', encoding="utf-8")
    subprocess.run(["cmd", "/c", str(tmp / "build.bat")], check=True, cwd=tmp)
    subprocess.run([str(tmp / "click.exe")], check=True)
