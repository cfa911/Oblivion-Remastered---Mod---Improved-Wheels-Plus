"""Repeated watched events must not convert UFunction names repeatedly."""
from pathlib import Path
import subprocess
import tempfile

root=Path(__file__).resolve().parents[1]
source=(root/'src/engine/PEHook.cpp').read_text(encoding='utf-8')
start=source.index('FunctionName(UE::UFunction*')
start=source.rfind('\n',0,start)+1
end=source.index('\n\tstatic bool WatchImpl',start)
body=source[start:end]
helper=root/'src/core/FunctionClassCache.h'
program=r'''
#include <string>
#include <cstdint>
#include <cstdio>
int conversions=0;
namespace UE {
struct FName { std::uint64_t value=1; std::uint64_t ToUnstableInt()const{return value;} std::string ToString()const{++conversions;return value==1?"Tick":"BP_OnClicked";} };
struct UFunction { int internalIndex=1; FName name; FName GetFName(){return name;} };
}
namespace perfdiag { enum Counter{kFnName}; void Add(Counter){} }
std::string Utf8(std::string s){return s;}
''' + (f'#include "{helper.as_posix()}"\n' if helper.exists() else '') + body + r'''
int main(){
UE::UFunction fn;
if(FunctionName(&fn)!="Tick")return 1;
auto n=conversions;
if(FunctionName(&fn)!="Tick" || conversions!=n){std::puts("FAIL: repeated irrelevant event converts its name again");return 1;}
fn.internalIndex=2; fn.name.value=2;
if(FunctionName(&fn)!="BP_OnClicked"){std::puts("FAIL: recycled function identity must invalidate");return 1;}
if(!FunctionName(nullptr).empty())return 1;
std::puts("PASS: function names cached including irrelevant events; identity changes invalidate");
}
'''
with tempfile.TemporaryDirectory() as folder:
    folder=Path(folder)
    (folder/'test.cpp').write_text(program,encoding='utf-8')
    (folder/'build.bat').write_text('@echo off\ncall F:\\modbuild\\tools\\msvc\\setup_x64.bat >nul\ncl /nologo /EHsc /std:c++20 test.cpp /Fe:test.exe\n',encoding='utf-8')
    subprocess.run(['cmd','/c',str(folder/'build.bat')],cwd=folder,check=True)
    subprocess.run([str(folder/'test.exe')],cwd=folder,check=True)
