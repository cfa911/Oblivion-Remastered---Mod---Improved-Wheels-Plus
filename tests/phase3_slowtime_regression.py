"""Idle slow time avoids reflected calls but still restores on wheel close."""
from pathlib import Path
import subprocess
import tempfile

root=Path(__file__).resolve().parents[1]
source=(root/'src/slowtime/SlowTime.cpp').read_text(encoding='utf-8')
body=source[source.index('\tvoid Tick()'):source.rfind('\n}')]
program=r'''
#include <cstdint>
#include <string_view>
#include <type_traits>
#include <cstdio>
''' + f'#include "{(root/"src/slowtime/SlowTimeLease.h").as_posix()}"\n' + r'''
using ULONGLONG=unsigned long long;
ULONGLONG GetTickCount64(){static ULONGLONG now=0;return now+=100;}
namespace settings {struct Values{bool slowTime=true;int slowTimePercent=10;};Values& Get(){static Values v;return v;}}
namespace UE {struct UObject{UObject* GetDefaultObject(bool){return this;}};using UClass=UObject;struct UWorld:UObject{static UWorld* GetSingleton(){static UWorld w;return &w;}};}
namespace reflect {using Handle=UE::UObject*;bool Ok(){return true;} UE::UObject* Get(Handle h){return h;}Handle Hold(UE::UObject* p){return p;}}
namespace RE {
struct TESDataHandler {bool saveLoadGame=false,clearingData=false,loadingFiles=false;static TESDataHandler* GetSingleton(){static TESDataHandler d;return &d;}};
struct PlayerCharacter{bool deadFlag=false;static PlayerCharacter* GetSingleton(){static PlayerCharacter p;return &p;}};
struct InterfaceManager{int menuMode=1;static InterfaceManager* GetInstance(bool,bool){static InterfaceManager i;return &i;}};
}
namespace quickkeys {bool open=false;bool RadialOpen(){return open;}bool PanelOpen(){return false;}unsigned OpenCount(){return 1;}}
namespace ammo {bool Open(){return false;}}
namespace logger {template<class...A>void warn(const char*,A...){}template<class...A>void info(const char*,A...){} }
namespace perfdiag {enum Counter{kSlowTime};void Add(Counter){} }
namespace ui {
int finds=0; float dilation=1;
UE::UObject* Class(const wchar_t*){++finds;static UE::UObject c;return &c;}
struct Call{
bool setting;float value=1;
Call(UE::UObject*,const wchar_t* s):setting(std::wstring_view(s)==L"SetGlobalTimeDilation"){}
operator bool()const{return true;}
int Size(const char* name){return std::string_view(name)=="WorldContextObject"?sizeof(void*):sizeof(float);}
template<class T>bool Set(const char*,T v){if constexpr(std::is_same_v<T,float>)value=v;return true;}
bool RunGuarded(){if(setting)dilation=value;return true;}
template<class T>T Get(const char*){return static_cast<T>(dilation);}
};
}
namespace slowtime {CloseGate g_close;ULONGLONG g_next=0;
''' + body + r'''
}
int main(){
slowtime::Tick();if(ui::finds){std::puts("FAIL: idle slow time still performs reflection");return 1;}
quickkeys::open=true;slowtime::Tick();if(ui::dilation!=0.1f){std::puts("FAIL: opening must apply slowdown");return 1;}
quickkeys::open=false;slowtime::Tick();if(ui::dilation!=1){std::puts("FAIL: close must restore time");return 1;}
auto n=ui::finds;slowtime::Tick();if(ui::finds!=n)return 1;
std::puts("PASS: idle skips reflection; opening applies and closing restores slowdown");
}
'''
with tempfile.TemporaryDirectory() as folder:
    folder=Path(folder)
    (folder/'test.cpp').write_text(program,encoding='utf-8')
    (folder/'build.bat').write_text('@echo off\ncall F:\\modbuild\\tools\\msvc\\setup_x64.bat >nul\ncl /nologo /EHsc /std:c++20 test.cpp /Fe:test.exe\n',encoding='utf-8')
    subprocess.run(['cmd','/c',str(folder/'build.bat')],cwd=folder,check=True)
    subprocess.run([str(folder/'test.exe')],cwd=folder,check=True)
