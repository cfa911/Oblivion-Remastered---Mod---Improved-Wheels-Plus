"""Exercise actual EnsureLoaded maintenance, including a same-read generation change."""
from pathlib import Path
import subprocess
import tempfile

root=Path(__file__).resolve().parents[1]
source=(root/'src/wheels/Model.cpp').read_text(encoding='utf-8')
start=source.index('\tstatic bool EnsureLoadedNow') if '\tstatic bool EnsureLoadedNow' in source else source.index('\tbool EnsureLoaded')
body=source[start:source.rfind('\n}')]
program=r'''
#include <string>
#include <atomic>
#include <cstdint>
#include <cstdio>
''' + f'#include "{(root / "src/core/ReadMemo.h").as_posix()}"\n' + r'''
using ULONGLONG=unsigned long long;
ULONGLONG GetTickCount64(){return 10000;}
namespace RE {struct PlayerCharacter {static PlayerCharacter* GetSingleton(){static PlayerCharacter p;return &p;}};}
namespace inventory {bool Ready(){return true;} std::string PlayerName(){return "Hero";} bool ClearAllNativeKeys(){return true;}}
namespace rows {int PlayerSpellCount(){return 1;}}
namespace quickkeys {void RequestRedraw(){}}
namespace testhread {bool Installed(){return false;} template<class F>void Post(F f){f();}}
namespace logger {void warn(const char*){}}
namespace perfdiag {enum Counter{kEnsureLoaded};void Add(Counter){}}
bool context=true; bool ObserveContext(){return context;}
std::string g_loadedFor="Hero";
bool g_sanitized=true, g_storageRejected=false, g_initializeEmpty=false;
std::atomic<std::uint32_t> g_generation{1};
ULONGLONG g_sanitizeAt=0;
struct Empty {bool Complete(){return true;} int Queue(ULONGLONG){return 0;} template<class F>void Run(int,F f){f();}}g_emptySetup;
void Load(const std::string&){} void Save(){} void Sanitize(){}
bool AssignPending(){return false;}
int reconciles=0;
void RemoveLeavers(){} void RemoveGoneInMenus(){} void Reconcile(){++reconciles;} void MigrateAmmo(){}
''' + body + r'''
int main(){
{readmemo::Scope scope;
if(!EnsureLoaded() || !EnsureLoaded() || reconciles!=1){std::puts("FAIL: unchanged queries repeat maintenance within a read");return 1;}
++g_generation; if(!EnsureLoaded() || reconciles!=2){std::puts("FAIL: a saved change must invalidate maintenance");return 1;}
context=false; if(EnsureLoaded()){std::puts("FAIL: unavailable context must bypass memo");return 1;} context=true;
}
{readmemo::Scope scope; if(!EnsureLoaded() || reconciles!=3){std::puts("FAIL: each read must observe external inventory changes");return 1;}}
std::puts("PASS: maintenance coalesced per read/generation; context and next-read changes remain live");
}
'''
with tempfile.TemporaryDirectory() as folder:
    folder=Path(folder)
    (folder/'test.cpp').write_text(program,encoding='utf-8')
    (folder/'build.bat').write_text('@echo off\ncall F:\\modbuild\\tools\\msvc\\setup_x64.bat >nul\ncl /nologo /EHsc /std:c++20 test.cpp /Fe:test.exe\n',encoding='utf-8')
    subprocess.run(['cmd','/c',str(folder/'build.bat')],cwd=folder,check=True)
    subprocess.run([str(folder/'test.exe')],cwd=folder,check=True)
