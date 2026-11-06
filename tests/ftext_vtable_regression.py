"""reflect::TextSet must refuse an FText whose text object does not carry a vtable inside the game executable.

A freed or garbage ITextData reaches a pure virtual slot ("Pure virtual function being called"), which ends the process
and no __except can catch (KB docs/game-internals.md, RMI-022). The check reads the first qword under the AV guard and
accepts it only inside the executable image. This compiles the real Reflect.cpp guard block and TextSet with stubs; the
test executable plays the game's role as GetModuleHandleW(nullptr).
"""
from pathlib import Path
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]
source = (root / 'src/engine/Reflect.cpp').read_text(encoding='utf-8').replace('\r\n', '\n')

# the anonymous namespace holding Guarded (and the vtable helpers), up to the first function after it
start = source.index('\tnamespace\n\t{\n\t\t// A row the engine is tearing down')
end = source.index('\n\tstd::string Text(const UE::FText& a_text)', start)
guard_block = source[start:end]
ts_start = source.index('\tbool TextSet(const UE::FText& a_text)')
ts_end = source.index('\n\t}\n', ts_start) + len('\n\t}\n')
text_set = source[ts_start:ts_end]

program = r'''
#include <windows.h>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <string>
namespace UE {
struct FString {};
struct FText { void* data; void* controller; FString ToString() const { return {}; } };
struct UFunction;
struct UObject { void ProcessEvent(UFunction*, void*) {} };
}
namespace pe { std::string Utf8(const UE::FString&) { return {}; } }
namespace reflect {
''' + guard_block + '\n' + text_set + r'''
}
struct FakeTextData { std::uintptr_t vtable; };
static void Marker() {}
int failures = 0;
void Check(bool a_ok, const char* a_what) { std::printf("%s: %s\n", a_ok ? "PASS" : "FAIL", a_what); if (!a_ok) ++failures; }
int main() {
	static int controller = 1;
	UE::FText zero{ nullptr, nullptr };
	Check(!reflect::TextSet(zero), "zeroed FText is not set");

	FakeTextData good{ reinterpret_cast<std::uintptr_t>(&Marker) };
	UE::FText live{ &good, &controller };
	Check(reflect::TextSet(live), "text object with a vtable inside the executable is set");

	FakeTextData smallInts{ 0x8000000000ull };   // RMI-022: two small ints, not a pointer
	UE::FText garbage{ &smallInts, &controller };
	Check(!reflect::TextSet(garbage), "text object whose first qword is 0x8000000000 is refused");

	auto* heap = static_cast<FakeTextData*>(std::malloc(sizeof(FakeTextData)));
	heap->vtable = reinterpret_cast<std::uintptr_t>(heap);
	UE::FText freedLike{ heap, &controller };
	Check(!reflect::TextSet(freedLike), "text object whose vtable points into the heap is refused");
	std::free(heap);

	UE::FText unreadable{ reinterpret_cast<void*>(0x10), &controller };
	Check(!reflect::TextSet(unreadable), "unreadable text object is refused without a crash");
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
    result = subprocess.run([str(folder / 'test.exe')], cwd=folder)
    raise SystemExit(result.returncode)
