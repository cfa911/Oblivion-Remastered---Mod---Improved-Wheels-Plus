"""IWP-041: an item held only through the player's base record has no ExtraContainerChanges entry to carry an ExtraQuickKey.
The wheel gives it one the way the game's own add/move/equip routines build entries (0x18 ItemChange with the object at +0x10 and
count 0, linked by a list node; KB docs/reverse-engineering/native-hotkey-routines.md), once, on the TES thread; then the item keys
like any other. Runs Inventory.cpp's real SetKey / Keys / entry creation / QueueKeyed against fake lists. A not-carried item is
warned about once, not every 100 ms.
"""
import pathlib
import re
import subprocess
import tempfile

ROOT = pathlib.Path(__file__).resolve().parents[1]
source = (ROOT / "src/game/Inventory.cpp").read_text(encoding="utf-8")


def function(name, required=True):
    match = re.search(r"\n\t+(?:RE::BSExtraData\*|RE::ItemChange\*|RE::TESBoundObject\*|std::uint32_t|bool|int|void|KeyMap|State) " + name + r"\([^)]*\)\s*\{", source)
    if not match:
        assert not required, name
        return ""
    pos = source.index("{", match.start())
    depth, end = 1, pos + 1
    while depth:
        depth += (source[end] == "{") - (source[end] == "}")
        end += 1
    return source[match.start():end]


optional = ("BaseCountGuarded", "BaseObjectGuarded", "HeldGuarded", "AppendEntryGuarded", "PostCreateEntry")
anon = "".join(function(n, required=n not in optional)
               for n in ("KeyExtra", "KeyOf", "IsWorn", "FindGuarded", "Find", "KeysGuarded", "BaseCountGuarded", "BaseObjectGuarded", "HeldGuarded",
                         "CountGuarded", "AppendEntryGuarded", "PostCreateEntry"))
