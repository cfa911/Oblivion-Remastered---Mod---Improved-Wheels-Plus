#pragma once

// ============================================================================================================
// What the wheel files share: the wheels and the character they belong to, the pending assign and use, and the
// helpers each file offers the others. Game thread. The wheels' interface is Wheels.h; this is not part of it.
//   Model.cpp    the model and its file: kinds of item, Save/Load, Reconcile, Sanitize, EnsureLoaded
//   Assign.cpp   entries added, removed and moved; the pending Equipment assign; favourites; arrows to Ammo; pictures
//   Use.cpp      using a slot: the pending use, the repair hammer, DriveUse
//   HudIcon.cpp  the HUD's spell picture after a Magic choice
//   Wheels.cpp   the session observer and the rest of the interface
// ============================================================================================================

#include "wheels/Wheels.h"
#include "wheels/WheelStorage.h"
#include "wheels/EmptySetup.h"
#include "wheels/CarriedHistory.h"

#include "game/Inventory.h"
#include "game/QuickKeys.h"

#include <unordered_set>

namespace wheels::detail
{
	constexpr int kEquip = 0;
	constexpr int kMagic = 1;
	constexpr int kAmmo = 2;   // the bow's ammo wheel: one arrow kind a slot, entirely ours
	constexpr int kFav = 3;    // favourites that go on no wheel: armour and clothing (the owner, 2026-09-30: "It should be fine
	                           // to favorite armor, just not added to the wheel") - all in slot 1, no cap
	constexpr int kWheels = 4;
	constexpr const char* kWheelNames[kWheels] = { "Equipment", "Magic", "Ammo", "Favourite" };

	inline std::atomic<int> g_active{ static_cast<int>(Wheel::kEquipment) };

	using Slot = wheelstorage::Slot;
	inline wheelstorage::Model g_wheels;
	inline std::string g_loadedFor;   // the character g_wheels belongs to
	inline bool g_storageRejected = false;   // never load or overwrite a file without our format and character identity
	inline emptysetup::Gate g_emptySetup;
	inline bool g_initializeEmpty = false;   // no stored wheel setup: clear native item keys once inventory is ready
	inline std::unordered_set<std::uint32_t> g_carriedSeen;
	inline std::uint64_t g_goneCheckAt = 0;
	inline void ResetCarriedHistory() { carriedhistory::Reset(g_carriedSeen, g_goneCheckAt); }

	// what the wheel's pictures are showing now
	enum class Shown { kGame, kMagic };
	inline Shown g_shown = Shown::kGame;
	inline ULONGLONG g_keepUntil = 0;   // until then a radial that opens keeps the wheel that is active (a wheel's own button or a keyboard key chose it)

	// Armour and clothing never go on the wheel - shields do (the owner, 2026-09-30: "I don't want armor that's favorited
	// to appear on the wheel at all. I just want the shields to be appearing on it"). Oblivion's body slot 13 is the shield.
	constexpr std::uint16_t kShieldSlot = 1u << 13;

	inline std::atomic<std::uint32_t> g_generation{ 1 };   // moves with every saved change (the favourites column redraws)

	// Stored wheel assignments are authoritative. Game hotkeys are only the backing keys for
	// explicit wheel choices; loading a save must never import new entries into the wheel.
	inline int           g_holdKey = -1;      // the quick key taken off its item while the radial closes on another wheel's slot
	inline std::uint32_t g_holdItem = 0;

	// Sanitize runs once, a moment after a character loads
	inline ULONGLONG g_sanitizeAt = 0;
	inline bool      g_sanitized = true;

	// Favourites the player dropped or sold (bBlockFavouriteDropSell=0): once none is carried they leave every wheel, the Equipment
	// wheel's key going to the slot's next entry.
	inline std::unordered_set<std::uint32_t> g_leaving;

	struct Pending
	{
		bool              on = false;
		int               slot = -1;
		std::uint32_t     parked = 0;       // the pointed slot's item, taken off its key before the game's assign
		inventory::KeyMap before{};
		int               reads = 0;
		int               readsSinceRelease = 0;
		bool              released = false;
	};
	inline Pending g_pending;

