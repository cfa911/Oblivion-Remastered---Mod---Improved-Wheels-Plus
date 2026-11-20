"""quickkeys::GameMessageUp must not walk the whole object array while it is asked (performance review 2026-11-19).

Measured: with a wheel or the Inventory assign panel up it is asked ~80 times a second and, with no message showing,
ran a whole-object-array scan (reflect::Instances, ~1 ms) about 10 times a second. Message widgets are learned from
their own ProcessEvent calls instead (pe::Watch on the message class): one scan when the watch goes in, none after.
A message created later must still be found the moment it shows, an input press must find a dialog the watch never
saw (the poison question repeated in the owner's run of 2026-11-20 when presses stopped scanning), one that is answered must read as gone, and a failed
watch must keep the old throttled discovery. The real function is compiled against stubs; ui::Call::Run goes through
the stub detour like the game's ProcessEvent, so a lock held across the IsActivated/IsVisible calls would deadlock here.
"""
from pathlib import Path
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]
s = (root / 'src/game/QuickKeys.cpp').read_text(encoding='utf-8').replace('\r\n', '\n')
start = s.index('\tbool GameMessageUp(bool a_immediate)')
end = s.index('\n\tbool PanelOpen()', start)
fn = '\n'.join(l for l in s[start:end].split('\n') if 'TEMP-DIAG' not in l)

