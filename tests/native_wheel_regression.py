"""Compile production native-wheel message and polling code against a fake window.
No game or Wine session is launched. Run with Python 3 and g++ on Linux.
"""
from pathlib import Path
import subprocess, tempfile
source=(Path(__file__).resolve().parents[1]/'src/Keyboard.cpp').read_text()
helpers=source[source.index('\t\tHWND g_keyboardWindow'):source.index('\t\tbool GameHasFocus()')]
start=source.index('\t\tif (nativeToggle) {')
branch=source[start:source.index(' else if (k.wheelToggle)',start)]
program=r'''
#include <atomic>
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <vector>
using HWND=void*; const HWND testWindow=reinterpret_cast<HWND>(1); using UINT=unsigned; using DWORD=unsigned; using WPARAM=uintptr_t;
using LPARAM=intptr_t; using LRESULT=intptr_t; using LONG_PTR=intptr_t; using ULONGLONG=unsigned long long;
#define CALLBACK
using WNDPROC=LRESULT(*)(HWND,UINT,WPARAM,LPARAM);
constexpr UINT WM_NCDESTROY=1, WM_KILLFOCUS=2, WM_ACTIVATEAPP=3, WM_KEYUP=4, WM_KEYDOWN=5;
constexpr int FALSE=0, GWLP_WNDPROC=0, MAPVK_VK_TO_VSC_EX=4, XINPUT_GAMEPAD_B=0x2000;
namespace settings { struct K { int lb=6,nativeWheelKey='Q'; } k; struct V { K& keyboard=k; } v; auto& Get(){return v;} }
namespace logger { template<class... T> void info(const char*,T...){} template<class... T> void debug(const char*,T...){} template<class... T> void warn(const char*,T...){} }
namespace wheels { void KeepActive(){} }
namespace slowtime { void CloseRequested(){} }
namespace ammo { bool open=false; bool Open(){return open;} }
bool g_mouseRadial=false, g_mouseRadialSeen=false;
struct Message { UINT msg; WPARAM key; LPARAM data; };
std::vector<Message> posted,forwarded;
HWND foreground=testWindow; bool postOK=true; ULONGLONG ticks=1000;
UINT MapVirtualKeyW(int key, int){return key=='Q'?0x10:0x13;}
LRESULT CallWindowProcW(WNDPROC,HWND,UINT msg,WPARAM key,LPARAM data){forwarded.push_back({msg,key,data});return 1;}
LRESULT DefWindowProcW(HWND w,UINT m,WPARAM k,LPARAM d){return CallWindowProcW(nullptr,w,m,k,d);}
LONG_PTR GetWindowLongPtrW(HWND,int){return 0;}
LONG_PTR SetWindowLongPtrW(HWND,int,LONG_PTR){return 1;}
HWND GetForegroundWindow(){return foreground;}
DWORD GetCurrentProcessId(){return 1;}
DWORD GetWindowThreadProcessId(HWND,DWORD* p){*p=1;return 1;}
bool IsWindow(HWND w){return w==testWindow;}
UINT RegisterWindowMessageW(const wchar_t*){return 99;}
void SetLastError(int){} int GetLastError(){return 5;}
ULONGLONG GetTickCount64(){return ticks;}
bool PostMessageW(HWND,UINT m,WPARAM k,LPARAM d){if(!postOK)return false; posted.push_back({m,k,d});return true;}
''' + helpers + r'''
void pump(){auto copy=posted;posted.clear();for(auto m:copy)KeyboardWindowProc(testWindow,m.msg,m.key,m.data);}
struct Pad { unsigned wButtons=0; } a_pad;
bool previousWheel=false;
void step(bool wheelDown,bool radial=false,bool gameplay=true){
const auto& k=settings::k; const bool nativeToggle=true; bool any=false;
''' + branch + r'''
previousWheel=wheelDown;
}
int main(){
g_keyboardWindow=testWindow; g_releaseWheelMessage=99;
step(true); assert(g_nativeWheelHeld && a_pad.wButtons==0 && posted.size()==1);
pump(); assert(forwarded.back().key=='Q' && forwarded.back().msg==WM_KEYDOWN);
assert((forwarded.back().data & 0xff0000)==0x100000);
step(true,true); assert(posted.empty()); // holding trigger is not a second toggle
step(false,true); assert(g_nativeWheelHeld && posted.empty());
step(true,true); assert(!g_nativeWheelHeld && a_pad.wButtons==XINPUT_GAMEPAD_B);
pump(); assert(forwarded.back().key=='Q' && forwarded.back().msg==WM_KEYUP);
assert((uint32_t(forwarded.back().data)&0xc0000000u)==0xc0000000u);
a_pad={};step(false);step(true); // pending press cancelled by focus loss
foreground=0; KeyboardWindowProc(testWindow,WM_KILLFOCUS,0,0);auto count=forwarded.size();pump();
assert(!g_nativeWheelHeld && forwarded.size()==count);
foreground=testWindow;step(false);postOK=false;step(true);assert(!g_nativeWheelHeld);postOK=true;
step(false);ammo::open=true;step(true);assert(!g_nativeWheelHeld && posted.empty());ammo::open=false;
step(false);step(true,false,false);assert(!g_nativeWheelHeld && posted.empty());
step(false);step(true);pump();ticks+=1300;step(false);pump();assert(!g_nativeWheelHeld); // failed open times out
settings::k.lb='R';settings::k.nativeWheelKey=0;g_nativeWheelAllowed=true;
KeyboardWindowProc(testWindow,WM_KEYDOWN,'R',0x130001);assert(g_nativeWheelHeld);auto n=forwarded.size();
KeyboardWindowProc(testWindow,WM_KEYUP,'R',0xc0130001);assert(forwarded.size()==n); // legacy hold stays held
KeyboardWindowProc(testWindow,WM_KILLFOCUS,0,0);assert(!g_nativeWheelHeld);
puts("PASS: native Q bridge, hold/release, cancel, focus loss, failed post, menu/ammo guards, timeout and legacy R");
}
'''
with tempfile.TemporaryDirectory() as d:
 cpp=Path(d)/'test.cpp';binary=Path(d)/'test';cpp.write_text(program)
 subprocess.run(['g++','-std=c++20',str(cpp),'-o',str(binary)],check=True)
 subprocess.run([str(binary)],check=True)
