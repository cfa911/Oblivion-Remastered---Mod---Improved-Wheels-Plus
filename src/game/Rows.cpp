#include "game/Rows.h"
#include "core/TextUtil.h"

#include "engine/PEHook.h"
#include "game/Inventory.h"
#include "engine/Reflect.h"
#include "core/Settings.h"
#include "game/SessionContext.h"
#include "game/SpellNames.h"
#include "game/TypedNames.h"
#include "engine/Ui.h"

#include <unordered_map>
#include <unordered_set>

namespace rows
{
	namespace
	{
		constexpr const wchar_t* kInventoryRowPath = L"/Game/UI/Original/GameMenuLayer/Inventory/MainPart/WBP_OriginalMenu_InventoryEntry.WBP_OriginalMenu_InventoryEntry_C";
		constexpr const wchar_t* kMagicRowPath = L"/Game/UI/Modern/GameMenuLayer/Magic/WBP_ModernMenu_MagicEntry.WBP_ModernMenu_MagicEntry_C";
		constexpr const wchar_t* kInventoryPropsPath = L"/Script/Altar.OriginalInventoryMenuItemProperties";
		constexpr const wchar_t* kMagicPropsPath = L"/Script/Altar.LegacyMagicMenuItemProperties";
		constexpr std::ptrdiff_t kUTESFormID = 0x28;   // UTESForm::m_formID (CommonLibOB64 UTESForm.h)

		struct Kind
		{
			UE::UClass*  rowClass = nullptr;
			std::int32_t properties = -1;   // the row widget's Properties struct
			std::int32_t name = -1, icon = -1, form = -1, index = -1;   // index: InventoryIndex
			std::int32_t inContainer = -1, playerItem = -1;   // bIsInContainerMenu, bIsInventoryItem (inventory rows)
			std::int32_t weight = -1, count = -1;   // Weight (float), Count (int32): what tells apart items that share a typed name
			reflect::Handle highlighted;   // kept across frames: resolved through its slot before every use
			UE::UFunction* fnSelection = nullptr;   // this row class's BP_OnItemSelectionChanged
			std::unordered_map<UE::UObject*, std::int32_t> seen;   // row -> its object-array slot
		};
		Kind g_inventory, g_magic;
		void ObserveContext();

		std::unordered_map<std::uint32_t, reflect::Handle> g_itemIcons, g_spellIcons;   // resolved through the slot when asked
		std::unordered_map<std::string, std::uint32_t> g_spellsByName;
		bool g_dirty = true;   // a row changed since the caches were last filled
		std::atomic<bool> g_rowsChanged{ true };
		std::atomic<bool> g_magicRowsChanged{ true };   // the same for the magic menu's rows   // an inventory row fired an event since the favourites column last looked

		// a_row must be live NOW: resolved from its Handle (or its seen slot) this frame, never a pointer kept from before
		void* PropsOf(Kind& a_kind, UE::UObject* a_row)
		{
			return a_row && a_kind.properties >= 0 ? reflect::At<void>(a_row, a_kind.properties) : nullptr;
		}

		std::string NameOfRow(Kind& a_kind, UE::UObject* a_row)
		{
			void* props = PropsOf(a_kind, a_row);
			auto* text = reflect::At<UE::FText>(props, a_kind.name);
			return text ? reflect::Text(*text) : std::string();
		}

		// the name's string-table key: a spell's full name in the game data IS this key ("LOC_FN_...")
		std::string KeyOfRow(Kind& a_kind, UE::UObject* a_row)
		{
			void* props = PropsOf(a_kind, a_row);
			auto* text = reflect::At<UE::FText>(props, a_kind.name);
			return text ? reflect::TextKey(*text) : std::string();
		}

		std::uint32_t SpellOfRow(Kind& a_kind, UE::UObject* a_row);   // defined below, once the player's spell list is reachable

		// An inventory row's item. Its Properties.form is null in the player's inventory (read 2026-09-29: Iron Arrow,
		// bIsInventoryItem true, form null), so the name decides: its string-table key is the item's full name in the
		// game data; a name the player gave (an enchanted item) is not from a table and is matched as it reads.
		int IndexOfRow(Kind& a_kind, UE::UObject* a_row);

		// An item the player named (a crafted potion, an enchanted piece) has no string-table key: its full name is a placeholder and the row's text is
		// the typed text. Several items can share that text, and the row's InventoryIndex is only within about 2 of the item's place in the list, so the
		// row's own numbers decide: of the carried items with that typed text, the one whose weight matches the row's Weight (and, when two match, whose
		// count matches the row's Count).
		// row InventoryIndex -> (the item's place in the list - the index, when it was seen), learned from rows whose item is known by its name key. The two
		// orders differ by a place or two that is the same over a stretch of the list, so a neighbour's difference tells where a twin row's item sits exactly.
		std::map<int, std::pair<int, ULONGLONG>> g_offsetAt;