	// RB (or A) on an Equipment slot of the HUD radial: the radial closes on NOTHING (the choice cleared, the stick
	// held centred), and once it has closed the game's own quick-key press for that key is run - the path the number
	// keys 1-8 take (quickkeys::PressQuickKey). The vanilla close did not equip reliably: 2026-10-01 01:52:39, "USE
	// Equipment slot 2 now (RB)" then "the game uses slot 2" (the view model's KeyIndex, our reading), and the Steel
	// Claymore on key 2 never came to hand - the bow stayed. What the use did is read back from the item itself.
	struct PendingUse
	{
		bool                                  on = false;
		bool                                  closed = false;
		bool                                  pressed = false;
		bool                                  direct = false;   // equipped at once through Actor::EquipObject: only the read-back is left
		bool                                  waitClose = false;   // a potion, poison or ingredient: used through the same call, but only once the radial has closed
		int                                   key = -1;
		std::uint32_t                         id = 0;
		inventory::State                      before{};
		const char*                           how = "RB";
		std::chrono::steady_clock::time_point armed{}, closedAt{}, verifyAt{};
	};
	inline PendingUse g_use;

	// A repair hammer is used from INSIDE the Inventory menu, as the game does (IWP-011): the EquipObject call made in plain gameplay opened the repair menu but left
	// a stuck blurred screen (and, with no wheel, the game in menu mode). The Inventory is opened through the controller's own open-inventory input function
	// (pressed, released 200 ms later: released in the same frame the player menu opened on its System page), the hammer is equipped once the Inventory is up,
	// and the repair menu then sits over it; Esc returns to the Inventory and a second Esc to the game.
	struct HammerInv
	{
		bool          on = false;
		int           stage = 0;
		ULONGLONG     at = 0;
		std::uint32_t id = 0;
		std::string   fn, rel;
		bool          released = false;
	};
	inline HammerInv g_hi;

	// the HUD's spell picture after a Magic choice (HudIcon.cpp)
	inline std::chrono::steady_clock::time_point g_hudCheckAt{};
	inline UE::UObject*                          g_hudIconBefore = nullptr;   // compared only

	// Model.cpp: the model and its file
	const char* WheelName(int a_wheel);
	bool IsAmmo(std::uint32_t a_id);
	bool Wearable(std::uint32_t a_id, bool* a_shield = nullptr);
	bool AllowedOnEquipment(std::uint32_t a_id);
	std::string NameOf(int a_wheel, std::uint32_t a_id);
	std::filesystem::path FileFor(const std::string& a_name);
	void Save();
	void Load(const std::string& a_name);
	int IndexOf(const Slot& a_slot, std::uint32_t a_id);
	int Cap();
	bool Poison(std::uint32_t a_id);
	void Reconcile();
	void Sanitize();
	bool EnsureLoaded();

	// Assign.cpp: entries added, removed and moved; pictures
	int KnownMagicEntry(const Slot& a_slot, int a_from, int a_dir, bool a_includeFrom);
	quickkeys::Icons MagicIcons();
	void ShowMagic(bool a_on);
	void PreloadIcon(int a_slot);
	void PreloadChangedIcons(const inventory::KeyMap& a_before);
	std::string Unreachable(int a_slot, std::uint32_t a_id, const inventory::KeyMap& a_keys);
	int NextEquipment(int a_slot, int a_from, int a_dir, std::string* a_skipped = nullptr);
	void RemoveAt(int a_wheel, int a_slot, int a_index, const std::string& a_why);
	void RemoveLeavers();
	void RemoveGoneInMenus();
	void RemoveFromOtherSlots(int a_wheel, int a_keepSlot, std::uint32_t a_id);
	bool AddTo(int a_wheel, int a_slot, std::uint32_t a_id, const char* a_why);
	bool AssignPending();   // the game's own assign is being watched (Settle reads the keys' change itself)
	void Settle(int a_slot, std::uint32_t a_arrived);
	void SettleNothing();
	std::uint32_t KindOf(std::uint32_t a_id);
	void ToggleFavourite(int a_wheel, std::uint32_t a_id);
	int MigrateAmmo();

	// Use.cpp: using a slot
	bool Equippable(std::uint32_t a_id);
	bool Consumable(std::uint32_t a_id);
	bool MiscTool(std::uint32_t a_id);
	std::string OpenInventoryFunction(UE::UObject* a_pc);
	bool HammerInvStart(std::uint32_t a_id);
	void HammerInvTick();
	void DriveUse();

	// HudIcon.cpp: the HUD's spell picture
	UE::UObject* HudSpellIcon();
	int SetHudWidgetTexture(UE::UObject* a_icon);
	void PushHudSpellIcon(std::uint32_t a_formID);
	void CheckHud();

	// Wheels.cpp
	bool ObserveContext();   // a character loaded or left: per-character state starts again; false while none is ready
}
