"""quickkeys::FindVisibleWheel must not walk the whole object array every 2 s while the wheel it knows is on screen.

Measured (TEMP-DIAG run 2026-11-20): VisibleWheel ~2 ms/s with a wheel or an assign panel up, its 2-second whole-array
scan up to 2.4 ms each. Now: no scan while a known wheel widget is shown; wheel widgets are also learned from their own
ProcessEvent calls (the existing OnWidgetEvent watch), so a new one is found at once; a wheel never seen by the watch is
still found by the 2-second scan when no known wheel is shown. The real function is compiled against stubs.
"""
from pathlib import Path
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]
s = (root / 'src/game/QuickKeys.cpp').read_text(encoding='utf-8').replace('\r\n', '\n')
strip = lambda t: '\n'.join(l for l in t.split('\n') if 'TEMP-DIAG' not in l)
f0 = s.index('\tstatic UE::UObject* FindVisibleWheel()')
find = strip(s[f0:s.index('\n\tUE::UObject* VisibleWheel()', f0)])
note = ''
has_note = '// wheel widgets learned from their events' in s
if has_note:
    n0 = s.index('\t// wheel widgets learned from their events')
    note = strip(s[n0:s.index('\t// wheel widgets learned end', n0)])

program = r'''
#include <atomic>
#include <cstdint>
#include <cstdio>
#include <mutex>
#include <vector>
using ULONGLONG = unsigned long long;
ULONGLONG g_now = 10000;
ULONGLONG GetTickCount64() { return g_now; }
namespace UE { struct UObject { std::int32_t internalIndex = 0; bool shown = false; }; struct UClass {}; }
UE::UClass g_cls;
std::atomic<UE::UClass*> g_widgetClass{ &g_cls };
std::vector<UE::UObject*> g_slots;
int g_scans = 0;
namespace reflect {
struct Handle { UE::UObject* ptr = nullptr; std::int32_t index = -1; };
Handle Hold(UE::UObject* o) { return { o, o ? o->internalIndex : -1 }; }
UE::UObject* Get(const Handle& h) { return h.index >= 0 && h.index < (int)g_slots.size() && g_slots[h.index] == h.ptr ? h.ptr : nullptr; }
bool Ok() { return true; }
std::vector<UE::UObject*> Instances(UE::UClass*) { ++g_scans; std::vector<UE::UObject*> o; for (auto* x : g_slots) if (x) o.push_back(x); return o; }
}
int g_shownAsks = 0;
namespace ui { bool ShownOnScreen(UE::UObject* w) { ++g_shownAsks; return w->shown; } }
namespace quickkeys {
''' + note + '\n' + find + r'''
}
UE::UObject* Spawn() { auto* o = new UE::UObject; o->internalIndex = (int)g_slots.size(); g_slots.push_back(o); return o; }
#define FAIL(m) do { std::printf("FAIL: %s (scans=%d)\n", m, g_scans); return 1; } while (0)
int main() {
	Spawn(); Spawn();   // two hidden wheel widgets known before the shown one (the Inventory and Magic panels)
	auto* hud = Spawn();
	if (quickkeys::FindVisibleWheel()) FAIL("nothing is shown yet");
	hud->shown = true;
	g_now += 2000;
	if (quickkeys::FindVisibleWheel() != hud) FAIL("the shown wheel is found");
	const int before = g_scans;
	for (int i = 0; i < 100; ++i) {   // ten seconds of the wheel up, asked every 100 ms
		g_now += 100;
		if (quickkeys::FindVisibleWheel() != hud) FAIL("the shown wheel stays found");
	}
	if (g_scans != before) { std::printf("FAIL: %d whole-array scans in 10 s while the known wheel was shown\n", g_scans - before); return 1; }
	g_shownAsks = 0;
	quickkeys::FindVisibleWheel();
	if (g_shownAsks != 1) { std::printf("FAIL: %d on-screen checks per ask while the same wheel stays shown; the last shown one is asked first\n", g_shownAsks); return 1; }
	hud->shown = false;
#ifdef HAS_NOTE
	auto* panel = Spawn();   // the Inventory's assign panel: a new widget, its own events make it known
	quickkeys::NoteWheel(panel);
	panel->shown = true;
	g_now += 10;
	const int beforeNote = g_scans;
	if (quickkeys::FindVisibleWheel() != panel) FAIL("a wheel learned from its events is found at once");
	if (g_scans != beforeNote) FAIL("finding a learned wheel needs no scan");
	panel->shown = false;
#endif
	auto* unseen = Spawn();   // never seen by the watch
	unseen->shown = true;
	g_now += 2001;
	if (quickkeys::FindVisibleWheel() != unseen) FAIL("a wheel the watch never saw is still found by the 2 s scan");
	std::puts("PASS: no scans while the known wheel is shown; learned wheels found at once; unseen ones by the 2 s scan");
	return 0;
}
'''
with tempfile.TemporaryDirectory() as folder:
    folder = Path(folder)
    (folder / 'test.cpp').write_text(program, encoding='utf-8')
    define = '/DHAS_NOTE ' if has_note else ''
    (folder / 'build.bat').write_text('@echo off\ncall F:\\modbuild\\tools\\msvc\\setup_x64.bat >nul\n'
                                      f'cl /nologo /EHsc /std:c++20 {define}test.cpp /Fe:test.exe\n', encoding='utf-8')
    subprocess.run(['cmd', '/c', str(folder / 'build.bat')], cwd=folder, check=True, stdout=subprocess.DEVNULL)
    raise SystemExit(subprocess.run([str(folder / 'test.exe')], cwd=folder, timeout=20).returncode)