		void NoteOffset(int a_idx, std::uint32_t a_id)
		{
			if (a_idx < 0) return;
			const int pos = inventory::PositionOf(a_id);
			if (pos >= 0) g_offsetAt[a_idx] = { pos - a_idx, GetTickCount64() };
		}

		// the list place a row's item has, from the nearest fresh neighbour's difference (-1 when none is known)
		int PredictedPosition(int a_idx)
		{
			const ULONGLONG now = GetTickCount64();
			int             best = -1, bestDist = 1 << 30, offset = 0;
			for (const auto& [i, v] : g_offsetAt) {
				if (now - v.second > 15000) continue;   // the list changes (an item added or gone): only what was seen lately
				const int d = std::abs(i - a_idx);
				if (d < bestDist) bestDist = d, best = i, offset = v.first;
			}
			return best < 0 ? -1 : a_idx + offset;
		}

		std::uint32_t CustomItemOfRow(UE::UObject* a_row, void* a_props, const std::string& a_text)
		{
			const auto* weight = reflect::At<float>(a_props, g_inventory.weight);
			const auto* count = reflect::At<std::int32_t>(a_props, g_inventory.count);
			const int   idx = IndexOfRow(g_inventory, a_row);
			// the carried items with this typed text, narrowed by the row's Weight and then Count
			std::vector<inventory::Carried> cands;
			for (const auto& c : inventory::CarriedWithTypedNames()) {
				if (DisplayName(c.id) == a_text && (!weight || c.weight < 0.0f || std::fabs(*weight - c.weight) <= 0.06f)) cands.push_back(c);
			}
			if (cands.size() > 1 && count) {
				std::vector<inventory::Carried> same;
				for (const auto& c : cands) {
					if (c.count == *count) same.push_back(c);
				}
				if (!same.empty()) cands = std::move(same);
			}
			if (cands.empty()) return 0;
			if (cands.size() == 1) return cands.front().id;
			// several: the one that sits where the row's neighbours say this row's item sits (does not depend on which twin rows are built now)
			if (const int predicted = PredictedPosition(idx); predicted >= 0) {
				for (const auto& c : cands) {
					if (inventory::PositionOf(c.id) == predicted) return c.id;
				}
			}
			// still several: the live rows that look just like this one take the candidates in order - rows by InventoryIndex, items by their place in the
			// list (the two orders agree to within a place or two, so the order is the same even where the numbers are not)
			std::vector<std::pair<int, UE::UObject*>> twins;   // InventoryIndex, row
			for (const auto& [obj, slot] : g_inventory.seen) {
				auto* row = reflect::Get(obj, slot);
				void* p = row ? PropsOf(g_inventory, row) : nullptr;
				if (!p) continue;
				const auto* n = reflect::At<UE::FText>(p, g_inventory.name);
				const auto* w = reflect::At<float>(p, g_inventory.weight);
				const auto* k = reflect::At<std::int32_t>(p, g_inventory.count);
				if (!n || NameOfRow(g_inventory, row) != a_text) continue;
				if (weight && w && std::fabs(*w - *weight) > 0.06f) continue;
				if (count && k && *k != *count) continue;
				twins.emplace_back(IndexOfRow(g_inventory, row), row);
			}
			std::sort(twins.begin(), twins.end());
			std::sort(cands.begin(), cands.end(), [](const auto& a, const auto& b) { return inventory::PositionOf(a.id) < inventory::PositionOf(b.id); });
			if (twins.size() == cands.size()) {
				for (std::size_t r = 0; r < twins.size(); ++r) {
					if (twins[r].second == a_row) return cands[r].id;
				}
			}
			// some twins are not built (the list shows only part of its rows): the item whose place is nearest to the row's index
			std::uint32_t best = 0;
			int           bestDist = 1 << 30;
			for (const auto& c : cands) {
				const int d = std::abs(inventory::PositionOf(c.id) - idx);
				if (d < bestDist) bestDist = d, best = c.id;
			}
			return best;
		}

