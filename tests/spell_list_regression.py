"""Spell-list walks in Rows.cpp (code review 2026-11-06, findings 6 and 7).

6: SpellByName walked the player's spell list with an unguarded range-for while the TES thread may change it; the other
   walks in the same file are fault-guarded. A broken node must read as "not found", not crash.
7: A Magic row's InventoryIndex counts the spells the menu lists: the native rebuild skips spell types 1 (disease) and
   4 (ability) (KB docs/inventory/magic-scroll-filter-2026-10-30.md, RVA 0x67767E1). Counting raw list positions put a
   custom spell's name on another spell once an ability sat before it. Membership (PlayerHasSpell) still counts every
   entry. The real helpers and SpellByName are compiled with stub game types.
"""
from pathlib import Path
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]
s = (root / 'src/game/Rows.cpp').read_text(encoding='utf-8').replace('\r\n', '\n')


def function(signature, required=True):
    start = s.find(signature)
    if start < 0:
        if required:
            raise SystemExit('missing ' + signature)
        return ''
    op = s.index('{', start)
    end, depth = op + 1, 1
    while depth:
        depth += (s[end] == '{') - (s[end] == '}')
        end += 1
    return s[start:end] + '\n'


parts = [
    function('bool InMagicMenuList(', False),
    function('int SpellPositionGuarded('),
    function('std::uint32_t SpellAtGuarded('),
    function('std::uint32_t SpellByNameGuarded(', False),
]
by_name = function('std::uint32_t SpellByName(const std::string& a_name)')

menu_order = 'a_menuOrder' in ''.join(parts)
program = '#define MENU_ORDER %d\n' % menu_order + r'''
#include <windows.h>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <unordered_map>
namespace RE {
namespace MagicSystem { enum class SpellType : std::int32_t { kSpell, kDisease, kPower, kLesserPower, kAbility, kPoison }; }
struct SpellItem {
	std::uint32_t id; const char* name; struct { MagicSystem::SpellType spellType; } data;
	std::uint32_t GetFormID() const { return id; }
};
template <class T> struct BSSimpleList {
	struct Node { T item; Node* next; };
	Node head{};
	struct iterator {
		Node* n;
		T operator*() const { return n->item; }
		iterator& operator++() { n = n->next; return *this; }
		bool operator!=(const iterator& o) const { return n != o.n; }
	};
	iterator begin() { return { head.item || head.next ? &head : nullptr }; }
	iterator end() { return { nullptr }; }
};
struct TESSpellList { BSSimpleList<SpellItem*> spells; };
struct TESFullName { static const char* GetFullName(SpellItem* a) { return a->name; } };
struct TESForm { template <class T> static T* LookupByID(std::uint32_t) { return nullptr; } };
}
namespace rows {
using SpellNode = RE::BSSimpleList<RE::SpellItem*>::Node;
RE::TESSpellList* g_list = nullptr;
RE::TESSpellList* PlayerSpells() { return g_list; }
bool PlayerHasSpell(std::uint32_t) { return false; }
void ObserveContext() {}
std::unordered_map<std::string, std::uint32_t> g_spellsByName;
''' + ''.join(parts) + by_name + r'''
}
int failures = 0;
void Check(bool a_ok, const char* a_what) { std::printf("%s: %s\n", a_ok ? "PASS" : "FAIL", a_what); if (!a_ok) ++failures; }
using RE::MagicSystem::SpellType;
// 1 = found, 0 = not found, 2 = the fault went past the walk
int FindGuardedCall(const std::string& a_name)
{
	__try {
		return rows::SpellByName(a_name) ? 1 : 0;
	} __except (EXCEPTION_EXECUTE_HANDLER) {
		return 2;
	}
}
int main() {
	RE::SpellItem a{ 0x14, "Flare", { SpellType::kSpell } }, ability{ 0x15, "Birthsign Ability", { SpellType::kAbility } },
		b{ 0x16, "Heal Minor Wounds", { SpellType::kSpell } }, power{ 0x17, "Lesser Power", { SpellType::kLesserPower } },
		custom{ 0xFF000801, "UI_UserInputText_1", { SpellType::kSpell } }, disease{ 0x18, "Rattles", { SpellType::kDisease } };
	// raw list: Flare, ability, Heal, power, disease, custom. The menu lists Flare 1, Heal 2, power 3, custom 4.
	using Node = rows::SpellNode;
	Node n6{ &custom, nullptr }, n5{ &disease, &n6 }, n4{ &power, &n5 }, n3{ &b, &n4 }, n2{ &ability, &n3 };
	RE::TESSpellList list; list.spells.head = { &a, &n2 };
	rows::g_list = &list;

#if MENU_ORDER
	auto at = [&](int pos) { return rows::SpellAtGuarded(&list, pos, true); };
	auto where = [&](std::uint32_t id) { return rows::SpellPositionGuarded(&list, nullptr, id, true); };
#else
	auto at = [&](int pos) { return rows::SpellAtGuarded(&list, pos); };
	auto where = [&](std::uint32_t id) { return rows::SpellPositionGuarded(&list, nullptr, id); };
#endif
	Check(at(3) == custom.id, "InventoryIndex 4 (position 3) is the custom spell, abilities and diseases not counted");
	Check(where(b.id) == 1, "Heal Minor Wounds sits at menu position 1");
	Check(rows::SpellPositionGuarded(&list, &ability, 0) == 1, "membership still finds an ability (raw position)");

	Check(FindGuardedCall("Heal Minor Wounds") == 1, "SpellByName finds a spell by its name");
	Node broken{ &b, reinterpret_cast<Node*>(0x10) };   // a node freed under the walk
	list.spells.head = { &a, &broken };
	Check(FindGuardedCall("Not There") == 0, "a broken list node reads as not found, no crash");
	return failures ? 1 : 0;
}
'''

with tempfile.TemporaryDirectory() as folder:
    folder = Path(folder)
    (folder / 'test.cpp').write_text(program, encoding='utf-8')
    (folder / 'build.bat').write_text(
        '@echo off\ncall F:\\modbuild\\tools\\msvc\\setup_x64.bat >nul\ncl /nologo /EHsc /std:c++20 test.cpp /Fe:test.exe >build.log\nif errorlevel 1 type build.log\n',
        encoding='utf-8')
    subprocess.run(['cmd', '/c', str(folder / 'build.bat')], cwd=folder, check=True)
    if not (folder / 'test.exe').exists():
        raise SystemExit('BUILD FAILED')
    raise SystemExit(subprocess.run([str(folder / 'test.exe')], cwd=folder).returncode)
