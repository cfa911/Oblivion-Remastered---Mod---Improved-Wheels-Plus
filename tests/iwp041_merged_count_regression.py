"""IWP-041: an item's held count is the base container's count plus the ExtraContainerChanges delta.

Starting gear (the prison Sack Cloth Shirt) comes from the player's base record: its change entry carries count 0 while the
game shows and uses one (native merged lookup RVA 0x663E080, KB docs/game-internals.md). Runs Inventory.cpp's real counting
and name lookups against fake game lists.
"""
import pathlib
import re
import subprocess
import tempfile

ROOT = pathlib.Path(__file__).resolve().parents[1]
source = (ROOT / "src/game/Inventory.cpp").read_text(encoding="utf-8")


def function(name, required=True):
    match = re.search(r"\n\t+(?:RE::ItemChange\*|std::uint32_t|bool|int|State) " + name + r"\([^)]*\)\s*\{", source)
    if not match:
        assert not required, name
        return ""
    pos = source.index("{", match.start())
    depth, end = 1, pos + 1
    while depth:
        depth += (source[end] == "{") - (source[end] == "}")
        end += 1
    return source[match.start():end]


helpers = "\n".join(function(n, required=False) for n in ("BaseCountGuarded", "HeldGuarded", "BaseNamedGuarded"))
program = r'''
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <excpt.h>
#include <string>
namespace logger { template <class... A> void warn(const char*, A&&...) {} template <class... A> void info(const char*, A&&...) {} }
namespace RE {
enum class FormType : std::uint8_t { None = 0, NPC = 35, Clothing = 22 };
enum class EXTRA_DATA_TYPE { Worn, WornLeft };
template <class T> struct BSSimpleList { struct Node { T item; Node* next; }; Node head; };
struct TESForm {
    std::uint32_t id = 0; FormType type = FormType::Clothing; const char* name = "";
    std::uint32_t GetFormID() const { return id; } FormType GetFormType() const { return type; }
};
struct TESBoundObject : TESForm {};
struct ExtraDataList {
    bool worn = false;
    const void* GetExtraData(EXTRA_DATA_TYPE t) const { return worn && t == EXTRA_DATA_TYPE::Worn ? this : nullptr; }
};
struct ItemChange { BSSimpleList<ExtraDataList*>* extraData; std::int32_t count; TESBoundObject* object; };
struct TESFullName { static const char* GetFullName(const TESForm* f) { return f ? f->name : nullptr; } };
struct ContainerObject;
struct TESContainer { std::uint64_t vtable = 0; std::uint8_t contFlag = 0; BSSimpleList<ContainerObject*> objectList{}; };
struct TESNPC : TESBoundObject, TESContainer {};
struct OBJ_REFR { TESBoundObject* objectReference = nullptr; };
struct PlayerCharacter { OBJ_REFR data; static PlayerCharacter* GetSingleton(); };
}
RE::PlayerCharacter g_player;
RE::PlayerCharacter* RE::PlayerCharacter::GetSingleton() { return &g_player; }
namespace inventory {
struct State { int count = 0; bool worn = false; bool carried = false; };
namespace {
using ItemNode = RE::BSSimpleList<RE::ItemChange*>::Node;
using ExtraNode = RE::BSSimpleList<RE::ExtraDataList*>::Node;
RE::BSSimpleList<RE::ItemChange*> g_changes{};
RE::BSSimpleList<RE::ItemChange*>* Items() { return &g_changes; }
''' + function("IsWorn") + function("FindGuarded") + helpers + function("CountGuarded") + function("FindNamedGuarded") + r'''
}
''' + function("Has") + function("FindByName") + function("StateOf") + r'''
}
struct BaseEntry { std::int32_t count; std::int32_t pad; RE::TESForm* form; };   // ContainerObject: count +0x0, form +0x8
int main() {
    int failures = 0;
    auto check = [&](bool ok, const char* what) { std::printf("%s: %s\n", ok ? "PASS" : "FAIL", what); failures += !ok; };
    RE::TESBoundObject shirt; shirt.id = 0x27319; shirt.name = "Sack Cloth Shirt";
    RE::TESBoundObject sword; sword.id = 0x229B3; sword.name = "Iron Sword";
    RE::TESBoundObject pants; pants.id = 0x2731A; pants.name = "Sack Cloth Pants";
    RE::TESNPC base; base.type = RE::FormType::NPC;
    g_player.data.objectReference = &base;
    BaseEntry shirtBase{ 1, 0, &shirt }, pantsBase{ 1, 0, &pants };
    using CNode = RE::BSSimpleList<RE::ContainerObject*>::Node;
    CNode pantsNode{ reinterpret_cast<RE::ContainerObject*>(&pantsBase), nullptr };
    base.objectList.head = { reinterpret_cast<RE::ContainerObject*>(&shirtBase), &pantsNode };

    RE::ExtraDataList wornList; wornList.worn = true;
    RE::BSSimpleList<RE::ExtraDataList*> shirtExtra{ { &wornList, nullptr } };
    RE::ItemChange shirtChange{ &shirtExtra, 0, &shirt };   // worn starting shirt: delta 0
    RE::ItemChange swordChange{ nullptr, 2, &sword };        // picked up: no base entry
    inventory::ItemNode swordNode{ &swordChange, nullptr };
    inventory::g_changes.head = { &shirtChange, &swordNode };

    const auto shirtState = inventory::StateOf(shirt.id);
    check(inventory::Has(shirt.id) && shirtState.count == 1 && shirtState.worn, "starting gear with a zero change entry is held (count 1, worn)");
    check(inventory::FindByName("Sack Cloth Shirt") == shirt.id, "a row's name finds the starting shirt");
    check(inventory::Has(pants.id) && inventory::FindByName("Sack Cloth Pants") == pants.id, "base-only gear with no change entry is held and found");
    check(inventory::StateOf(sword.id).count == 2 && inventory::FindByName("Iron Sword") == sword.id, "picked-up items keep their count");
    shirtChange.count = -1;   // the starting shirt dropped
    check(!inventory::Has(shirt.id) && inventory::FindByName("Sack Cloth Shirt") == 0, "a dropped starting item is not held");
    shirtBase.count = -1; shirtChange.count = 0;   // the merged lookup reads a negative base count by its magnitude
    check(inventory::StateOf(shirt.id).count == 1, "a negative base count counts by magnitude, as the game's lookup does");
    return failures ? 1 : 0;
}
'''

with tempfile.TemporaryDirectory(prefix="iwp041-") as folder:
    tmp = pathlib.Path(folder)
    (tmp / "count.cpp").write_text(program, encoding="utf-8")
    (tmp / "build.bat").write_text(
        '@echo off\ncall "F:\\modbuild\\tools\\msvc\\setup_x64.bat" >nul\n'
        f'cl /nologo /EHsc /std:c++20 "{tmp / "count.cpp"}" /Fe:"{tmp / "count.exe"}" /Fo:"{tmp / "count.obj"}"\n', encoding="utf-8")
    subprocess.run(["cmd", "/c", str(tmp / "build.bat")], check=True, cwd=tmp)
    subprocess.run([str(tmp / "count.exe")], check=True)
