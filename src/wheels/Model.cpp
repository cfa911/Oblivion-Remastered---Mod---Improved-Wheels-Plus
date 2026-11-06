#include "core/ReadMemo.h"
// The wheel model and its file (Assignments/<character>.iwp): what may go on which wheel, saving and loading, keeping the wheels in step with the game's keys and the character's items.
#include "wheels/WheelsInternal.h"

#include "core/AtomicFile.h"
#include "core/Settings.h"
#include "engine/TesThread.h"
#include "game/Rows.h"
#include "game/SpellNames.h"

#include <fstream>
#include <sstream>

namespace wheels::detail
{
	const char* WheelName(int a_wheel) { return a_wheel >= 0 && a_wheel < kWheels ? kWheelNames[a_wheel] : "?"; }

	bool IsAmmo(std::uint32_t a_id)
	{
		auto* form = a_id ? RE::TESForm::LookupByID(a_id) : nullptr;
		return form && form->GetFormType() == RE::FormType::Ammo;
	}



	std::string NameOf(int a_wheel, std::uint32_t a_id)
	{
		return a_wheel == kMagic ? rows::SpellName(a_id) : inventory::NameOf(a_id);
	}

	std::filesystem::path FileFor(const std::string& a_name)
	{
		return settings::PluginFolder() / L"ImprovedWheelsPlus" / L"Assignments" / (wheelstorage::SafeName(a_name) + ".iwp");
	}

	// IWP-039: a file of the same character name under another seed. A missing file is then no proof of a new character, and
	// starting empty would wipe the save's native hotkeys and write an empty file next to the real one.
	bool OtherIdentityFile(const std::string& a_name, std::string& a_found, int* a_count = nullptr)
	{
		if (a_count) *a_count = 0;
		const std::string stem = wheelstorage::SafeName(wheelstorage::IdentityBase(a_name)) + " _";
		const std::wstring wstem(stem.begin(), stem.end());   // SafeName keeps ASCII only
		const auto own = FileFor(a_name);
		std::error_code ec;
		for (std::filesystem::directory_iterator it(own.parent_path(), ec), end; !ec && it != end; it.increment(ec)) {
			const std::wstring file = it->path().filename().wstring();
			if (file.size() != wstem.size() + 12 || !file.starts_with(wstem) || !file.ends_with(L".iwp") || it->path().filename() == own.filename()) continue;
			const bool hex = std::all_of(file.begin() + wstem.size(), file.end() - 4, [](wchar_t c) { return c < 128 && std::isxdigit(static_cast<int>(c)); });
			if (!hex) continue;
			std::string seed;
			for (auto c = file.begin() + wstem.size(); c != file.end() - 4; ++c) seed += static_cast<char>(*c);   // ASCII hex digits
			// a file without a single entry (the 142-byte file the old empty setup wrote, IWP-039) has nothing to keep: no candidate.
			// One that cannot be read as ours still counts.
			const std::string    identity = std::string(wheelstorage::IdentityBase(a_name)) + " #" + seed;
			wheelstorage::Model content{};
			if (std::ifstream in(it->path()); in && wheelstorage::ReadAssignments(in, identity, content)) {
				bool any = false;
				for (const auto& wheel : content) {
					for (const auto& slot : wheel) any = any || !slot.entries.empty();
				}
				if (!any) continue;
			}
			a_found = stem + seed + ".iwp";
			if (!a_count) return true;
			++*a_count;
		}
		return a_count && *a_count > 0;
	}

	// The wheels keep each Equipment slot's active entry on the game's quick key, and the save stores those keys: a file whose
	// active entries are exactly the save's keyed items is this character's. No key at all proves nothing.
	bool MatchesNativeKeys(const wheelstorage::Model& a_model)
	{
		const auto keys = inventory::Keys();
		int        keyed = 0;
		for (int s = 0; s < inventory::kSlots; ++s) {
			if (!keys[s]) continue;
			++keyed;
			const auto& slot = a_model[kEquip][s];
			if (slot.active < 0 || slot.active >= static_cast<int>(slot.entries.size()) || slot.entries[slot.active] != keys[s]) return false;
		}
		return keyed > 0;
	}

	// how many of a file's entries the character now playing has (items carried, spells known or scrolls carried); a_total = all entries
	int OwnedEntries(const wheelstorage::Model& a_model, int& a_total)
	{
		int owned = 0;
		a_total = 0;
		for (const int w : { kEquip, kMagic, kAmmo }) {
			for (const auto& slot : a_model[w]) {
				for (const auto id : slot.entries) {
					++a_total;
					owned += (w == kMagic ? rows::PlayerHasMagicEntry(id) : inventory::Has(id)) ? 1 : 0;
				}
			}
		}
		return owned;
	}

