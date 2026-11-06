"""Run Model.cpp's real loading/saving functions with controlled game/time/write results."""
import pathlib
import re
import subprocess
import tempfile

ROOT = pathlib.Path(__file__).resolve().parents[1]
source = (ROOT / "src/wheels/Model.cpp").read_text(encoding="utf-8")


def function(name):
    import re
    match = re.search(r"(?:static )?(?:bool|int|void|std::filesystem::path) " + name + r"\([^)]*\)\s*\{", source)
    assert match, name
    start = match.start()
    pos = source.index("{", start)
    depth = 1
    end = pos + 1
    while depth:
        depth += (source[end] == "{") - (source[end] == "}")
        end += 1
    return source[start:end]


# Helpers added by the IWP-039 seed-change fix; absent from older sources (the checks below then fail, not the build).
OPTIONAL = tuple(n for n in ("OtherIdentityFile", "MatchesNativeKeys", "OwnedEntries", "AdoptedIndex", "AdoptedElsewhere", "MarkAdopted", "AdoptLegacy")
                 if re.search(r"(?:bool|int|void|std::filesystem::path) " + n + r"\(", source))

program = r'''
#include <algorithm>
#include <array>
#include <set>
#include <vector>
#include <chrono>
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
int readyMessages = 0, notAdopted = 0;
template<class... Args> void info(const char*, Args&&...) {}
template<class... Args> void error(const char*, Args&&...) {}
template<class... Args> void warn(const char* message, Args&&...) {
    if (std::string(message).find("empty setup ready") != std::string::npos) ++readyMessages;
    if (std::string(message).find("not adopted") != std::string::npos) ++notAdopted;
}
}
namespace settings {
std::filesystem::path root;
auto PluginFolder() { return root; }
}
namespace inventory {
std::string name = "Character #11111111";
std::string characterId;   // the save's CharacterId as the game reports it ("" = not available)
constexpr int kSlots = 8;
using KeyMap = std::array<std::uint32_t, kSlots>;
KeyMap keys{};
KeyMap Keys() { return keys; }
std::set<std::uint32_t> owned;   // items carried and spells known by the character now playing
bool Has(std::uint32_t id) { return owned.count(id) > 0; }
std::string Identity() { return characterId.empty() ? name : characterId; }
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
namespace rows { int PlayerSpellCount() { return 1; } bool PlayerHasMagicEntry(std::uint32_t id) { return inventory::owned.count(id) > 0; } }
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
constexpr int kEquip = 0;
constexpr int kMagic = 1;
constexpr int kAmmo = 2;
wheelstorage::Model g_wheels;
std::string g_loadedFor, g_loadCandidate;
ULONGLONG g_loadCandidateAt = 0, g_storageRetryAt = 0;
bool g_storageRejected = false, g_initializeEmpty = false, g_sanitized = false, g_identityHeld = false;
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
bool Save();
''' + "\n".join(function(name) for name in ("FileFor",) + OPTIONAL + ("Save", "Load", "EnsureLoadedNow", "EnsureLoaded")) + r'''
}
using namespace wheels::detail;
void reset(const std::filesystem::path& root) {
    std::filesystem::remove_all(root);
    std::filesystem::create_directories(root);
    settings::root = root;
    g_wheels = {}; g_loadedFor.clear(); g_loadCandidate.clear();
    g_loadCandidateAt = g_storageRetryAt = g_sanitizeAt = 0;
    g_storageRejected = g_initializeEmpty = g_sanitized = g_identityHeld = false;
    g_emptySetup.Reset(); ++g_generation;
    inventory::name = "Character #11111111"; inventory::ready = true; inventory::characterId.clear(); inventory::keys = {}; inventory::owned.clear();
    inventory::clearCalls = logger::readyMessages = logger::notAdopted = atomicfile::writes = 0;
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

    // IWP-039: the same character under a new seed (file "Character _22222222.iwp" exists, the game now says #11111111).
    reset(root); writeModel("Character #22222222", 0x7777);
    settled(); clockNow += 4000; EnsureLoadedNow(); EnsureLoadedNow(); clockNow += 6000;
    const bool heldActions = !EnsureLoadedNow();
    check(inventory::clearCalls == 0, "seed change never wipes the save's native hotkeys");
    check(!std::filesystem::exists(FileFor(inventory::name)), "seed change never writes an empty assignment file");
    check(heldActions && g_wheels[0][0].entries.empty(), "seed change keeps wheel actions blocked");
    wheelstorage::Model kept{};
    { std::ifstream in(FileFor("Character #22222222")); wheelstorage::ReadAssignments(in, "Character #22222222", kept); }
    check(kept[0][0].entries == std::vector<std::uint32_t>{0x7777}, "the other-seed file is left untouched");
    writeModel(inventory::name, 0x8888); clockNow += 6000; EnsureLoadedNow();
    check(present(0x8888) && EnsureLoadedNow(), "the character's own file appearing later is loaded");

    reset(root); writeModel("Other #22222222", 0x1111);
    settled(); clockNow += 4000; EnsureLoadedNow(); EnsureLoadedNow();
    check(inventory::clearCalls == 1 && std::filesystem::exists(FileFor(inventory::name)),
          "another character's file does not block fresh setup");

    // Step 2 (owner-approved rule): files keyed on the save's CharacterId, old seed files adopted.
    const std::string G = "4A1A9262-487462B9-F49E7884-5B878754";
    auto readModel = [](const std::string& who, wheelstorage::Model& m) {
        std::ifstream in(FileFor(who)); return wheelstorage::ReadAssignments(in, who, m);
    };
    reset(root); inventory::characterId = G;
    settled(); clockNow += 4000; EnsureLoadedNow(); EnsureLoadedNow();
    check(std::filesystem::exists(FileFor(G)) && !std::filesystem::exists(FileFor(inventory::name)),
          "a new character's file is named by its CharacterId");

    reset(root); writeModel(inventory::name, 0x1234); inventory::characterId = G;
    settled();
    wheelstorage::Model adopted{};
    check(present(0x1234) && readModel(G, adopted) && adopted[0][0].entries == std::vector<std::uint32_t>{0x1234} &&
          inventory::clearCalls == 0, "the old file with the same name and seed is adopted under the CharacterId");
    check(std::filesystem::exists(FileFor(inventory::name)), "the adopted old file is kept");

    reset(root); writeModel("Character #22222222", 0x7777); inventory::characterId = G; inventory::keys[0] = 0x7777;
    settled();
    check(present(0x7777) && std::filesystem::exists(FileFor(G)) && std::filesystem::exists(FileFor("Character #22222222")) &&
          inventory::clearCalls == 0, "a single same-name file is adopted when it matches the save's quick keys");

    reset(root); writeModel("Character #22222222", 0x7777); inventory::characterId = G; inventory::keys[0] = 0x9999;
    inventory::owned = { 0x7777 };   // one of its three entries: not enough to prove it, not nothing either
    settled(); clockNow += 4000; EnsureLoadedNow();
    check(!EnsureLoadedNow() && !std::filesystem::exists(FileFor(G)) && inventory::clearCalls == 0 && g_wheels[0][0].entries.empty(),
          "a same-name file with other quick keys and few owned entries is held and nothing is wiped");
    for (int i = 0; i < 5; ++i) { clockNow += 6000; EnsureLoadedNow(); }
    check(logger::notAdopted == 1, "a held adoption is reported once, not on every retry");

    reset(root); writeModel("Character #22222222", 0x7777); writeModel("Character #33333333", 0x6666);
    std::filesystem::last_write_time(FileFor("Character #22222222"), std::filesystem::file_time_type::clock::now() - std::chrono::hours(48));
    inventory::characterId = G; inventory::owned = { 0x7777, 0x7778, 0x7779, 0x6666, 0x6667, 0x6668 };
    settled();
    check(present(0x6666) && inventory::clearCalls == 0, "of two usable same-name files the most recent is adopted");

    // the reporter's case: quick keys changed since, but the character carries the file's items and knows its spells
    reset(root); writeModel("Character #22222222", 0x7777); inventory::characterId = G; inventory::keys[0] = 0x9999;
    inventory::owned = { 0x7777, 0x7778, 0x7779 };
    settled();
    check(present(0x7777) && std::filesystem::exists(FileFor(G)), "a file whose entries this character owns is adopted despite other quick keys");
    // a second character with the same name and the same items never takes the file again
    const std::string G2 = "1B2C3D4E-11111111-22222222-33333333";
    inventory::characterId = G2; settled(); clockNow += 4000; EnsureLoadedNow(); EnsureLoadedNow();
    check(!present(0x7777) && std::filesystem::exists(FileFor(G2)), "an old file is adopted by one character only");

    // a new character that owns none of the items: another character's file, fresh setup, the file stays for its owner
    reset(root); writeModel("Character #22222222", 0x7777); inventory::characterId = G;
    settled(); clockNow += 4000; EnsureLoadedNow(); EnsureLoadedNow();
    check(!present(0x7777) && inventory::clearCalls == 1 && std::filesystem::exists(FileFor(G)) &&
          std::filesystem::exists(FileFor("Character #22222222")), "a character owning none of a file's entries starts fresh and leaves it");

    // the manual fix: the player renamed an old file to the CharacterId name
    reset(root); writeModel("Character #22222222", 0x7777);
    std::filesystem::rename(FileFor("Character #22222222"), FileFor(G)); inventory::characterId = G;
    settled();
    wheelstorage::Model renamed{};
    check(present(0x7777) && readModel(G, renamed) && renamed[0][0].entries == std::vector<std::uint32_t>{0x7777},
          "an old file renamed to the CharacterId is accepted and rewritten under it");

    reset(root); writeModel("Other #22222222", 0x1111); inventory::characterId = G; inventory::keys[0] = 0x1111;
    settled(); clockNow += 4000; EnsureLoadedNow(); EnsureLoadedNow();
    check(inventory::clearCalls == 1 && std::filesystem::exists(FileFor(G)) && !present(0x1111),
          "another character's file is never adopted");

    // The reporter's shape: the populated old file plus the empty one the old bug wrote. The empty one is no candidate.
    reset(root); writeModel("Character #22222222", 0x7777);
    { std::ofstream out(FileFor("Character #33333333")); wheelstorage::WriteAssignments(out, "Character #33333333", wheelstorage::Model{}); }
    inventory::characterId = G; inventory::keys[0] = 0x7777;
    settled();
    check(present(0x7777) && std::filesystem::exists(FileFor(G)), "an empty old file does not block adopting the populated one");

    // A CharacterId comes from the save itself: one second of settling is enough; a seed name keeps three.
    reset(root); inventory::characterId = G; writeModel(G, 0x5555);
    EnsureLoadedNow(); clockNow += 1001;
    check(EnsureLoadedNow() && present(0x5555), "a CharacterId identity is ready after one second");
    reset(root); writeModel(inventory::name, 0x5556);
    EnsureLoadedNow(); clockNow += 1001;
    check(!EnsureLoadedNow() && g_loadedFor.empty(), "a seed-name identity still settles for three seconds");

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