program = r'''
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <mutex>
#include <string>
#include <vector>
#include "game/MessageDiscovery.h"
using ULONGLONG = unsigned long long;
ULONGLONG g_now = 1000;
ULONGLONG GetTickCount64() { return g_now; }
namespace UE {
struct UClass;
struct UFunction {};
struct UObject { std::int32_t internalIndex = -1; UClass* cls = nullptr; bool activated = false, visible = false; UClass* GetClass() const { return cls; } };
struct UClass : UObject {};
UClass g_msgClass;
template <class T> T* StaticFindObject(void*, void*, const wchar_t*) { return &g_msgClass; }
}
namespace logger { template <class... A> void info(A&&...) {} template <class... A> void warn(A&&...) {} }
std::vector<UE::UObject*> g_slots;   // the object array
int g_scans = 0;
namespace reflect {
struct Handle { UE::UObject* ptr = nullptr; std::int32_t index = -1; };
Handle Hold(UE::UObject* a) { return { a, a ? a->internalIndex : -1 }; }
UE::UObject* Get(const Handle& h) { return h.index >= 0 && h.index < (int)g_slots.size() && g_slots[h.index] == h.ptr ? h.ptr : nullptr; }
std::vector<UE::UObject*> Instances(UE::UClass* c) { ++g_scans; std::vector<UE::UObject*> o; for (auto* x : g_slots) if (x && x->cls == c) o.push_back(x); return o; }
}
namespace pe {
using Handler = void (*)(UE::UObject*, UE::UFunction*, void*);
Handler g_handler = nullptr; UE::UClass* g_watched = nullptr; bool g_fail = false; int g_tries = 0;
bool Watch(UE::UClass* c, Handler h) { ++g_tries; if (g_fail) return false; g_watched = c; g_handler = h; return true; }
UE::UFunction g_fn;
void Detour(UE::UObject* o) { if (g_handler && o && o->cls == g_watched) g_handler(o, &g_fn, nullptr); }   // before the body, like the game
}
namespace ui {
struct Call {
	UE::UObject* obj; std::wstring name; bool result = false;
	Call(UE::UObject* o, const wchar_t* n) : obj(o), name(n) {}
	explicit operator bool() const { return obj != nullptr; }
	bool Run() { pe::Detour(obj); result = name == L"IsActivated" ? obj->activated : obj->visible; return true; }
	template <class T> T Get(const char*) const { return result; }
};
}
UE::UObject* Spawn() { auto* o = new UE::UObject; o->cls = &UE::g_msgClass; o->internalIndex = (int)g_slots.size(); g_slots.push_back(o); pe::Detour(o); return o; }
void Show(UE::UObject* o, bool up) { o->activated = up; o->visible = up; pe::Detour(o); }
#define FAIL(m) do { std::printf("FAIL: %s (scans=%d)\n", m, g_scans); return 1; } while (0)
namespace quickkeys {
''' + fn + r'''
}
int main() {
#ifdef WATCH_FAILS
	pe::g_fail = true;
	auto* a = Spawn();
	if (quickkeys::GameMessageUp(true)) FAIL("nothing is up yet");
	Show(a, true);
	if (!quickkeys::GameMessageUp(true)) FAIL("failed watch: the old discovery must still find a shown message");
	Show(a, false);
	if (quickkeys::GameMessageUp(true)) FAIL("failed watch: an answered message must read as gone");
	for (int i = 0; i < 20; ++i) quickkeys::GameMessageUp(false);
	if (pe::g_tries != 1) FAIL("a failed watch must not be retried on every ask");
	std::puts("PASS: a failed watch keeps the throttled discovery");
	return 0;
#else
	auto* early = Spawn();   // made before the class was ever looked up: only a scan can know it
	for (int i = 0; i < 100; ++i) {   // ten seconds of a wheel up with no message, asked every 100 ms by the overlay
		g_now += 100;
		if (quickkeys::GameMessageUp(false)) FAIL("no message is up");
	}
	if (g_scans > 1) FAIL("the overlay's asks with no message up must not scan the object array again and again");
	Show(early, true);
	if (!quickkeys::GameMessageUp(false)) FAIL("a message that existed before the watch must be found when it shows");
	Show(early, false);
	if (quickkeys::GameMessageUp(true)) FAIL("an answered message must read as gone");
	auto* late = Spawn();   // a new dialog: the game's own ProcessEvent calls on it make it known
	Show(late, true);
	if (!quickkeys::GameMessageUp(false)) FAIL("a message created later must be found the moment it shows");
	Show(late, false);
	g_slots[late->internalIndex] = nullptr;   // destroyed: its slot is empty
	auto* reuse = Spawn();
	Show(reuse, true);
	if (!quickkeys::GameMessageUp(true)) FAIL("a message in a new slot must be found");
	Show(reuse, false);
	const int before = g_scans;
	for (int i = 0; i < 50; ++i) { g_now += 100; quickkeys::GameMessageUp(false); }
	if (g_scans != before) FAIL("no overlay scan after the watch is in");
	// a dialog whose creation and showing reached no watched call (owner run 2026-11-20: the poison question repeated):
	// an input press must still find it, so the press goes to the dialog and is not taken as a new wheel choice
	auto* silent = new UE::UObject; silent->cls = &UE::g_msgClass; silent->internalIndex = (int)g_slots.size(); g_slots.push_back(silent);
	silent->activated = silent->visible = true;
	if (!quickkeys::GameMessageUp(true)) FAIL("an input press must find a shown dialog the watch never saw");
	if (!quickkeys::GameMessageUp(false)) FAIL("once found, the overlay must see that dialog too");
	std::puts("PASS: overlay asks need at most one object-array scan; presses still find any shown dialog");
	return 0;
#endif
}
'''
with tempfile.TemporaryDirectory() as folder:
    folder = Path(folder)
    (folder / 'test.cpp').write_text(program, encoding='utf-8')
    inc = str(root / 'src')
    (folder / 'build.bat').write_text(
        '@echo off\ncall F:\\modbuild\\tools\\msvc\\setup_x64.bat >nul\n'
        f'cl /nologo /EHsc /std:c++20 /I"{inc}" test.cpp /Fe:test.exe || exit /b 1\n'
        f'cl /nologo /EHsc /std:c++20 /DWATCH_FAILS /I"{inc}" test.cpp /Fe:test_fail.exe || exit /b 1\n', encoding='utf-8')
    subprocess.run(['cmd', '/c', str(folder / 'build.bat')], cwd=folder, check=True, stdout=subprocess.DEVNULL)
    r1 = subprocess.run([str(folder / 'test.exe')], cwd=folder, timeout=20)
    r2 = subprocess.run([str(folder / 'test_fail.exe')], cwd=folder, timeout=20)
    raise SystemExit(r1.returncode or r2.returncode)
