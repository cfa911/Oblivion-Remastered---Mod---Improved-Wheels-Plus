"""Keyboard quick-key unbinding must not walk the whole object table every ~5 s (code review 2026-11-06, finding 5).

Standing rule: no periodic object-table scans. The full walk (reflect::Instances) may run on a return to gameplay
(after a menu, a load or the start); the periodic check only looks at the mapping contexts found then, and must still
take a re-added quick key off again. The real controls::Tick and the quick-key unbinding block are compiled with stubs.
"""
from pathlib import Path
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]
s = (root / 'src/input/Controls.cpp').read_text(encoding='utf-8').replace('\r\n', '\n')
block = s[s.index('\t\t// IA_Game_QuickKeys_Keyboard_1..8'):s.index('\t\tvoid RebuildMappings()')]
start = s.index('\tvoid Tick()')
op = s.index('{', start)
end, depth = op + 1, 1
while depth:
    depth += (s[end] == '{') - (s[end] == '}')
    end += 1
tick = s[start:end]

program = r'''
#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <string>
#include <utility>
#include <vector>
namespace UE {
struct FName { std::uint64_t v; std::uint64_t ToUnstableInt() const { return v; } };
struct UObject { std::int32_t internalIndex = 0; std::string name; FName GetFName() const { return { std::uint64_t(internalIndex) }; } };
struct UClass : UObject {};
struct UStruct : UObject {};
template <class T> T* StaticFindObject(void*, void*, const wchar_t*) { static T t; return &t; }
}
struct RawArray { std::uint8_t* data; std::int32_t num; std::int32_t max; };
struct Entry { UE::UObject* action; std::uint64_t key; };
struct Ctx : UE::UObject { RawArray arr{}; std::vector<Entry> backing; void Sync() { arr.data = reinterpret_cast<std::uint8_t*>(backing.data()); arr.num = (std::int32_t)backing.size(); } };
std::vector<Ctx*> g_all;
int g_fullScans = 0;
std::int32_t g_off = 0;
namespace reflect {
struct Handle { UE::UObject* ptr = nullptr; std::int32_t index = -1; };
Handle Hold(UE::UObject* a) { return { a, a->internalIndex }; }
UE::UObject* Get(const Handle& h) { return h.ptr; }
std::int32_t Offset(UE::UStruct*, const char*) { return g_off; }
std::vector<UE::UObject*> Instances(UE::UClass*) { ++g_fullScans; return { g_all.begin(), g_all.end() }; }
}
namespace pe { std::string NameOf(UE::UObject* a) { return a->name; } }
namespace logger { template <class... A> void info(A&&...) {} template <class... A> void warn(A&&...) {} }
namespace ui {
struct Call {
	UE::UObject* obj; UE::UObject* act = nullptr;
	Call(UE::UObject* o, const wchar_t*) : obj(o) {}
	explicit operator bool() const { return true; }
	void Set(const char*, UE::UObject* a) { act = a; }
	void Run() { auto* c = static_cast<Ctx*>(obj); std::erase_if(c->backing, [&](const Entry& e) { return e.action == act; }); c->Sync(); }
};
}
template <class K, class T, class V> struct FunctionClassCache { template <class F> V Get(K, T, F f) { return f(); } };
struct KB { bool enabled = true, replaceQuickKeys = true; };
struct S { bool ammoWheel = true; KB keyboard; };
namespace settings { S& Get() { static S s; return s; } }
bool g_menu = false;
namespace menus { bool AnyOpen() { return g_menu; } }
namespace controls {
struct MappingLayout { std::int32_t size = sizeof(Entry), action = 0, key = 8; };
MappingLayout Mappings() { return {}; }
bool g_setUp = true;
UE::UObject g_actionObj;
UE::UObject* g_action = &g_actionObj;
std::uint64_t g_ticks = 0;
bool g_wasInMenu = true;
void Setup() {}
void Follow() {}
int g_rebuilds = 0;
void RebuildMappings() { ++g_rebuilds; }
''' + block + tick + r'''
}
int failures = 0;
void Check(bool a_ok, const char* a_what) { std::printf("%s: %s\n", a_ok ? "PASS" : "FAIL", a_what); if (!a_ok) ++failures; }
int main() {
	UE::UObject quick1{ 10, "IA_Game_QuickKeys_Keyboard_1" }, other{ 11, "IA_Game_Default_Jump" };
	Ctx ctx; ctx.internalIndex = 5; ctx.name = "IMC_Game_QuickKeys";
	g_off = (std::int32_t)(reinterpret_cast<std::uint8_t*>(&ctx.arr) - reinterpret_cast<std::uint8_t*>(&ctx));
	ctx.backing = { { &quick1, 1 }, { &other, 2 } }; ctx.Sync();
	g_all = { &ctx };

	controls::Tick();   // start: back in gameplay
	Check(ctx.backing.size() == 1, "the start takes the quick key off its key");
	const int afterStart = g_fullScans;
	for (int i = 0; i < 300; ++i) controls::Tick();   // about 60 s of gameplay
	std::printf("full object-table scans in 300 gameplay ticks: %d\n", g_fullScans - afterStart);
	Check(g_fullScans - afterStart == 0, "no full object-table scan while staying in gameplay");

	ctx.backing.push_back({ &quick1, 1 }); ctx.Sync();   // the game maps the quick key again
	for (int i = 0; i < 25; ++i) controls::Tick();
	Check(ctx.backing.size() == 1, "a quick key mapped again is taken off within ~5 s");

	g_menu = true; controls::Tick(); g_menu = false;
	const int beforeClose = g_fullScans;
	controls::Tick();   // back in gameplay after a menu
	Check(g_fullScans == beforeClose + 1, "returning to gameplay runs one full scan");
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