		std::uint32_t FormOfInventoryRow(UE::UObject* a_row)
		{
			void* props = PropsOf(g_inventory, a_row);
			if (!props) {
				return 0;
			}
			auto** form = reflect::At<UE::UObject*>(props, g_inventory.form);
			if (form && *form && reflect::IsLive(*form)) {
				const auto id = static_cast<std::uint32_t>(*reinterpret_cast<const std::int64_t*>(reinterpret_cast<const std::uint8_t*>(*form) + kUTESFormID));
				if (RE::TESForm::LookupByID(id)) {
					return id;
				}
			}
			const std::string key = KeyOfRow(g_inventory, a_row);
			const std::string text = NameOfRow(g_inventory, a_row);
			if (const auto id = inventory::FindByName(key.empty() ? text : key)) {
				if (!key.empty()) NoteOffset(IndexOfRow(g_inventory, a_row), id);
				return id;
			}
			return key.empty() && !text.empty() ? CustomItemOfRow(a_row, props, text) : 0u;
		}

		UE::UObject* IconOfRow(Kind& a_kind, UE::UObject* a_row)
		{
			void* props = PropsOf(a_kind, a_row);
			auto** icon = reflect::At<UE::UObject*>(props, a_kind.icon);
			return icon ? *icon : nullptr;
		}

		// what every live row shows now goes into the icon caches
		void Harvest()
		{
			ObserveContext();
			if (!g_dirty) {
				return;
			}
			g_dirty = false;
			for (auto it = g_inventory.seen.begin(); it != g_inventory.seen.end();) {
				auto* row = reflect::Get(it->first, it->second);
				if (!row) {
					it = g_inventory.seen.erase(it);
					continue;
				}
				if (const auto id = FormOfInventoryRow(row)) {
					if (auto* icon = IconOfRow(g_inventory, row); icon && reflect::IsLive(icon)) {
						g_itemIcons[id] = reflect::Hold(icon);
					}
				}
				++it;
			}
			for (auto it = g_magic.seen.begin(); it != g_magic.seen.end();) {
				auto* row = reflect::Get(it->first, it->second);
				if (!row) {
					it = g_magic.seen.erase(it);
					continue;
				}
				if (const auto id = SpellOfRow(g_magic, row)) {
					if (auto* icon = IconOfRow(g_magic, row); icon && reflect::IsLive(icon)) {
						g_spellIcons[id] = reflect::Hold(icon);
					}
				}
				++it;
			}
		}

		void OnRow(Kind& a_kind, UE::UObject* a_obj, UE::UFunction* a_fn, void* a_params)
		{
			a_kind.seen.insert_or_assign(a_obj, a_obj->internalIndex);   // live: the engine is calling it now
			g_dirty = true;
			if (!a_kind.fnSelection && pe::FunctionName(a_fn) == "BP_OnItemSelectionChanged") {
				a_kind.fnSelection = a_fn;
			}
			if (a_fn == a_kind.fnSelection && a_params && *static_cast<const bool*>(a_params)) {
				a_kind.highlighted = reflect::Hold(a_obj);
			}
		}

		void OnInventoryRow(UE::UObject* a_obj, UE::UFunction* a_fn, void* a_params)
		{
			OnRow(g_inventory, a_obj, a_fn, a_params);
			g_rowsChanged.store(true, std::memory_order_relaxed);
		}
		void OnMagicRow(UE::UObject* a_obj, UE::UFunction* a_fn, void* a_params)
		{
			OnRow(g_magic, a_obj, a_fn, a_params);
			g_magicRowsChanged.store(true, std::memory_order_relaxed);
		}

		void TryWatch(Kind& a_kind, const wchar_t* a_rowPath, const wchar_t* a_propsPath, pe::Handler a_handler, bool a_hasForm, const char* a_what)
		{
			if (a_kind.rowClass || !reflect::Ok()) {
				return;
			}
			auto* cls = UE::StaticFindObject<UE::UClass>(nullptr, nullptr, a_rowPath);
			auto* props = UE::StaticFindObject<UE::UStruct>(nullptr, nullptr, a_propsPath);
			if (!cls || !props) {
				return;
			}
			a_kind.properties = reflect::Offset(cls, "Properties");
			a_kind.name = reflect::Offset(props, "Name");
			a_kind.icon = reflect::Offset(props, "Icon");
			a_kind.index = reflect::Offset(props, "InventoryIndex");
			a_kind.weight = reflect::Offset(props, "Weight");
			a_kind.count = reflect::Offset(props, "Count");
			a_kind.form = a_hasForm ? reflect::Offset(props, "form") : -1;
			if (a_hasForm) {
				a_kind.inContainer = reflect::Offset(props, "bIsInContainerMenu");
				a_kind.playerItem = reflect::Offset(props, "bIsInventoryItem");
			}
			if (a_kind.properties < 0 || a_kind.name < 0 || a_kind.icon < 0 || (a_hasForm && a_kind.form < 0)) {
				logger::error("rows: the {} row's layout is not as read (Properties {}, Name {}, Icon {}, form {}) - Favourite is off there",
					a_what, a_kind.properties, a_kind.name, a_kind.icon, a_kind.form);
				a_kind.rowClass = cls;   // do not ask again
				a_kind.properties = -1;
				return;
			}
			if (pe::Watch(cls, a_handler)) {
				a_kind.rowClass = cls;
				logger::info("rows: watching the {} rows (Properties at 0x{:X}: Name 0x{:X}, Icon 0x{:X}{})", a_what, a_kind.properties, a_kind.name,
					a_kind.icon, a_hasForm ? std::format(", form 0x{:X}", a_kind.form) : std::string());
			}
		}

