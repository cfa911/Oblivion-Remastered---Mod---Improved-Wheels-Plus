// The save's CharacterId (the GUID saves_meta.sav keeps per save), read from the game: VSaveLoadMenuViewModel::
// GetCurrentPlayerCharacterId (member, FGuid return, one live instance in gameplay - proven 2026-11-06, KB docs/game-internals.md
// "CharacterId at runtime"). The wheel files are keyed on it (IWP-039): PlayerCharacter::characterSeed changed between launches
// for one character on a player's machine, so "<name> #<seed>" lost that character's wheels.
#include "game/Inventory.h"

#include "core/Settings.h"
#include "engine/Reflect.h"
#include "engine/Ui.h"
#include "wheels/WheelStorage.h"

namespace inventory
{
	namespace
	{
		std::string ReadCharacterId()
		{
			static UE::UClass*     cls = nullptr;
			static reflect::Handle vm{};
			static ULONGLONG       findAt = 0;
			if (!reflect::Ok()) return {};
			if (!cls) cls = UE::StaticFindObject<UE::UClass>(nullptr, nullptr, L"/Script/Altar.VSaveLoadMenuViewModel");
			if (!cls) return {};
			auto* obj = reflect::Get(vm);
			if (obj && obj->GetClass() != cls) obj = nullptr;   // the slot holds another object now
			if (!obj) {
				const auto now = GetTickCount64();
				if (now < findAt) return {};
				findAt = now + 2000;   // one object-array walk at most every 2 s, and only while the view model is not held
				obj = reflect::FirstOf(cls);
				if (!obj) return {};
				vm = reflect::Hold(obj);
			}
			ui::Call call(obj, L"GetCurrentPlayerCharacterId");
			if (!call || call.Size("ReturnValue") != 16 || !call.RunGuarded()) return {};
			const auto* p = static_cast<const std::uint8_t*>(call.At("ReturnValue"));
			if (!p) return {};
			std::uint32_t w[4];
			std::memcpy(w, p, sizeof(w));
			if (!(w[0] | w[1] | w[2] | w[3])) return {};   // no id reported: the seed-based name stays in use
			return std::format("{:08X}-{:08X}-{:08X}-{:08X}", w[0], w[1], w[2], w[3]);
		}
	}

	std::string CharacterId()
	{
		static std::string cached;
		static ULONGLONG   readAt = 0;
		const auto         now = GetTickCount64();
		if (now >= readAt) {
			readAt = now + 250;   // one reflected call at most every 250 ms (the wheel asks every 100 ms)
			cached = ReadCharacterId();
		}
		return cached;
	}

	std::string Identity()
	{
		const bool byPlayer = settings::Get().savingByPlayerName;   // the name alone needs no CharacterId read
		return wheelstorage::FileIdentity(byPlayer, byPlayer ? std::string() : CharacterId(), PlayerName());
	}
}
