"""The highlighted row must not survive its row being deselected or recycled (code review 2026-11-06, finding 3).

Rows are recycled on scroll: every visible row gets BP_OnEntryReleased, then OnListItemObjectSet with another item
(KB docs/game-internals.md, alchemy row events, build 1.512.105.0). A highlight kept from BP_OnItemSelectionChanged(true)
then points at a row now showing a different item, so assign/favourite/drop-block would act on the wrong item. The real
rows::OnRow is compiled with stubs and fed the event sequences.
"""
from pathlib import Path
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]
s = (root / 'src/game/Rows.cpp').read_text(encoding='utf-8').replace('\r\n', '\n')
start = s.index('\t\tvoid OnRow(Kind& a_kind')
op = s.index('{', start)
end, depth = op + 1, 1
while depth:
    depth += (s[end] == '{') - (s[end] == '}')
    end += 1
body = s[start:end]

program = r'''
#include <cstdint>
#include <cstdio>
#include <string>
#include <unordered_map>
namespace UE {
struct UFunction { std::string name; };
struct UObject { std::int32_t internalIndex = 0; };
}
namespace pe { const std::string& FunctionName(UE::UFunction* a_fn) { return a_fn->name; } }
namespace reflect {
struct Handle { UE::UObject* ptr = nullptr; std::int32_t index = -1; };
Handle Hold(UE::UObject* a) { return { a, a ? a->internalIndex : -1 }; }
UE::UObject* Get(const Handle& h) { return h.ptr; }
}
namespace rows {
struct Kind {
	reflect::Handle highlighted;
	UE::UFunction* fnSelection = nullptr;
	UE::UFunction* fnReleased = nullptr;
	std::unordered_map<UE::UObject*, std::int32_t> seen;
};
std::uint64_t g_rowGeneration = 0;
bool g_dirty = false;
''' + body + r'''
}
int failures = 0;
void Check(bool a_ok, const char* a_what) { std::printf("%s: %s\n", a_ok ? "PASS" : "FAIL", a_what); if (!a_ok) ++failures; }
UE::UFunction sel{ "BP_OnItemSelectionChanged" }, rel{ "BP_OnEntryReleased" }, set{ "OnListItemObjectSet" };
void Fire(rows::Kind& k, UE::UObject* row, UE::UFunction* fn, bool on = false) { rows::OnRow(k, row, fn, &on); }
int main() {
	UE::UObject a{ 1 }, b{ 2 };
	{
		rows::Kind k;
		Fire(k, &a, &sel, true);
		Check(reflect::Get(k.highlighted) == &a, "selection(true) highlights the row");
		Fire(k, &a, &rel);
		Fire(k, &a, &set);
		Check(reflect::Get(k.highlighted) == nullptr, "a recycled highlighted row (released, new item) is no longer highlighted");
	}
	{
		rows::Kind k;
		Fire(k, &a, &sel, true);
		Fire(k, &a, &sel, false);
		Check(reflect::Get(k.highlighted) == nullptr, "selection(false) on the highlighted row clears the highlight");
	}
	{
		rows::Kind k;
		Fire(k, &a, &sel, true);
		Fire(k, &b, &sel, true);
		Fire(k, &a, &sel, false);   // the old row's deselect arrives after the new row's select
		Check(reflect::Get(k.highlighted) == &b, "deselecting another row keeps the new highlight");
		Fire(k, &a, &rel);
		Check(reflect::Get(k.highlighted) == &b, "releasing another row keeps the highlight");
	}
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
