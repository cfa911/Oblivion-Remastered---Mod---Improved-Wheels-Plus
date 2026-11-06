"""IWP-041: the first assignment of a starting item (held only through the base record) failed and the second worked. SetKey only
starts making the item's inventory entry (TES thread) and returns false, and AddTo dropped the assignment; Reconcile likewise cleared
the slot's active choice. While the entry is being made (inventory::KeyPending), the assignment stays and the key follows.
Runs Assign.cpp's real AddTo and Model.cpp's real Reconcile.
"""
import pathlib
import re
import subprocess
import tempfile

ROOT = pathlib.Path(__file__).resolve().parents[1]


def function(path, signature):
    source = (ROOT / path).read_text(encoding="utf-8")
    match = re.search(r"\n\t" + signature + r"\s*\{", source)
    assert match, signature
    pos = source.index("{", match.start())
    depth, end = 1, pos + 1
    while depth:
        depth += (source[end] == "{") - (source[end] == "}")
        end += 1
    return source[match.start():end]


program = r'''
#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdio>
#include <format>
#include <set>
#include <string>
#include <vector>
namespace logger { template <class... A> void info(const char*, A&&...) {} }
namespace inventory {
constexpr int kSlots = 8;
using KeyMap = std::array<std::uint32_t, kSlots>;
KeyMap keys{};
std::set<std::uint32_t> held, withEntry;
int setKeyCalls = 0;
KeyMap Keys() { return keys; }
bool Has(std::uint32_t id) { return held.count(id) > 0; }
bool HasEntry(std::uint32_t id) { return withEntry.count(id) > 0; }
bool KeyPending(std::uint32_t id) { return Has(id) && !HasEntry(id); }
bool SetKey(std::uint32_t id, int key) {
    ++setKeyCalls;
    if (!Has(id) || !HasEntry(id)) return false;   // no entry yet: the TES thread is making it
    for (auto& k : keys) if (k == id) k = 0;
    keys[key] = id; return true;
}
void ClearKey(std::uint32_t id) { for (auto& k : keys) if (k == id) k = 0; }
std::string NameOf(std::uint32_t) { return "item"; }
}
namespace wheels::detail {
constexpr int kEquip = 0, kMagic = 1, kAmmo = 2;
struct Slot { std::vector<std::uint32_t> entries; int active = -1; };
std::array<std::array<Slot, 8>, 4> g_wheels;
int g_holdKey = -1;
const char* WheelName(int) { return "Equipment"; }
std::string NameOf(int, std::uint32_t) { return "item"; }
int Cap() { return 5; }
void ReclaimDepletedConsumable(int, int) {}
void Save() {}
void PreloadIcon(int) {}
int IndexOf(const Slot& s, std::uint32_t id) { auto it = std::find(s.entries.begin(), s.entries.end(), id); return it == s.entries.end() ? -1 : int(it - s.entries.begin()); }
std::string Unreachable(int, std::uint32_t id, const inventory::KeyMap&) { return inventory::Has(id) ? "" : "not carried"; }
int NextEquipment(int, int, int, std::string* = nullptr) { return -1; }
''' + function("src/wheels/Assign.cpp", r"bool AddTo\(int a_wheel, int a_slot, std::uint32_t a_id, const char\* a_why\)") + \
    function("src/wheels/Model.cpp", r"void Reconcile\(\)") + r'''
}
int main() {
    using namespace wheels::detail;
    int failures = 0;
    auto check = [&](bool ok, const char* what) { std::printf("%s: %s\n", ok ? "PASS" : "FAIL", what); failures += !ok; };
    const std::uint32_t shirt = 0x27319, gone = 0x1234;
    inventory::held = { shirt };   // held through the base record, no entry yet
    check(AddTo(kEquip, 2, shirt, "test") && g_wheels[kEquip][2].active == 0, "the first assignment of a starting item stays while its entry is made");
    Reconcile();
    check(g_wheels[kEquip][2].active == 0 && inventory::keys[2] == 0, "maintenance keeps the slot's choice while the entry is made");
    inventory::withEntry.insert(shirt);   // the TES thread made the entry
    Reconcile();
    check(g_wheels[kEquip][2].active == 0 && inventory::keys[2] == shirt, "the key follows once the entry exists");
    check(!AddTo(kEquip, 3, gone, "test") && g_wheels[kEquip][3].entries.empty(), "an item that is not carried is still refused");
    return failures ? 1 : 0;
}
'''

with tempfile.TemporaryDirectory(prefix="iwp041p-") as folder:
    tmp = pathlib.Path(folder)
    (tmp / "pending.cpp").write_text(program, encoding="utf-8")
    (tmp / "build.bat").write_text(
        '@echo off\ncall "F:\\modbuild\\tools\\msvc\\setup_x64.bat" >nul\n'
        f'cl /nologo /EHsc /std:c++20 "{tmp / "pending.cpp"}" /Fe:"{tmp / "pending.exe"}" /Fo:"{tmp / "pending.obj"}"\n', encoding="utf-8")
    subprocess.run(["cmd", "/c", str(tmp / "build.bat")], check=True, cwd=tmp)
    subprocess.run([str(tmp / "pending.exe")], check=True)
