# Improved Wheel - Keyboard and Better Controller Support (Oblivion Remastered)

**Playing the mod? Read the [User Documentation](USER_DOCUMENTATION.md)** for installation, controls, wheel assignments, settings, compatibility and troubleshooting.

A fork of [Improved Wheel Menu](https://github.com/ApocryphaRealm/ImprovedWheelMenuOR) by ApocryphaRealm, version **2.4.1** of this fork.

**Not compatible with other wheel mods.** It replaces Improved Wheel Menu (same file and setting names) and takes over the game's quick-key radial and hotkeys; use it instead of any other wheel mod, never beside one.

**Plug and play.** The first time a character loads, the game's own hotkey wheel (items and spells) is cleared and the mod's own wheels start empty. Nothing to clear by hand before installing.

**The main addition is keyboard support:** the whole wheel menu (the inventory, Magic and ammo wheels, favouriting, the assign panel) works from the keyboard, with or without a controller. Every key is configurable in the `[Keyboard]` section of `ImprovedWheelMenu.ini`, and so is every other setting. On top of that it adds the Select/L3/LB controller rules, clearing a slot, spells you made, slot counters and names on the Magic wheel, and per-character wheels. See [`CHANGELOG.md`](CHANGELOG.md). The DLL keeps the original's name, so install it instead of the original. Original authorship and commit history are kept; this fork is GPL-3.0-or-later like the original.

## Wheel appearance in 2.2.0

Magic has a blue wheel, gold pointer and blue title with a gold outline. Inventory has a red wheel, silver pointer and red title with a silver outline. Both assignment panels use the same themes. Ammo retains its existing appearance. In `[Theme]`, `bMagicWheel=1` and `bInventoryWheel=1` enable each theme independently; set either to `0` for the native look.

## Keyboard defaults in 2.3.0

- **R:** open the gameplay wheel; press again to cancel. In Inventory/Magic, R opens the native assign panel.
- **F6 / F7 / F8:** hold to open Equipment / Magic / Ammo directly.
- **Q / E:** previous / next wheel.
- **Z / C:** previous / next entry; **right click** also advances an entry.
- **Mouse / left click:** point / use.
- **F:** favorite; hold **X** in the assign panel to clear a slot.
- **1-8 / Numpad 1-8:** directly use a slot without opening the wheel in gameplay, or assign the highlighted menu item/spell.

All bindings are configurable in the ini. Existing ini files keep their configured keys.

## Slow motion in 2.4.0

While a gameplay wheel is open (Inventory, Magic or Ammo), the world slows to one tenth speed and returns to normal
the moment the wheel closes. It is on by default. `[Wheel] bSlowTime=0` turns it off, and `uSlowTimePercent` (1 to 99)
sets the speed. Sound is not slowed. It may not be compatible with other slow-down mods.

---

# Improved Wheel Menu (Oblivion Remastered)

An OBSE64 plugin that makes Oblivion Remastered's own quick-key radial a full wheel menu: eight slots that each hold
several items (LT / RT choose, RB or A use), a Magic wheel of its own for spells, and an ammo wheel of eight cells for arrows (no
bow needed). The game's radial, prompts and quick keys are used throughout. The package README is
[`dist/README.txt`](dist/README.txt); the history is [`CHANGELOG.md`](CHANGELOG.md).

## Build

Requires [XMake](https://xmake.io) 3.0+ and a C++23 compiler (MSVC or clang-cl).

```bat
git clone --recurse-submodules https://github.com/ApocryphaRealm/ImprovedWheelMenuOR
cd ImprovedWheelMenuOR
xmake build
```

The DLL is written to `build/windows/x64/releasedbg/ImprovedWheelMenu.dll`; the shipped files are under `dist/`.
Tests: `xmake build Tests && xmake run Tests`, and `python tests/iwp0NN_*.py` (each needs MSVC on the path).

## Source layout

`src/` is the one include root; includes name the folder (`#include "wheels/Wheels.h"`).

| Folder | What is in it |
|---|---|
| `core/` | plugin entry (`main.cpp`), precompiled header, logger, settings, text and atomic-file helpers |
| `engine/` | Unreal plumbing: ProcessEvent hook, reflection, widget building, text blocks, the TES-thread hook, pattern scan, session token |
| `game/` | what the game is doing: open menus, the native quick-key radial, the player's inventory, inventory and magic list rows, the character's session, the texts the player typed (`TypedNames`) and the names learned for spells the player made (`SpellNames`, the `.spells` file) |
| `input/` | controller (`Pad`), keyboard and mouse, the Controls page row, the wheel's geometry (`RadialSelection`), what other plugins say (`Interop`) |
| `wheels/` | the wheel model, one concern a file: `Model` (the `.iwp` file, what may go where), `Assign` (entries added, removed, moved), `Use` (using a slot, the repair hammer), `HudIcon`, and `Wheels` (the interface); `WheelsInternal.h` is what they share |
| `overlays/` | what is drawn over the game's wheel: Magic wheel, ring picture, slot counters, wheel name |
| `ammo/` | the Ammo wheel |
| `favourites/` | the star column in the Inventory and Magic lists |
| `slowtime/` | slowing the world while a wheel is open |

Headers with no engine code (for example `wheels/WheelStorage.h`, `input/CloseEpoch.h`) are what the `Tests` target checks.

## Licence

GPL-3.0-or-later ([`dist/LICENSE`](dist/LICENSE), [`dist/NOTICE.md`](dist/NOTICE.md)). Built on
[CommonLibOB64](https://github.com/libxse/commonlibob64) and its plugin template (GPL-3.0, with the modding exception in
`EXCEPTIONS`); other linked components are listed in [`dist/THIRD_PARTY_NOTICES.md`](dist/THIRD_PARTY_NOTICES.md).

While an Equipment or Magic assignment panel is open, Z / C or LT / RT cycle entries in the pointed slot; right-click advances. Triggers do not switch menu tabs while the panel is open.

Minor issue: the direct-wheel shortcuts F6/F7/F8 can reset Caps Lock walking to running. Opening with R and direct 1-8 quick use preserve walking.
