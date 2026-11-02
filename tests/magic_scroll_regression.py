"""Exercise actual Magic entry selection and use with spells and carried scrolls."""
from pathlib import Path
import re
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]
def function(path, signature):
    s = (root/path).read_text(encoding='utf-8')
    start = re.search(re.escape(signature)+r'\s*\{', s).start()
    op = s.index('{', start)
    end, depth = op+1, 1
    while depth:
        depth += (s[end]=='{')-(s[end]=='}')
        end += 1
    return s[start:end]

program = r'''
#include <cstdint>
#include <vector>
#include <array>
#include <chrono>
#include <cstdio>
#include <string>
#include <type_traits>
using namespace std::chrono_literals;
''' + '#include "' + (root/'src/wheels/KnownEntry.h').as_posix() + '"\n' + r'''
namespace RE {
struct MagicItem {};
struct TESObjectBOOK { MagicItem enchantment; } book;
struct SpellItem : MagicItem { std::uint32_t GetFormID() { return 100; } } spell;
struct PlayerCharacter { MagicItem* selectedSpell = nullptr; TESObjectBOOK* selectedScroll = nullptr; static PlayerCharacter* GetSingleton(); } player;
PlayerCharacter* PlayerCharacter::GetSingleton() { return &player; }
struct TESForm { template<class T> static T* LookupByID(std::uint32_t id) { if constexpr (std::is_same_v<T,TESObjectBOOK>) return id==200 ? &book : nullptr; else return id==100 ? &spell : nullptr; } };
}
namespace REL {
struct ID { int id; };
template<class T> struct Relocation {
    Relocation(ID) {}
    void operator()(RE::PlayerCharacter* p, RE::MagicItem* s) { p->selectedSpell=s; }
    void operator()(RE::PlayerCharacter* p, RE::TESObjectBOOK* b) { if(p->selectedScroll==b && b) {p->selectedScroll=nullptr;p->selectedSpell=nullptr;} else {p->selectedScroll=b;p->selectedSpell=b ? &b->enchantment : nullptr;} }
};
}
namespace logger { template<class... T> void info(T...) {} template<class... T> void warn(T...) {} }
namespace inventory {
constexpr int kSlots=8;
bool carried=true;
int equipped=0, sweeps=0;
bool IsScroll(std::uint32_t id) { return id==200; }
bool Has(std::uint32_t id) { return id==200 && carried; }
std::array<int,8> Keys(){return {};}
using KeyMap=std::array<int,8>;
std::string NameOf(std::uint32_t) { return "scroll"; }
bool EquipKeyed(std::uint32_t id) { if (!Has(id)) return false; ++equipped; return true; }
void SweepNativeSpellKeys() { ++sweeps; }
}
namespace rows {
bool PlayerHasSpell(std::uint32_t id) { return id==100; }
bool PlayerHasMagicEntry(std::uint32_t id) { return PlayerHasSpell(id) || (inventory::IsScroll(id) && inventory::Has(id)); }
std::string SpellName(std::uint32_t) { return "name"; }
}
namespace testhread { template<class F> void Post(F f) { f(); } }
struct Slot { std::vector<std::uint32_t> entries; int active=-1; };
std::array<std::array<Slot,8>,4> g_wheels;
constexpr int kEquip=0, kMagic=1, kAmmo=2;
enum class Wheel { kEquipment, kMagic, kAmmo };
struct SlotCount { int position=0, count=0; };
bool IsAmmo(std::uint32_t){return false;}
std::vector<int> Unreachable(int,std::uint32_t,const std::array<int,8>&){return {1};}
bool EnsureLoaded() { return true; }
std::chrono::steady_clock::time_point g_hudCheckAt;
void* g_hudIconBefore=nullptr;
void* HudSpellIcon() { return nullptr; }
void PushHudSpellIcon(std::uint32_t) {}
''' + function('src/wheels/Assign.cpp','int KnownMagicEntry(const Slot& a_slot, int a_from, int a_dir, bool a_includeFrom)') + '\n' + function('src/wheels/Wheels.cpp','std::array<SlotCount, 8> SlotCounts(Wheel a_wheel)') + '\n' + function('src/wheels/Use.cpp','void UseMagic(int a_slot)') + r'''
int main() {
    Slot slot{{200},0};
    if (KnownMagicEntry(slot,0,1,true)!=0) { std::puts("FAIL: carried scroll must be available on Magic wheel"); return 1; }
    g_wheels[kMagic][0]=slot;
    auto count=SlotCounts(Wheel::kMagic)[0];
    if(count.position!=1 || count.count!=1){std::puts("FAIL: carried scroll must count as available Magic entry");return 1;}
    UseMagic(0);
    if (inventory::equipped!=0 || RE::player.selectedSpell!=&RE::book.enchantment || RE::player.selectedScroll!=&RE::book) { std::puts("FAIL: scroll must use native scroll selection, not item equip"); return 1; }
    UseMagic(0);
    if(RE::player.selectedScroll!=&RE::book || RE::player.selectedSpell!=&RE::book.enchantment){std::puts("FAIL: selecting same scroll toggled it off");return 1;}
    inventory::carried=false;
    if (KnownMagicEntry(slot,0,1,true)!=-1) { std::puts("FAIL: depleted scroll must be skipped"); return 1; }
    UseMagic(0);
    if (inventory::equipped!=0) { std::puts("FAIL: depleted scroll used"); return 1; }
    slot.entries={200,100}; slot.active=0;
    if (KnownMagicEntry(slot,0,1,true)!=1) { std::puts("FAIL: next learned spell must remain available"); return 1; }
    g_wheels[kMagic][0]=Slot{{100},0}; UseMagic(0);
    if (RE::player.selectedSpell!=&RE::spell || RE::player.selectedScroll || inventory::equipped!=0) { std::puts("FAIL: learned spell path changed"); return 1; }
    inventory::carried=true;
    g_wheels[kMagic][1]=Slot{{200},0}; UseMagic(1);
    if (RE::player.selectedScroll!=&RE::book || RE::player.selectedSpell!=&RE::book.enchantment) { std::puts("FAIL: replenished scroll must be usable"); return 1; }
    std::puts("PASS: carried/depleted/replenished scrolls, mixed slots and normal spell selection");
}
'''
with tempfile.TemporaryDirectory(prefix='iwp-magic-scroll-') as folder:
    tmp=Path(folder)
    (tmp/'test.cpp').write_text(program,encoding='utf-8')
    batch=tmp/'build.bat'
    batch.write_text('@echo off\ncall F:\\modbuild\\tools\\msvc\\setup_x64.bat >nul\ncl /nologo /EHsc /std:c++latest test.cpp /Fe:test.exe\n',encoding='utf-8')
    subprocess.run(['cmd','/c',str(batch)],check=True,cwd=tmp)
    subprocess.run([str(tmp/'test.exe')],check=True,cwd=tmp)
