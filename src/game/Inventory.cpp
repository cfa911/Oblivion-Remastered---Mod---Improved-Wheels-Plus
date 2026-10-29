#include "game/Inventory.h"

#include "engine/TesThread.h"

namespace inventory
{
	namespace
	{
		constexpr std::uint8_t kQuickKeyType = 0x55;
		constexpr std::ptrdiff_t kKeyIDOffset = 0x18;
		constexpr std::size_t kQuickKeySize = 0x20;

RE::BSExtraData* KeyExtra(RE::ExtraDataList* a_list)
		{
			for (RE::BSExtraData* x = a_list ? a_list->head : nullptr; x; x = x->next) {
				if (static_cast<std::uint8_t>(x->type.get()) == kQuickKeyType) {
					return x;
				}
			}
			return nullptr;
		}

		int KeyOf(const RE::BSExtraData* a_x)
		{
			return *(reinterpret_cast<const std::uint8_t*>(a_x) + kKeyIDOffset);
		}

		// The TES thread may change the player's item list while the UI thread reads it, and a node freed under a walk faults. Every raw
		// walk below sits in a small helper of plain pointers (__try cannot unwind C++ objects, C2712); a fault reads as "nothing there".
		// Nodes are walked as { item, next } from the list's inline first node (an empty list is a null item and a null next).
		using ItemNode = RE::BSSimpleList<RE::ItemChange*>::Node;
		using ExtraNode = RE::BSSimpleList<RE::ExtraDataList*>::Node;

		RE::BSSimpleList<RE::ItemChange*>* Items()
		{
			__try {
				auto* player = RE::PlayerCharacter::GetSingleton();
				auto* changes = player ? player->extra.GetExtraData<RE::ExtraContainerChanges>() : nullptr;
				return changes && changes->changes ? changes->changes->list : nullptr;
			} __except (EXCEPTION_EXECUTE_HANDLER) {
				return nullptr;
			}
		}

		bool IsWorn(const RE::ExtraDataList* a_list)
		{
			return a_list && (a_list->GetExtraData(RE::EXTRA_DATA_TYPE::Worn) || a_list->GetExtraData(RE::EXTRA_DATA_TYPE::WornLeft));
		}

		RE::ItemChange* FindGuarded(RE::BSSimpleList<RE::ItemChange*>* a_items, std::uint32_t a_formID)
		{
			__try {
				for (auto* node = reinterpret_cast<ItemNode*>(a_items); node; node = node->next) {
					RE::ItemChange* item = node->item;
					if (item && item->object && item->object->GetFormID() == a_formID) {
						return item;
					}
				}
			} __except (EXCEPTION_EXECUTE_HANDLER) {
			}
			return nullptr;
		}

		// the pointer is the game's own node: callers that change the item (keys) are on the thread that owns the list
		RE::ItemChange* Find(std::uint32_t a_formID)
		{
			auto* items = Items();
			return items && a_formID ? FindGuarded(items, a_formID) : nullptr;
		}

		bool KeysGuarded(RE::BSSimpleList<RE::ItemChange*>* a_items, KeyMap& a_out)
		{
			__try {
				for (auto* node = reinterpret_cast<ItemNode*>(a_items); node; node = node->next) {
					RE::ItemChange* item = node->item;
					if (!item || !item->object || !item->extraData) {
						continue;
					}
					for (auto* xn = reinterpret_cast<ExtraNode*>(item->extraData); xn; xn = xn->next) {
						if (auto* x = KeyExtra(xn->item)) {
							const int key = KeyOf(x);
							if (key >= 0 && key < kSlots) {
								a_out[key] = item->object->GetFormID();
							}
						}
					}
				}
				return true;
			} __except (EXCEPTION_EXECUTE_HANDLER) {
				return false;
			}
		}

		// how many are carried (0 = none, or the list changed under the walk)
		int CountGuarded(RE::BSSimpleList<RE::ItemChange*>* a_items, std::uint32_t a_formID, bool* a_worn)
		{
			__try {
				RE::ItemChange* item = FindGuarded(a_items, a_formID);
				if (!item || item->count <= 0) {
					return 0;
				}
				const int count = item->count;
				if (a_worn && item->extraData) {
					for (auto* xn = reinterpret_cast<ExtraNode*>(item->extraData); xn; xn = xn->next) {
						if (IsWorn(xn->item)) {
							*a_worn = true;
						}
					}
				}
				return count;
			} __except (EXCEPTION_EXECUTE_HANDLER) {
				return 0;
			}
		}

