Improved Wheel - Keyboard and Better Controller Support
=======================================================
Version 2.5.0

The dedicated controller Ammo shortcut is unbound by default ([AmmoWheel] uButton=0).
Set uButton=8 for D-pad Right. The Ammo wheel stays enabled; existing INI bindings are preserved.

A modified build of Improved Wheel Menu (Oblivion Remastered) by ApocryphaRealm (GPL-3.0). It replaces the original
ImprovedWheelMenu.dll and keeps its file and setting names, so install it instead of the original, not beside it.
NOT COMPATIBLE WITH OTHER WHEEL MODS: it takes over the game's quick-key radial and hotkeys, so use only one wheel mod.
What this build adds to the original 1.0.1 is listed under ADDED IN THIS BUILD below and in CHANGELOG.md.

Oblivion Remastered's own quick-key radial, made into a full wheel menu: eight slots that each hold several items, a
Magic wheel of its own for spells, and an ammo wheel of eight cells for your arrows, no bow needed. The game's own radial, button
prompts and quick keys are used throughout; the Inventory and Magic wheel pictures, pointers and title colors have a new look.

ADDED IN THIS BUILD
-------------------
  * AMMO WHEEL OF EIGHT CELLS: one arrow type per cell, kept exactly where you put it, and no bow is needed to open it. Assign from
    the Inventory: highlight arrows and press L3 (the Show Shortcuts panel), point at a cell and press A. Above five arrow types the
    edge layout becomes a full ring on its own. The Ammo wheel has no 1/x counters.
  * NEW LOOK: blue Magic wheel with a gold pointer and title outline; red Inventory wheel with a silver pointer and title outline.
    Also applies to the assignment panels. [Theme] bMagicWheel=1 and bInventoryWheel=1 enable each look separately; 0 restores the native look.
    Missing wheel pictures retain the native wheel; the new overlays step aside while a game message is up.
  * KEYBOARD SUPPORT, the main addition: the whole wheel menu works from the keyboard, with or without a controller. Every key is
    configurable in the [Keyboard] section of ImprovedWheelMenu.ini (and so is every other setting): each key presses the controller
    control it is named for, so every rule below works from the keyboard. Defaults: R toggles the gameplay wheel; Q / E switch wheels;
    Hold F6 / F7 / F8 to open Equipment / Magic / Ammo directly.
    Z / C step entries; right click advances an entry; backslash uses; F favourites;
    native menu R opens assignment; hold X clears a slot. The number keys 1-8 and Numpad 1-8
    are slot keys (in gameplay they directly use the selected entry without opening the wheel; in the Inventory / Magic menu they assign the highlighted item or spell to it).
  * Hold X (0.6 s) on a pointed slot of the assign panel to clear the whole slot; L3 opens the assign panel.
  * Select (uFavouriteButton) favourites the highlighted item or spell; LB (uWheelButton) opens the wheel; the game's Y sorting is kept.
  * Spells you made at the Altar of Spellmaking can go on the Magic wheel, also when two share a name.
  * Slot counters under every slot of the Equipment and Magic wheels ("2/3": the entry shown out of the entries the slot holds, "0/0" when empty; bShowSlotCapacity=1 for "2/5"), and the wheel's centre names the pointed spell or item (also names you typed).
  * A repair hammer on the Equipment wheel opens the Inventory and then the repair menu (the game needs the Inventory open for it); Esc leaves the repair menu, a second Esc the Inventory.
  * The whole wheel from the keyboard: every key of the [Keyboard] section is rebindable; number keys 1-8 and Numpad 1-8 are slot keys.
  * Buttons that open a wheel directly (uEquipmentWheelButton, uMagicWheelButton, uAmmoWheelButton), the radial's animation speed
    (uAnimSpeedPercent), and where the ammo wheel opens (uPosition).
  * Wheels are kept per character, and anything a loaded save does not have (a spell or item that character does not own there, for example in an earlier save) is
    hidden from the wheel, not erased, so nothing tries to use a form that is not there and it returns in a save that has it.
  * Favourites can be dropped and sold (bBlockFavouriteDropSell, off); they leave the wheel once none is carried. 1 restores the original mod's block.
  * bStayOpenAfterUse (on by default): an item chosen on the Equipment wheel (gear, potions, poisons) is used and the wheel stays open, so several can be used in one opening.
  * Armour and clothing can be put on the Equipment wheel like any item (bArmourOnWheel, on; 0 keeps only shields, as the original mod).
  * The stick points only at slots that hold something (bPointFilledOnly, off by default; 1 turns it on): empty slots are skipped on the Equipment, Magic and Ammo wheels, as the Ammo
    shortcut always did.
  * bChooseOnClose (on by default): closing the wheel by letting go of its button, or pressing it again, uses the slot the stick points at, on whichever wheel is up -
    point and close, no A or RB needed. B still closes without using anything, and so does letting go with the stick back at rest (bCentreRestSnap).
  * bRememberWheel (on): the radial opens on the wheel you left it on; 0 = always on the Equipment wheel.
  * bShowSlotCapacity (off): the counter under a slot reads the entry shown out of the entries the slot holds ("2/3", "0/0" when empty); 1 = out of the capacity (uEntriesPerSlot), "2/5", "0/5".
  * A left mouse click on the radial or the assign panel is the A button on the slot the mouse points at. A potion, poison or ingredient is used at
    once, however you choose it. Choosing the slot of a weapon, armour or clothing that is already worn takes it off.
  * The Ammo wheel is laid out the same on the shortcut and on the radial (LB, then D-pad right), and shows the pointed arrows' name inside the wheel.
  * The wheels' own texts use the game's UI font: the name above the wheel is gold with a black outline and a shadow, the texts inside are a soft grey.
  * Works with Menus Revamped (2.4.1): while one of its search bars is being typed in, the mouse is over a bar, or its controller keyboard is open, the wheel's menu rules
    stand aside. The separate Menu Search / SearchLight and controller keyboard plugins are no longer detected (use wheel 2.4.0 with them).
  * SLOW MOTION while a gameplay wheel is open (Equipment, Magic or Ammo): the world slows to one tenth speed and
    returns to normal the moment the wheel closes. It is on by default. [Wheel] bSlowTime=0 turns it off;
    uSlowTimePercent (1 to 99) sets the speed. Sound is not slowed. It may not be compatible with other slow-down
    mods.