		RE::TESSpellList* PlayerSpells()
		{
			auto* player = RE::PlayerCharacter::GetSingleton();
			auto* base = player ? player->data.objectReference : nullptr;
			if (!base || !base->Is(RE::FormType::NPC)) {
				return nullptr;
			}
			return static_cast<RE::TESSpellList*>(static_cast<RE::TESNPC*>(base));
		}
		// the row's InventoryIndex (-1 when unreadable)
		int IndexOfRow(Kind& a_kind, UE::UObject* a_row)
		{
			void* props = PropsOf(a_kind, a_row);
			const auto* v = a_kind.index >= 0 ? reflect::At<std::int32_t>(props, a_kind.index) : nullptr;
			return v ? *v : -1;
		}

		// The TES thread may change the player's spell list while the UI thread reads it, and a node freed under a walk faults: each raw walk
		// sits in a helper of plain pointers (__try cannot unwind C++ objects, C2712) and a fault reads as "not there". The list's first node
		// is inline ({ item, next }); an empty list is a null item and a null next.
		using SpellNode = RE::BSSimpleList<RE::SpellItem*>::Node;

		// where a spell sits in the player's list (-1 = not in it); a_wanted is a pointer to compare, a_formID (when no pointer) is read
		int SpellPositionGuarded(RE::TESSpellList* a_list, const RE::SpellItem* a_wanted, std::uint32_t a_formID)
		{
			__try {
				int i = 0;
				for (auto* node = reinterpret_cast<SpellNode*>(&a_list->spells); node; node = node->next, ++i) {
					RE::SpellItem* spell = node->item;
					if (a_wanted ? spell == a_wanted : spell && spell->GetFormID() == a_formID) {
						return i;
					}
				}
			} __except (EXCEPTION_EXECUTE_HANDLER) {
			}
			return -1;
		}

		std::uint32_t SpellAtGuarded(RE::TESSpellList* a_list, int a_pos)
		{
			__try {
				int i = 0;
				for (auto* node = reinterpret_cast<SpellNode*>(&a_list->spells); node; node = node->next) {
					if (i++ == a_pos) {
						return node->item ? node->item->GetFormID() : 0u;
					}
				}
			} __except (EXCEPTION_EXECUTE_HANDLER) {
			}
			return 0;
		}

		std::size_t SpellCountGuarded(RE::TESSpellList* a_list)
		{
			std::size_t n = 0;
			__try {
				for (auto* node = reinterpret_cast<SpellNode*>(&a_list->spells); node; node = node->next) {
					n += node->item ? 1 : 0;
				}
			} __except (EXCEPTION_EXECUTE_HANDLER) {
			}
			return n;
		}

		// where a spell sits in the player's list (-1 = not in it)
		int ListPosition(std::uint32_t a_formID)
		{
			auto* list = PlayerSpells();
			return list && a_formID ? SpellPositionGuarded(list, nullptr, a_formID) : -1;
		}
		std::uint32_t SpellAtPosition(int a_pos)
		{
			auto* list = PlayerSpells();
			return list && a_pos >= 0 ? SpellAtGuarded(list, a_pos) : 0u;
		}

		// A magic row's InventoryIndex is its spell's place in the player's list, counted from 1: proven live on the rows whose spell the
		// string-table key names (the vanilla ones), then used for the spells the player made, which have no key (their full
		// name is a placeholder "UI_UserInputText_..." and the row's text is not from a table) and may share names.
		std::unordered_map<int, bool> g_indexAgrees;   // InventoryIndex -> did the key's spell sit at that place
		bool                          g_indexTrusted = false;
		bool                          g_indexJudged = false;

		void JudgeIndex()
		{
			int yes = 0;
			for (const auto& [idx, ok] : g_indexAgrees) {
				yes += ok ? 1 : 0;
			}
			const int  total = static_cast<int>(g_indexAgrees.size());
			const bool trusted = total >= 12 && yes * 10 >= total * 9;
			if (trusted != g_indexTrusted || !g_indexJudged) {
				if (total >= 12 || g_indexJudged) {
					logger::info("rows: magic row InventoryIndex {} the player's list order ({} of {} keyed rows agree) - {}",
						trusted ? "follows" : "does not follow", yes, total, trusted ? "used for spells without a key" : "not used");
					g_indexJudged = true;
				}
				g_indexTrusted = trusted;
			}
		}