		std::uint32_t FindNamedGuarded(RE::BSSimpleList<RE::ItemChange*>* a_items, const char* a_name)
		{
			__try {
				for (auto* node = reinterpret_cast<ItemNode*>(a_items); node; node = node->next) {
					RE::ItemChange* item = node->item;
					if (!item || !item->object || item->count <= 0) {
						continue;
					}
					const char* n = RE::TESFullName::GetFullName(item->object);
					if (n && std::strcmp(a_name, n) == 0) {
						return item->object->GetFormID();
					}
				}
			} __except (EXCEPTION_EXECUTE_HANDLER) {
			}
			return 0;
		}

		int PositionGuarded(RE::BSSimpleList<RE::ItemChange*>* a_items, std::uint32_t a_formID)
		{
			__try {
				int i = 0;
				for (auto* node = reinterpret_cast<ItemNode*>(a_items); node; node = node->next, ++i) {
					RE::ItemChange* item = node->item;
					if (item && item->object && item->object->GetFormID() == a_formID) {
						return i;
					}
				}
			} __except (EXCEPTION_EXECUTE_HANDLER) {
			}
			return -1;
		}

		std::uint32_t WornAmmoGuarded(RE::BSSimpleList<RE::ItemChange*>* a_items)
		{
			__try {
				for (auto* node = reinterpret_cast<ItemNode*>(a_items); node; node = node->next) {
					RE::ItemChange* item = node->item;
					if (!item || !item->object || !item->extraData || item->count <= 0 ||
						item->object->GetFormType() != RE::FormType::Ammo) continue;
					for (auto* xn = reinterpret_cast<ExtraNode*>(item->extraData); xn; xn = xn->next) {
						if (IsWorn(xn->item)) return item->object->GetFormID();
					}
				}
			} __except (EXCEPTION_EXECUTE_HANDLER) {
			}
			return 0;
		}

		// A new ExtraQuickKey the way the game's own looks: vtable, type 0x55, next, keyID - on the game's heap.
		RE::BSExtraData* NewQuickKey(int a_key)
		{
			static REL::Relocation<std::uintptr_t> vtable{ RE::VTABLE::ExtraQuickKey[0] };
			auto* mem = static_cast<std::uint8_t*>(RE::malloc(kQuickKeySize));
			if (!mem) {
				return nullptr;
			}
			std::memset(mem, 0, kQuickKeySize);
			*reinterpret_cast<std::uintptr_t*>(mem) = vtable.address();
			mem[0x08] = kQuickKeyType;
			mem[kKeyIDOffset] = static_cast<std::uint8_t>(a_key);
			return reinterpret_cast<RE::BSExtraData*>(mem);
		}

		// A new, empty ExtraDataList appended to the item's lists (nodes on the game's heap, never CRT new).
		RE::ExtraDataList* NewList(RE::ItemChange* a_item)
		{
			static REL::Relocation<std::uintptr_t> vtable{ RE::VTABLE::ExtraDataList[0] };
			auto* list = static_cast<RE::ExtraDataList*>(RE::malloc(sizeof(RE::ExtraDataList)));
			if (!list) {
				return nullptr;
			}
			std::memset(static_cast<void*>(list), 0, sizeof(RE::ExtraDataList));
			*reinterpret_cast<std::uintptr_t*>(list) = vtable.address();

			using Node = RE::BSSimpleList<RE::ExtraDataList*>::Node;
			if (!a_item->extraData) {
				auto* head = static_cast<Node*>(RE::malloc(sizeof(Node)));   // the list object IS its first node
				if (!head) {
					return nullptr;
				}
				head->item = list;
				head->next = nullptr;
				a_item->extraData = reinterpret_cast<RE::BSSimpleList<RE::ExtraDataList*>*>(head);
				return list;
			}
			auto* node = reinterpret_cast<Node*>(a_item->extraData);
			if (!node->item) {
				node->item = list;   // an empty first node
				return list;
			}
			while (node->next) {
				node = node->next;
			}
			auto* added = static_cast<Node*>(RE::malloc(sizeof(Node)));
			if (!added) {
				return nullptr;
			}
			added->item = list;
			added->next = nullptr;
			node->next = added;
			return list;
		}
	}