	// Assignments/adopted.txt: "<old file>\t<CharacterId>" per adoption, so an old file goes to one character only (it stays on disk for a downgrade)
	std::filesystem::path AdoptedIndex() { return settings::PluginFolder() / L"ImprovedWheelsPlus" / L"Assignments" / L"adopted.txt"; }

	bool AdoptedElsewhere(const std::string& a_file, const std::string& a_id)
	{
		std::ifstream in(AdoptedIndex());
		for (std::string line; std::getline(in, line);) {
			const auto tab = line.find('\t');
			if (tab != std::string::npos && line.substr(0, tab) == a_file && line.substr(tab + 1) != a_id) return true;
		}
		return false;
	}

	void MarkAdopted(const std::string& a_file, const std::string& a_id)
	{
		std::string data;
		if (std::ifstream in(AdoptedIndex()); in) {
			std::ostringstream all;
			all << in.rdbuf();
			data = all.str();
		}
		if (!data.empty() && data.back() != '\n') data += '\n';
		data += a_file + '\t' + a_id + '\n';
		if (!atomicfile::Write(AdoptedIndex(), data)) logger::error("wheels: could not record the adoption of {} in adopted.txt", a_file);
	}

	// The custom-spell names of an adopted wheel file go with it: "<old identity>.spells" is copied once to "<CharacterId>.spells"
	// with its identity line rewritten. The old file stays; an existing CharacterId names file is never overwritten.
	void AdoptSpellNames(const std::string& a_oldIdentity, const std::string& a_id)
	{
		const auto      folder = FileFor(a_id).parent_path();
		const auto      from = folder / (wheelstorage::SafeName(a_oldIdentity) + ".spells");
		const auto      to = folder / (wheelstorage::SafeName(a_id) + ".spells");
		std::error_code ec;
		if (std::filesystem::exists(to, ec) || ec) return;
		std::ifstream in(from);
		if (!in || !wheelstorage::ReadHeader(in, a_oldIdentity, wheelstorage::kSpellNamesSignature)) return;
		std::ostringstream data;
		wheelstorage::WriteHeader(data, a_id, wheelstorage::kSpellNamesSignature);
		for (std::string line; std::getline(in, line);) data << line << '\n';
		if (atomicfile::Write(to, data.str(), false)) {
			spellnames::Reset();   // read the names under the CharacterId from now on
		} else {
			logger::error("wheels: could not copy custom-spell names {} to {}", from.filename().string(), to.filename().string());
		}
	}

