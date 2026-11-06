"""Exercise actual AddTo capacity behavior with depleted scroll assignments."""
from pathlib import Path
import tempfile, subprocess
root=Path(__file__).resolve().parents[1]
s=(root/'src/wheels/Assign.cpp').read_text()
def extract(signature):
 start=s.index(signature); op=s.index('{',start);end=op+1;depth=1
 while depth:
  depth+=(s[end]=='{')-(s[end]=='}');end+=1
 return s[start:end]
helper_name='ReclaimDepletedConsumable' if 'void ReclaimDepletedConsumable(' in s else 'ReclaimDepletedScroll'
helper=extract('void '+helper_name+'(')
program=r'''
#include <cstdint>
#include <vector>
#include <array>
#include <algorithm>
#include <cstdio>
struct Slot { std::vector<std::uint32_t> entries; int active=-1; };
std::array<std::array<Slot,8>,3> g_wheels;
constexpr int kEquip=0,kMagic=1;
int Cap(){return 5;}
const char* WheelName(int){return "Magic";}
const char* NameOf(int,std::uint32_t){return "entry";}
namespace logger { template<class...T>void info(T...){} }
namespace inventory {
bool Has(std::uint32_t id){return id!=999 && id!=200 && id!=201 && !(id>=300 && id<=304);}
bool IsScroll(std::uint32_t id){return id>=200 && id<=202;}
bool SetKey(std::uint32_t,int){return true;} bool KeyPending(std::uint32_t){return false;}
}
bool Consumable(std::uint32_t id){return id>=300 && id<=302;}
bool MiscTool(std::uint32_t id){return id==303 || id==304;}
void RemoveAt(int wheel,int slot,int index,const char*){
 auto& s=g_wheels[wheel][slot];s.entries.erase(s.entries.begin()+index);
 if(s.active>index)--s.active;else if(s.active==index)s.active=s.entries.empty()?-1:0;
}
''' +helper+'\n'+extract('bool AddTo(')+r'''
int main(){
 auto& slot=g_wheels[kMagic][0];
 slot={{100,200,101,201,202},4};
 if(!AddTo(kMagic,0,102,"assign")){std::puts("FAIL: depleted scroll blocks new assignment in full slot");return 1;}
 if(slot.entries!=std::vector<std::uint32_t>{100,101,201,202,102} || slot.active!=4)return 2;
 if(!AddTo(kMagic,0,103,"assign"))return 3;
 if(slot.entries!=std::vector<std::uint32_t>{100,101,202,102,103} || slot.active!=4)return 4;
 if(AddTo(kMagic,0,104,"assign"))return 5;
 // Unknown learned spells and unavailable equipment must remain stored.
 slot={{100,101,102,103,999},2};
 if(AddTo(kMagic,0,105,"assign") || slot.entries.size()!=5)return 6;
 g_wheels[kEquip][0]={{100,200,101,201,202},4};
 if(!AddTo(kEquip,0,103,"assign")){std::puts("FAIL: spent Inventory scroll blocks assignment");return 7;}
 for(std::uint32_t spent=300;spent<=304;++spent){
  g_wheels[kEquip][0]={{100,spent,101,102,202},4};
  if(!AddTo(kEquip,0,103,"assign")){std::puts("FAIL: spent Inventory consumable blocks assignment");return 8;}
  if(g_wheels[kEquip][0].entries!=std::vector<std::uint32_t>{100,101,102,202,103})return 9;
 }
 g_wheels[kEquip][0]={{100,101,102,103,999},4};
 if(AddTo(kEquip,0,105,"assign"))return 10;
 g_wheels[kEquip][0]={{100,300},0};
 if(!AddTo(kEquip,0,103,"assign") || g_wheels[kEquip][0].entries!=std::vector<std::uint32_t>{100,300,103})return 11;
 std::puts("PASS: full slots reuse depleted Inventory consumables and Magic scrolls; other assignments preserved");
}
'''
with tempfile.TemporaryDirectory(prefix='iwp-scroll-capacity-') as folder:
 p=Path(folder);(p/'test.cpp').write_text(program)
 (p/'build.bat').write_text('@echo off\ncall F:\\modbuild\\tools\\msvc\\setup_x64.bat >nul\ncl /nologo /EHsc /std:c++latest test.cpp /Fe:test.exe\n')
 subprocess.run(['cmd','/c',str(p/'build.bat')],cwd=p,check=True)
 subprocess.run([str(p/'test.exe')],check=True)