	KeyMap Keys()
	{
		KeyMap out{};
		if (auto* items = Items()) {
			if (!KeysGuarded(items, out)) {
				out = {};   // the list changed under the walk: no keys rather than half of them
			}
		}
		return out;
}

	bool Ready()
	{
		return Items() != nullptr;
	}

	bool Has(std::uint32_t a_formID)
	{
		auto* items = a_formID ? Items() : nullptr;
		return items && CountGuarded(items, a_formID, nullptr) > 0;
}

	std::string NameOf(std::uint32_t a_formID)
	{
		auto* form = RE::TESForm::LookupByID(a_formID);
		const char* n = form ? RE::TESFullName::GetFullName(form) : nullptr;
		return n && *n ? std::string(n) : std::format("0x{:08X}", a_formID);
	}

	std::uint32_t FindByName(const std::string& a_name)
	{
		auto* items = Items();
		return items && !a_name.empty() ? FindNamedGuarded(items, a_name.c_str()) : 0;
}

	std::vector<Carried> CarriedWithTypedNames()
	{
		std::vector<Carried> out;
		auto*                items = Items();
		if (!items) {
			return out;
		}
		for (RE::ItemChange* item : *items) {
			if (!item || !item->object || item->count <= 0) {
				continue;
			}
			const char* n = RE::TESFullName::GetFullName(item->object);
			if (!n || std::strncmp(n, "UI_UserInputText", 16) != 0) {
				continue;
			}
			const RE::TESWeightForm* w = nullptr;   // a mixin: reached through the item's own class
			switch (item->object->GetFormType()) {
			case RE::FormType::Weapon: w = item->object->As<RE::TESObjectWEAP>(); break;
			case RE::FormType::Armor: w = item->object->As<RE::TESObjectARMO>(); break;
			case RE::FormType::Clothing: w = item->object->As<RE::TESObjectCLOT>(); break;
			case RE::FormType::AlchemyItem: w = item->object->As<RE::AlchemyItem>(); break;
			case RE::FormType::Ingredient: w = item->object->As<RE::IngredientItem>(); break;
			default: break;
			}
			out.push_back({ item->object->GetFormID(), item->count, w ? w->weight : -1.0f });
		}
		return out;
	}

	int PositionOf(std::uint32_t a_formID)
	{
		auto* items = Items();
		return items && a_formID ? PositionGuarded(items, a_formID) : -1;
}

	// Read currently worn arrows independently of the weapon: Ammo can be preselected with no bow.
	std::uint32_t WornAmmo()
	{
		auto* items = Items();
		return items ? WornAmmoGuarded(items) : 0;
	}

	void EquipAmmo(std::uint32_t a_id)
	{
		testhread::Post([a_id]() {
			auto* player = RE::PlayerCharacter::GetSingleton();
			auto* items = Items();
			if (!player || !items) {
				return;
			}
			for (RE::ItemChange* item : *items) {
				if (item && item->object && item->object->GetFormID() == a_id && item->count > 0 &&
					item->object->GetFormType() == RE::FormType::Ammo) {
					// already worn: nothing to do. Every open of the wheel used to equip the stack again (40 equips of the
					// same iron arrows in one session) and the owner saw the arrows duplicated in the inventory (2026-09-30)
					bool worn = false;
					if (item->extraData) {
						for (RE::ExtraDataList* xl : *item->extraData) {
							worn = worn || IsWorn(xl);
						}
					}
					if (worn) {
						logger::info("ammo: {} are worn already - nothing equipped", NameOf(a_id));
						return;
					}
					// arrows are worn as the whole stack; no lock (EquipObject's last argument is the console's NoUnequip)
					// EquipObject may rebuild inventory entries: do not dereference item after it returns.
					const auto count = item->count;
					const auto name = NameOf(a_id);
					player->EquipObject(item->object, count, nullptr, false, false);
					logger::info("ammo: {} x{} equipped (TES thread)", name, count);
					return;
				}
			}
			logger::info("ammo: 0x{:08X} is not carried ammunition - nothing equipped", a_id);
		});
	}