	// First load under the save's CharacterId (owner-approved rule, KB bugs/improved-wheels-plus/IWP-039 "Step 2 migration rule", item 6):
	// candidates are the non-empty "<name> #<seed>" files of this character's name not yet adopted by another CharacterId, the exact
	// name+seed file first, then the most recent. One is adopted when it is the exact file, its Equipment actives are the save's quick
	// keys, or the character owns at least half of its entries. Old files are copied, never removed.
	// 1 = adopted, 0 = nothing of this character's (fresh setup), -1 = held. a_report: log why (once per identity; Load retries every 5 s).
	int AdoptLegacy(const std::string& a_id, bool a_report)
	{
		const std::string  legacy = inventory::PlayerName();
		const std::string  base(wheelstorage::IdentityBase(legacy));
		const std::string  stem = wheelstorage::SafeName(base) + " _";
		const std::wstring wstem(stem.begin(), stem.end());   // SafeName keeps ASCII only
		struct Candidate
		{
			std::string                     file, identity;
			wheelstorage::Model             model{};
			std::filesystem::file_time_type time{};
			bool                            exact = false;
		};
		std::vector<Candidate> found;
		std::error_code        ec;
		for (std::filesystem::directory_iterator it(FileFor(a_id).parent_path(), ec), end; !ec && it != end; it.increment(ec)) {
			const std::wstring file = it->path().filename().wstring();
			if (file.size() != wstem.size() + 12 || !file.starts_with(wstem) || !file.ends_with(L".iwp")) continue;
			Candidate c;
			std::string seed;
			bool        hex = true;
			for (auto ch = file.begin() + wstem.size(); ch != file.end() - 4; ++ch) {
				hex = hex && *ch < 128 && std::isxdigit(static_cast<int>(*ch));
				seed += static_cast<char>(*ch);
			}
			if (!hex) continue;
			c.file = stem + seed + ".iwp";
			c.identity = base + " #" + seed;
			std::ifstream in(it->path());
			if (!in || !wheelstorage::ReadAssignments(in, c.identity, c.model)) continue;   // not ours or unreadable: never adopted
			int total = 0;
			OwnedEntries(c.model, total);
			if (total == 0 || AdoptedElsewhere(c.file, a_id)) continue;   // empty (the old empty setup's file), or another character's now
			std::error_code tec;
			c.time = std::filesystem::last_write_time(it->path(), tec);
			c.exact = c.identity == legacy;
			found.push_back(std::move(c));
		}
		if (found.empty()) return 0;
		std::sort(found.begin(), found.end(), [](const Candidate& a, const Candidate& b) { return a.exact != b.exact ? a.exact : a.time > b.time; });
		bool partial = false;
		for (const auto& c : found) {
			int       total = 0;
			const int owned = OwnedEntries(c.model, total);
			if (c.exact || MatchesNativeKeys(c.model) || (owned > 0 && owned * 2 >= total)) {
				g_wheels = c.model;
				MarkAdopted(c.file, a_id);
				AdoptSpellNames(c.identity, a_id);
				if (!Save()) logger::error("wheels: adopted assignments could not be written to {}; kept in memory, retried on the next change", FileFor(a_id).filename().string());
				logger::warn("wheels: assignments of {} adopted for CharacterId {} ({}); the old file is kept", c.file, a_id, FileFor(a_id).filename().string());
				return 1;
			}
			partial = partial || owned > 0;
			if (a_report) {
				logger::warn("wheels: old assignment file {} not adopted - the quick keys differ and this character has {} of its {} entries", c.file, owned, total);
			}
		}
		if (!partial) return 0;   // none of these files is this character's: start fresh, they stay for their owners
		if (a_report) {
			logger::warn("wheels: wheels stay off - to use one of these files anyway, close the game and rename it to {}", FileFor(a_id).filename().string());
		}
		return -1;
	}

	bool Save()
	{
		g_generation.fetch_add(1, std::memory_order_relaxed);
		if (g_loadedFor.empty() || g_storageRejected || g_identityHeld) {
			return false;
		}
		const auto path = FileFor(g_loadedFor);
		std::error_code ec;
		std::filesystem::create_directories(path.parent_path(), ec);
		if (ec) {
			logger::error("wheels: cannot create assignment directory: {}", ec.message());
			return false;
		}
		// Do not overwrite a foreign file even if it appeared after Load.
		const bool exists = std::filesystem::exists(path, ec);
		if (ec) {
			logger::error("wheels: cannot check assignment file: {}", ec.message());
			return false;
		}
		if (exists) {
			// A file may become available during native cleanup. Load it instead of replacing it.
			if (g_initializeEmpty) return false;
			std::ifstream existing(path);
			if (!wheelstorage::ReadHeader(existing, g_loadedFor)) {
				g_storageRejected = true;
				g_storageRetryAt = GetTickCount64() + 5000;
				logger::error("wheels: refusing to overwrite unrecognized assignment file {}", path.string());
				return false;
			}
		}
		std::ostringstream data;
		wheelstorage::WriteAssignments(data, g_loadedFor, g_wheels);
		if (!data || !atomicfile::Write(path, data.str(), !g_initializeEmpty)) {
			logger::error("wheels: atomic assignment save failed; previous file preserved: {}", path.string());
			return false;
		}
		return true;
	}

