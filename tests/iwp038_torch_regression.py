"""Real use-routing regression: torch equip must not race the native radial close."""
from pathlib import Path
import runpy
import subprocess
import tempfile

fixture = runpy.run_path(str(Path(__file__).with_name('iwp036_soulgem_regression.py')))
program = fixture['program'].split('int main() {', 1)[0] + r'''
int main() {
    for (bool wasWorn : {true, false}) for (bool stayRequested : {false, true}) {
        inventory::worn = wasWorn;
        inventory::keys[0] = 600;
        equipped = toggled = unequipped = inventoryStarts = quickPresses = 0; heldSlot = -1;
        quickkeys::radial = true;
        UseNow(0, "close", stayRequested);
        if (equipped || toggled || unequipped || !g_use.direct || !g_use.waitClose || heldSlot != 0 || inventory::keys[0]) {
            std::puts("FAIL: torch must defer equip and hold native slot empty while radial closes"); return 1;
        }
        DriveUse();
        if (equipped || toggled || unequipped || quickPresses || inventoryStarts) { std::puts("FAIL: torch used with radial still open"); return 1; }
        quickkeys::radial = false;
        g_use.closed = true; g_use.closedAt = std::chrono::steady_clock::now();
        DriveUse();
        if (equipped || toggled || unequipped) { std::puts("FAIL: torch used before close settled"); return 1; }
        // The existing close handler restores the native key before the delayed use.
        inventory::keys[0] = 600;
        g_use.closedAt -= 200ms;
        DriveUse(); DriveUse();
        if (toggled != 1 || equipped || unequipped || quickPresses || inventoryStarts) { std::puts("FAIL: settled torch must resolve equip/unequip on TES thread exactly once"); return 1; }
        if (StaysOpenAfterUse(0)) { std::puts("FAIL: torch must close even with stay-open option"); return 1; }
    }
    inventory::worn = false;
    equipped = 0; heldSlot = -1;
    inventory::keys[0] = 200;
    UseNow(0, "A", true);
    if (equipped != 1 || heldSlot != -1 || g_use.waitClose) { std::puts("FAIL: ordinary equipment behavior changed"); return 1; }
    g_wheels[0][0].entries[0] = 600;
    equipped = toggled = 0; heldSlot = -1;
    QuickUse(0);
    if (toggled != 1 || equipped || heldSlot != -1) { std::puts("FAIL: closed-wheel torch shortcut changed"); return 1; }
    std::puts("PASS: torch use waits for close/settling, suppresses native duplicate, preserves equipment/shortcut paths");
}
'''
with tempfile.TemporaryDirectory() as temporary:
    tmp = Path(temporary)
    cpp = tmp / 'iwp038.cpp'
    cpp.write_text(program, encoding='utf-8')
    batch = tmp / 'build.bat'
    batch.write_text('@echo off\ncall F:\\modbuild\\tools\\msvc\\setup_x64.bat >nul\n'
                     f'cl /nologo /EHsc /std:c++17 "{cpp}" /Fe:"{tmp / "test.exe"}" /Fo:"{tmp / "test.obj"}"\n', encoding='utf-8')
    subprocess.run(['cmd', '/c', str(batch)], cwd=tmp, check=True)
    subprocess.run([str(tmp / 'test.exe')], check=True)
