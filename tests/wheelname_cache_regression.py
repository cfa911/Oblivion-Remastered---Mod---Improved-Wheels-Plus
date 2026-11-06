"""The wheel's centre name must not call the engine's string table on every update (code review 2026-11-06, finding 16).

UpdateDetails runs every 100 ms and on pointer moves while a wheel is open; Resolve turned a LOC_ key into its text through
KismetTextLibrary::TextFromStringTable (a ProcessEvent call) each time. A key's text is the same for the session, so it is
looked up once. The real Resolve is compiled with a stub engine call that counts the lookups.
"""
from pathlib import Path
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]
s = (root / 'src/overlays/WheelName.cpp').read_text(encoding='utf-8').replace('\r\n', '\n')
start = s.index('\t\tstd::string Resolve(const std::string& a_text)')
op = s.index('{', start)
end, depth = op + 1, 1
while depth:
    depth += (s[end] == '{') - (s[end] == '}')
    end += 1
resolve = s[start:end]

program = r'''
#include <cstdio>
#include <new>
#include <string>
#include <unordered_map>
int g_lookups = 0;
namespace UE {
enum class EFindName { Find, Add };
struct FName { FName(const wchar_t*, EFindName) {} };
struct FString { std::wstring s; FString(const wchar_t* a) : s(a) {} };
struct FText { std::string text; };
struct UObject { UObject* GetDefaultObject(bool) { return this; } };
}
namespace pe { std::wstring Wide(const std::string& a) { return std::wstring(a.begin(), a.end()); } }
namespace reflect { std::string Text(const UE::FText& a) { return a.text; } }
namespace ui {
UE::UObject g_lib;
UE::UObject* Class(const wchar_t*) { return &g_lib; }
struct Call {
	alignas(8) unsigned char buf[256]{};
	Call(UE::UObject*, const wchar_t*) {}
	void* At(const char* a) { std::string n = a; return buf + (n == "TableId" ? 0 : n == "Key" ? 16 : 64); }
	void Run() {
		++g_lookups;
		auto* key = reinterpret_cast<UE::FString*>(buf + 16);
		new (buf + 64) UE::FText{ key->s == L"LOC_FN_IronSword" ? "Iron Longsword" : "" };
	}
};
}
constexpr const wchar_t* kFullNames = L"FullNames";
namespace wheelname {
''' + resolve + r'''
}
int failures = 0;
void Check(bool a_ok, const char* a_what) { std::printf("%s: %s\n", a_ok ? "PASS" : "FAIL", a_what); if (!a_ok) ++failures; }
int main() {
	Check(wheelname::Resolve("LOC_FN_IronSword") == "Iron Longsword", "a key resolves to its text");
	for (int i = 0; i < 20; ++i) wheelname::Resolve("LOC_FN_IronSword");
	std::printf("engine lookups for 21 updates of one name: %d\n", g_lookups);
	Check(g_lookups == 1, "the same key is looked up in the engine once");
	Check(wheelname::Resolve("Fire Bolt") == "Fire Bolt" && g_lookups == 1, "a plain name needs no lookup");
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