		// A magic row's spell. The string-table key is the spell's full name in the game data; a spell the player made has no
		// key, so the row's index decides (never its name: two spells may share one).
		std::uint32_t SpellOfRow(Kind& a_kind, UE::UObject* a_row)
		{
			const int idx = IndexOfRow(a_kind, a_row);
			if (const auto id = SpellByName(KeyOfRow(a_kind, a_row))) {
				if (idx >= 0) {
					const int pos = ListPosition(id);
					if (g_indexAgrees.insert_or_assign(idx, pos == idx - 1).second) {
						JudgeIndex();
					}
				}
				return id;
			}
			if (idx >= 0 && g_indexTrusted) {
				const auto id = SpellAtPosition(idx - 1);   // InventoryIndex is the list position plus one (read 2026-10-16, 50 of 50 keyed rows)
				if (id && (id >> 24) == 0xFF) {
					ObserveContext();
					spellnames::Remember(id, NameOfRow(a_kind, a_row));
					return id;   // a spell made in the game
				}
			}
			return SpellByName(NameOfRow(a_kind, a_row));
		}


	}

	void Tick()
	{
		reflect::SelfCheck();
		TryWatch(g_inventory, kInventoryRowPath, kInventoryPropsPath, &OnInventoryRow, true, "inventory");
		TryWatch(g_magic, kMagicRowPath, kMagicPropsPath, &OnMagicRow, false, "magic menu");
	}

	std::vector<UE::UObject*> LiveInventoryRows()
	{
		std::vector<UE::UObject*> out;
		for (auto it = g_inventory.seen.begin(); it != g_inventory.seen.end();) {
			auto* row = reflect::Get(it->first, it->second);
			if (!row) {
				it = g_inventory.seen.erase(it);
				continue;
			}
			out.push_back(row);
			++it;
		}
		return out;
	}

	std::string InventoryRowKey(UE::UObject* a_row)
	{
		const std::string key = KeyOfRow(g_inventory, a_row);
		if (!key.empty()) return key;
		// a typed name is shared by several items: the row's index, weight and count make the key tell them apart, so a row reused for a twin is looked up again
		void*       props = PropsOf(g_inventory, a_row);
		const auto* w = reflect::At<float>(props, g_inventory.weight);
		const auto* k = reflect::At<std::int32_t>(props, g_inventory.count);
		return std::format("{}#{}|{:.2f}|{}", NameOfRow(g_inventory, a_row), IndexOfRow(g_inventory, a_row), w ? *w : -1.0f, k ? *k : -1);
	}

	std::uint32_t InventoryRowForm(UE::UObject* a_row) { return FormOfInventoryRow(a_row); }

	int InventoryRowSide(UE::UObject* a_row)
	{
		void*       props = PropsOf(g_inventory, a_row);
		const bool* inContainer = reflect::At<bool>(props, g_inventory.inContainer);
		const bool* playerItem = reflect::At<bool>(props, g_inventory.playerItem);
		if (!inContainer || !*inContainer) return 0;
		return playerItem && *playerItem ? 1 : 2;
	}

	bool TakeInventoryRowsChanged() { return g_rowsChanged.exchange(false, std::memory_order_relaxed); }

	UE::UClass* InventoryRowClass() { return g_inventory.properties >= 0 ? g_inventory.rowClass : nullptr; }
	UE::UClass* MagicRowClass() { return g_magic.properties >= 0 ? g_magic.rowClass : nullptr; }

	std::vector<UE::UObject*> LiveMagicRows()
	{
		std::vector<UE::UObject*> out;
		for (auto it = g_magic.seen.begin(); it != g_magic.seen.end();) {
			auto* row = reflect::Get(it->first, it->second);
			if (!row) {
				it = g_magic.seen.erase(it);
				continue;
			}
			out.push_back(row);
			++it;
		}
		return out;
	}

	std::string MagicRowKey(UE::UObject* a_row)
	{
		const std::string key = KeyOfRow(g_magic, a_row);
		return key.empty() ? NameOfRow(g_magic, a_row) : key;
	}

	std::uint32_t MagicRowSpell(UE::UObject* a_row) { return SpellOfRow(g_magic, a_row); }

	bool TakeMagicRowsChanged() { return g_magicRowsChanged.exchange(false, std::memory_order_relaxed); }

