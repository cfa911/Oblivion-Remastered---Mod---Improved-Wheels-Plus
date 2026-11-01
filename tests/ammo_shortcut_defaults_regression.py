"""Verify actual compiled defaults/INI fallback and preservation of explicit bindings."""
from pathlib import Path
import configparser
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]
source = (root / 'src/core/Settings.cpp').read_text(encoding='utf-8')
start = source.index('int ReadInt(')
end = source.index('\n\t\tstd::wstring ReadStr', start)
reader = source[start:end]
assignment = next(line for line in source.splitlines() if 'v.ammoButton = ReadInt' in line)
program = r'''
#define NOMINMAX
#include <Windows.h>
#include <algorithm>
#include <array>
#include <filesystem>
#include <fstream>
#include <string>
#include <cstdio>
''' + f'#include "{(root / "src/core/Settings.h").as_posix()}"\n' + reader + r'''
int load(const std::filesystem::path& ini) {
    settings::Values v;
''' + assignment + r'''
    return v.ammoButton;
}
int main() {
    if (settings::Values{}.ammoButton != 0) { std::puts("FAIL: compiled Ammo shortcut default must be disabled"); return 1; }
    const auto ini = std::filesystem::absolute("test.ini");
    if (load(ini) != 0) { std::puts("FAIL: absent INI fallback"); return 1; }
    { std::ofstream out(ini); out << "[AmmoWheel]\nbEnabled=1\n"; }
    if (load(ini) != 0) { std::puts("FAIL: missing key fallback"); return 1; }
    { std::ofstream out(ini); out << "[AmmoWheel]\nuButton=8\n"; }
    if (load(ini) != 8) { std::puts("FAIL: explicit D-pad Right binding must remain usable"); return 1; }
    { std::ofstream out(ini); out << "[AmmoWheel]\nuButton=0\n"; }
    if (load(ini) != 0) { std::puts("FAIL: explicit disabled binding"); return 1; }
    std::puts("PASS: Ammo shortcut defaults/fallback disabled; explicit binding preserved");
}
'''
with tempfile.TemporaryDirectory() as temporary:
    tmp = Path(temporary)
    cpp = tmp / 'defaults.cpp'
    cpp.write_text(program, encoding='utf-8')
    batch = tmp / 'build.bat'
    batch.write_text('@echo off\ncall F:\\modbuild\\tools\\msvc\\setup_x64.bat >nul\n'
                     f'cl /nologo /EHsc /std:c++17 "{cpp}" /Fe:"{tmp / "test.exe"}" /Fo:"{tmp / "test.obj"}"\n', encoding='utf-8')
    subprocess.run(['cmd', '/c', str(batch)], cwd=tmp, check=True)
    subprocess.run([str(tmp / 'test.exe')], cwd=tmp, check=True)
ini = configparser.ConfigParser()
ini.read(root / 'dist/OblivionRemastered/Binaries/Win64/OBSE/Plugins/ImprovedWheelMenu.ini')
assert ini.getint('AmmoWheel', 'uButton') == 0, 'Shipped shortcut must default to disabled'
assert ini.getint('AmmoWheel', 'bEnabled') == 1, 'Ammo wheel must remain enabled'
print('PASS: shipped INI disables only the shortcut')
