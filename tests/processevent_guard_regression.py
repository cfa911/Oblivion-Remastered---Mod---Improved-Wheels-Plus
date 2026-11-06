"""ui::GuardedProcessEvent must catch access violations only (code review 2026-11-06, finding 4).

A catch-all __except swallows every SEH exception and unwinds engine frames without their destructors, which can leave
engine locks held (hang). reflect::Guarded already filters to EXCEPTION_ACCESS_VIOLATION; this does the same. The real
function is compiled with a stub UObject whose ProcessEvent raises the chosen exception.
"""
from pathlib import Path
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]
s = (root / 'src/engine/Ui.cpp').read_text(encoding='utf-8').replace('\r\n', '\n')
start = s.index('\tbool GuardedProcessEvent(')
op = s.index('{', start)
end, depth = op + 1, 1
while depth:
    depth += (s[end] == '{') - (s[end] == '}')
    end += 1
body = s[start:end]

program = r'''
#include <windows.h>
#include <cstdio>
namespace UE {
struct UFunction {};
DWORD g_raise = 0;
struct UObject {
	void ProcessEvent(UFunction*, void*) {
		if (g_raise == EXCEPTION_ACCESS_VIOLATION) { volatile int* p = nullptr; *p = 1; }
		else if (g_raise) RaiseException(g_raise, 0, 0, nullptr);
	}
};
}
namespace ui {
''' + body + r'''
}
int failures = 0;
void Check(bool a_ok, const char* a_what) { std::printf("%s: %s\n", a_ok ? "PASS" : "FAIL", a_what); if (!a_ok) ++failures; }
// 1 = returned true, 0 = returned false, 2 = the exception went past the guard
int Outer(DWORD a_code)
{
	UE::g_raise = a_code;
	UE::UObject obj; UE::UFunction fn;
	__try {
		return ui::GuardedProcessEvent(&obj, &fn, nullptr) ? 1 : 0;
	} __except (EXCEPTION_EXECUTE_HANDLER) {
		return 2;
	}
}
int main() {
	Check(Outer(0) == 1, "a normal call returns true");
	Check(Outer(EXCEPTION_ACCESS_VIOLATION) == 0, "an access violation is caught and returns false");
	Check(Outer(0xE0000001) == 2, "any other exception is not swallowed by the guard");
	Check(Outer(EXCEPTION_INT_DIVIDE_BY_ZERO) == 2, "a divide-by-zero is not swallowed by the guard");
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