WHAT YOU GET
------------
  * The inventory wheel: hold the game's quick-key button to open the radial and point with the right stick. Each of
    the eight slots holds up to five items (uEntriesPerSlot); the counter under the wheel shows which place the shown
    item has and how many the slot holds. LT and RT choose the item within the pointed slot, RB or A use it, and
    D-pad left and right switch between the inventory and Magic wheels. Pointing alone never uses anything. With
    bChooseOnClose on (the default) letting go of the wheel button uses the pointed slot; with it off, or with the stick at rest, it uses
    nothing. B always closes the wheel without using anything.
  * The centre rest snap (bCentreRestSnap, on by default): with it on, the stick back at rest in the middle points
    no slot, so letting go of the stick never picks one by mistake. With it off a slot stays pointed until you pick
    (A or RB) or close the wheel.
  * Putting things on the wheel: the favourite button (Select by default, uFavouriteButton) on an item in the
    inventory, or on a spell in the magic menu, makes it a favourite and puts it on its wheel; again takes it off. A
    favourite cannot be sold or dropped. To place it yourself, press L3 for the game's assign panel, point a slot,
    choose its place with D-pad left and right, and press A. Hold X (0.6 s) on a pointed slot of the panel to clear the
    whole slot. The game's own sorting stays on Y.
  * Spells you made at the Altar of Spellmaking go on the Magic wheel like any other, also when two share a name (the
    menu row's index identifies the spell).
  * Each slot shows "2/5" (the place of the shown entry out of the slot's cap) on both wheels, and the centre of the wheel
    names the pointed spell or item.
  * Keyboard (INI [Keyboard], every key rebindable): the keyboard is a virtual controller - each key presses the
    controller control it is named for, so every rule here works from the keyboard. Defaults: R toggles the gameplay wheel, Q / E switch wheels,
    Z / C step entries, right click advances an entry, backslash or left click uses,
    F favourites, native menu R opens assignment, and hold X clears a slot. Number keys and Numpad 1-8
    use slots in gameplay or assign the highlighted item/spell in menus. Existing ini files retain their bindings.
  * The Magic wheel: a separate wheel for spells, always drawn as itself. A pick makes the spell the one the cast
    button casts, and the HUD's quick-magic picture follows it.
  * The Ammo wheel has eight cells, one arrow type per cell. In Inventory, highlight arrows and open Show Shortcuts
    (L3), point at the desired cell and press A. Press A again on the same assignment to remove it; hold X to clear
    a cell. The favourite shortcut still fills an empty cell. No bow is required to open Ammo or preselect arrows.
    The radial and centred shortcut preserve your exact cell positions. Legacy edge mode stays compact for up to
    five arrow types and automatically expands to a full ring for six to eight, so every assignment stays reachable.
  * Settings in ImprovedWheelMenu.ini: entries per slot, the rest snap and its time, the ammo wheel on/off, button
    and size, the log level.

KNOWN ISSUES
------------
  * There is no settings page; the INI holds the few settings there are.
  * Existing ini files retain their configured bindings; update [Keyboard] for the new defaults.
  * The number keys 1-8 keep your Caps Lock walk mode only while the wheel key is R (the default). With another wheel key set in [Keyboard] sLB they use the older controller-style script, which switches walk back to run.

FIRST START: PLUG AND PLAY
--------------------------
Nothing to clear by hand. The first time each character loads (about three seconds in), the mod clears the game's own hotkey wheel
(items and spells) and starts its own wheels empty, then saves them as OBSE\Plugins\ImprovedWheelsPlus\Assignments\<character>.iwp
(and <character>.spells, the names of the spells you made). Anything that was on the game's own wheel is gone after that first load; make
your wheels with the favourite button, the assign panel (L3 / keyboard R, then hold X for 0.6 s on a slot to clear it) or the number keys.
Wheels with no stored file start empty, and the game's own hotkeys are checked empty before you can assign anything.
Wheels of an earlier version or of another wheel mod are not imported. Existing ini files retain their bindings.

INSTALLATION
------------
  * Everything goes under OblivionRemastered\Binaries\Win64\OBSE\Plugins\: ImprovedWheelMenu.dll, .ini and the
    ImprovedWheelsPlus folder (the favourite star's fill picture).
  * Install with your mod manager or drop the OblivionRemastered folder over the game's own. Mod Organizer 2 users
    need the folder installed at the game's root (Root Builder or an equivalent).

DEBUGGING
---------
  Send the log with any bug report: Documents\My Games\Oblivion Remastered\OBSE\Logs\ImprovedWheelMenu.log. Set
  [Log] uLogLevel=1 in ImprovedWheelMenu.ini for more detail first.

REQUIREMENTS
------------
  * OBSE64 (Nexus 282), 0.2.2 or newer
  * Address Library for OBSE Plugins (Nexus 4475)

LICENCE
-------
  GPL-3.0-or-later (LICENSE, NOTICE.md). Copyright (C) 2026 ApocryphaRealm; the additions of this build Copyright (C) 2026 cfa911.
  Built on CommonLibOB64 (GPL-3.0); spdlog and the other linked components are listed with their notices in THIRD_PARTY_NOTICES.md.
  Source of this build: https://github.com/cfa911/Improved-Wheel-Keyboard-and-Better-Controller-Support
  Original: https://github.com/ApocryphaRealm/ImprovedWheelMenuOR

MINOR ISSUE
-----------
F6/F7/F8 direct-wheel opening can reset Caps Lock walking to running. R and 1-8 quick use preserve walking.

In Equipment and Magic assignment panels, right-click/C/RT advance the pointed slot entry; Z/LT go backward. Triggers do not change tabs while the panel is open. Esc is no longer a mod-provided wheel or assignment-panel cancel binding.