	std::uint32_t HighlightedItem()
	{
		Harvest();
		auto*      row = reflect::Get(g_inventory.highlighted);   // gone after a drop or a close: no item
		const auto id = FormOfInventoryRow(row);
		logger::info("rows: highlighted inventory row: {} ({})", row ? NameOfRow(g_inventory, row) : std::string("(gone)"),
			id ? std::format("0x{:08X}", id) : "no item");
		return id;
	}

	std::uint32_t HighlightedPlayerItemInContainer()
	{
		UE::UObject* row = reflect::Get(g_inventory.highlighted);
		void* props = PropsOf(g_inventory, row);
		const bool* inContainer = reflect::At<bool>(props, g_inventory.inContainer);
		const bool* playerItem = reflect::At<bool>(props, g_inventory.playerItem);
		if (!inContainer || !playerItem || !*inContainer || !*playerItem) {
			return 0;
		}
		return FormOfInventoryRow(row);
	}

	std::uint32_t HighlightedSpell()
	{
		Harvest();
		auto*             row = reflect::Get(g_magic.highlighted);
		const std::string name = NameOfRow(g_magic, row);
		const std::string key = KeyOfRow(g_magic, row);
		const auto id = SpellOfRow(g_magic, row);
		logger::info("rows: highlighted magic row: \"{}\" (key {}; {}) (row index {})", name, key.empty() ? "none" : key,
			id ? std::format("spell 0x{:08X}", id) : "not one of the player's spells", IndexOfRow(g_magic, row));
		return id;
	}

	namespace
	{
		// the first of the two inventory icons (male, female) that has a path
		const RE::TESIcon* BipedIcon(RE::TESBipedModelForm* a_biped)
		{
			if (!a_biped) return nullptr;
			for (const auto& i : a_biped->inventoryIcon) {
				if (i.textureName.c_str() && *i.textureName.c_str()) return &i;
			}
			return nullptr;
		}

		// The form's own icon path (TESIcon), as the plugin data gives it: "Weapons\IronArrow.dds"
		const RE::TESIcon* IconOf(RE::TESForm* a_form)
		{
			if (!a_form) return nullptr;
			switch (a_form->GetFormType()) {
			case RE::FormType::Ammo: return a_form->As<RE::TESAmmo>();
			case RE::FormType::Weapon: return a_form->As<RE::TESObjectWEAP>();
			case RE::FormType::Book: return a_form->As<RE::TESObjectBOOK>();
			case RE::FormType::Misc: return a_form->As<RE::TESObjectMISC>();
			case RE::FormType::Apparatus: return a_form->As<RE::TESObjectAPPA>();
			case RE::FormType::Ingredient: return a_form->As<RE::IngredientItem>();
			case RE::FormType::AlchemyItem: return a_form->As<RE::AlchemyItem>();
			case RE::FormType::Light: return a_form->As<RE::TESObjectLIGH>();
			// armor and clothing keep their picture in the biped form's inventoryIcon[2] (male, female), not in a TESIcon base:
			// without these two the wheel had no picture for them until an inventory row had shown it (user report, 2026-10-19)
			case RE::FormType::Armor: return BipedIcon(a_form->As<RE::TESObjectARMO>());
			case RE::FormType::Clothing: return BipedIcon(a_form->As<RE::TESObjectCLOT>());
			default: return nullptr;
			}
		}

		// the remaster's texture for an icon path: see textutil::IconAssetPath (tested in tests/)
		std::string IconAssetPath(std::string_view a_icon)
		{
			return textutil::IconAssetPath(a_icon);
		}

		std::unordered_set<std::uint32_t> g_iconFailed;   // forms whose icon path loaded nothing: not tried again
	}

	// the icon a row showed for it this session; else the form's own icon, loaded from the paks (works straight after a
	// load - the owner, 2026-09-30: no arrows on the ammo wheel until the inventory had shown them)
	UE::UObject* ItemIcon(std::uint32_t a_formID)
	{
		Harvest();
		if (const auto it = g_itemIcons.find(a_formID); it != g_itemIcons.end()) {
			if (auto* icon = reflect::Get(it->second)) return icon;
		}
		if (!a_formID || g_iconFailed.contains(a_formID)) return nullptr;
		const auto* tex = IconOf(RE::TESForm::LookupByID(a_formID));
		const char* icon = tex ? tex->textureName.c_str() : nullptr;
		const auto  path = icon && *icon ? IconAssetPath(icon) : std::string();
		const std::wstring wpath(path.begin(), path.end());   // the path is ASCII
		auto*       loaded = path.empty() ? nullptr : ui::Load(wpath.c_str());
		if (!loaded) {
			g_iconFailed.insert(a_formID);
			logger::info("rows: no icon for 0x{:08X} from its form ({}{})", a_formID, icon && *icon ? icon : "no icon path",
				path.empty() ? std::string() : " -> " + path + " did not load");
			return nullptr;
		}
		g_itemIcons[a_formID] = reflect::Hold(loaded);
		logger::info("rows: icon for 0x{:08X} from its form: {} -> {}", a_formID, icon, path);
		return loaded;
	}