	void ClearKey(std::uint32_t a_formID)
	{
		auto* item = Find(a_formID);
		if (!item || !item->extraData) {
			return;
		}
		for (RE::ExtraDataList* xl : *item->extraData) {
			if (xl && KeyExtra(xl)) {
				xl->RemoveExtra(RE::EXTRA_DATA_TYPE(kQuickKeyType));
			}
		}
	}

	void ClearSlot(int a_key)
	{
		auto* items = Items();
		if (!items) {
			return;
		}
		for (RE::ItemChange* item : *items) {
			if (!item || !item->extraData) {
				continue;
			}
			for (RE::ExtraDataList* xl : *item->extraData) {
				if (auto* x = KeyExtra(xl); x && KeyOf(x) == a_key) {
					xl->RemoveExtra(RE::EXTRA_DATA_TYPE(kQuickKeyType));
				}
			}
		}
	}

	namespace
	{
		// the first bytes of the two game routines below, game 1.512.105.0 (read from the exe 2026-10-21): the call is made only when
		// both Address Library ids still land on them
		constexpr std::uint8_t kRemoveItemKeyHead[] = { 0x40, 0x55, 0x41, 0x56, 0x41, 0x57, 0x48, 0x83, 0xEC, 0x30, 0x48, 0x8B, 0x01, 0x41, 0x8B, 0xE8, 0x4C, 0x8B, 0xFA, 0x4C, 0x8B, 0xF1 };
		constexpr std::uint8_t kClearAllHead[] = { 0x48, 0x89, 0x5C, 0x24, 0x08, 0x48, 0x89, 0x6C, 0x24, 0x10, 0x48, 0x89, 0x74, 0x24, 0x18, 0x57, 0x48, 0x83, 0xEC, 0x20, 0x48, 0x8D, 0x1D };

		template <std::size_t N>
		bool HeadIs(std::uintptr_t a_address, const std::uint8_t (&a_bytes)[N])
		{
			return a_address && std::memcmp(reinterpret_cast<const void*>(a_address), a_bytes, N) == 0;
		}
	}

	namespace
	{
		// the first bytes of the game's generic list RemoveAll (id 404978, RVA 0x65626C0), game 1.512.105.0
		constexpr std::uint8_t kRemoveAllHead[] = { 0x48, 0x89, 0x5C, 0x24, 0x08, 0x57, 0x48, 0x83, 0xEC, 0x20, 0x48, 0x8B, 0x59, 0x08, 0x48, 0x8B, 0xF9, 0x48, 0x85, 0xDB };

		struct NativeList { std::uintptr_t vt, head, tail; std::uint32_t count, padding; };   // layout proven 2026-10-21 (native-hotkey-routines.md)
		struct NativeNode { std::uintptr_t next, prev, form; };

		std::uintptr_t NativeTable()
		{
			static REL::Relocation<std::uintptr_t> table{ REL::ID(1669427) };
			return table.address();
		}

		// the slot holds exactly one entry and it is a spell (type 16); faults (a list being rebuilt) read as "no"
		bool SlotHoldsSpell(const NativeList* a_list, std::uint32_t& a_id)
		{
			__try {
				if (a_list->count != 1 || !a_list->head || a_list->head != a_list->tail) {
					return false;
				}
				const auto* node = reinterpret_cast<const NativeNode*>(a_list->head);
				if (node->next || node->prev || !node->form) {
					return false;
				}
				const auto* form = reinterpret_cast<const RE::TESForm*>(node->form);
				if (form->GetFormType() != RE::FormType::Spell) {
					return false;
				}
				a_id = form->GetFormID();
				return true;
			} __except (EXCEPTION_EXECUTE_HANDLER) {
				return false;
			}
		}

		bool NativeListsEmpty()
		{
			__try {
				const auto table = NativeTable();
				for (int slot = 0; slot < kSlots; ++slot) {
					const auto* list = reinterpret_cast<const NativeList*>(table + slot * sizeof(NativeList));
					if (list->count || list->head || list->tail) return false;
				}
				return true;
			} __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
		}
	}

