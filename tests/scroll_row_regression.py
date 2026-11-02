"""Exercise the actual guarded native-scroll-list resolver with synthetic native rows."""
from pathlib import Path
import re
import subprocess
import tempfile

root=Path(__file__).resolve().parents[1]
s=(root/'src/game/Rows.cpp').read_text(encoding='utf-8')
start=s.index('std::uint32_t ScrollAtIndexGuarded(')
op=s.index('{',start)
end,depth=op+1,1
while depth:
    depth+=(s[end]=='{')-(s[end]=='}');end+=1
body=s[start:end]
program=r'''
#define NOMINMAX
#include <Windows.h>
#include <cstdint>
#include <cstddef>
#include <cstdio>
namespace RE {
enum class FormType { Book, Other };
struct TESObjectBOOK {
    std::uint32_t id;
    FormType type=FormType::Book;
    struct { std::uint8_t flags=1; } data;
    FormType GetFormType() { return type; }
    std::uint32_t GetFormID() { return id; }
};
struct ItemChange { void* extraData=nullptr; int count=1; TESObjectBOOK* object=nullptr; };
}
''' + body + r'''
struct Payload { RE::ItemChange* item=nullptr; int index=-1; };
struct Node { std::uintptr_t next=0, previous=0; Payload* payload=nullptr; };
static_assert(offsetof(Node,payload)==0x10 && offsetof(Payload,index)==8);
int main() {
    RE::TESObjectBOOK first{200}, second{201};
    RE::ItemChange a{nullptr,2,&first}, b{nullptr,1,&second};
    Payload firstPayload{&a,13}, secondPayload{&b,14};
    Node tail{0,0,&secondPayload}, head{reinterpret_cast<std::uintptr_t>(&tail),0,&firstPayload};
    auto address=reinterpret_cast<std::uintptr_t>(&head);
    if (ScrollAtIndexGuarded(&address,13)!=200 || ScrollAtIndexGuarded(&address,14)!=201) return 1;
    if (ScrollAtIndexGuarded(&address,12) || ScrollAtIndexGuarded(&address,15)) return 2;
    first.data.flags=0;
    if (ScrollAtIndexGuarded(&address,13)) return 3;
    first.data.flags=1; a.count=0;
    if (ScrollAtIndexGuarded(&address,13)) return 4;
    a.count=1; first.type=RE::FormType::Other;
    if (ScrollAtIndexGuarded(&address,13)) return 5;
    if (ScrollAtIndexGuarded(nullptr,13) || ScrollAtIndexGuarded(reinterpret_cast<const std::uintptr_t*>(1),13)) return 6;
    tail.next=address;
    if (ScrollAtIndexGuarded(&address,99)) return 7;
    std::puts("PASS: exact scroll-row identity, absent/non-scroll/depleted rows, fault guard and bounded cycles");
}
'''
with tempfile.TemporaryDirectory(prefix='iwp-scroll-row-') as folder:
    tmp=Path(folder)
    (tmp/'test.cpp').write_text(program,encoding='utf-8')
    batch=tmp/'build.bat'
    batch.write_text('@echo off\ncall F:\\modbuild\\tools\\msvc\\setup_x64.bat >nul\ncl /nologo /EHsc /std:c++latest test.cpp /Fe:test.exe\n',encoding='utf-8')
    subprocess.run(['cmd','/c',str(batch)],check=True,cwd=tmp)
    subprocess.run([str(tmp/'test.exe')],check=True,cwd=tmp)
