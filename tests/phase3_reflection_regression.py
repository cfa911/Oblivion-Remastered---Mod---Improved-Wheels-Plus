"""Exercise actual field lookup with synthetic structs and counted name conversions."""
from pathlib import Path
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]
source = (root/'src/engine/Reflect.cpp').read_text(encoding='utf-8')
source = source[source.index('namespace reflect'):source.index('\n\tstd::vector<std::pair')]+ '\n}'
program = r'''
#include <atomic>
#include <cstdint>
#include <cstddef>
#include <string>
#include <string_view>
#include <unordered_map>
#include <functional>
#include <cstring>
#include <cstdio>
int conversions=0;
using DWORD=unsigned long;
DWORD GetCurrentThreadId(){return 1;}
namespace UE {
enum class EFindName{Find};
struct FName {
std::uint64_t id=0;
FName()=default;
FName(const char* s,EFindName){id=std::string_view(s)=="Value"?1:std::string_view(s)=="Late"?2:0;}
bool operator==(FName other)const{return id==other.id;}
bool IsNone()const{return id==0;}
std::string ToString()const{++conversions;return id==1?"Value":id==2?"Late":"None";}
};
struct UStruct { int internalIndex=1; void* childProperties=nullptr; UStruct* superStruct=nullptr; };
}
namespace pe { std::string Utf8(std::string s){return s;} }
namespace perfdiag { enum Counter{kOffset}; void Add(Counter){} }
''' + source + r'''
struct alignas(8) Field { unsigned char bytes[0x50]{}; };
void set(Field& f,int id,int offset){UE::FName n; n.id=id; std::memcpy(f.bytes+0x20,&n,8); std::memcpy(f.bytes+0x44,&offset,4);}
int main(){
Field first,second; set(first,1,12); set(second,1,24);
UE::UStruct parent{1,&first,nullptr}, child{2,nullptr,&parent};
if(reflect::Offset(&child,"Value")!=12)return 1;
auto before=conversions;
if(reflect::Offset(&child,"Value")!=12 || conversions!=before){std::puts("FAIL: repeated field lookup converts names again");return 1;}
parent.childProperties=&second;
if(reflect::Offset(&child,"Value")!=24){std::puts("FAIL: changed parent fields must invalidate cache");return 1;}
UE::UStruct late{3,nullptr,nullptr};
if(reflect::Offset(&late,"Late")!=-1)return 1;
set(first,2,36); late.childProperties=&first;
if(reflect::Offset(&late,"Late")!=36){std::puts("FAIL: early missing fields must be retried");return 1;}
if(reflect::Offset(nullptr,"Value")!=-1)return 1;
std::puts("PASS: cached fields avoid conversions, follow parent replacement, retry missing fields");
}
'''
with tempfile.TemporaryDirectory() as folder:
    folder=Path(folder)
    (folder/'test.cpp').write_text(program,encoding='utf-8')
    (folder/'build.bat').write_text('@echo off\ncall F:\\modbuild\\tools\\msvc\\setup_x64.bat >nul\ncl /nologo /EHsc /std:c++20 test.cpp /Fe:test.exe\n',encoding='utf-8')
    subprocess.run(['cmd','/c',str(folder/'build.bat')],cwd=folder,check=True)
    subprocess.run([str(folder/'test.exe')],cwd=folder,check=True)