	// The game keeps the hotkeys of the Magic menu's own assign in the same eight lists as the item keys, and its quick key use
	// (and the radial's close) selects what a list holds - over the spell the Magic wheel chose (IWP-013: the log of 2026-10-22 showed
	// the game's SetSelectedSpell re-selecting the native slot's spell ~100 ms after the wheel's). The wheel never reads native spell
	// entries, so every list that holds a spell is emptied through the game's own RemoveAll, the routine its hotkey setter uses.
	// Items are left alone (the Equipment wheel lives on their keys). TES thread only.
	void SweepNativeSpellKeys()
	{
		static int s_state = 0;   // 0 = not checked yet, 1 = ready, -1 = this build does not match: never called
		if (s_state < 0 || !RE::PlayerCharacter::GetSingleton()) {
			return;
		}
		static REL::Relocation<void (*)(void*)> removeAll{ REL::ID(404978) };
		if (s_state == 0) {
			if (!HeadIs(removeAll.address(), kRemoveAllHead)) {
				s_state = -1;
				logger::error("native hotkeys: the game's list clear is not where this build expects it - native spell hotkeys are left alone");
				return;
			}
			s_state = 1;
		}
		const auto table = NativeTable();
		for (int slot = 0; slot < kSlots; ++slot) {
			auto*         list = reinterpret_cast<NativeList*>(table + static_cast<std::size_t>(slot) * sizeof(NativeList));
			std::uint32_t id = 0;
			if (SlotHoldsSpell(list, id)) {
				removeAll(list);
				logger::info("native hotkeys: the spell 0x{:08X} on native slot {} was emptied", id, slot + 1);
			}
		}
	}

	// The game's own way (docs/reverse-engineering/native-hotkey-routines.md, proven in game 2026-10-21): the item-key removal its hotkey
	// setter uses for every keyed item (id 407660, RVA 0x66471E0), then the clear-all of the eight hotkey lists (id 409811, RVA 0x6725EE0,
	// called by the world unload). The lists hold the spells too, which no item key reaches. TES thread only.
	bool ClearAllNativeKeys()
	{
		static REL::Relocation<void (*)(RE::InventoryChanges*, RE::TESBoundObject*, int)> removeItemKey{ REL::ID(407660) };
		static REL::Relocation<void (*)()>                                                  clearAll{ REL::ID(409811) };
		if (!HeadIs(removeItemKey.address(), kRemoveItemKeyHead) || !HeadIs(clearAll.address(), kClearAllHead)) {
			logger::error("native hotkeys: the game's clear routines are not where this build expects them - nothing cleared");
			return false;
		}
		auto* player = RE::PlayerCharacter::GetSingleton();
		auto* extra = player ? player->extra.GetExtraData<RE::ExtraContainerChanges>() : nullptr;
		auto* changes = extra ? extra->changes : nullptr;
		if (!changes || !changes->list) {
			logger::warn("native hotkeys: the player's items cannot be read yet - nothing cleared");
			return false;
		}
		int removed = 0;
		for (int pass = 0; pass < 3; ++pass) {   // a second stack of one item can carry the same key
			std::vector<std::pair<RE::TESBoundObject*, int>> keyed;
			for (RE::ItemChange* item : *changes->list) {
				if (!item || !item->object || !item->extraData) {
					continue;
				}
				for (RE::ExtraDataList* xl : *item->extraData) {
					if (auto* x = KeyExtra(xl); x && KeyOf(x) < kSlots) {
						keyed.emplace_back(item->object, KeyOf(x));
					}
				}
			}
			if (keyed.empty()) {
				break;
			}
			for (const auto& [object, key] : keyed) {
				removeItemKey(changes, object, key);
				++removed;
			}
		}
		clearAll();
		const auto keys = Keys();
		if (!NativeListsEmpty() || std::any_of(keys.begin(), keys.end(), [](auto id) { return id != 0; })) {
			logger::warn("native hotkeys: empty setup verification failed - initialization remains blocked for retry");
			return false;
		}
		logger::info("native hotkeys: cleared through the game's own routines ({} item key{} removed, all eight hotkey lists emptied)", removed, removed == 1 ? "" : "s");
		return true;
	}

