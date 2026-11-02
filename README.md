# Improved Wheels Plus

Three wheels for gear, spells, scrolls and arrows in **Oblivion Remastered**, with keyboard, mouse and controller support.

**Current release: 2.5.0.** [Download on Nexus Mods](https://www.nexusmods.com/oblivionremastered/mods/5713) or [GitHub Releases](https://github.com/cfa911/Oblivion-Remastered---Mod---Improved-Wheels-Plus/releases/latest).

Read the [User Documentation](USER_DOCUMENTATION.md) for detailed controls, settings, assignment backups and troubleshooting, or the [changelog](CHANGELOG.md) for version history.

## What you get

- **Inventory wheel:** weapons, armor, clothing, potions, poisons, ingredients and supported utility items. Equip or unequip gear and use items from the wheel.
- **Magic wheel:** learned spells, custom spells and scrolls. Select an entry to ready it, then cast with your usual control.
- **Ammo wheel:** eight cells, one arrow type per cell, with no bow required. Arrows stay in the cells you choose.
- **More room:** Inventory and Magic each have eight slots with up to five entries per slot by default. Browse several entries without moving to another slot.
- **Per-character assignments:** wheel setups survive restarts. Entries unavailable in the loaded save are hidden rather than erased.
- **Optional slow motion:** gameplay slows while a wheel is open and returns to normal when it closes. Sound is unchanged.
- **Wheel themes:** blue Magic with gold details and red Inventory with silver details, including assignment panels. Each theme can be disabled separately.

## New in 2.5.0

- Assign scrolls to the Magic wheel, see owned-entry counts, and ready/cast through the game's native scroll action.
- Cycle gameplay wheels with controller **L3 / R3**, alongside **D-pad left / right**.
- Reuse a full slot's depleted consumable space when adding a new entry, without clearing its other assignments.
- Fixed wheel input interfering with potion-name typing in Alchemy.
- Added safeguards for wheel-file loading and initial creation. Local reload and full-restart tests passed; the separately reported remote persistence problem remains unverified.
- Simplified keyboard support to `[Keyboard] bKeyboardSupport`. Removed obsolete settings; default center-rest time is now 400 ms.

## Install

**Use instead of other wheel mods, including Improved Wheel Menu.** They share the game's quick-key radial and hotkeys. The plugin keeps the filename `ImprovedWheelMenu.dll`.

Requires the **Steam version** of Oblivion Remastered, [OBSE64](https://www.nexusmods.com/oblivionremastered/mods/282) 0.2.2 or newer, and [Address Library for OBSE Plugins](https://www.nexusmods.com/oblivionremastered/mods/4475). Tested on game build **1.512.105.0**; Game Pass / Microsoft Store is unsupported.

1. Close the game completely and remove other wheel mods.
2. Install the release ZIP through Vortex, or copy its `OblivionRemastered` folder into the game folder. MO2 requires a game-root installation through Root Builder or an equivalent.
3. Load your character and assign entries. A new character setup starts empty and clears the game's original quick-key assignments automatically; no manual cleanup is needed. Existing Improved Wheels Plus 2.x assignments are retained.

## Default controls

| Action | Keyboard / mouse | Controller |
|---|---|---|
| Open gameplay wheel | Tap **R**; tap again to cancel | Hold **LB** |
| Point at a slot | Mouse | Right stick |
| Previous / next wheel | **Q / E** | **D-pad left / right** or **L3 / R3** |
| Previous / next entry | **Z / C**; right click advances | **LT / RT** |
| Use pointed entry | Left click or **Backslash** | **A / RB**, or release **LB** on the highlighted entry |
| Cancel | **R** | **B** |
| Favorite highlighted menu entry | **F** | **Select** |
| Open assignment panel in Inventory / Magic | **R** | **L3** |
| Assign highlighted menu entry to pointed slot | Left click | **A** |
| Clear pointed slot in assignment panel | Hold **X** for 0.6 seconds | Hold **X** for 0.6 seconds |

Hold **F6 / F7 / F8** to open Inventory / Magic / Ammo directly. **1–8 / Numpad 1–8** use slots directly in gameplay or assign highlighted entries in menus. While the assignment panel is open, **Z / C**, **LT / RT**, or right click browse entries within the pointed slot.

The dedicated controller Ammo shortcut is unbound by default (`[AmmoWheel] uButton=0`); Ammo remains available by switching wheels. Set `uButton=8` for D-pad Right. Existing INI bindings are retained.

## Settings and compatibility

Close the game before editing `OblivionRemastered\Binaries\Win64\OBSE\Plugins\ImprovedWheelMenu.ini`, then restart.

- `[Keyboard] bKeyboardSupport=1` enables keyboard/mouse support, including without a connected controller. Key bindings are configurable in the same section.
- `[Wheel] bSlowTime=0` disables slow motion; `uSlowTimePercent` sets its speed. Other slow-down mods may conflict.
- Capacity and behavior options include `uEntriesPerSlot`, `bChooseOnClose`, `bStayOpenAfterUse`, `bPointFilledOnly` and `bShowSlotCapacity`.
- `[Theme] bMagicWheel=0` or `bInventoryWheel=0` restores that wheel's native appearance.
- Animation speed, standard Ammo size, left-side favorite stars, armor/clothing assignment and native L3 assignment-panel behavior are fixed; the old options for these are no longer read.
- Assignments are stored under `OBSE\Plugins\ImprovedWheelsPlus\Assignments`. Back them up before resetting a setup; see the User Documentation.

**Works together with Menus Revamped.** Its search-bar typing, mouse interaction and controller keyboard take priority over wheel shortcuts. Neither mod requires the other. Use the separate **2.4.0 legacy download** if you still use the standalone SearchLight or Smooth Menus plugins; do not run those alongside Menus Revamped.

Known limitation: direct **F6 / F7 / F8** wheel shortcuts can reset Caps Lock walking to running. The default **R** wheel and direct **1–8** activation preserve walking.

## Build and tests

Requires [XMake](https://xmake.io) 3.0+, a C++23 compiler (MSVC or clang-cl), and the Windows SDK. Run from a configured compiler environment:

```bat
git clone --recurse-submodules https://github.com/cfa911/Oblivion-Remastered---Mod---Improved-Wheels-Plus.git
cd Oblivion-Remastered---Mod---Improved-Wheels-Plus
xmake f -y -p windows -a x64 -m releasedbg
xmake build ImprovedWheelMenu
xmake build Tests
xmake run Tests
```

The DLL is written to `build/windows/x64/releasedbg/ImprovedWheelMenu.dll`. Shipped assets are under `dist/`. Additional focused regression scripts live in `tests/`; their compiler requirements are documented in each script.

## Source layout

| Folder under `src/` | Purpose |
|---|---|
| `core/` | Entry point, logging, settings, text and atomic-file helpers |
| `engine/` | Unreal reflection, hooks, widgets and session helpers |
| `game/` | Menus, inventory, quick keys, rows and custom names |
| `input/` | Controller, keyboard, mouse and optional mod integration |
| `wheels/` | Assignment storage, selection, item use and wheel state |
| `overlays/` | Wheel themes, labels and counters |
| `ammo/` | Ammo wheel |
| `favourites/` | Favorite indicators and behavior |
| `slowtime/` | Gameplay wheel slow motion |

## Credits and license

Based on [Improved Wheel Menu](https://github.com/ApocryphaRealm/ImprovedWheelMenuOR) by ApocryphaRealm. Its native radial, multi-entry slots, separate spell and arrow wheels, favorites and assignment panel form the foundation. Modifications are maintained by **cfa911**; original authorship and Git history are retained.

**GPL-3.0-or-later:** see [LICENSE](dist/LICENSE), [NOTICE](dist/NOTICE.md) and [third-party notices](dist/THIRD_PARTY_NOTICES.md). Built with [CommonLibOB64](https://github.com/libxse/commonlibob64); its modding exception and other component notices remain in the distributed license files.
