"""A repair-hammer use in flight must not carry over a save change (code review 2026-11-06, finding 8).

HammerInvStart opens the Inventory and HammerInvTick equips the hammer once it is up (up to ~3.5 s later). ObserveContext
reset the ordinary use flow (g_use) on a context change (a load, a quick-load) but not the hammer flow (g_hi), so the
hammer could be equipped in the newly loaded save. The real ObserveContext and HammerInv are compiled with stubs.
"""
from pathlib import Path
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]
w = (root / 'src/wheels/Wheels.cpp').read_text(encoding='utf-8').replace('\r\n', '\n')
start = w.index('\tbool ObserveContext()')
op = w.index('{', start)
end, depth = op + 1, 1
while depth:
    depth += (w[end] == '{') - (w[end] == '}')
    end += 1
observe = w[start:end]
h = (root / 'src/wheels/WheelsInternal.h').read_text(encoding='utf-8').replace('\r\n', '\n')
hs = h.index('\tstruct HammerInv')
hammer = h[hs:h.index('};', hs) + 2]

program = r'''
#include <windows.h>
#include <atomic>
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>
namespace inventory { bool Ready() { return true; } }
namespace sessionepoch { struct Token { int v = 0; struct { bool ready = true; } context; bool operator!=(const Token& o) const { return v != o.v; } }; }
namespace sessioncontext { sessionepoch::Token g_now; sessionepoch::Token Current() { return g_now; } }
namespace wheels::detail {
struct Resettable { void Reset() {} };
struct Use { bool on = false; };
std::atomic<std::uint64_t> g_generation{ 0 };
Use g_pending, g_use;
int g_holdKey = -1; std::uint32_t g_holdItem = 0;
std::vector<int> g_leaving;
ULONGLONG g_hudCheckAt = 0; void* g_hudIconBefore = nullptr;
std::string g_loadedFor, g_loadCandidate;
ULONGLONG g_loadCandidateAt = 0, g_storageRetryAt = 0;
Resettable g_emptySetup;
bool g_initializeEmpty = false, g_identityHeld = false, g_sanitized = false;
void ResetCarriedHistory() {}
''' + hammer + r'''
HammerInv g_hi;
''' + observe + r'''
}
int failures = 0;
void Check(bool a_ok, const char* a_what) { std::printf("%s: %s\n", a_ok ? "PASS" : "FAIL", a_what); if (!a_ok) ++failures; }
int main() {
	using namespace wheels::detail;
	ObserveContext();   // first sight of this save
	g_hi.on = true; g_hi.stage = 1; g_hi.id = 0xC;
	ObserveContext();   // same save: the flow goes on
	Check(g_hi.on, "the hammer flow survives a tick in the same save");
	sessioncontext::g_now.v = 2;   // a quick-load
	ObserveContext();
	Check(!g_hi.on && g_hi.id == 0, "a save change drops the hammer flow in flight");
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
