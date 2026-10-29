# Improved Wheels Plus — User Documentation

For version **2.4.1**. [Nexus page](https://www.nexusmods.com/oblivionremastered/mods/5713) · [Change history](CHANGELOG.md)

Keep weapons, armor, supplies, spells and arrows within reach using three separate wheels, with full keyboard, mouse and controller support.

## Contents

- [Requirements and installation](#requirements-and-installation)
- [Your first load and upgrades](#your-first-load-and-upgrades)
- [The three wheels](#the-three-wheels)
- [Keyboard and mouse controls](#keyboard-and-mouse-controls)
- [Controller controls](#controller-controls)
- [Adding and organizing entries](#adding-and-organizing-entries)
- [Choosing and canceling](#choosing-and-canceling)
- [Slow motion while a wheel is open](#slow-motion-while-a-wheel-is-open)
- [Settings and appearance](#settings-and-appearance)
- [Assignments and backups](#assignments-and-backups)
- [Compatibility](#compatibility)
- [Troubleshooting](#troubleshooting)
- [Credits](#credits)

## Requirements and installation

**Steam only. Use this instead of other wheel mods, including the original Improved Wheel Menu.** The mod takes over the game's quick-key radial and hotkeys.

Requirements:

- [OBSE64](https://www.nexusmods.com/oblivionremastered/mods/282), version 0.2.2 or newer.
- [Address Library for OBSE Plugins](https://www.nexusmods.com/oblivionremastered/mods/4475).
- Oblivion Remastered for Steam. Tested on game build **1.512.105.0**. Game Pass / Microsoft Store support is not established; ASI or UNBSE loading does not establish compatibility.

1. Close the game completely.
2. Remove any other wheel mod.
3. Install with your mod manager, or copy the included `OblivionRemastered` folder into your game folder.
4. Start the game, load your character and assign your favorites.

For Mod Organizer 2, install at the game root through Root Builder or an equivalent. The plugin files belong under `OblivionRemastered\Binaries\Win64\OBSE\Plugins`. Keep the filenames `ImprovedWheelMenu.dll` and `ImprovedWheelMenu.ini`; the asset folder is `ImprovedWheelsPlus`.

## Your first load and upgrades

**The first time each character uses the mod, the game's original item and spell hotkeys are cleared and the mod's wheels start empty.** No manual cleanup is required. Rebuild your loadout using favorites, the assign panel or number keys.

Existing Improved Wheels Plus **2.x** assignments are retained when updating to 2.4.1. Legacy wheel files and other wheel mods' assignments are not imported. Existing INI files keep their configured bindings, so your keys may differ from the defaults below.

## The three wheels

| Wheel | Contents | Behavior |
| --- | --- | --- |
| Inventory | Weapons, armor, clothing, potions, poisons and ingredients | Equip or unequip gear; use consumables. Choosing gear you already wear takes it off. |
| Magic | Spells, including spells made at the Altar of Spellmaking | Selects your active spell and updates its HUD icon. Cast with your normal cast control. Custom spells with identical names are supported. Spells the character does not know (for example in an earlier save) are hidden, not erased. |
| Ammo | Assigned arrow types | Eight cells, one arrow type each, kept where you put them. No bow is needed. Choosing an entry equips those arrows. To assign, highlight arrows in the Inventory and press **L3**. |

Inventory and Magic each have **eight slots, with up to five entries per slot by default**: space for 40 item entries and 40 spell entries. Capacity is configurable with `uEntriesPerSlot`.

Point at a slot and cycle its entries to choose the one you want. The center shows the item or spell's name, including custom names. Counters such as **2/3** mean the second entry out of three entries currently in that slot; **0/0** means empty. The Ammo wheel has no counters.

A repair hammer is also supported: choosing it opens the Inventory, then the game's repair menu. Press **Esc** to leave the repair menu and again to leave the Inventory. This does not imply support for other utility tools.

## Keyboard and mouse controls

Default bindings:

| Action | Input |
| --- | --- |
| Open gameplay wheel / close without choosing | Tap **R** |
| Point at a slot | **Mouse** |
| Use the pointed entry | **Left click** or **\** (backslash) |
| Browse previous / next entry | **Z / C** |
| Browse next entry | **Right click** |
| Switch previous / next wheel | **Q / E** |
| Use the selected entry in a numbered slot, without opening the wheel | **1–8** or **Numpad 1–8** |
| Open Inventory / Magic / Ammo directly | Hold **F6 / F7 / F8** |
| Favorite / unfavorite the highlighted menu entry | **F** |
| Open the Inventory / Magic assign panel | **R** in that menu |
| Assign the highlighted item or spell to a numbered slot | **1–8** or **Numpad 1–8** in its menu |
| Clear a pointed slot in the assign panel | Hold **X** for 0.6 seconds |

To get started: tap **R**, point with the mouse, right click until the entry you want is shown, then left click. Tap **R** again to close. Number keys act on the active wheel and its selected entries.

## Controller controls

| Action | Input |
| --- | --- |
| Open the wheel | Hold **LB** |
| Point at a slot | **Right stick** |
| Browse previous / next entry | **LT / RT** |
| Use the pointed entry | **A** or **RB**, or release **LB** while pointing |
| Switch previous / next wheel | **D-pad left / right** |
| Cancel without choosing | **B** |
| Favorite / unfavorite the highlighted menu entry | **Select** |
| Open / close the Inventory / Magic assign panel | **L3** |
| Assign to the pointed slot | **A** in the assign panel |
| Clear a pointed slot | Hold **X** for 0.6 seconds in the assign panel |

The dedicated Ammo shortcut opens the Ammo wheel; no bow is needed. By default it is a full ring of eight cells in the middle of the screen; with `uPosition=0` in `[AmmoWheel]` it is a compact half-wheel on the right edge, which becomes a full ring on its own above five arrow types. Point with the right stick or **D-pad up / down**, then press its button again or **A** to equip. **B** cancels. Its binding has its own row in the game's Controls menu.

## Adding and organizing entries

- **Quick favorite:** highlight an item in Inventory or a spell in Magic, then press **Select / F**. Press again to unfavorite it. Favorite stars also appear on your own items in barter and container lists.
- **Choose a slot:** highlight the item or spell and open the assign panel with **L3 / R**. Point at a slot and press **A / left click**, or use **1–8 / Numpad 1–8** to assign directly.
- **Browse an assigned slot:** in the panel, **LT / RT** or **Z / C** cycle its entries; right click advances. Triggers do not switch menu tabs while this panel is open.
- **Clear a whole slot:** point at it in the assign panel and hold **X** for 0.6 seconds.

Favorites can be dropped, sold and stored by default. An item leaves the wheel when you no longer carry any of it. The game's sorting stays on **Y** for controller players.

## Choosing and canceling

The default controller behavior is **point and release**: releasing the wheel button while a slot is highlighted uses that entry. To close without choosing, press **B**, or center the stick and wait for the highlight to clear before releasing **LB**. Pointing alone does not use an entry.

Using an Inventory entry with **A**, **RB** or **left click** keeps the wheel open by default, allowing several items to be used in one opening. Releasing immediately after an explicit choice does not use that same slot twice.

Keyboard **R** toggles the gameplay wheel; pressing it again closes without choosing. **Esc** is not a mod-provided wheel or assignment-panel cancel binding.

Choose a spell to select it, then cast normally. Selecting a spell or arrows does not use an Inventory item in the matching slot. Game message dialogs take priority over the wheel.

## Slow motion while a wheel is open

While a gameplay wheel is open (Inventory, Magic or Ammo), the game world slows to one tenth speed. It returns to
normal the moment the wheel closes. Sound is not slowed.

Slow motion is **on by default**. To turn it off, set `bSlowTime=0` in the `[Wheel]` section of the INI.
`uSlowTimePercent` (1 to 99, default `10`) sets the speed while it is on. Slow motion may not be compatible with
other slow-down mods.

## Settings and appearance

Close the game, edit `OblivionRemastered\Binaries\Win64\OBSE\Plugins\ImprovedWheelMenu.ini`, then restart. Use the existing sections and comments in the [shipped INI](dist/OblivionRemastered/Binaries/Win64/OBSE/Plugins/ImprovedWheelMenu.ini) as the reference for placement and accepted values. `1` enables a Boolean option; `0` disables it.

| Setting | Purpose |
| --- | --- |
| `uEntriesPerSlot` | Maximum entries per Inventory / Magic slot; default 5. |
| `bChooseOnClose` | Use the pointed entry when releasing the controller wheel button; enabled by default. |
| `bStayOpenAfterUse` | Keep the Inventory wheel open after an explicit use; enabled by default. |
| `bPointFilledOnly` | Skip empty slots when pointing; disabled by default. |
| `bRememberWheel` | Reopen the last wheel rather than always Inventory. |
| `bCentreRestSnap`, `uRestSnapMs` | Clear the pointed slot when the stick returns to rest; adjust the rest delay. |
| `bArmourOnWheel` | Allow armor and clothing; enabled by default. |
| `bBlockFavouriteDropSell` | Block dropping and selling favorites; disabled by default. |
| `bShowSlotCapacity` | Show counters against capacity, such as 2/5, instead of current entry count, such as 2/3. |
| `uAnimSpeedPercent` | Adjust the radial animation speed. |
| `bSlowTime` | World slowdown while a gameplay wheel is open. Default `1` (on); set `0` to turn it off. |
| `uSlowTimePercent` | World speed with slowdown enabled: `1`–`99`, default `10` (one tenth speed). Normal speed returns when closing starts. |

Keyboard controls are configurable under `[Keyboard]`, including `sEquipmentWheel`, `sMagicWheel` and `sAmmoWheel` for direct shortcuts. `NONE` disables a direct shortcut. Controller wheel and favorite bindings are configurable too; see the INI comments.

If the wheel or menu cursor flickers after rebinding the gameplay wheel, set `sNativeWheelKey` to the game's own keyboard Quick Keys binding and keep `bWheelToggle=1`. For example, with Quick Keys on Q and the wheel trigger on mouse button 5, use `sNativeWheelKey=Q` and `sLB=0x06`. The trigger opens the wheel through Q instead of virtual controller LB. This key must match the game's Controls setting; the mod does not change that setting. `sNativeWheelKey=NONE` (the default) preserves the original behavior, including the native R toggle when `sLB=R`. Restart the game after editing. This option affects opening the gameplay wheel; it does not remove every use of virtual controller input.

### Wheel themes

Version 2.2.0 gives Magic a blue wheel with a gold pointer and title outline, and Inventory a red wheel with a silver pointer and title outline. Assignment panels match. Ammo keeps its existing appearance.

Under `[Theme]`, the defaults are:

```ini
[Theme]
bMagicWheel=1
bInventoryWheel=1
```

Set either value to `0` to restore that wheel's original look independently.

## Assignments and backups

Each character has separate files under:

```text
OblivionRemastered\Binaries\Win64\OBSE\Plugins\ImprovedWheelsPlus\Assignments
```

The `.iwp` file stores wheel assignments; the matching `.spells` file stores custom spell names. Back up both together with the game closed. Keep your character's matching game saves too: when loading a save, entries for items or spells it does not own are removed from the wheel.

## Compatibility

- **Other wheel mods:** incompatible. Use one wheel mod only.
- **Menus Revamped:** optional; neither mod requires the other. From 2.4.1 the wheel asks `MenusRevamped.dll` whether a search bar or the controller keyboard has the input: typing into or clicking a bar takes priority over wheel shortcuts, and the wheel steps aside while the on-screen keyboard is open. Menus Revamped replaces the separate SearchLight and SearchLight - Controller Keyboard plugins; 2.4.1 no longer detects those older plugins (use wheel 2.4.0 with them).
- **Game Pass / Microsoft Store, alternative loaders:** unsupported unless separately verified. The released build requires OBSE64 and Address Library.

## Troubleshooting

**My controls differ from this guide.** Existing INI files retain their configured bindings. Check `[Keyboard]` and the controller settings before replacing your configuration.

**Choosing a weapon or armor takes it off.** This is intentional: choosing worn gear unequips it; choosing unworn gear equips it.

**The wheel uses something when I release its button.** Set `bChooseOnClose=0` if you prefer an explicit use button. **B** always cancels the gameplay wheel.

**The wheel stays open after I use an item.** Set `bStayOpenAfterUse=0` if you prefer it to close after each use.

**Walking switches to running.** Direct wheel shortcuts **F6 / F7 / F8** can reset **Caps Lock** walking. Opening with default **R** and using **1–8** directly preserve walking. The packaged README also notes that changing the main wheel key away from default **R** can affect walking with number-key use.

**Controller spell assignment does not work.** One player reported this on 1.1.7; it remains unconfirmed. Include controller model, Steam Input state and the exact assignment steps in a report.

**Reporting another issue:** include the mod version, game build, input device, other wheel/input mods, and the exact action used: release, **A**, **RB**, mouse click, number key or direct shortcut. The log is:

```text
Documents\My Games\Oblivion Remastered\OBSE\Logs\ImprovedWheelMenu.log
```

Normal logging uses `[Log] uLogLevel=3`. If more detail is needed, temporarily use `uLogLevel=1`, reproduce once, then return to `3`.

## Credits

Based on [Improved Wheel Menu](https://www.nexusmods.com/oblivionremastered/mods/5671) 1.0.1 by ApocryphaRealm. The original provides the radial, multi-entry slots, separate Magic and Ammo wheels, favorites and assignment foundation. This fork adds keyboard and mouse support, expanded gear handling, controller options, per-character storage, custom-name handling, themes and fixes.

Thanks to the OBSE64, Address Library and CommonLibOB64 teams. GPL-3.0-or-later; see [LICENSE](LICENSE) and [third-party notices](dist/THIRD_PARTY_NOTICES.md). Fork additions credited to cfa911.
