"""Compile the actual overlay tick against a counted message-discovery stub."""
from pathlib import Path
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]
source = (root / 'src/overlays/MagicWheel.cpp').read_text(encoding='utf-8')
start = source.index('\tvoid Tick()')
end = source.index('\n\tbool IsSpellKey', start)
tick = source[start:end]
program = r'''
#include <cstdio>
namespace UE { struct UObject {}; }
namespace menus { enum class Menu { kNone, kInventory, kMagic }; Menu menu=Menu::kNone; Menu Active(){return menu;} }
namespace wheels { enum class Wheel { kEquipment, kMagic, kAmmo }; Wheel active=Wheel::kEquipment; Wheel Active(){return active;} Wheel AssignmentWheel(menus::Menu){return active;} }
UE::UObject widget;
namespace quickkeys {
bool radial=false, panel=false, message=false; int calls=0;
bool GameMessageUp(bool){++calls;return message;}
bool RadialOpen(){return radial;} bool MagicPanelUp(){return panel;} bool PanelOpen(){return panel;}
UE::UObject* VisibleWheel(){return &widget;}
}
namespace slotcounts { int shown=0; void Hide(){} void Show(UE::UObject*, wheels::Wheel=wheels::Wheel::kEquipment){++shown;} }
namespace ringoverlay { enum class Kind { kEquipment, kMagic }; void Hide(){} void Show(UE::UObject*,Kind,bool=false){} }
void Show(UE::UObject*,int){} void Hide(const char*){} void HideSpellKeys(UE::UObject*){}
UE::UObject* GameImage(UE::UObject*){return &widget;}
''' + tick + r'''
int main(){
Tick(); if(quickkeys::calls){std::puts("FAIL: idle gameplay must not discover messages");return 1;}
quickkeys::radial=true; Tick();
if(quickkeys::calls!=1 || slotcounts::shown!=1){std::puts("FAIL: visible wheel must check messages and show counts");return 1;}
quickkeys::message=true; Tick();
if(quickkeys::calls!=2 || slotcounts::shown!=1){std::puts("FAIL: a message must suppress slot counts");return 1;}
quickkeys::radial=false; quickkeys::panel=true; menus::menu=menus::Menu::kInventory; Tick();
if(quickkeys::calls!=3){std::puts("FAIL: assign panel must retain the message gate");return 1;}
std::puts("PASS: idle message scans skipped; radial and panel message gates preserved");
}
'''
with tempfile.TemporaryDirectory() as folder:
    folder = Path(folder)
    (folder/'test.cpp').write_text(program, encoding='utf-8')
    (folder/'build.bat').write_text('@echo off\ncall F:\\modbuild\\tools\\msvc\\setup_x64.bat >nul\ncl /nologo /EHsc /std:c++20 test.cpp /Fe:test.exe\n', encoding='utf-8')
    subprocess.run(['cmd','/c',str(folder/'build.bat')],cwd=folder,check=True)
    subprocess.run([str(folder/'test.exe')],cwd=folder,check=True)