	void Load(const std::string& a_name)
	{
		const bool retrying = g_storageRejected && g_loadedFor == a_name;
		const bool wasHeld = g_identityHeld && g_loadedFor == a_name;
		ResetCarriedHistory();
		g_wheels = {};
		g_emptySetup.Reset();
		g_initializeEmpty = false;
		g_identityHeld = false;
		g_storageRejected = false;
		g_storageRetryAt = 0;
		g_loadedFor = a_name;
		g_generation.fetch_add(1, std::memory_order_relaxed);
		const auto path = FileFor(a_name);
		std::ifstream f(path);
		if (!f) {
			std::error_code ec;
			const bool exists = std::filesystem::exists(path, ec);
			if (ec || exists) {
				g_storageRejected = true;
				g_storageRetryAt = GetTickCount64() + 5000;
				if (!retrying) logger::error("wheels: assignment file cannot be read; will retry without overwriting: {}", path.string());
			} else if (const int adopted = wheelstorage::IsCharacterId(a_name) ? AdoptLegacy(a_name, !wasHeld) : 0; adopted > 0) {
				return;
			} else if (adopted < 0) {
				g_identityHeld = true;
				g_wheels = {};
				g_storageRetryAt = GetTickCount64() + 5000;
			} else if (std::string other; !wheelstorage::IsCharacterId(a_name) && OtherIdentityFile(a_name, other)) {
				g_identityHeld = true;
				g_storageRetryAt = GetTickCount64() + 5000;
				if (!wasHeld) {
					logger::warn("wheels: no assignment file for '{}' but {} exists - the character's seed changed (IWP-039); wheels stay off, no hotkey is cleared and no file is written",
						a_name, other);
				}
			} else {
				g_initializeEmpty = true;
				logger::info("wheels: no ImprovedWheelsPlus assignments for {} yet ({})", a_name, path.filename().string());
			}
			return;
		}
		if (!wheelstorage::ReadAssignments(f, a_name, g_wheels)) {
			// The manual fix the held warning names: an old file the player renamed to this CharacterId. Its old identity line is accepted
			// once and rewritten; a file naming another CharacterId is still refused.
			f.close();
			g_wheels = {};
			std::string inside;
			std::ifstream head(path);
			if (wheelstorage::IsCharacterId(a_name) && wheelstorage::ReadIdentity(head, inside) && !wheelstorage::IsCharacterId(inside)) {
				head.close();
				std::ifstream again(path);
				const bool    read = wheelstorage::ReadAssignments(again, inside, g_wheels);
				again.close();
				std::ostringstream data;
				if (read) wheelstorage::WriteAssignments(data, a_name, g_wheels);
				if (read && atomicfile::Write(path, data.str())) {
					AdoptSpellNames(inside, a_name);
					logger::warn("wheels: renamed assignment file accepted for CharacterId {} (it named '{}')", a_name, inside);
					return;
				}
				g_wheels = {};
			}
			g_storageRejected = true;
			g_storageRetryAt = GetTickCount64() + 5000;
			if (!retrying) logger::error("wheels: invalid assignment data or identity in {}; file left untouched; will retry", path.string());
			return;
		}
		logger::info("wheels: assignments loaded for {}", a_name);
		if (retrying) logger::warn("wheels: assignment file recovered for '{}': {}", a_name, path.filename().string());
	}

	int IndexOf(const Slot& a_slot, std::uint32_t a_id)
	{
		const auto it = std::find(a_slot.entries.begin(), a_slot.entries.end(), a_id);
		return it == a_slot.entries.end() ? -1 : static_cast<int>(it - a_slot.entries.begin());
	}

	int Cap() { return std::max(1, settings::Get().entriesPerSlot); }

	// a poison (an alchemy item the game types as poison): the game asks whether to apply it to the weapon
	bool Poison(std::uint32_t a_id)
	{
		auto* form = a_id ? RE::TESForm::LookupByID(a_id) : nullptr;
		auto* alch = form && form->GetFormType() == RE::FormType::AlchemyItem ? form->As<RE::AlchemyItem>() : nullptr;
		if (!alch) return false;
		auto* item = static_cast<RE::MagicItem*>(alch);
		if (item->GetSpellType() == RE::MagicSystem::SpellType::kPoison) return true;
		// a poison the player made reads as a potion: every one of its effects is hostile (Oblivion's own rule for poisons)
		int effects = 0;
		for ([[maybe_unused]] auto* e : item->effectList) ++effects;
		return effects > 0 && item->hostileCount >= effects;
	}