	bool SetKey(std::uint32_t a_formID, int a_key)
	{
		if (a_key < 0 || a_key >= kSlots) {
			return false;
		}
		auto* item = Find(a_formID);
		if (!item || item->count <= 0) {
			logger::warn("inventory: {} is not carried - quick key {} left as it was", NameOf(a_formID), a_key + 1);
			return false;
		}
		ClearSlot(a_key);
		// already keyed (to another slot): move that key
		if (item->extraData) {
			for (RE::ExtraDataList* xl : *item->extraData) {
				if (auto* x = KeyExtra(xl)) {
					*(reinterpret_cast<std::uint8_t*>(x) + kKeyIDOffset) = static_cast<std::uint8_t>(a_key);
					return true;
				}
			}
		}
		// the item's first extra list, or a new one
		RE::ExtraDataList* target = nullptr;
		if (item->extraData) {
			for (RE::ExtraDataList* xl : *item->extraData) {
				if (xl) {
					target = xl;
					break;
				}
			}
		}
		if (!target) {
			target = NewList(item);
		}
		auto* key = target ? NewQuickKey(a_key) : nullptr;
		if (!key) {
			logger::error("inventory: no memory for quick key {} on {}", a_key + 1, NameOf(a_formID));
			return false;
		}
		target->AddExtra(key);
		return true;
	}

	std::string PlayerName()
	{
		// the name chosen at character creation lives on the player's base record; the reference itself reads "Player"
		auto* player = RE::PlayerCharacter::GetSingleton();
		auto* base = player ? player->data.objectReference : nullptr;
		const char* n = base ? RE::TESFullName::GetFullName(base) : nullptr;
		if (!n || !*n) {
			n = player ? RE::TESFullName::GetFullName(player) : nullptr;
		}
		std::string name = n && *n ? std::string(n) : std::string("Player");
		// A new game must not inherit the last character's wheels: the typed name alone is not unique (the file of a new game
		// came out as the same "UI_UserInputText_0_705_0"), and a save loaded after another character's must not keep that one's
		// either. The character's own seed (set when the character is made, kept in the save) tells them apart.
		if (player && player->characterSeed != 0) {
			name += std::format(" #{:08X}", player->characterSeed);
		}
		return name;
	}

	State StateOf(std::uint32_t a_formID)
	{
		State out;
		auto* items = a_formID ? Items() : nullptr;
		if (!items) {
			return out;
		}
		out.count = CountGuarded(items, a_formID, &out.worn);
		out.carried = out.count > 0;
		return out;
}

	std::string Describe(const State& a_state)
	{
		return a_state.carried ? std::format("x{}{}", a_state.count, a_state.worn ? ", worn" : ", not worn") : std::string("not carried");
	}

	namespace
	{
		enum class KeyedAction { kEquip, kUnequip, kToggle };

		void QueueKeyed(std::uint32_t id, KeyedAction action)
		{
			testhread::Post([id, action] {
				auto* player = RE::PlayerCharacter::GetSingleton();
				auto* item = Find(id);
				if (!player || !item || !item->object || item->count <= 0) return;
				RE::ExtraDataList* keyed = nullptr;
				if (item->extraData) {
					for (RE::ExtraDataList* xl : *item->extraData) {
						if (xl && KeyExtra(xl)) { keyed = xl; break; }
					}
				}
				const bool unequip = action == KeyedAction::kUnequip || (action == KeyedAction::kToggle && StateOf(id).worn);
				if (unequip) player->UnequipObject(item->object, 1, keyed, false, false);
				else player->EquipObject(item->object, 1, keyed, false, false);
				logger::info("wheels: {} {} on the TES thread ({})", NameOf(id), unequip ? "unequipped" : "equipped", Describe(StateOf(id)));
			});
		}
	}

	void EquipKeyed(std::uint32_t id) { QueueKeyed(id, KeyedAction::kEquip); }
	void UnequipKeyed(std::uint32_t id) { QueueKeyed(id, KeyedAction::kUnequip); }
	void ToggleKeyed(std::uint32_t id) { QueueKeyed(id, KeyedAction::kToggle); }
}
