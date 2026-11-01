"""Reuse the actual-source use harness; compare apparatus mouse and shortcut routing."""
import pathlib
import runpy
import subprocess
import tempfile

fixture = runpy.run_path(str(pathlib.Path(__file__).with_name('iwp036_soulgem_regression.py')))
program = fixture['program'].split('int main() {', 1)[0]
program += r'''
int main() {
    inventory::keys[0] = 500;
    g_wheels[0][0].entries[0] = 500;
    QuickUse(0);
    if (equipped != 1 || inventoryStarts || quickPresses) { std::puts("FAIL: shortcut baseline"); return 1; }
    equipped = 0;
    UseNow(0, "click", false);
    if (!g_use.direct || !g_use.waitClose || equipped || inventoryStarts || quickPresses) {
        std::puts("FAIL: apparatus mouse click must defer shortcut action until radial closes"); return 1;
    }
    quickkeys::radial = true; DriveUse();
    if (equipped || inventoryStarts || quickPresses) { std::puts("FAIL: apparatus used before radial closes"); return 1; }
    quickkeys::radial = false;
    g_use.closed = true;
    g_use.closedAt = std::chrono::steady_clock::now() - 200ms;
    DriveUse();
    if (equipped != 1 || inventoryStarts || quickPresses) {
        std::puts("FAIL: mouse must use same item action as shortcut, without quick key or Inventory workaround"); return 1;
    }
    if (StaysOpenAfterUse(0)) { std::puts("FAIL: alchemy menu must close radial"); return 1; }
    std::puts("PASS: apparatus mouse/shortcut action parity and radial-close deferral");
    return 0;
}
'''
with tempfile.TemporaryDirectory() as tmp:
    tmp = pathlib.Path(tmp)
    cpp = tmp / 'iwp037.cpp'
    cpp.write_text(program, encoding='utf-8')
    batch = tmp / 'build.bat'
    batch.write_text('@echo off\ncall F:\\modbuild\\tools\\msvc\\setup_x64.bat >nul\n'
                     f'cl /nologo /EHsc /std:c++17 "{cpp}" /Fe:"{tmp / "test.exe"}" /Fo:"{tmp / "test.obj"}"\n', encoding='utf-8')
    subprocess.run(['cmd', '/c', str(batch)], check=True, cwd=tmp)
    subprocess.run([str(tmp / 'test.exe')], check=True)