	void Reconcile()
	{
		auto keys = inventory::Keys();
		bool changed = false, cleared = false;
		std::vector<int> advanced;
		for (int s = 0; s < inventory::kSlots; ++s) {
			const auto id = keys[s];
			Slot& slot = g_wheels[kEquip][s];
			if (!id || IndexOf(slot, id) >= 0) {
				continue;
			}
			// A saved game's key is not an assignment request. Clear it instead of importing
			// an item (or evicting a stored entry when the slot is full). Pending explicit
			// menu assignments bypass Reconcile until Settle has recorded the new entry.
			inventory::ClearKey(id);
			cleared = true;
			logger::info("wheels: game hotkey {} cleared - stored wheel assignments only (no automatic import)", s + 1);
		}
		if (cleared) {
			keys = inventory::Keys();
		}
		// a keyed item leaves every OTHER slot's entries (an item is on one slot)
		for (int s = 0; s < inventory::kSlots; ++s) {
			if (!keys[s]) {
				continue;
			}
			for (int t = 0; t < inventory::kSlots; ++t) {
				Slot& other = g_wheels[kEquip][t];
				for (int idx = t == s ? -1 : IndexOf(other, keys[s]); idx >= 0; idx = IndexOf(other, keys[s])) {
					other.entries.erase(other.entries.begin() + idx);
					if (other.active == idx) {
						other.active = -1;
					} else if (other.active > idx) {
						--other.active;
					}
					changed = true;
					logger::info("wheels: {} left Equipment slot {}'s entries - it is on slot {}'s key, and an item is on one slot",
						inventory::NameOf(keys[s]), t + 1, s + 1);
				}
			}
		}
		// each slot's active entry on its key
		for (int s = 0; s < inventory::kSlots; ++s) {
			if (s == g_holdKey) continue;   // taken off on purpose, back when the radial has closed
			Slot& slot = g_wheels[kEquip][s];
			const int keyed = keys[s] ? IndexOf(slot, keys[s]) : -1;
			const int want = slot.active >= 0 && slot.active < static_cast<int>(slot.entries.size()) ? slot.active : -1;
			if (want >= 0 && want != keyed && Unreachable(s, slot.entries[want], keys).empty() && inventory::SetKey(slot.entries[want], s)) {
				logger::info("wheels: Equipment slot {}'s choice {} put back on the game's key {} (it held {})", s + 1, inventory::NameOf(slot.entries[want]),
					s + 1, keys[s] ? inventory::NameOf(keys[s]) : std::string("nothing"));
				keys[s] = slot.entries[want];
				continue;
			}
			// the active entry is gone from the bag (the last potion of a stack was drunk, the item sold or dropped): the slot moves on
			// to its next entry that can be used, instead of showing nothing
			if (want >= 0 && keyed < 0 && !Unreachable(s, slot.entries[want], keys).empty()) {
				if (const int next = NextEquipment(s, want, +1); next >= 0 && next != want && inventory::SetKey(slot.entries[next], s)) {
					logger::info("wheels: Equipment slot {}: {} is gone - {} is the slot's entry now ({} of {})", s + 1, inventory::NameOf(slot.entries[want]),
						inventory::NameOf(slot.entries[next]), next + 1, slot.entries.size());
					slot.active = next;
					keys[s] = slot.entries[next];
					changed = true;
					advanced.push_back(s);
					continue;
				}
			}
			// a slot that holds entries but shows none (the entry it showed was not carried when the save loaded, so it was dropped and nothing took its
			// place): its first usable entry becomes the shown one - the wheel read -/5 after a session in which a stack was used up
			if (want < 0 && keyed < 0 && !slot.entries.empty()) {
				if (const int next = NextEquipment(s, -1, +1); next >= 0 && inventory::SetKey(slot.entries[next], s)) {
					logger::info("wheels: Equipment slot {} showed nothing - {} is the slot's entry now ({} of {})", s + 1, inventory::NameOf(slot.entries[next]), next + 1,
						slot.entries.size());
					slot.active = next;
					keys[s] = slot.entries[next];
					changed = true;
					advanced.push_back(s);
					continue;
				}
			}
			slot.active = keyed;
		}
		if (changed) {
			Save();
			for (const int s : advanced) PreloadIcon(s);
		}
	}

	// What the wheels hold that THIS character does not have now (a save loaded after another, or a spell or item lost since) is
	// KEPT: a stored entry is the character's, not the save's, and a later save of the same character may carry it - erasing it here
	// wiped the .iwp assignments of every later save (the audit of 2026-10-23). Nothing ever tries to use or draw a form the save
	// does not have because the display and every use already filter by ownership (KnownMagicEntry, Unreachable, IsAmmo and
	// inventory::Has in AmmoSlots / SlotCounts). Run once, a moment after a character loads, when the inventory and the spell list
	// exist. It only repairs the structure (an empty form ID, an active entry outside the list) and writes the file again only for
	// that; an unowned entry is logged once and left alone.
	void Sanitize()
	{
		int repaired = 0, unowned = 0;
		for (int w : { static_cast<int>(kEquip), static_cast<int>(kMagic), static_cast<int>(kAmmo) }) {
			for (int s = 0; s < inventory::kSlots; ++s) {
				Slot& slot = g_wheels[w][s];
				if (slot.entries.empty()) continue;
				if (wheelstorage::NormalizeSlot(slot)) {
					++repaired;
					logger::info("wheels: {} slot {} had an empty form ID or an active entry outside its list - repaired", WheelName(w), s + 1);
				}
				for (const auto id : slot.entries) {
					const bool owned = RE::TESForm::LookupByID(id) != nullptr &&
						(w == kMagic ? rows::PlayerHasMagicEntry(id) : inventory::Has(id) && (w != kAmmo || IsAmmo(id)));
					if (!owned) {
						++unowned;
						logger::info("wheels: {} 0x{:08X} on {} slot {} is not owned, kept (this character may have it in another save)",
							w == kMagic ? "spell" : "item", id, WheelName(w), s + 1);
					}
				}
			}
		}
		if (unowned) {
			logger::info("wheels: {} stored entr{} not owned in this save, kept (shown only while owned)", unowned, unowned == 1 ? "y is" : "ies are");
		}
		if (repaired) {
			Save();
		}
	}