	namespace
	{
		// A spell's first effect's magic effect. EffectItem is not declared by CommonLibOB64; Oblivion's layout (OBSE's
		// GameObjects.h, widened to 64-bit) keeps EffectSetting* after six u32 of data and the script-effect pointer: +0x20.
		// Read fault-guarded, and believed only when it is a live form of type MagicEffect.
		int g_effectOffset = -1;   // where an EffectItem keeps its EffectSetting*, once found

		// a committed, readable range - asked before the read, so a wrong guess never faults (TestBench writes a crash
		// record on every first-chance access violation, even one a __try catches)
		bool Readable(std::uintptr_t a_p, std::size_t a_n)
		{
			if (a_p < 0x10000 || a_p > 0x7FFFFFFFFFFFull - a_n) return false;
			MEMORY_BASIC_INFORMATION mbi{};
			if (VirtualQuery(reinterpret_cast<const void*>(a_p), &mbi, sizeof(mbi)) != sizeof(mbi) || mbi.State != MEM_COMMIT) return false;
			if (mbi.Protect & (PAGE_NOACCESS | PAGE_GUARD)) return false;
			const auto end = reinterpret_cast<std::uintptr_t>(mbi.BaseAddress) + mbi.RegionSize;
			return a_p + a_n <= end;
		}

		// one 8-byte word, fault-guarded (no C++ objects here: __try cannot unwind them)
		bool ReadWord(std::uintptr_t a_p, std::uintptr_t& a_out)
		{
			if (!Readable(a_p, sizeof(a_out))) return false;
			__try {
				a_out = *reinterpret_cast<const std::uintptr_t*>(a_p);
				return true;
			} __except (EXCEPTION_EXECUTE_HANDLER) {
				return false;
			}
		}

		// a_p is a live MagicEffect form: its formID (TESForm +0x10) looks up to this very pointer, and its type is
		// MagicEffect - the round trip rules out a word that only happens to point at readable memory
		bool IsMagicEffect(std::uintptr_t a_p)
		{
			if ((a_p & 7) != 0 || !Readable(a_p, 0x18)) return false;
			std::uintptr_t word = 0;
			if (!ReadWord(a_p + 0x10, word)) return false;
			auto* form = RE::TESForm::LookupByID(static_cast<std::uint32_t>(word & 0xFFFFFFFFu));
			return form && reinterpret_cast<std::uintptr_t>(form) == a_p && form->GetFormType() == RE::FormType::MagicEffect;
		}

		// The spell's first effect's EffectSetting. Found 2026-10-01: the 2026-09-30 search ran the WHOLE walk inside one
		// __try, and its first candidate word (0x0000000800000000 - two u32 fields of the effect, read as a pointer) faulted
		// on its +0x08 read (TestBench crash record 01:54:33, "reading address 0x800000008", ImprovedWheelMenu.dll) -
		// the fault ended the search before it reached the real pointer, so every spell said "no effect read" and the Magic
		// wheel drew empty until the magic menu's rows had shown the pictures. Each candidate is now checked on its own,
		// never read unless the memory is readable, and believed only on a formID round trip. +0x00 is searched too.
		RE::EffectSetting* FirstEffectRaw(RE::SpellItem* a_spell)
		{
			if (!a_spell) return nullptr;
			// BSSimpleList keeps its first node inline: { item, next }
			std::uintptr_t node = reinterpret_cast<std::uintptr_t>(&a_spell->effectList);
			for (int guard = 0; node && guard < 16; ++guard) {
				std::uintptr_t item = 0, next = 0;
				if (!ReadWord(node, item) || !ReadWord(node + 8, next)) return nullptr;
				if (item) {
					static int s_offset = -1;
					const int from = s_offset >= 0 ? s_offset : 0x00, to = s_offset >= 0 ? s_offset : 0x58;
					for (int off = from; off <= to; off += 8) {
						std::uintptr_t raw = 0;
						if (!ReadWord(item + static_cast<std::uintptr_t>(off), raw) || !IsMagicEffect(raw)) continue;
						if (s_offset < 0) {
							s_offset = off;
							g_effectOffset = off;
						}
						return reinterpret_cast<RE::EffectSetting*>(raw);
					}
					return nullptr;   // the first effect is the spell's picture
				}
				node = next;
			}
			return nullptr;
		}

		std::unordered_set<std::uint32_t> g_spellIconFailed;

