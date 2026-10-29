#include "wheels/WheelStorage.h"
#include "engine/SessionEpoch.h"
#include "core/AtomicFile.h"
#include "wheels/KnownEntry.h"
#include "wheels/EmptySetup.h"
#include "ammo/AmmoLayout.h"
#include "input/RadialSelection.h"
#include "input/CloseEpoch.h"
#include "wheels/CarriedHistory.h"
#include "game/MessageDiscovery.h"
#include "slowtime/SlowTimeLease.h"
#include <vector>
#include <unordered_set>
#include <cstdio>
#include <fstream>
#include <iterator>

int ReviewFixTests()
{
	int checks = 0, failed = 0;
	const auto check = [&](bool ok, const char* label) {
		++checks;
		if (!ok) { ++failed; std::printf("FAIL: %s\n", label); }
	};
	// Current stick direction is usable before native hover reports the new slot.
	{
		messagediscovery::Gate discovery;
		check(discovery.Due(1000, false), "first draw discovers existing dialogs");
		check(!discovery.Due(1001, false), "repeated drawing avoids object-array discovery");
		check(discovery.Due(1001, true), "input immediately discovers a newly created dialog");
		check(!discovery.Due(1100, false) && discovery.Due(1101, false), "drawing refreshes after100ms since last actual discovery");
		int asks = 0;
		const auto messageUp = [&](bool a_up) { return [&asks, a_up] { ++asks; return a_up; }; };
		check(!messagediscovery::InputAllowed(false, messageUp(false)) && asks == 0, "no guarded key down: the dialog is not asked about");
		check(messagediscovery::InputAllowed(true, messageUp(false)) && asks == 1, "a key down and no dialog: input allowed after exactly one ask");
		check(!messagediscovery::InputAllowed(true, messageUp(true)) && asks == 2, "a key down and a dialog up: input is held back");
	}
	{
		std::unordered_set<std::uint32_t> seen{ 42 };
		std::uint64_t nextCheck = 250;
		std::vector<std::uint32_t> stored{ 42 };
		const auto removeMissing = [&](bool carried) {
			if (carried) seen.insert(42);
			else if (seen.contains(42)) stored.clear();
		};
		carriedhistory::Reset(seen, nextCheck); // Load/context change/readiness loss use this same reset.
		removeMissing(false); // Earlier save does not carry the later save's assigned item.
		check(stored.size() == 1, "load keeps stored entry absent from the earlier save");
		check(seen.empty() && nextCheck == 0, "load clears carried history and its throttle together");
		removeMissing(true);
		removeMissing(false); // A real drop after observing the item in this session.
		check(stored.empty(), "same-session real drop still removes the entry");
		seen.insert(42); nextCheck = 999;
		carriedhistory::Reset(seen, nextCheck);
		carriedhistory::Reset(seen, nextCheck);
		check(seen.empty() && nextCheck == 0, "repeated not-ready observations remain reset");
	}
	{
		slowtime::CloseGate gate;
		check(gate.Allows(true, 1), "new wheel can slow time");
		gate.Close(1);
		check(!gate.Allows(true, 1), "close request excludes still-visible animation");
		check(!gate.Allows(true, 1), "closing animation cannot reacquire slowdown");
		check(gate.Allows(true, 2), "quick reopen permits slowdown on new wheel");
		gate.Close(2);
		check(!gate.Allows(false, 2) && gate.Allows(true, 2), "closed shortcut resets close gate");
		gate.Close(2);
		gate.Reopen();
		check(gate.Allows(true, 2), "abandoned close permits same-count reopening");
	}
	{
		slowtime::Lease lease;
		float speed = 1;
		int writes = 0;
		const auto write = [&](float value) { speed = value; ++writes; return true; };
		lease.Update(false, speed, .1f, write);
		lease.Update(true, speed, .1f, write);
		check(speed == .1f && writes == 1, "slow time acquires normal speed once");
		lease.Update(true, speed, .1f, write);
		check(writes == 1, "continuous wheel switch does not rewrite speed");
		lease.Update(false, speed, .1f, write);
		check(speed == 1 && writes == 2, "wheel close restores owned speed");
		lease.Update(true, .5f, .1f, write);
		lease.Update(false, .5f, .1f, write);
		check(writes == 2, "preexisting slow motion left alone");
		lease.Update(true, 1, .1f, write);
		lease.Update(true, .3f, .1f, write);
		lease.Update(true, 1, .1f, write);
		lease.Update(false, .3f, .1f, write);
		check(writes == 3, "external change relinquishes until wheel closes");
		lease.Update(true, 1, .1f, write);
		lease.Forget();
		lease.Update(false, .1f, .1f, write);
		check(writes == 4, "world replacement never restores a stale world");
		lease.Update(true, 1, 0, write);
		check(writes == 4, "zero multiplier rejected");
		lease.Update(false, 1, .1f, write);
		lease.Update(true, 1, .1f, write);
		lease.Update(false, speed, .1f, [](float) { return false; });
		lease.Update(false, speed, .1f, write);
		check(speed == 1 && writes == 6, "failed restoration is retried while still owned");
	}
	check(radialselection::LiveStickKey(0, 32767) == 0, "quick outward stick plus A selects top immediately");
	check(radialselection::LiveStickKey(32767, 0) == 2, "latest right direction overrides stale previous slot");
	check(radialselection::LiveStickKey(0, -32768) == 4, "release while pointing down selects bottom");
	check(radialselection::LiveStickKey(-32768, 0) == 6, "left direction selects slot seven");
	check(radialselection::LiveStickKey(23000, 23000) == 1, "diagonal points to its matching eighth");
	check(radialselection::LiveStickKey(0, 0) == -1, "center does not create a top selection after rest cancellation");
	check(radialselection::LiveStickKey(3000, -3000) == -1, "stick drift does not revive cleared selection");
	check(radialselection::LiveStickKey(0, 14744) == -1, "direction below active threshold retains native rest policy");
	// Initialization must stay blocked until a successful native worker, never just queueing.
	emptysetup::Gate setup;
	check(!setup.Complete(), "missing setup starts blocked");
	const auto request = setup.Queue(3000);
	check(request != 0 && !setup.Complete(), "queue acceptance does not enable assignment or save empty setup");
	check(setup.Queue(3500) == 0, "in-flight cleanup is not enqueued every frame");
	check(!setup.Run(request, [] { return false; }) && !setup.Complete(), "failed clear remains blocked");
	const auto retry = setup.Queue(4000);
	int clears = 0;
	check(!setup.Run(request, [&] { ++clears; return true; }) && clears == 0, "superseded worker cannot clear newer state");
	check(setup.Run(retry, [&] { ++clears; return true; }) && setup.Complete(), "verified native completion unlocks setup");
	check(!setup.Run(retry, [&] { ++clears; return true; }) && clears == 1, "completed worker cannot clear later assignments");
	check(setup.Queue(9000) == 0, "completion schedules no further cleanup");
	setup.Reset();
	check(!setup.Complete(), "new context requires its own completion");
	check(!setup.Run(retry, [&] { ++clears; return true; }) && clears == 1, "old context callback cannot clear new context");
	const auto dropped = setup.Queue(10000);
	check(setup.Queue(10500) == 0, "dropped context-wrapped work waits for retry lease");
	const auto replacement = setup.Queue(11000);
	check(replacement && replacement != dropped && !setup.Run(dropped, [] { return true; }), "dropped queue work recovers with fresh ticket");
	check(setup.Run(replacement, [] { return true; }) && setup.Complete(), "replacement worker completes safely");

	setup.Reset();
	const auto reentrant = setup.Queue(12000);
	check(!setup.Run(reentrant, [&] {
		check(!setup.Complete() && setup.Queue(14000) == 0, "native callback can reenter readers without deadlock or parallel clear");
		setup.Reset();
		check(setup.Queue(15000) == 0, "context reset cannot overtake running native worker");
		return true;
	}) && !setup.Complete(), "reset during native worker rejects stale completion");
	const auto afterReentry = setup.Queue(16000);
	check(afterReentry && setup.Run(afterReentry, [] { return true; }), "new context worker can run after prior native callback returns");

	// An earlier save can lose vanilla and custom spells without changing character identity.
	const std::vector<unsigned> spells{ 1, 0xFF123456, 3, 4 };
	std::unordered_set<unsigned> owned{ 1, 3 };
	const auto known = [&](unsigned id) { return owned.contains(id); };
	check(knownentry::Find(spells, 1, +1, true, known) == 2, "missing active custom spell falls forward to known spell");
	check(knownentry::Find(spells, 2, +1, false, known) == 0, "forward cycle skips missing spell and wraps");
	check(knownentry::Find(spells, 0, -1, false, known) == 2, "reverse cycle skips missing spell and wraps");
	check(knownentry::Find(spells, -1, +1, true, known) == 0, "inactive slot finds first owned spell");
	check(knownentry::Find(spells, 99, -1, true, known) == 2, "invalid active index searches safely in reverse");
	owned = { 3 };
	check(knownentry::Find(spells, 2, +1, false, known) == 2, "only owned entry remains selected");
	owned.clear();
	check(knownentry::Find(spells, 2, +1, true, known) == -1, "absent ownership produces empty display and no selection");
	check(knownentry::Find(std::vector<unsigned>{}, 0, +1, true, known) == -1, "empty slot cannot be indexed");
	owned = { 0xFF123456 };
	check(knownentry::Find(spells, 0, +1, true, known) == 1, "restoring a later save makes retained custom assignment available");
	check(spells == std::vector<unsigned>({1, 0xFF123456, 3, 4}), "live filtering preserves stored assignment order");
	const std::array<std::uint32_t, 8> arrows{11, 12, 13, 14, 15, 16, 17, 18};
	const auto allAmmo = ammolayout::Build(arrows, true);
	check(allAmmo.full && allAmmo.count == 8 && allAmmo.ids == arrows, "all eight ammunition types remain visible and selectable");
	for (int cell = 0; cell < 8; ++cell) check(allAmmo.savedSlots[cell] == cell, "full Ammo ring preserves each exact saved cell");
	const auto legacyEight = ammolayout::Build(arrows, false);
	check(legacyEight.full && legacyEight.count == 8, "legacy half shortcut expands instead of hiding sixth through eighth arrows");
	const std::array<std::uint32_t, 8> sparseAmmo{0, 0, 13, 0, 0, 0, 0, 18};
	const auto sparse = ammolayout::Build(sparseAmmo, true);
	check(sparse.count == 2 && sparse.savedSlots[0] == 2 && sparse.savedSlots[1] == 7, "sparse centred shortcut retains cells three and eight");
	check(!ammolayout::Build(sparseAmmo, false).full, "legacy compact shortcut remains available for five or fewer arrows");
	wheelstorage::Model ammoModel{};
	for (int cell = 0; cell < 8; ++cell) ammoModel[2][cell] = {{arrows[cell]}, 0};
	std::ostringstream ammoFile;
	wheelstorage::WriteAssignments(ammoFile, "Archer", ammoModel);
	wheelstorage::Model restoredAmmo{};
	std::istringstream ammoInput(ammoFile.str());
	check(wheelstorage::ReadAssignments(ammoInput, "Archer", restoredAmmo) && restoredAmmo == ammoModel, "all eight Ammo assignments survive persistence with exact cells");
	wheelstorage::Model model{};
	model[0][0] = { {0x1234, 0xFFFFFFFF}, 1 };
	model[1][7] = { {0xFF123456}, 0 };
	model[2][3] = { {0x4321}, -1 };
	model[3][0] = { {0x88}, 0 };
	// IWP-024: a wheel reopened before the radial reported closed must not inherit the close hold (stick centred, key off the item).
	closeepoch::Epoch closing;
	check(!closing.Superseded(false, 4, true), "no close in progress is never superseded");
	check(!closing.Superseded(true, 4, true), "a close not yet noted in a read is not superseded by the press that started it");
	closing.Note(true, 4);
	check(!closing.Superseded(true, 4, false), "a close with no new press and no new opening keeps its hold until the radial reports closed");
	check(closing.Superseded(true, 4, true), "the wheel button pressed again during the close ends the hold at once");
	check(closing.Superseded(true, 5, false), "another reported opening during the close ends the hold");
	closing.Note(true, 5);
	check(closing.Superseded(true, 5, false), "later notes of the same close keep the opening count of its start (no drift)");
	closing.Reset();
	check(!closing.Superseded(true, 9, true), "an abandoned close is not superseded again");
	closing.Note(true, 9);
	closing.Note(false, 9);
	check(!closing.Superseded(true, 10, true), "a close that ended normally leaves nothing behind for the next one");
	closing.Note(true, 10);
	check(closing.Superseded(true, 10, true) && !closing.Superseded(false, 10, true), "the next close starts from its own count and only counts while closing");
	// Sanitize never erases for ownership: it has no ownership input, so entries this save does not carry or know survive it,
	// and only a structural repair asks for a new file.
	wheelstorage::Slot unowned{ {0xFF123456, 0x0001ABCD, 0x00000B21}, 2 };
	const auto unownedBefore = unowned;
	check(!wheelstorage::NormalizeSlot(unowned) && unowned == unownedBefore, "unowned spell, equipment and ammo ids survive Sanitize and ask for no save");
	wheelstorage::Slot offList{ {7, 8}, 5 };
	check(wheelstorage::NormalizeSlot(offList) && offList.entries == std::vector<std::uint32_t>({7, 8}) && offList.active == -1, "active outside the list is repaired, the entries kept");
	wheelstorage::Slot inactive{ {7, 8}, -1 };
	check(!wheelstorage::NormalizeSlot(inactive) && inactive.active == -1, "an inactive slot with entries is valid as it is");
	wheelstorage::Slot withZero{ {7, 0, 8}, 2 };
	check(wheelstorage::NormalizeSlot(withZero) && withZero.entries == std::vector<std::uint32_t>({7, 8}) && withZero.active == 1, "an empty form id is dropped and active follows its entry");
	wheelstorage::Slot zeroActive{ {7, 0}, 1 };
	check(wheelstorage::NormalizeSlot(zeroActive) && zeroActive.entries.size() == 1 && zeroActive.active == -1, "active on an empty form id becomes inactive");
	wheelstorage::Slot noEntries{ {}, 0 };
	check(wheelstorage::NormalizeSlot(noEntries) && noEntries.active == -1, "active on a slot without entries is repaired");
	std::ostringstream encoded;
	wheelstorage::WriteAssignments(encoded, "Hero #12345678", model);
	wheelstorage::Model decoded{};
	std::istringstream roundTrip(encoded.str());
	check(wheelstorage::ReadAssignments(roundTrip, "Hero #12345678", decoded) && decoded == model,
		"v1 round trip preserves four wheels, ordering, active and inactive slots");
	std::ostringstream header;
	wheelstorage::WriteHeader(header, "Hero #12345678");
	const std::string badRows[] = {
		"Equipment 1 active=1 nonsense", "Equipment 1 active=1 0x100000000", "Equipment 1 active=1 -1",
		"Equipment 1 active=1 0x1234junk", "Equipment 1 active=1 0x", "Equipment 1 active=1 0",
		"Equipment 0 active=1 1234", "Equipment 9 active=1 1234", "Equipment 1 active=-1 1234",
		"Equipment 1 active=2 1234", "Equipment 1 active=999999999999999999999 1234",
		"Equipment 1 active=1junk 1234", "Unknown 1 active=1 1234",
		"Magic 1 active=1 1234\nMagic 1 active=1 5678",
		"Equipment 1 active=1 1234\nMagic 2 active=1 broken"
	};
	for (const auto& row : badRows) {
		auto preserved = model;
		std::istringstream file(header.str() + row + '\n');
		check(!wheelstorage::ReadAssignments(file, "Hero #12345678", preserved) && preserved == model,
			"malformed file rejected without partial model mutation");
	}
	std::istringstream wrongCharacter(encoded.str());
	check(!wheelstorage::ReadAssignments(wrongCharacter, "Other", decoded), "identity mismatch rejected");
	std::istringstream ioFailure(encoded.str());
	ioFailure.setstate(std::ios::badbit);
	check(!wheelstorage::ReadAssignments(ioFailure, "Hero #12345678", decoded), "read failure rejected");
	std::istringstream empty(header.str());
	check(wheelstorage::ReadAssignments(empty, "Hero #12345678", decoded) && decoded == wheelstorage::Model{},
		"legacy empty setup accepted");
	std::istringstream noNewline(header.str() + "Magic 1 active=1 0Xff123456");
	check(wheelstorage::ReadAssignments(noNewline, "Hero #12345678", decoded) && decoded[1][0].entries[0] == 0xFF123456,
		"final line without newline and uppercase prefix accepted");

	sessionepoch::Epoch epoch;
	const auto unavailable = epoch.Observe({});
	const auto a = epoch.Observe({1, 42, true});
	check(!sessionepoch::Epoch::Allows(unavailable, a), "unavailable enqueue cannot dispatch");
	check(sessionepoch::Epoch::Allows(a, epoch.Observe({1, 42, true})), "normal same-character dispatch retained");
	const auto other = epoch.Observe({1, 43, true});
	check(!sessionepoch::Epoch::Allows(a, other), "same player pointer with different character seed cancels");
	const auto returned = epoch.Observe({1, 42, true});
	check(!sessionepoch::Epoch::Allows(a, returned), "return to old character does not revive work");
	const auto loading = epoch.Observe({1, 0, false});
	check(!sessionepoch::Epoch::Allows(returned, loading), "observed load interval cancels pending work");
	check(!sessionepoch::Epoch::Allows(returned, epoch.Observe({1, 42, true})), "same-character reload gets fresh epoch");
	const auto beforeReplacement = epoch.Observe({1, 42, true});
	check(!sessionepoch::Epoch::Allows(beforeReplacement, epoch.Observe({2, 42, true})), "player replacement cancels work");

	// Synthetic temporary files only. Never open installed wheel files or game saves.
	const auto dir = std::filesystem::temp_directory_path() /
		(L"iwp-storage-tests-" + std::to_wstring(GetCurrentProcessId()) + L"-" + std::to_wstring(GetTickCount64()));
	std::filesystem::create_directory(dir);
	const auto path = dir / L"synthetic-\u00F6.iwp";
	const auto contents = [&] {
		std::ifstream f(path, std::ios::binary);
		return std::string(std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>());
	};
	check(atomicfile::Write(path, encoded.str()) && contents() == encoded.str(), "atomic create with Unicode path");
	const std::string large(2 * 1024 * 1024 + 19, 'x');
	check(atomicfile::Write(path, large) && contents() == large, "atomic replace writes multiple chunks exactly");
	const HANDLE locked = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0, nullptr);
	check(locked != INVALID_HANDLE_VALUE, "failure fixture locks destination against replacement");
	check(!atomicfile::Write(path, "replacement") && contents() == large, "failed replacement preserves previous bytes");
	if (locked != INVALID_HANDLE_VALUE) CloseHandle(locked);
	check(std::distance(std::filesystem::directory_iterator(dir), std::filesystem::directory_iterator{}) == 1,
		"failed replacement removes only its temporary file");
	check(!atomicfile::Write(dir / L"missing" / L"file.iwp", "data"), "temporary create failure reported");
	check(atomicfile::Write(path, "") && contents().empty(), "empty atomic replacement");
	std::filesystem::remove(path);
	std::filesystem::remove(dir);
	std::printf("Review fixes: %d checks, %d failed\n", checks, failed);
	return failed;
}
