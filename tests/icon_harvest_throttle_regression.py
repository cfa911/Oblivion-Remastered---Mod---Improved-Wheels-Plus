"""The icon caches must not re-walk every menu row on every read (performance review 2026-11-19, owner run 2026-11-20).

Measured (TEMP-DIAG, Inventory/Magic assign panels): rows::Harvest 19 ms/s - the rows fire events every frame, each
event marks the caches dirty, and the next icon lookup (8 per read from the Magic wheel overlay) walked every row the
menu had shown (~200 us). The walk now runs at most every 100 ms while rows keep changing; a lookup that misses the
cache still walks at once, so a newly shown item gets its row's icon straight away, and an item with no icon at all does
not force a walk on every read. The real Harvest and ItemIcon are compiled against stubs.
"""
from pathlib import Path
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]
s = (root / 'src/game/Rows.cpp').read_text(encoding='utf-8').replace('\r\n', '\n')
strip = lambda t: '\n'.join(l for l in t.split('\n') if 'TEMP-DIAG' not in l)
h0 = s.index('\t\tvoid Harvest(')
harvest = strip(s[h0:s.index('\n\t\tvoid OnRow(', h0)])
i0 = s.index('\tUE::UObject* ItemIcon(std::uint32_t a_formID)')
item_icon = strip(s[i0:s.index('\n\tnamespace\n', i0)])
k0 = s.index('\tstd::uint32_t HighlightedItem()')
highlighted = strip(s[k0:s.index('\n\tstd::uint32_t HighlightedPlayerItemInContainer()', k0)])

program = r'''
#include <cstdint>
#include <cstdio>
#include <string>
#include <unordered_map>
#include <unordered_set>
using ULONGLONG = unsigned long long;
ULONGLONG g_now = 5000;
ULONGLONG GetTickCount64() { return g_now; }
namespace UE { struct UObject { std::int32_t internalIndex = 0; std::uint32_t form = 0; UObject* icon = nullptr; }; }
namespace logger { template <class... A> void info(A&&...) {} }
namespace reflect {
struct Handle { UE::UObject* ptr = nullptr; std::int32_t index = -1; };
Handle Hold(UE::UObject* o) { return { o, o ? o->internalIndex : -1 }; }
UE::UObject* Get(const Handle& h) { return h.ptr; }
UE::UObject* Get(UE::UObject* p, std::int32_t) { return p; }
bool IsLive(UE::UObject* o) { return o != nullptr; }
}
struct Kind { std::unordered_map<UE::UObject*, std::int32_t> seen; reflect::Handle highlighted; };
std::uint64_t g_rowGeneration = 0;
namespace readmemo { template <class K, class V> struct Memo { template <class F> V Get(const K&, F&& f) { return f(); } }; }
Kind g_inventory, g_magic;
std::unordered_map<std::uint32_t, reflect::Handle> g_itemIcons, g_spellIcons;
std::unordered_set<std::uint32_t> g_iconFailed;
bool g_dirty = true;
int g_walks = 0, g_formLoads = 0;
void ObserveContext() {}
std::uint32_t FormOfInventoryRow(UE::UObject* r) { ++g_walks; return r->form; }   // counted once per row read
std::uint32_t SpellOfRow(Kind&, UE::UObject*) { return 0; }
UE::UObject* IconOfRow(Kind&, UE::UObject* r) { return r->icon; }
namespace RE { struct TESForm { static void* LookupByID(std::uint32_t) { return nullptr; } }; }
struct Tex { std::string textureName; };
const Tex* IconOf(void*) { return nullptr; }   // no form icon: the fallback fails
std::string IconAssetPath(const char*) { return {}; }
namespace ui { UE::UObject* Load(const wchar_t*) { ++g_formLoads; return nullptr; } }
''' + harvest + '\n' + item_icon + '\n' + highlighted + r'''
#define FAIL(m) do { std::printf("FAIL: %s (row reads=%d)\n", m, g_walks); return 1; } while (0)
int main() {
	UE::UObject icons[10];
	UE::UObject rows[10];
	for (int i = 0; i < 8; ++i) { rows[i].internalIndex = i; rows[i].form = 0x100 + i; rows[i].icon = &icons[i]; g_inventory.seen[&rows[i]] = i; }
	for (int i = 0; i < 8; ++i) if (ItemIcon(0x100 + i) != &icons[i]) FAIL("each row's icon is found");
	g_walks = 0;
	// 1.2 s of an assign panel: rows fire events every frame, the overlay asks 8 icons and one empty-icon item per read
	for (int read = 0; read < 100; ++read) {
		g_now += 12;
		g_dirty = true;
		for (int i = 0; i < 8; ++i) if (ItemIcon(0x100 + i) != &icons[i]) FAIL("cached icons stay right");
		ItemIcon(0x999);   // an item with no icon anywhere
	}
	const int walks = g_walks / 8;   // a walk reads the 8 rows (the highlighted row's own read is not a walk: below)
	if (walks > 15) { std::printf("FAIL: %d whole-row walks in 1.2 s; at most one per 100 ms (plus one) expected\n", walks); return 1; }
	// the Inventory panel asks the highlighted item on every read (wheels::AssignmentWheel); it must not walk every row each time
	g_walks = 0;
	g_inventory.highlighted = reflect::Hold(&rows[3]);
	for (int read = 0; read < 100; ++read) {
		g_now += 12;
		g_dirty = true;
		if (HighlightedItem() != 0x103) FAIL("the highlighted row's item is read directly");
	}
	const int hlWalks = (g_walks - 100) / 8;   // each call also reads the highlighted row itself once
	if (hlWalks > 15) { std::printf("FAIL: HighlightedItem walked every row %d times in 1.2 s\n", hlWalks); return 1; }
	// a newly shown item: its row appears (an event) and the overlay asks for it at once, inside the 100 ms
	rows[8].internalIndex = 8; rows[8].form = 0x200; rows[8].icon = &icons[8]; g_inventory.seen[&rows[8]] = 8;
	g_dirty = true; g_now += 1;
	if (ItemIcon(0x200) != &icons[8]) FAIL("a new item gets its row's icon at once, not a form icon later");
	// a row recycled for another icon: the cache follows within 100 ms
	rows[0].icon = &icons[9]; g_dirty = true; g_now += 150;
	if (ItemIcon(0x100) != &icons[9]) FAIL("a changed row icon is picked up after the throttle");
	std::printf("PASS: %d row walks in 1.2 s of dirty rows; new items found at once\n", walks);
	return 0;
}
'''
with tempfile.TemporaryDirectory() as folder:
    folder = Path(folder)
    (folder / 'test.cpp').write_text(program, encoding='utf-8')
    (folder / 'build.bat').write_text('@echo off\ncall F:\\modbuild\\tools\\msvc\\setup_x64.bat >nul\ncl /nologo /EHsc /std:c++20 test.cpp /Fe:test.exe\n', encoding='utf-8')
    subprocess.run(['cmd', '/c', str(folder / 'build.bat')], cwd=folder, check=True, stdout=subprocess.DEVNULL)
    raise SystemExit(subprocess.run([str(folder / 'test.exe')], cwd=folder, timeout=20).returncode)
