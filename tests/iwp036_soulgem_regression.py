"""Execute real item classification and use routing with a stubbed game boundary."""
import pathlib
import subprocess
import tempfile

root = pathlib.Path(__file__).resolve().parents[1]
source = (root / "src/wheels/Use.cpp").read_text(encoding="utf-8")


def function(signature):
    start = source.index(signature)
    opening = source.index("{", start)
    depth = 1
    end = opening + 1
    while depth:
        depth += (source[end] == "{") - (source[end] == "}")
        end += 1
    return source[start:end]


program = r'''
#include <array>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <string>
using namespace std::chrono_literals;
namespace RE {
enum class FormType { Misc, SoulGem, Weapon, Armor, Clothing, Light, AlchemyItem, Ingredient, Apparatus };
struct TESForm {
    FormType type;
    FormType GetFormType() { return type; }
    static TESForm* LookupByID(unsigned id) {
        static TESForm gem{FormType::SoulGem}, weapon{FormType::Weapon}, potion{FormType::AlchemyItem}, misc{FormType::Misc}, apparatus{FormType::Apparatus};
        if (id == 500) return &apparatus;
        if (id == 100 || id == 101) return &gem;
        if (id == 200) return &weapon;
        if (id == 300) return &potion;
        if (id == 12 || id == 400) return &misc;
        return nullptr;
    }
};
}
namespace logger { template<class... T> void info(T&&...) {} }
namespace menus { bool open = false; bool AnyOpen() { return open; } }
namespace settings { struct Values { bool ammoWheel = true, stayOpenAfterUse = true; }; Values& Get() { static Values v; return v; } }
int equipped = 0, toggled = 0, unequipped = 0, inventoryStarts = 0, quickPresses = 0;
unsigned startedId = 0;
namespace inventory {
constexpr int kSlots = 8;
struct State { bool worn = false, carried = true; bool operator!=(const State&) const { return false; } };
std::array<unsigned, 8> keys{};
auto Keys() { return keys; }
bool Has(unsigned) { return true; }
State StateOf(unsigned) { return {}; }
std::string NameOf(unsigned) { return "item"; }
std::string Describe(State) { return "state"; }
void EquipKeyed(unsigned) { ++equipped; }
void ToggleKeyed(unsigned) { ++toggled; }
void UnequipKeyed(unsigned) { ++unequipped; }
}
namespace quickkeys {
bool radial = false;
bool RadialOpen() { return radial; }
bool GameMessageUp() { return false; }
bool PressQuickKey(int) { ++quickPresses; return true; }
}
enum class Wheel { kEquipment, kMagic, kAmmo };
Wheel Active() { return Wheel::kEquipment; }
bool EnsureLoaded() { return true; }
void UseMagic(int) {}
void UseAmmo(int) {}
struct Slot { int active = 0; std::array<unsigned, 1> entries{}; };
constexpr int kEquip = 0;
Slot g_wheels[1][8];
struct PendingUse {
    bool on = false, direct = false, waitClose = false, closed = false, pressed = false;
    int key = 0; unsigned id = 0; inventory::State before;
    const char* how = "test";
    std::chrono::steady_clock::time_point armed{}, closedAt{}, verifyAt{};
} g_use;
bool HammerInvStart(unsigned id) { ++inventoryStarts; startedId = id; return true; }
constexpr unsigned kRepairHammer = 12;
'''
for signature in ("bool Equippable(", "bool Consumable(", "bool MiscTool(", "void DriveUse(", "void QuickUse(", "void UseNow(", "bool StaysOpenAfterUse("):
    program += "\n" + function(signature) + "\n"
program += r'''
int main() {
    for (unsigned id : {100u, 101u, 12u}) {
        equipped = toggled = unequipped = inventoryStarts = quickPresses = 0;
        inventory::keys[0] = id;
        g_wheels[0][0].entries[0] = id;
        QuickUse(0);
        if (inventoryStarts != 1 || startedId != id || equipped || toggled || quickPresses) {
            std::printf("FAIL: item %u shortcut must open Inventory before use\n", id); return 1;
        }
        inventoryStarts = 0;
        UseNow(0, "test", false);
        if (!g_use.direct || !g_use.waitClose || inventoryStarts || equipped) {
            std::printf("FAIL: item %u wheel must defer use until radial closes\n", id); return 1;
        }
        quickkeys::radial = true; DriveUse();
        if (inventoryStarts || equipped) { std::puts("FAIL: use before radial closed"); return 1; }
        quickkeys::radial = false;
        g_use.closed = true;
        g_use.closedAt = std::chrono::steady_clock::now() - 200ms;
        DriveUse();
        if (inventoryStarts != 1 || startedId != id || equipped || quickPresses) {
            std::printf("FAIL: item %u wheel must open Inventory instead of direct equip/quick key\n", id); return 1;
        }
        if (StaysOpenAfterUse(0)) { std::puts("FAIL: submenu item must close radial"); return 1; }
    }
    for (unsigned id : {0u, 200u, 300u, 400u, 999u}) {
        if (MiscTool(id)) { std::puts("FAIL: unrelated item incorrectly uses submenu workaround"); return 1; }
    }
    inventoryStarts = equipped = toggled = 0;
    g_wheels[0][0].entries[0] = 200;
    QuickUse(0);
    g_wheels[0][0].entries[0] = 300;
    QuickUse(0);
    if (inventoryStarts || toggled != 1 || equipped != 1) { std::puts("FAIL: equipment/potion shortcut changed"); return 1; }
    std::puts("PASS: Soul Gems and hammers defer through Inventory on wheel/shortcut; ordinary items unchanged");
}
'''
with tempfile.TemporaryDirectory() as tmp:
    tmp = pathlib.Path(tmp)
    cpp = tmp / "iwp036.cpp"
    cpp.write_text(program, encoding="utf-8")
    batch = tmp / "build.bat"
    batch.write_text('@echo off\ncall F:\\modbuild\\tools\\msvc\\setup_x64.bat >nul\n'
                     f'cl /nologo /EHsc /std:c++17 "{cpp}" /Fe:"{tmp / "test.exe"}" /Fo:"{tmp / "test.obj"}"\n', encoding="utf-8")
    subprocess.run(["cmd", "/c", str(batch)], check=True, cwd=tmp)
    subprocess.run([str(tmp / "test.exe")], check=True)
