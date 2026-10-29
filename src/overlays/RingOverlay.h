#pragma once

// A redrawn wheel picture (blue for Magic, red for Equipment) drawn over the game's wheel while it is up (the game's picture has one ring colour, baked in:
// docs/game-internals.md "HUD wheel material: no ring colour parameter"). Game thread only.
namespace ringoverlay
{
	enum class Kind { kMagic, kEquipment };
	void Show(UE::UObject* a_gameWheelImage, Kind a_kind);   // over the wheel picture's rectangle
	void Hide();
}
