"""Run the real settings loader and no-controller gate against the new single switch."""
from pathlib import Path
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]
keyboard = (root / 'src/input/Keyboard.cpp').read_text(encoding='utf-8')
start = keyboard.index('bool PresentWhenNoPad()')
end = keyboard.index('\n\tbool Apply(', start)
gate = keyboard[start:end]
program = r'''
#define NOMINMAX
#include <Windows.h>
#include <algorithm>
#include <array>
#include <filesystem>
#include <fstream>
#include <string>
#include <cwctype>
#include <cstdio>
namespace logger {
template<class... T> void info(T...) {}
template<class... T> void warn(T...) {}
}
''' + '#include "' + (root / 'src/core/Settings.cpp').as_posix() + '"\n' + gate + r'''
bool scenario(const char* ini, bool expected, const char* label) {
    const auto path = settings::PluginFolder() / L"ImprovedWheelMenu.ini";
    if (ini) { std::ofstream out(path); out << ini; }
    else std::filesystem::remove(path);
    WritePrivateProfileStringW(nullptr, nullptr, nullptr, path.c_str());
    settings::Load();
    if (settings::Get().keyboard.enabled != expected || PresentWhenNoPad() != expected) {
        std::printf("FAIL: %s\n", label);
        return false;
    }
    return true;
}
int main() {
    if (!scenario("[Keyboard]\nbKeyboardSupport=0\n", false, "new flag disables keyboard and disconnected-controller support")) return 1;
    if (!scenario("[Keyboard]\nbKeyboardSupport=1\nbVirtualPadWhenNone=0\n", true, "new flag enables support without a controller; obsolete flag cannot disable it")) return 1;
    if (!scenario("[Keyboard]\nbKeyboardSupport=0\nbVirtualPadWhenNone=1\n", false, "obsolete flag cannot bypass disabled keyboard support")) return 1;
    if (!scenario("[Keyboard]\nbKeyboardSupport=1\n", true, "keyboard support enabled")) return 1;
    if (!scenario("[Keyboard]\n", true, "missing key uses enabled default")) return 1;
    if (!scenario(nullptr, true, "missing INI uses enabled default")) return 1;
    std::puts("PASS: bKeyboardSupport controls keyboard and no-controller support together");
}
'''
with tempfile.TemporaryDirectory(prefix='iwp-keyboard-support-') as folder:
    tmp = Path(folder)
    (tmp / 'test.cpp').write_text(program, encoding='utf-8')
    batch = tmp / 'build.bat'
    batch.write_text(
        '@echo off\ncall F:\\modbuild\\tools\\msvc\\setup_x64.bat >nul\n'
        f'cl /nologo /EHsc /std:c++latest /I"{root / "src"}" test.cpp /Fe:test.exe\n',
        encoding='utf-8',
    )
    subprocess.run(['cmd', '/c', str(batch)], check=True, cwd=tmp)
    subprocess.run([str(tmp / 'test.exe')], check=True, cwd=tmp)
