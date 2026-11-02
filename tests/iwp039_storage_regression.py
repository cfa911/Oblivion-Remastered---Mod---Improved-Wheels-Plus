"""Run Model.cpp's real loading/saving functions with controlled game/time/write results."""
import pathlib
import subprocess
import tempfile

ROOT = pathlib.Path(__file__).resolve().parents[1]
source = (ROOT / "src/wheels/Model.cpp").read_text(encoding="utf-8")


def function(name):
    import re
    match = re.search(r"(?:static )?(?:bool|void|std::filesystem::path) " + name + r"\([^)]*\)\s*\{", source)
    assert match, name
    start = match.start()
    pos = source.index("{", start)
    depth = 1
    end = pos + 1
    while depth:
        depth += (source[end] == "{") - (source[end] == "}")
        end += 1
    return source[start:end]


program = r'''
#include <atomic>
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include "wheels/WheelStorage.h"
#include "wheels/EmptySetup.h"
#include "core/ReadMemo.h"
using ULONGLONG = std::uint64_t;
ULONGLONG clockNow = 10000;
ULONGLONG GetTickCount64() { return clockNow; }
namespace logger {
int readyMessages = 0;
template<class... Args> void info(const char*, Args&&...) {}
template<class... Args> void error(const char*, Args&&...) {}
template<class... Args> void warn(const char* message, Args&&...) {
    if (std::string(message).find("empty setup ready") != std::string::npos) ++readyMessages;
}
}
namespace settings {
std::filesystem::path root;
auto PluginFolder() { return root; }
}
namespace inventory {
std::string name = "Character #11111111";
bool ready = true;
int clearCalls = 0;
bool Ready() { return ready; }
std::string PlayerName() { return name; }
bool ClearAllNativeKeys() { ++clearCalls; return true; }
}
namespace RE {
struct PlayerCharacter { static PlayerCharacter* GetSingleton() { static PlayerCharacter p; return &p; } };
}
namespace testhread {
bool Installed() { return true; }
template<class F> void Post(F f) { f(); }
}
namespace quickkeys { void RequestRedraw() {} }
namespace rows { int PlayerSpellCount() { return 1; } }
namespace atomicfile {
bool fail = false;
bool race = false;
int writes = 0;
bool Write(const std::filesystem::path& path, const std::string& data, bool replace = true) {
    ++writes;
    if (fail) return false;
    if (race) {
        race = false;
        std::ofstream other(path);
        other << "# ImprovedWheelsPlus assignments v1\n# Character: " << inventory::name
              << "\nEquipment 1 active=1 0x6789\n";
    }
    if (!replace && std::filesystem::exists(path)) return false;
    std::ofstream out(path); out << data; return static_cast<bool>(out);
}
}
namespace wheels::detail {
wheelstorage::Model g_wheels;
std::string g_loadedFor, g_loadCandidate;
ULONGLONG g_loadCandidateAt = 0, g_storageRetryAt = 0;
bool g_storageRejected = false, g_initializeEmpty = false, g_sanitized = false;
ULONGLONG g_sanitizeAt = 0;
std::atomic<std::uint32_t> g_generation{1};
emptysetup::Gate g_emptySetup;
void ResetCarriedHistory() {}
bool AssignPending() { return false; }
void Sanitize() {}
void RemoveLeavers() {}
void RemoveGoneInMenus() {}
void Reconcile() {}
void MigrateAmmo() {}
bool ObserveContext() { return inventory::ready; }
''' + "\n".join(function(name) for name in ("FileFor", "Save", "Load", "EnsureLoadedNow", "EnsureLoaded")) + r'''
}
using namespace wheels::detail;
void reset(const std::filesystem::path& root) {
    std::filesystem::remove_all(root);
    std::filesystem::create_directories(root);
    settings::root = root;
    g_wheels = {}; g_loadedFor.clear(); g_loadCandidate.clear();
    g_loadCandidateAt = g_storageRetryAt = g_sanitizeAt = 0;
    g_storageRejected = g_initializeEmpty = g_sanitized = false;
    g_emptySetup.Reset(); ++g_generation;
    inventory::name = "Character #11111111"; inventory::ready = true;
    inventory::clearCalls = logger::readyMessages = atomicfile::writes = 0;
    atomicfile::fail = atomicfile::race = false; clockNow = 10000;
}
void writeModel(const std::string& name, std::uint32_t id) {
    const auto path = FileFor(name);
    std::filesystem::create_directories(path.parent_path());
    wheelstorage::Model model{};
    for (int w = 0; w < 3; ++w) { model[w][0].entries = { id + w }; model[w][0].active = 0; }
    std::ofstream out(path); wheelstorage::WriteAssignments(out, name, model);
}
bool present(std::uint32_t id) { return g_wheels[0][0].entries == std::vector<std::uint32_t>{id}; }
void settled() { EnsureLoadedNow(); clockNow += 3001; EnsureLoadedNow(); }
int main(int argc, char** argv) {
    assert(argc == 2);
    const std::filesystem::path root = argv[1];
    int failures = 0;
    auto check = [&](bool ok, const char* message) {
        std::printf("%s: %s\n", ok ? "PASS" : "FAIL", message);
        failures += !ok;
    };
    reset(root);
    writeModel(inventory::name, 0x1234);
    EnsureLoadedNow();
    check(g_loadedFor.empty() && g_wheels[0][0].entries.empty(), "do not accept an identity immediately");
    clockNow += 1000; inventory::name = "Character #22222222";
    EnsureLoadedNow(); clockNow += 2500;
    check(!EnsureLoadedNow() && g_loadedFor.empty(), "changed identity restarts settling interval");
    inventory::name = "Character #11111111";
    settled();
    check(present(0x1234) && g_wheels[1][0].entries == std::vector<std::uint32_t>{0x1235}
          && g_wheels[2][0].entries == std::vector<std::uint32_t>{0x1236} && !g_initializeEmpty,
          "stable original identity loads all stored wheels");

    reset(root);
    std::filesystem::create_directories(FileFor(inventory::name).parent_path());
    { std::ofstream out(FileFor(inventory::name)); out << "invalid file"; }
    settled();
    check(g_storageRejected && atomicfile::writes == 0, "invalid file is blocked without overwrite");
    writeModel(inventory::name, 0x2345);
    EnsureLoadedNow();
    check(g_storageRejected, "failed reads are throttled before retry");
    const int attempts = atomicfile::writes;
    clockNow += 6000; EnsureLoadedNow();
    check(!g_storageRejected && present(0x2345), "corrected file reloads without restarting game");
    check(atomicfile::writes == attempts, "recovery never overwrites recovered assignments");

    reset(root);
    std::filesystem::create_directories(FileFor(inventory::name));
    settled();
    check(g_storageRejected && atomicfile::writes == 0, "unreadable existing path never becomes fresh setup");
    std::filesystem::remove(FileFor(inventory::name));
    writeModel(inventory::name, 0x2345);
    clockNow += 6000; EnsureLoadedNow();
    check(!g_storageRejected && present(0x2345), "unreadable path recovers on retry");

    reset(root); settled();
    // The first load found no file; another writer makes it available before empty setup commits.
    writeModel(inventory::name, 0x3456);
    clockNow += 4000; EnsureLoadedNow(); EnsureLoadedNow();
    wheelstorage::Model disk{};
    { std::ifstream in(FileFor(inventory::name)); wheelstorage::ReadAssignments(in, inventory::name, disk); }
    check(present(0x3456) && disk[0][0].entries == std::vector<std::uint32_t>{0x3456},
          "file appearing during fresh setup is loaded instead of overwritten");

    reset(root); atomicfile::race = true;
    settled(); EnsureLoadedNow(); clockNow += 6000; EnsureLoadedNow(); EnsureLoadedNow();
    check(present(0x6789), "file appearing at the atomic commit is preserved and then loaded");

    reset(root); atomicfile::fail = true;
    settled(); clockNow += 4000; EnsureLoadedNow(); EnsureLoadedNow();
    check(g_initializeEmpty && logger::readyMessages == 0, "failed initial save remains blocked and never reports ready");
    const int failedWrites = atomicfile::writes;
    EnsureLoadedNow();
    check(atomicfile::writes == failedWrites, "failed saves are throttled between attempts");
    atomicfile::fail = false; clockNow += 6000;
    const bool recovered = EnsureLoadedNow();
    check(recovered && !g_initializeEmpty && logger::readyMessages == 1 && std::filesystem::exists(FileFor(inventory::name)),
          "fresh setup becomes ready only after a successful retry");

    reset(root); writeModel(inventory::name, 0x4567); settled();
    inventory::name = "Other #33333333"; EnsureLoadedNow();
    inventory::name = "Character #11111111"; EnsureLoadedNow();
    clockNow += 4000; inventory::name = "Other #33333333";
    check(!EnsureLoadedNow() && g_loadedFor != inventory::name, "interrupted identity candidate must settle again");
    inventory::name = "Character #11111111"; EnsureLoadedNow();
    inventory::name = "Other #33333333"; writeModel(inventory::name, 0x5678);
    check(!EnsureLoadedNow(), "character switch blocks actions during identity settling");
    clockNow += 3001; EnsureLoadedNow();
    check(present(0x5678) && g_loadedFor == inventory::name, "stable character switch loads only its own assignments");
    std::filesystem::remove_all(root);
    return failures ? 1 : 0;
}
'''

with tempfile.TemporaryDirectory(prefix="iwp039-") as folder:
    tmp = pathlib.Path(folder)
    cpp = tmp / "storage.cpp"
    cpp.write_text(program, encoding="utf-8")
    batch = tmp / "build.bat"
    batch.write_text(
        '@echo off\ncall "F:\\modbuild\\tools\\msvc\\setup_x64.bat" >nul\n'
        f'cl /nologo /EHsc /std:c++20 /I"{ROOT / "src"}" "{cpp}" '
        f'/Fe:"{tmp / "storage.exe"}" /Fo:"{tmp / "storage.obj"}"\n', encoding="utf-8")
    subprocess.run(["cmd", "/c", str(batch)], check=True, cwd=tmp)
    subprocess.run([str(tmp / "storage.exe"), str(tmp / "assignments")], check=True)
