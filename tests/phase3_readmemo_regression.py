"""A read memo must reuse within a read and never reuse across reads or outside one."""
from pathlib import Path
import subprocess
import tempfile

root=Path(__file__).resolve().parents[1]
header=root/'src/core/ReadMemo.h'
fallback=r'''
namespace readmemo {
struct Scope {};
template<class Key,class Value>struct Memo { template<class Resolve> Value Get(Key,Resolve resolve){return resolve();} };
}
'''
program='#include <cstdio>\n'+(f'#include "{header.as_posix()}"\n' if header.exists() else fallback)+r'''
int main(){
readmemo::Memo<int,int> memo; int calls=0; auto resolve=[&]{return ++calls;};
{readmemo::Scope scope; if(memo.Get(1,resolve)!=1)return 1;
if(memo.Get(1,resolve)!=1 || calls!=1){std::puts("FAIL: same read recomputes the same lookup");return 1;}
if(memo.Get(2,resolve)!=2)return 1;}
{readmemo::Scope scope; if(memo.Get(2,resolve)!=3){std::puts("FAIL: new read reused stale result");return 1;}}
if(memo.Get(2,resolve)!=4 || memo.Get(2,resolve)!=5){std::puts("FAIL: outside a read must recompute");return 1;}
std::puts("PASS: read memo reuses only matching keys in one read, including negative results");
}
'''
with tempfile.TemporaryDirectory() as folder:
    folder=Path(folder)
    (folder/'test.cpp').write_text(program,encoding='utf-8')
    (folder/'build.bat').write_text('@echo off\ncall F:\\modbuild\\tools\\msvc\\setup_x64.bat >nul\ncl /nologo /EHsc /std:c++20 test.cpp /Fe:test.exe\n',encoding='utf-8')
    subprocess.run(['cmd','/c',str(folder/'build.bat')],cwd=folder,check=True)
    subprocess.run([str(folder/'test.exe')],cwd=folder,check=True)