	static bool EnsureLoadedNow()
	{
		if (!RE::PlayerCharacter::GetSingleton()) {
			return false;
		}
		const std::string name = inventory::Identity();
		const auto now = GetTickCount64();
		if (name != g_loadedFor) {
			// Settle before reading or creating this identity's file. Actions stay blocked until then.
			if (name != g_loadCandidate) {
				g_loadCandidate = name;
				// a CharacterId comes from the save and is right on its first read (IWP-039 tests, 2026-11-06); a seed name may still change
				g_loadCandidateAt = now + (wheelstorage::IsCharacterId(name) ? 1000 : 3000);
			}
			if (now < g_loadCandidateAt) return false;
			Load(name);
			g_sanitized = false;
			g_sanitizeAt = now;   // the identity has already settled
		} else {
			g_loadCandidate.clear();
			g_loadCandidateAt = 0;
		}
		if (g_storageRejected) {
			if (now < g_storageRetryAt) return false;
			Load(name);
			if (g_storageRejected) return false;
		}
		if (g_identityHeld) {
			if (now < g_storageRetryAt) return false;
			Load(name);   // the character's own file may have appeared
			if (g_identityHeld || g_storageRejected) return false;
		}
		if (g_initializeEmpty) {
			// Keep saved setup absent and wheel actions blocked until native cleanup really completes.
			// Never strip ExtraQuickKey here: the native remover needs it for its bookkeeping.
			if (GetTickCount64() < g_sanitizeAt || !inventory::Ready() || AssignPending()) return false;
			if (now < g_storageRetryAt) return false;
			std::error_code ec;
			const bool exists = std::filesystem::exists(FileFor(name), ec);
			if (ec || exists) {
				Load(name);   // a setup became available after the missing-file check
				return false;
			}
			if (!g_emptySetup.Complete()) {
				if (testhread::Installed()) {
					if (const auto ticket = g_emptySetup.Queue(GetTickCount64())) {
						testhread::Post([ticket] {
							g_emptySetup.Run(ticket, [] { return inventory::ClearAllNativeKeys(); });
						});
					}
				}
				return false;
			}
			if (!Save()) {
				g_storageRetryAt = now + 5000;
				return false;
			}
			g_initializeEmpty = false;
			g_storageRetryAt = 0;
			quickkeys::RequestRedraw();
			logger::warn("wheels: empty setup ready - native item and spell hotkeys verified empty before assignments enabled; saved for '{}' in {}", name, FileFor(name).filename().string());
		}
		if (!g_sanitized && GetTickCount64() >= g_sanitizeAt && inventory::Ready() && rows::PlayerSpellCount() > 0 &&
			!AssignPending()) {
			g_sanitized = true;
			Sanitize();
		}
		// not while the game's own assign is watched: Settle reads what arrived on the key and decides ADDED / REMOVED /
		// moved itself - reconciling first (the favourites column and the slot counters ask every 100 ms) took the
		// arrival as already "in the slot"
		if (!AssignPending()) {
			RemoveLeavers();
			RemoveGoneInMenus();
			Reconcile();
		}
		MigrateAmmo();
		return true;
	}

	bool EnsureLoaded()
	{
		// Recheck context even on a memo hit. Inventory may also change outside our
		// actions: maintenance is reused only in this read, never across reads.
		if (!ObserveContext()) return false;
		static thread_local readmemo::Memo<std::uint32_t, bool> memo;
		return memo.Get(g_generation.load(std::memory_order_relaxed), [] { return EnsureLoadedNow(); });
	}
}