		void ObserveContext()
		{
			static sessionepoch::Token observed{};
			const auto now = sessioncontext::Current();
			if (now == observed) return;
			observed = now;
			g_itemIcons.clear();
			g_spellIcons.clear();
			g_spellIconFailed.clear();
			g_spellsByName.clear();
			typednames::Reset();
			spellnames::Reset();
			g_indexAgrees.clear();
			g_indexTrusted = g_indexJudged = false;
			g_offsetAt.clear();
			g_dirty = true;
			g_rowsChanged = g_magicRowsChanged = true;
		}
	}

	// the icon a magic row showed this session; else the spell's first effect's own icon, loaded from the paks
	UE::UObject* SpellIcon(std::uint32_t a_formID)
	{
		if (!PlayerHasSpell(a_formID)) return nullptr;
		Harvest();
		if (const auto it = g_spellIcons.find(a_formID); it != g_spellIcons.end()) {
			if (auto* icon = reflect::Get(it->second)) return icon;
		}
		if (!a_formID || g_spellIconFailed.contains(a_formID)) return nullptr;
		auto* spell = RE::TESForm::LookupByID<RE::SpellItem>(a_formID);
		auto* effect = spell ? FirstEffectRaw(spell) : nullptr;
		static bool offsetLogged = false;
		if (!offsetLogged && g_effectOffset >= 0) {
			offsetLogged = true;
			logger::info("rows: a spell effect keeps its magic effect at +0x{:X}", g_effectOffset);
		}
		const char* icon = effect ? static_cast<RE::TESIcon*>(effect)->textureName.c_str() : nullptr;
		const auto  path = icon && *icon ? IconAssetPath(icon) : std::string();
		const std::wstring wpath(path.begin(), path.end());
		auto* loaded = path.empty() ? nullptr : ui::Load(wpath.c_str());
		if (!loaded) {
			g_spellIconFailed.insert(a_formID);
			logger::info("rows: no icon for spell 0x{:08X} from its effect ({}{})", a_formID, !effect ? "no effect read" : icon && *icon ? icon : "no icon path",
				path.empty() ? std::string() : " -> " + path + " did not load");
			return nullptr;
		}
		g_spellIcons[a_formID] = reflect::Hold(loaded);
		logger::info("rows: icon for spell 0x{:08X} from its effect: {} -> {}", a_formID, icon, path);
		return loaded;
	}

	bool PlayerHasSpell(std::uint32_t a_formID)
	{
		if (!a_formID || !sessioncontext::Current().context.ready) return false;
		auto* wanted = RE::TESForm::LookupByID<RE::SpellItem>(a_formID);
		auto* list = PlayerSpells();
		if (!list || !wanted) return false;
		// Match the current live form pointer, without dereferencing old list entries; the walk is fault-guarded (SpellPositionGuarded).
		return SpellPositionGuarded(list, wanted, 0) >= 0;
	}

	std::size_t PlayerSpellCount()
	{
		auto* list = PlayerSpells();
		return list ? SpellCountGuarded(list) : 0;
	}

	std::uint32_t SpellByName(const std::string& a_name)
	{
		ObserveContext();
		if (a_name.empty()) {
			return 0;
		}
		if (const auto it = g_spellsByName.find(a_name); it != g_spellsByName.end()) {
			auto* spell = RE::TESForm::LookupByID<RE::SpellItem>(it->second);
			const char* name = spell ? RE::TESFullName::GetFullName(spell) : nullptr;
			if (PlayerHasSpell(it->second) && name && a_name == name) return it->second;
			g_spellsByName.erase(it);
		}
		auto* list = PlayerSpells();
		if (!list) {
			return 0;
		}
		for (RE::SpellItem* spell : list->spells) {
			const char* n = spell ? RE::TESFullName::GetFullName(spell) : nullptr;
			if (n && a_name == n) {
				g_spellsByName[a_name] = spell->GetFormID();
				return spell->GetFormID();
			}
		}
		return 0;
	}

	std::string DisplayName(std::uint32_t a_formID)
	{
		ObserveContext();
		if (auto* form = RE::TESForm::LookupByID(a_formID)) {   // the placeholder is the form's full name: the game's own table of typed texts
			const char* n = RE::TESFullName::GetFullName(form);
			if (n && std::strncmp(n, "UI_UserInputText", 16) == 0) {
				if (auto t = typednames::Text(n); !t.empty()) return t;
			}
		}
		return spellnames::Find(a_formID);
	}

	std::string SpellName(std::uint32_t a_formID)
	{
		auto* form = RE::TESForm::LookupByID(a_formID);
		const char* n = form ? RE::TESFullName::GetFullName(form) : nullptr;
		return n && *n ? std::string(n) : std::format("0x{:08X}", a_formID);
	}

}