program = r'''
#include <array>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <excpt.h>
#include <functional>
#include <mutex>
#include <string>
#include <unordered_set>
#include <vector>
int warnings = 0;
namespace logger {
template <class... A> void warn(const char*, A&&...) { ++warnings; }
template <class... A> void info(const char*, A&&...) {}
template <class... A> void error(const char*, A&&...) {}
}
bool tesPaused = false;
std::vector<std::function<void()>> tesQueue;
int posts = 0;
namespace testhread { template <class F> void Post(F f) { ++posts; if (tesPaused) tesQueue.push_back(f); else f(); } }
namespace RE {
inline void* malloc(std::size_t a_size) { return std::calloc(1, a_size); }
enum class FormType : std::uint8_t { None = 0, NPC = 35, Clothing = 22 };
enum class EXTRA_DATA_TYPE : std::uint8_t { Worn = 0x16, WornLeft = 0x17 };
template <class T> struct BSSimpleList {
    struct Node { T item; Node* next; };
    Node head;
    struct It { Node* n; T operator*() const { return n->item; } It& operator++() { n = n->next; return *this; } bool operator!=(const It& o) const { return n != o.n; } };
    It begin() { return { head.item || head.next ? &head : nullptr }; } It end() { return { nullptr }; }
};
struct BSExtraData { BSExtraData* next = nullptr; struct T { std::uint8_t v; std::uint8_t get() const { return v; } } type{ 0 }; std::uint8_t pad[15]{}; std::uint8_t key = 0; };
struct ExtraDataList {
    BSExtraData* head = nullptr;
    const void* GetExtraData(EXTRA_DATA_TYPE) const { return nullptr; }
    void AddExtra(BSExtraData* x) { x->next = head; head = x; }
    void RemoveExtra(EXTRA_DATA_TYPE t) { for (auto** p = &head; *p;) { if ((*p)->type.v == static_cast<std::uint8_t>(t)) *p = (*p)->next; else p = &(*p)->next; } }
};
struct TESBoundObject;
struct TESForm {
    std::uint32_t id = 0; FormType type = FormType::Clothing;
    std::uint32_t GetFormID() const { return id; } FormType GetFormType() const { return type; }
    template <class T> static T* LookupByID(std::uint32_t);
};
struct TESBoundObject : TESForm {};
struct ItemChange { BSSimpleList<ExtraDataList*>* extraData; std::int32_t count; TESBoundObject* object; };
struct ContainerObject;
struct TESContainer { std::uint64_t vtable = 0; std::uint8_t contFlag = 0; BSSimpleList<ContainerObject*> objectList{}; };
struct TESNPC : TESBoundObject, TESContainer {};
struct OBJ_REFR { TESBoundObject* objectReference = nullptr; };
struct PlayerCharacter {
    OBJ_REFR data; int equips = 0, unequips = 0;
    static PlayerCharacter* GetSingleton();
    void EquipObject(TESBoundObject*, int, ExtraDataList*, bool, bool) { ++equips; }
    void UnequipObject(TESBoundObject*, int, ExtraDataList*, bool, bool) { ++unequips; }
};
}
std::vector<RE::TESBoundObject*> g_forms;
// in game the typed lookup is null for clothing (diagnostic trace 2026-11-06): the object must come from the base container
template <class T> T* RE::TESForm::LookupByID(std::uint32_t) { return nullptr; }
RE::PlayerCharacter g_player;
RE::PlayerCharacter* RE::PlayerCharacter::GetSingleton() { return &g_player; }
namespace inventory {
constexpr int kSlots = 8;
using KeyMap = std::array<std::uint32_t, kSlots>;
struct State { int count = 0; bool worn = false; bool carried = false; };
State StateOf(std::uint32_t);
std::string Describe(const State&) { return {}; }
std::string NameOf(std::uint32_t) { return "item"; }
namespace {
constexpr std::uint8_t kQuickKeyType = 0x55;
constexpr std::ptrdiff_t kKeyIDOffset = 0x18;
using ItemNode = RE::BSSimpleList<RE::ItemChange*>::Node;
using ExtraNode = RE::BSSimpleList<RE::ExtraDataList*>::Node;
RE::BSSimpleList<RE::ItemChange*> g_changes{};
RE::BSSimpleList<RE::ItemChange*>* Items() { return &g_changes; }
std::unordered_set<std::uint32_t> g_warnedNotCarried;
std::unordered_set<std::uint32_t> g_entryPending;
std::mutex g_entryLock;
RE::ExtraDataList* NewList(RE::ItemChange* a_item) {
    auto* list = new RE::ExtraDataList{};
    a_item->extraData = new RE::BSSimpleList<RE::ExtraDataList*>{ { list, nullptr } };
    return list;
}
RE::BSExtraData* NewQuickKey(int a_key) { auto* x = new RE::BSExtraData{}; x->type.v = kQuickKeyType; x->key = static_cast<std::uint8_t>(a_key); return x; }
''' + anon + r'''
}
''' + function("StateOf") + function("Keys") + function("ClearKey") + function("ClearSlot") + function("SetKey") + "namespace {\nenum class KeyedAction { kEquip, kUnequip, kToggle };\n" + function("QueueKeyed") + "}\n" + function("ToggleKeyed") + r'''
}
struct BaseEntry { std::int32_t count; std::int32_t pad; RE::TESForm* form; };
int entriesOf(std::uint32_t id) { int n = 0; for (auto* item : inventory::g_changes) n += item && item->object && item->object->id == id; return n; }
int main() {
    using namespace inventory;
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    int failures = 0;
    auto check = [&](bool ok, const char* what) { std::printf("%s: %s\n", ok ? "PASS" : "FAIL", what); failures += !ok; };
    RE::TESBoundObject shirt; shirt.id = 0x27319;
    RE::TESBoundObject sandals; sandals.id = 0x2731B;
    RE::TESBoundObject sword; sword.id = 0x229B3;
    g_forms = { &shirt, &sandals, &sword };
    RE::TESNPC base; base.type = RE::FormType::NPC;
    g_player.data.objectReference = &base;
    BaseEntry shirtBase{ 1, 0, &shirt }, sandalsBase{ 1, 0, &sandals };
    using CNode = RE::BSSimpleList<RE::ContainerObject*>::Node;
    CNode sandalsNode{ reinterpret_cast<RE::ContainerObject*>(&sandalsBase), nullptr };
    base.objectList.head = { reinterpret_cast<RE::ContainerObject*>(&shirtBase), &sandalsNode };
    RE::ExtraDataList swordList; RE::BSSimpleList<RE::ExtraDataList*> swordExtra{ { &swordList, nullptr } };
    RE::ItemChange swordChange{ &swordExtra, 1, &sword };
    g_changes.head = { &swordChange, nullptr };   // shirt and sandals: held only through the base record

    check(SetKey(shirt.id, 2) && Keys()[2] == shirt.id, "a base-only item gets its entry and its quick key");
    auto* made = FindGuarded(Items(), shirt.id);
    check(made && made->count == 0 && entriesOf(shirt.id) == 1 && StateOf(shirt.id).count == 1, "the new entry carries change 0: still one shirt");

    tesPaused = true; posts = 0; warnings = 0;   // the radial wheel is open: the TES thread waits
    bool quiet = true;
    for (int i = 0; i < 10; ++i) quiet = !SetKey(sandals.id, 5) && quiet;
    check(quiet && posts == 1 && warnings == 0 && Keys()[5] == 0, "while the TES thread waits, one creation is pending and nothing is logged");
    tesPaused = false; for (auto& f : tesQueue) f(); tesQueue.clear();
    check(SetKey(sandals.id, 5) && Keys()[5] == sandals.id && entriesOf(sandals.id) == 1, "after the TES thread ran, the item keys normally");

    check(SetKey(sword.id, 6) && Keys()[6] == sword.id, "an item with an entry still keys as before");
    ClearSlot(2);
    check(Keys()[2] == 0, "clearing works on a created entry");

    shirtBase.count = 0; if (made) made->count = 0; warnings = 0;   // the shirt is gone
    for (int i = 0; i < 5; ++i) SetKey(shirt.id, 7);
    check(warnings == 1 && Keys()[7] == 0, "a not-carried item is refused and warned about once");

    RE::TESBoundObject pants; pants.id = 0x2731A; g_forms.push_back(&pants);
    BaseEntry pantsBase{ 1, 0, &pants }; CNode pantsNode{ reinterpret_cast<RE::ContainerObject*>(&pantsBase), nullptr };
    sandalsNode.next = &pantsNode;
    ToggleKeyed(pants.id);
    check(g_player.equips == 1, "using a base-only item's slot equips it");
    return failures ? 1 : 0;
}
'''

with tempfile.TemporaryDirectory(prefix="iwp041e-") as folder:
    tmp = pathlib.Path(folder)
    (tmp / "entry.cpp").write_text(program, encoding="utf-8")
    (tmp / "build.bat").write_text(
        '@echo off\ncall "F:\\modbuild\\tools\\msvc\\setup_x64.bat" >nul\n'
        f'cl /nologo /EHsc /std:c++20 "{tmp / "entry.cpp"}" /Fe:"{tmp / "entry.exe"}" /Fo:"{tmp / "entry.obj"}"\n', encoding="utf-8")
    subprocess.run(["cmd", "/c", str(tmp / "build.bat")], check=True, cwd=tmp)
    subprocess.run([str(tmp / "entry.exe")], check=True)
