# Improved Wheel Menu (PerfectedWheelerOR) - changelog

Written as changes happen, not reconstructed afterwards (rule 61). Started 2026-09-29; the history before this file is
in `git log`. A version number is issued by the version gate only once a build is seen working in game (rule 48).

## 2.4.1 - 2026-10-27 - Improved Wheels Plus

### Changed
- Reduced repeated reflection, wheel-widget and maintenance work. Development measurements showed lower plugin processing time in gameplay, menus and assignment panels; no frame-rate or radial timing improvement is claimed.
- Reorganized the source into feature folders and removed duplicated helpers without changing controls or settings.
- Works with Menus Revamped: typing into a search bar, clicking a bar, and the controller keyboard now take priority over wheel shortcuts by asking `MenusRevamped.dll` (`MenusRevamped_IsTyping`, `MenusRevamped_CursorOverBar`, `MenusRevamped_IsControllerKeyboardOpen`).
- The separate SearchLight and SearchLight - Controller Keyboard plugins (`MenuSearch.dll`, `MenuSearchControllerKeyboard.dll`, `OblivionSearchKeyboard.dll`) are no longer detected. Menus Revamped replaces them and they must not run together; keep wheel 2.4.0 if you still use them.

## 2.4.0 - 2026-10-23 - Improved Wheels Plus

### Added
- Slow motion while a gameplay wheel is open (Equipment, Magic or Ammo): the game world slows to one tenth speed and goes back to normal the moment the wheel closes. It is on by default. `[Wheel] bSlowTime=0` turns it off and `uSlowTimePercent` (1 to 99) sets the speed. It may not be compatible with other slow-down mods. Sound is not slowed.

### Fixed
- Loading an earlier save of the same character no longer lets a later menu visit remove Equipment, Ammo and Favourite entries from your wheels.
- Choosing Magic or Ammo with a click in the main R wheel no longer leaves Q and E unable to switch wheels until the wheel is closed and opened again.

### Changed
- Less work every frame: the plugin no longer looks for open game messages on every read while a wheel or panel is open and nothing is pressed. In a diagnostic build on one machine, message look-ups fell from about 171 to about 13 per second and the time spent per controller read from 4.1 to 0.7 ms. This is a measure of the plugin's own work, not a measured frame-rate gain.

## 2.3.0 - 2026-10-23 - Improved Wheels Plus

### Added
- The Ammo wheel now has eight cells, one arrow type each. Assign arrows from the Inventory: highlight them, press L3 (Show Shortcuts), point at a cell and press A, as for the other wheels. Every arrow stays in the exact cell you chose.
- No bow is needed any more to open the Ammo wheel or to choose arrows on it.
- The edge-style Ammo wheel (uPosition=0) becomes a full ring on its own above five arrow types, so every assignment stays reachable.

### Changed
- The Ammo wheel no longer shows 1/x counters; each cell holds a single arrow type.
- The Magic wheel hides spells the character does not know, for example after loading an earlier save. They stay stored and come back in a save that has them.
- Loading an earlier save no longer erases wheel assignments that character does not have yet.
- A wheel with no stored file now starts empty and checked: the game's own hotkeys are cleared and verified empty before you can assign anything.
- [Wheel] uRestSnapMs default 150 -> 250 (an existing INI keeps its own value).

### Fixed
- Controller: choosing by pointing with the right stick and letting go of LB now selects reliably, also when you switch between wheels.
- Closing a wheel quickly and opening it again at once no longer leaves the right stick dead.
- Safer reading of the inventory, arrow and spell lists, and bounds checks on wheel slots, so a list that changes at the wrong moment is less likely to cause a crash.

### Minor issue
- F6/F7/F8 can still reset walking to running. R and direct 1-8 quick use preserve walking.

## 2.2.0 - 2026-10-22 - Improved Wheels Plus

### Added
- Blue Magic wheel with a gold pointer and a blue title outlined in gold.
- Red Inventory/Equipment wheel with a silver pointer and a red title outlined in silver.
- Matching themes in the Magic and Inventory assignment panels. Ammo keeps its existing appearance.
- Independent [Theme] bMagicWheel and bInventoryWheel switches, both on by default; set either to 0 to restore that wheel's native look. Missing wheel pictures retain the native wheel.

### Fixed
- Wheel overlays, slot counters and the center name step aside while a game message is up.
- Equipment title color uses a text comparison instead of comparing string pointers.

### Removed
- Development-only radial measurements, self-check file output, widget-tree dump helper and TestBench input-driving integration.

### Minor issue
- F6/F7/F8 can still reset walking to running. R and direct 1-8 quick use preserve walking.

## 2.1.0 - 2026-10-22 - Improved Wheels Plus

### Added
- Hold F6, F7 or F8 to open the Inventory, Magic or Ammo wheel directly (Ammo needs a bow). Rebind with sEquipmentWheel/sMagicWheel/sAmmoWheel.
- In Inventory and Magic assignment panels, right-click/C/RT advance the pointed slot entry; Z/LT go backward. Triggers no longer change menu tabs while the panel is open.

### Changed
- Gameplay 1-8 and Numpad 1-8 directly activate the selected entry on the active wheel without opening the radial or simulating controller input. Equipment toggles, consumables, spell selection and ammo use keep their normal actions.
- Removed mod-provided Esc cancellation and the sB keyboard option. R remains the gameplay wheel toggle; game-native Esc behavior is unchanged.
- Assignment parsing rejects malformed or overflowing IDs without partially loading the file. Assignment and spell-name writes use a flushed temporary file before replacement.
- Queued TES actions check a character-context epoch before execution; pending ordinary wheel state resets on observed context changes. This is not a guarantee of every same-character load transition.

### Fixed
- Repeated/alternating direct number-key use preserves Caps Lock walking instead of falling back to controller-style input.

### Minor issue
- Direct wheel shortcuts F6/F7/F8 can still reset walking to running. Opening with R and direct 1-8 quick use preserve walking.

## 2.0.0 - 2026-10-22 - Improved Wheels Plus

### Changed
- **Plug and play, with a clean start.** The first time a character loads, the mod clears the game's own hotkey wheel (the eight quick-key slots, items and spells) and starts its own wheels empty. The "clear your existing wheel before installing" step is gone. Anything that was on the game's own wheel is lost at that first load; the mod's wheels are made again with the favourite button, the assign panel or the number keys.
- **The wheels live in files of their own.** `OBSE\Plugins\ImprovedWheelsPlus\Assignments\<character>.iwp` holds the three wheels and the favourites of one character, with a format header and the character's identity. A file that is not in this format, or belongs to another character, is never read or overwritten, so the wheels of an earlier version or of another wheel mod are not picked up. The wheel no longer imports the game's hotkeys. Upgrading from 1.x: your wheels start empty.
- **The names of spells you made are kept per character** in `<character>.spells`, in the same folder (the old `ImprovedWheelMenu\SpellNames.txt` was shared by all characters and is no longer read; a spell made in the game has an id that means another spell in another save).
- **The favourite star's picture moved** to `OBSE\Plugins\ImprovedWheelsPlus\FavouriteStarFill.png`; the `ImprovedWheelMenu` data folder is not used any more.
- **Not compatible with other wheel mods** (stated on the page and in the README): it replaces Improved Wheel Menu and takes over the game's quick-key radial and hotkeys.

### Fixed
- The number keys 1-8, and closing the wheel with the mouse, cast the wrong spell on a slot with several entries after spells had been assigned in the Magic menu: the game's own hotkey routine re-selected the spell it kept for that slot about 100 ms after the wheel's choice (the Magic menu's own number-key assign fills that table). The mod now keeps the game's native spell hotkeys empty (checked on every pass of the game's main loop and before every spell choice, through the game's own list clear; items are never touched), so the spell shown on the wheel is the spell cast.
- Unused code of the old hotkey import and the old wheel-drawing path was removed.

## 1.3.0 - 2026-10-21 - Improved Wheels Plus

### Added
- The counter under each slot now reads the entry shown out of the entries the slot holds: "2/3" for a slot with three entries, "0/0" for an empty slot, "-/3" when the game's key holds none of them (a reader's idea: "if I see x/3 I know there's space for two more"). The Ammo wheel follows the same rule. New setting `bShowSlotCapacity` in `[Wheel]` (default 0); set it to 1 for the old style, "2/5", "0/5", "-/5".

### Fixed
- A repair hammer on the Equipment wheel did nothing. It now opens the Inventory and then the repair menu over it, as the game does (the game's own quick-key press has no effect on the hammer, and the repair menu opened from plain gameplay left a blurred screen with an empty black panel). Esc leaves the repair menu and a second Esc closes the Inventory. If the Inventory does not open within 3 seconds nothing is used. Only the standard repair hammer is covered.
- After a click on a slot of the wheel opened with R, the wheel stayed open until Esc or R was pressed; it now closes by itself after the use.
- Esc often needed many presses to close the assign panel (the wheel that opens with R in the Inventory and Magic menus): the game ignored the first L3 the mod played. The mod now plays it again when the panel is still up after 0.6 s, up to three tries, so one Esc closes it; the close sound is heard twice.
- The default look of the slot counters changes ("2/3" instead of "2/5"); `bShowSlotCapacity=1` restores it. No other change to the default settings or the controls.

## 1.2.0 - 2026-10-20 - Improved Wheels Plus

### Fixed
- The Inventory and Magic menus no longer lag while a SearchLight search plugin is installed. Each time the wheel checked whether a search box had focus it walked the game's whole object array (more than two million entries), which stalled the menu and made hovering and scrolling stutter. The check now remembers the search boxes it found and looks again at most once a second and once per menu change, so the cost is a handful of reads.
- Esc now closes the assign panel in the Inventory and Magic menus (the wheel that opens with R). It did nothing there: the game closes the panel on L3, not on B, so the Esc key now plays L3 while the panel is up. A second Esc closes the menu as before.
- Digits and the wheel letters typed into SearchLight's search bar no longer act as wheel keys (typing 1-8 assigned the highlighted item to that slot), and a click on the search bar with the wheel open no longer assigns the highlighted item to slot 1. The wheel asks SearchLight (`MenuSearch_IsTyping`, `MenuSearch_CursorOverBar`), so this needs the SearchLight release after 1.2.1; with 1.2.1 or older the wheel behaves as before.
- No change to the default settings or the controls.

## 1.1.7 - 2026-10-20 - Improved Wheels Plus

### Fixed
- Using a slot with the number keys 1-8 (or Numpad 1-8) no longer switches the character from walk back to run when Caps Lock had set walk mode. The keys fed the game a controller wheel button and right stick, and the game dropped the walk toggle on that input. With R as the wheel key (the default) a slot key now opens the wheel with the R key, points by moving the mouse cursor to the slot's direction and presses only A, as R, pointing and a click do; the cursor is put back where it was. With another wheel key set in the ini the old controller-style script is still used.
- Esc now closes the wheel opened with R, as a second R does (a lone virtual B left it open).
- No change to the default settings or the controls.

## 1.1.6 - 2026-10-19 - Improved Wheels Plus

### Fixed
- German letters (Ä Ö Ü ä ö ü ß) and every other non-ASCII letter in spell and item names now show on the wheels. Names were converted to text one byte at a time, so "Größere" was drawn as "Grere"; they are now converted as UTF-8 (the wheel's centre name, the slot counts, the ammo label).
- Armor and clothing icons now show on the Equipment wheel right after a game load, without opening the Inventory first. The wheel loads the item's own picture from the game's files, as it already did for weapons, potions and ammo.
- A crash when typing a digit key while a save was loading: the scan for the Menu Search text box read the name of an object the game had already freed. That read is now fault-guarded and such an object is skipped.
- No change to the controls or the default settings.

### Developer
- Unit tests for the text helpers (`tests/`, `xmake build Tests && xmake run Tests`); `src/TextUtil.h` holds the engine-free helpers. Not part of the plugin.

## 1.1.5.1 - 2026-10-19 - Improved Wheels Plus

### Fixed
- The keyboard's window hook (the native R toggle) now takes itself off again when the game window is destroyed, and forwards messages through a null-safe helper, so nothing is left in the window procedure chain.
- Its install message is logged at info level instead of warning, so the shipped log (warnings and errors only) stays quiet.
- No change to the controls or the default settings.

## 1.1.5 - 2026-10-19 - Improved Wheels Plus

### Changed
- Keyboard defaults: R toggles the gameplay wheel; Q/E switch wheels; Z/C cycle entries; Esc cancels; hold X clears an assign-panel slot. Native menu R still opens assignment. Existing ini bindings remain configurable.
- Added `[Keyboard] bWheelToggle` and `sNextEntryMouse`; right click advances the pointed slot's entry through the same trigger path as the next-entry key.
- Context-gated virtual keyboard controls to their wheel/menu actions.

### Fixed
- Mouse recentering after tapping and releasing R: the R toggle now preserves the native keyboard action instead of relying on a virtual controller opening pulse.
- Keyboard mouse selection is excluded from controller centre-rest snapping; real stick input returns ownership to the controller.
- Right-click entry cycling no longer directly cycles a separately calculated mouse slot, avoiding reported extra cycles and changes to previous slots.

## 1.1.4 - 2026-10-19 - Improved Wheel - Keyboard and Better Controller Support

### Changed
- `bChooseOnClose` is now 1 by default: pointing at a slot and closing the wheel (letting go of the wheel button, or pressing it again) uses that slot, on whichever wheel is up. B, or letting go with the stick back at rest, still closes without using anything (0 restores the old rule: only A or RB use a slot).
- `bStayOpenAfterUse` is now 1 by default: a gear, potion or poison chosen on the Equipment wheel is used and the wheel stays open for the next choice (0 closes the wheel on every choice). Letting go of the wheel button right after such a choice does not use that slot a second time.
- The built-in defaults of `uAnimSpeedPercent` (300) and of the ammo wheel position (`uPosition` 1, the whole ring in the middle of the screen) now equal the values the shipped ini has; they differed only when a key was missing from the ini.

### Fixed
- A potion on the Equipment wheel was drunk when the Magic wheel was closed with a slot pointed (the wheel button let go, or pressed again) or with B: the game re-points its own selector within a frame and its close uses the quick key of that slot. Every close on nothing now takes the pointed slot's quick key off its item until the radial has closed, as choosing a spell or arrows already did.

## 1.1.3 - 2026-10-18 - Improved Wheel - Keyboard and Better Controller Support

### Added
- An item you dropped, sold or stored leaves the wheels whichever way you did it, mouse and real keyboard keys included (equipment and ammo at once, potions and other consumables in container and barter menus).
- INI `[Wheel] bStayOpenAfterUse` (0 by default): choosing an item on the Equipment wheel (A, RB or a click) puts it on or takes it off and leaves the wheel open for the next choice; potions, poisons and ingredients too (a poison's question is answered without choosing again).
- INI `[Wheel] bArmourOnWheel` (1 by default): armour and clothing can be assigned to the Equipment wheel like any item (choosing one puts it on, choosing it again takes it off). 0 keeps the original rule: only shields.
- The favourite stars also show on your own items in container and barter lists, so a favourite can be seen (and set) when selling or storing; the container's and the merchant's rows have none.

### Changed
- `bPointFilledOnly` is now 0 by default: the stick points at any slot, filled or not (1 keeps the 1.1.2 behaviour of skipping empty slots).
- The favourite star of the Inventory and of the Magic menu moved to the margin before each row's icon (`[Wheel] bFavouriteStarAtLeft`, and `iFavouriteStarX` and `iFavouriteStarSpace` for its place and the room it takes; 0 keeps the old column at the right edge, which stood under the sort headers). The rows' columns no longer shift.
- Favourites can be dropped, sold or handed over (`[Wheel] bBlockFavouriteDropSell`, 0 by default; 1 restores the original mod's block). A dropped or sold item leaves the wheels once none is carried.

### Fixed
- Potions, enchanted gear and other items the player named got no white star, and some no icon on the wheel: their rows could not be matched to the item. A row is now matched by its typed name, then by its weight and count, and rows that still look alike are paired with the items in list order, so a favourite lights only its own row (items with the same name, weight and count cannot be told apart by their numbers and are paired in list order).
- With `bStayOpenAfterUse` and `bChooseOnClose` both on, letting go of the wheel button right after a choice used the same slot again (a weapon put on and taken off, a second potion drunk) - mostly with the mouse and the keyboard's slot keys. The slot just used is no longer used by the close.
- A poison chosen with the wheel staying open asked "apply it to the weapon?" again after every answer (the click or A that answered it was taken as a new choice on the still-open wheel). While any of the game's messages is up over the wheel ("You cannot drink any more potions", "already poisoned") A, RB and clicks answer the message and choose nothing.
- The player sometimes crouched after a choice on a wheel opened with the game's own keyboard key (R): the B the plugin presses to close a radial that stays open could reach the game after the radial had closed (B is crouch). It is now a single short press, sent only while the radial is up, and a poison no longer needs it (it is applied with the wheel open like a potion).
- A slot key (1-8) on a poison made the wheel open and close again and again (the mouse and controller glyphs flashing): the slot key's short press of the wheel button was taken as a tap that keeps the wheel open, and the poison's question closed it. A slot key's use is no tap, and a kept-open wheel that the game closes by itself is let go.
- A click, A or RB on an empty slot no longer closes a wheel that stays open, and no longer presses B (which crouched the player when the wheel was opened with R).
- Stars of items that share a typed name (several "[All] Combat Potion"): a row reused for a twin kept the old star, and twins were matched differently depending on which rows were on screen. A twin row is now matched by where its neighbours say its item sits in the bag, and re-checked whenever the row shows another twin.
- A keyboard slot key (1-8) in the Inventory or the Magic menu assigns the highlighted item or spell to that slot directly (again on the same slot: it comes off). It played controller presses before, and the game switched its button pictures to the controller's.
- The favourite button (Select, or the keyboard's key for it) works on your own rows in container and barter lists, as in the Inventory.
- After loading a game a slot could show `-/5` when all its potions had been used in the previous session: a slot that holds entries but shows none now takes its first usable entry.
- When the item a slot shows is used up (the last potion of a stack) or gone, the slot moves on to its next entry instead of showing -/5.
- Choosing a spell on the Magic wheel now uses the game's own spell selection, as the Magic menu does: the first cast has the right animation and sound (it had the previous spell's animation and no sound).
- The Magic wheel's icon reading no longer crashes the game when a spell is given a slot while the Magic menu rebuilds its list.
- The wheels no longer interfere: choosing a spell or arrows on the Magic or Ammo wheel could use a potion that sat on the same slot of the game's own wheel (the game used its pointed key as the radial closed). The game's own pointed key is now cleared on every close - Magic, Ammo, Equipment, B and a close on nothing - and each wheel uses only what it chose itself. The game re-points its own wheel within a frame of every close, so the quick key of the pointed slot is also taken off its item while the radial closes on a spell or arrows, and put back once it has closed.

## 1.1.2 - 2026-10-18 - Improved Wheel - Keyboard and Better Controller Support

### Added
- INI `[Wheel] bPointFilledOnly` (1 by default): on the Equipment, Magic and Ammo wheels the stick points only at slots that hold something. Empty slots are skipped: the stick's direction goes to the nearest filled slot, as the Ammo shortcut always did.
- INI `[Wheel] bChooseOnClose` (0 by default): with 1, closing the wheel by its button (let go, or pressed again) uses the slot the stick points at on whichever wheel is up (Equipment, Magic or Ammo), so pointing is enough and A or RB are not needed. B still closes without using anything.
- INI `[Wheel] bRememberWheel` (1 by default, as always): the radial opens on the wheel it was left on. 0 makes it always open on the Equipment wheel. A wheel's own button (uMagicWheelButton ...) and the keyboard's own presses (its wheel button, a slot key, a wheel's key) always open the wheel they chose.
- Choosing the slot of a weapon, armour or clothing that is already worn (A, RB, a click, or closing with bChooseOnClose) takes it off.

### Changed
- A left mouse click on the radial or the assign panel is now simply the A button on the slot the mouse already points at (the game points by the mouse's angle, as by the stick's). The cell hit-test and the stick script behind the old click are gone.
- All the wheels' own texts use the game's UI font (Font_Robinson_Localized): the name above the wheel is gold with a black outline and a shadow, the slot counters and the centre name of the pointed slot are a soft grey, a little smaller, without shadow. The centre name now comes from our own label on all three wheels (the Equipment wheel used the game's own text, in another font and size).
- The mod is called **Improved Wheel - Keyboard and Better Controller Support** (it was Improved Wheel Menu - Controller and Keyboard Support). The plugin file is still `ImprovedWheelMenu.dll`, so it still replaces the original and your settings and wheels keep working.
- The plugin finds the controller keyboard under its new file name (`MenuSearchControllerKeyboard.dll`, mod name Menu Search - Controller Keyboard) as well as the old one (`OblivionSearchKeyboard.dll`) and stands aside while it is open.
- The wheel's log is warnings and errors only by default (uLogLevel=3), and the self-check file is only written at uLogLevel 2 or lower.

### Fixed
- A potion, poison or ingredient chosen on the Equipment wheel was not used when chosen with a mouse click (the use waited for the radial to close, and it stayed open). It is now used at once, however it is chosen; if the radial is still open 400 ms after a use, B closes it.
- The wheel button pressed within a moment of closing the Inventory or the Magic menu no longer reads as "in a menu" (the game reports menu mode a little longer while a menu closes), which left the wheel dead for that press.
- The Ammo view of the radial (LB, then D-pad right until the Ammo wheel) puts the arrows where the shortcut's half wheel puts them (packed from the middle), with the counters, the pointed name and the choice (A or RB, LT / RT to step) at those places. The Ammo wheel shows the pointed arrows' name inside the wheel on both the radial and the shortcut.

## 1.1.1 - 2026-10-17 - Controller and Keyboard Support build

### Fixed
- The shipped INI and the built-in defaults now match the game's own controls and this README: the wheel button is LB (uWheelButton=256), favourites are on Select (uFavouriteButton=32) and D-pad down stays the game's own (bDpadDownPanel=0). With the 1.1.0 defaults (D-pad down as the wheel button) the radial opened and closed in a loop on a game that opens the wheel with LB.

## 1.1.0 - 2026-10-17 - Controller and Keyboard Support build (a fork of 1.0.1 by cfa911)

### Added
- Hold X (0.6 s) on a pointed slot of the Inventory or Magic assign panel clears every entry of that slot (the Equipment items stop being favourites). X is kept from the game's Drop while a slot is pointed.
- Spells the player made (full name a placeholder "UI_UserInputText_...", no string-table key) can be put on the Magic wheel: a magic row's InventoryIndex is its spell's place in the player's spell list plus one, proven on the rows the key names (12 of 12 agree before it is used), then used for the rows without a key. Same-named spells are told apart by it.
- Settings: uWheelButton (the wheel button, LB), uFavouriteButton (Select), bDpadDownPanel; the favourite button keeps the game's Y sorting.

- The Magic wheel shows the same "2/5" slot counters as the inventory wheel.
- The wheel's centre text names the pointed slot's spell or item (it said "Shortcut"). On the Magic wheel the name is drawn above the wheel's picture, centred, and the game's own text is hidden; names the player typed (made spells, renamed or enchanted items) are read from the game's own table of typed texts (UVAltarUserInputTextSubsystem > UserInputTextSaveData > UserInputTextsMap, keyed by the placeholder "UI_UserInputText_..." in the form's name); the names learned from the menu rows (ImprovedWheelMenu\SpellNames.txt) are the fallback.

- The keyboard as a virtual controller ([Keyboard] in the INI, every key rebindable): each key presses one controller control, laid over the real pad on the game thread, so every controller rule works from the keyboard; the number row and Numpad 1-8 are slot keys: in gameplay they point at slot N and use it at once (wheel button, the stick at the slot, then A), in the Inventory / Magic menu they assign the highlighted row to slot N through the assign panel; the game's own IA_Game_QuickKeys_Keyboard_N are taken off those keys (bReplaceQuickKeys).

- Direct wheel buttons (uEquipmentWheelButton, uMagicWheelButton, uAmmoWheelButton), the radial's animation speed (uAnimSpeedPercent) and the ammo wheel's position (uPosition) in the INI.
- The wheels are kept per character; what a character cannot have is taken off the wheels a moment after the character loads.
- While the Oblivion Search Keyboard is open the plugin's menu rules stand aside (it asks the keyboard through its export OblivionSearchKeyboard_IsOpen).

### Changed
- bCentreRestSnap defaults are left to the INI; the INI ships it on.

## 1.0.1 - 2026-10-02 - untested

### Fixed
- A quick tap of the wheel button let go before the wheel had shown no longer lets the game close it again: the button is held for the game until the wheel shows (at most 0.6 s), then the wheel stays open until the next press, as a tap is meant to. On a Steam Deck under Proton the wheel shows a few frames after the press, so an ordinary tap was already up and the Equipment wheel closed about 270 ms after opening (SaintAnhel's report, 2026-10-02).

### Added
- The log names every wheel-button press and release in gameplay with its length, the time from the press to the wheel showing, a line when the game runs under Wine / Proton, and a warning when a second controller index is connected (only controller 0 is rewritten).

### Changed
- The held tap only applies in gameplay: a quick D-pad down in the system menu, a container or a barter list is never held, so it cannot repeat a list scroll.

## 1.0.0 - 2026-10-01 - working

The first release. Confirmed by the owner in game on b259746 (DLL sha1 3b18f33ee342, the 05:59 launch): "Unless I'm
mistaken, IWM now works" and "The spell icon is finally swapping to the HUD widget"; earlier the same day he confirmed
LT/RT cycling, RB/A equipping the right item, the rest snap and the item counter. The rounds below are the build's
history as it was written; their "untested" marks are from the day each was built.

What 1.0.0 is:
- The game's own quick-key radial as an inventory wheel of eight slots, each holding up to five items: the slot counter
  shows the item's place and the slot's places; LT/RT choose the item within the pointed slot; RB or A use it; letting
  go of the wheel button uses nothing; a centre rest snap (on by default) holds no slot.
- Y in the inventory and the magic menu puts the highlighted item or spell on its wheel (a favourite: it cannot be
  sold or dropped); D-pad left/right choose which place it takes in the slot.
- A Magic wheel of its own, drawn as itself; a pick makes the spell the selected one and the HUD's quick-magic picture
  follows it.
- An ammo wheel for bows (D-pad right while a bow is held), rebindable from a row on the game's own Controls page.


### 2026-10-01 - the HUD's quick-magic picture is set on the widget itself
- **Fixed (untested): a Magic wheel pick still left the HUD's quick-magic picture as it was.** The owner: *"The widget
  did not update."* The view model's SpellIcon did change (logged CHANGED), but a TestBench trace of VHUDMainViewModel
  showed the five K2_BroadcastFieldValueChanged("SpellIcon") calls and no GetSpellIcon after any of them - the HUD's
  binding listens under another field id. WBP_ModernHud_MagicIcon_C has its own SetMagicTexture(InTexture); called on
  the live widget from TestBench with the Shield picture, the owner saw it change. `SetHudWidgetTexture` now calls it on
  every live instance (templates skipped) with the chosen spell's picture; the view model write and broadcast stay.

### 2026-10-01 - LB does nothing on the wheel
- **Fixed (untested): LB on the HUD radial took the pointed slot's shown entry off the wheel.** The owner: *"I pressed
  left bumper and it removed the selected magic from the wheel. But left bumper shouldn't do anything like that."* The
  log: `wheels: LB on the radial - LOC_FN_DefaultPlayerSpell REMOVED from Magic slot 4`. The LB branch in `Pad.cpp`
  (from aa7975d, the first several-items-per-slot model) and `wheels::RemoveEntry` are gone; LB is still held from the
  game while the radial is open, so it does nothing there. Y in the inventory and magic menus is how an entry comes off
  a wheel.

### 2026-10-01 (build only, primary session)
- **Fixed (untested): the HUD's spell picture now follows a Magic wheel choice.** Probe B showed the magic menu's pick
  ends with the game writing VHUDMainViewModel.SpellIcon and broadcasting the change (the HUD calls GetSpellIcon once);
  the wheel only set selectedSpell. The wheel now writes the spell's icon into SpellIcon and calls
  K2_BroadcastFieldValueChanged("SpellIcon"); the half-second read-back logs whether the picture CHANGED.
- **Fixed: the magic menu's wheel panel read as showing the moment the menu opened** (06ecaa8), so every A went to
  assigning Magic slot 1 and the owner's slots were rearranged. Probe A showed why: the panel (Magic_QuickKeys) shares a
  WidgetSwitcher with the description, and a switcher draws only its active child while every child reads visible; and
  the widget blueprint's archetype was counted as a live panel. The on-screen check now asks the switcher for its active
  child, and instance scans skip templates.
- **Fixed: the reflection self-check no longer latches failure early in a launch.** A KeyIndex not found yet (the
  class exists before its property chain is linked) now means "ask again", as Tween Menu OR c4625c8 does; a failure is
  latched only for a property found at a wrong offset (gate rule or-reflect-selfcheck-never-latches-not-found; CCM went
  completely dead this way on 2026-09-29).

### Round 5 (2026-10-01) - four findings from the owner's session of 02:29-02:49 (adadfdd), built, not yet seen in game
- **Fixed: the slot counter never changed while cycling** (the owner: "the numbers for which item I'm selecting isn't
  changing whether I'm on one out of five, two out of five"). It drew the reachable count against the cap ("3/5"), which
  LT / RT never change. It now shows the shown entry's place among the entries LT / RT can reach, out of that count -
  "2/3", "1/1" for one item, nothing for an empty slot, "-/N" for a slot whose key holds none of its entries - and it is
  redrawn on the read after any wheel change (the 100 ms throttle is skipped when wheels::Generation() moved; each LT / RT
  step saves and moves it).
- **Fixed: RB / A equipped 0.7 s late.** Every use logged "unchanged 0.7 s after the game's key press ... FALLBACK:
  equipped through Actor::EquipObject" (9 of 9, 02:32:49-02:36:59) and the fallback worked every time ("(x1, worn)") -
  `Quick<N>Input_Pressed / _Released` equips nothing. Equipment (weapon, shield, armour, clothing, torch) now goes on AT
  ONCE through Actor::EquipObject on the TES thread (lock argument false), with no key press and no wait for the close; the
  state is read back 300 ms later ("USE result ... equipped at once through Actor::EquipObject"). An item already worn is
  left alone and logged ("already worn ... nothing done"): what the game's own quick key does with a worn item could not be
  told from the code or the game, since the key press did nothing at all. Anything else on a key (potion, scroll) keeps the
  old path - the game's key press after the close, unproven for those kinds. The bow seen not drawing for about 40 s after
  02:36:59: the ammo wheel equipped arrows only at 02:37:07 (Iron x1) and 02:37:38 (Steel x32), and the pawn had the bow,
  its WeaponActor and a QuiverForm - read as no arrows equipped, not the equip path; not proven.
- **Fixed: the centre rest snap cleared nothing visible, and LT / RT / A after it still acted on the old slot.** Every snap
  logged "'Update Key Index'(-1) ... QuickKeyID 1 -> 1, HoveredKeyID 0 -> 0, CurrentScaledKeyID 2 -> 2", and the game put
  the view model's KeyIndex back within a frame (snap 02:35:41.541, "USE Equipment slot 2 (A)" at .555). Read live with
  TestBench: the selector is two scalars on the wheel picture's material instance (quickKeys_material > Image, MIC_UI_QuickKeys)
  - SelectorRotator (0.375 = slot 4) and SelectorArrowAlpha (1.0) - and the widget takes the stick itself
  (InpActEvt_IA_UI_Specific_QuickKeys_RightStick, then Focus Key By Quick Key / UpdateFocusedKey / UpdateKeySelectorAngle /
  PlayScaleUpAnimation). The snap now hides the arrow (SelectorArrowAlpha 0, held at 0 while the stick rests, put back when
  it points again or the wheel closes), writes the Blueprint's QuickKeyID / CurrentScaledKeyID / HoveredKeyID to -1 (their
  own value between opens), and our pointed slot stays none until the stick points again - whatever the game re-reports -
  so LT / RT / RB / A after a snap act on nothing. The snap ends when the game's slot agrees with the stick's angle, or
  after three reads from the angle. The snap delay is uRestSnapMs=150 (the shipped INI): a release then A within 150 ms
  still uses the slot, later uses nothing (02:35:36.097, "USE (A) with no slot pointed").
- **Fixed: a loaded save undid the player's slot stacking.** At 02:35:18 / 02:35:21 the longsword and the mace were
  assigned to slot 2 (the keys followed - the radial opened four times after with no reconcile line and LT / RT cycled all
  three); a save was then loaded (Tween Menu opened Save & Load at 02:40:19; the HUD came back in a new place at 02:42:23)
  and put the save's keys back (mace on 1, longsword on 3). At 02:46:16 - the radial opening, the first wheel read after -
  Reconcile took the game's keys as the truth: "slot 1 picked up ... Mace", "slot 3 picked up ... Longsword", "...left
  Equipment slot 2's entries". The move had cleared the old keys (RemoveAt -> ClearKey, the game moving its own key);
  the load brought them back. The wheel now wins for every item it holds: a key on an item the wheel has on another slot
  comes off it, a key on an item the player took off the wheel this session comes off it again, the slot's active entry
  goes back on its key; a key on an item on no slot is still picked up.

### Round 4 (2026-10-01) - four bugs from the owner's session of 01:35-01:56, built, not yet seen in game
- **Fixed: RB closed the wheel but did not equip** (the owner: "I tried selecting an item and pressing RB, which closed
  the wheel but didn't equip the weapon"). Log 01:52:39: "wheels: USE Equipment slot 2 now (RB) - the radial closes on
  it", then "radial closed; the game uses slot 2 (top-right)" - but key 2 held the Steel Claymore (re.quickkeys) and the
  bow stayed in hand. RB only logged and let go of the D-pad, trusting the radial's close to use the pointed key; "the game
  uses slot N" was only the view model's KeyIndex read at the close, never proof of a use (that log line now says so).
  Why the close did not use it is not proven: KeyIndex read 2 at the close and nothing of ours wrote it during that
  session. RB - and now A, which the game no longer sees on the inventory wheel - closes the radial on nothing (the choice
  cleared, the stick held centred until it has closed, as for the Magic wheel), and then runs the game's OWN direct
  quick-key use: `VEnhancedAltarPlayerController::Quick<N>Input_Pressed` / `_Released` (no parameters, found by reflection
  2026-10-01), what the number keys 1-8 run through Enhanced Input. The item is read back before and 0.7 s after
  (count, worn, a menu opening): "USE result ... used through the game's own quick key N". If the radial's own close had
  already used it, the key is not pressed again (never twice); if a weapon, shield, armour or torch is still not worn
  after the press, it is equipped through Actor::EquipObject on the TES thread and logged FALLBACK.
- **Fixed: LT / RT could not change the weapon** (log 01:50:56: "CYCLE Equipment slot 2 - 2 entries, nothing to step
  to", x5). The wheel file (written 2026-09-30) had slot 2 = {Steel Bow} and slot 3 = {Steel Claymore, Steel Longsword},
  while the loaded save's keys were claymore on 2, longsword on 3, bow on 4: slot 2 "picked up" the claymore and kept the
  bow, which was slot 4's key, so the only other entry could never be stepped to while the counter said 2. At load (and
  whenever the keys change under us) each keyed item now goes into its own slot and leaves every other slot's entries -
  an item is on one slot ("... left Equipment slot N's entries - it is on slot M's key in the game"). Every entry LT / RT
  passes over is named with its reason ("not carried" / "on slot N's key"), and the Equipment counter counts only the
  entries LT / RT can reach. Reconciling waits while the game's own assign is being watched (it pre-empted Settle).
- **Fixed: no centre rest snap visible on the inventory wheel.** The snap fired ("the right stick came back to rest -
  the wheel points at no slot") but only wrote the view model's KeyIndex = -1; the highlight is the widget's own (its
  Blueprint keeps QuickKeyID / HoveredKeyID / CurrentScaledKeyID and draws the selector through "Update Key Index" ->
  UpdateFocusedKey). The snap now also calls the widget's "Update Key Index"(-1) - what the game itself calls as the
  radial opens with nothing pointed - and logs the three Blueprint values before and after. If the game does not report
  the slot again when the stick returns to the same slot after a snap, the slot is taken from the stick's angle after
  three reads and pointed again ("pointed from the stick's angle"). The cancel message no longer adds one to KeyIndex,
  which is already the slot number as drawn ("pointed at slot 3" was slot 2).
- **Fixed: the Magic wheel looked empty until the magic menu had been opened** (the owner: "The magic wheel looks like
  it's empty and has nothing in it until you go into the magic menu and bring up the wheel inside the magic menu then it
  shows up when you go back in game"). Log 01:54:33: "rows: no icon for spell ... (no effect read)" for all three spells,
  "0 with its picture known"; after the magic menu, "3 with its picture known" (the rows' pictures). TestBench's crash
  record of 01:54:33 named it: an access violation in ImprovedWheelMenu.dll reading 0x800000008 - the spell-effect search
  read two u32 fields of the effect (0x0000000800000000) as a pointer, faulted, and because one __try wrapped the whole
  walk the fault ended the search before it reached the real EffectSetting pointer, for every spell. Each candidate is now
  checked on its own, only after VirtualQuery says it is readable (no first-chance fault for TestBench to record), and
  believed only when its formID looks up to the same pointer as a MagicEffect; +0x00 is searched too.

### Round 3 (in progress, 2026-09-30)
- Diagnostics: one "pad: radial session" line per HUD radial open - reads on our thread and on other threads (which the
  rules never touch), the largest LT / RT and stick values, the trigger presses seen, pointed-slot changes, rest snaps and
  reads stood down for AMF / Tween Menu - after the owner's report that LT / RT never cycled, the rest snap never fired
  and pointing alone selected (the 05:26 run logged no CYCLE and no rest snap at all).
- **Fixed: A in the magic menu did not equip a spell** (the owner, 2026-09-30: "I tried to select the alteration
  spell in there, but it wouldn't let me select it"). The wheel panel read as showing from the menu's opening, and every A
  was taken for an assign with no slot pointed. A is now the Magic wheel's only when a slot is pointed; otherwise the game
  gets it.
- **Fixed: the Magic wheel's spell pictures did not load** ("rows: no icon for spell ... (no effect read)"). OBSE's
  EffectItem layout (+0x20 for the EffectSetting*) read nothing in game; the effect's first 0x60 bytes are now searched,
  fault-guarded, for a pointer to a live MagicEffect form, and the offset found is kept and logged once.
- The owner saw the HUD's quick-magic widget dim and light up as active after a Magic wheel choice: selectedSpell is the
  game's selection - only the HUD's spell picture is not refreshed yet.
- **Changed: favourites stack with their kind** (the owner: "similar types of items stack in the same slot when they're
  favorited. So that great swords go to great swords, claymores to claymores, maces to maces"). Y (or a star) puts an
  item first in a slot already holding its kind, with room - a weapon by its type (blade or blunt, one- or two-handed,
  staff, bow), anything else by its form type - then an empty slot, then any slot with room.
- **Changed: armour and clothing are favourites, never on the wheel - shields go on it** (the owner: "I don't want armor
  that's favorited to appear on the wheel at all. I just want the shields", then "It should be fine to favorite armor, just
  not added to the wheel"). Y or a star on armour or clothing (body slot 13, the shield, excepted) keeps it on a list of
  its own ("Favourite 1" in the character's file): it cannot be dropped or sold, its star fills, and it never takes a
  slot. The game's own assign of armour to a slot is refused; armour already on a slot moves to that list.
- Known: setting PlayerCharacter::selectedSpell does not move the HUD's spell picture (05:16:54 / 05:17:05: "did NOT
  change"); the game's own equip path is being traced.
- **Changed: pointing only points - RB (or A) uses, LT / RT choose the item, letting go uses nothing** (the owner:
  "instead of using the games vanilla interaction where simply placing the cursor over the item selects it now that we
  have multiple items per slot will have to have the the right bumper activate the item and the right trigger and left
  trigger navigate which item they want from the slot and we'll need the center rest snap"). Closing the radial with the
  wheel button - let go after a hold, or pressed again after a tap - now uses nothing on either wheel (CloseOnNothing:
  the choice cleared and held cleared, the stick centred, until the radial has closed). RB and A use the pointed slot
  as before; LT / RT step the slot's items; the centre rest snap stays on. The owner's report that nothing could be taken
  from the wheel was the rest snap clearing every aim once the stick came back to the middle, and an RB on slot 2 after
  its only item had been taken off it.
- The settings page started for the rebind rule is dropped (the owner: "I'd rather just add a row in the control page
  for the ammo wheel. And that's it") - the ammo wheel's row on the game's Controls page is already there.
- **Added: each inventory-wheel slot shows its count, "3/5"** (the owner: "a counter on the slot showing how many items
  it holds and how many it allows in total ... so the player can see how many spaces are left"). SlotCounts.cpp lays
  eight labels of the game's text prefab over the game's wheel picture while the inventory wheel is up (the inventory's
  panel, or the HUD radial on the Equipment wheel), each just below its slot circle (0.71 of the half-width out, from the
  owner's 03:38 screenshot - to be checked in game); a label is set again only when its count changes.
- **Changed: a full slot takes a new favourite in place of the entry it shows** (the owner: "D-pad LEFT/RIGHT in the
  inventory steps through the slot's items to choose which one a new favourite replaces"). D-pad left / right already
  steps the pointed slot's shown entry; the game's assign on a full slot now replaces that entry (logged REPLACES) instead
  of being refused. LT / RT on the HUD radial step a slot's entries as before - with the wheel now drawn from the keys,
  the picture follows each step.
- **Changed: A chooses and closes on the Magic wheel, as on the game's own** (the owner: "I tried selecting clairvoyance
  with pressing A like the vanilla game does and it didn't close the wheel ... it only selected it when I pressed the right
  bumper it should do both"). A on the radial's Magic wheel sets the pointed slot's spell and lets the wheel close, like
  RB; the game never sees that A. On the inventory wheel A stays the game's own.
- **Fixed: the inventory wheel showed none of its equipment** (the owner: "the inventory wheel is not showing the
  equipment on the wheel"). The primary's read at 04:37: the game's picture list (the view model's Icons) was EMPTY while
  the keys held items, so the game set every slot's opacity to 0 on both wheels' material instances; the old redraw
  (SetQuickKeyByIndex, a click per slot) only swapped pictures in. The inventory wheel is now drawn from the keys on the
  wheel's own instance - each slot's IDn texture and opacity, silently, checked every 100 ms while the wheel is up;
  empty keys and spells stay empty. The widget redraw is off.
- **Fixed: closing the radial on a Magic slot also used the game's key there** (04:37:34 and 04:37:38: "the game uses
  slot 3" right after a Magic choice). The radial takes a moment to close and the stick, still pointing, pointed the slot
  again; the stick is now held centred and the choice kept cleared until the radial has closed.
- **Changed: a Magic slot chosen sets the SELECTED spell** (the owner: "I selected a magic from the magic wheel, but the
  characters UI widget for their quick magic didn't change"). SetCurrentSpell sets the caster's current spell only;
  PlayerCharacter::selectedSpell (+0x8F0) - what the HUD shows and the cast button casts - is now set too. No reflected
  equip function exists (the primary's search of 81 functions); half a second later the HUD's SpellIcon is read back and
  logged ("the HUD's spell picture CHANGED / did NOT change") to learn whether the HUD follows by itself.
- **Added: the favourites column in the magic menu** (the owner: "we need to add a star for favorites on the column in
  the magic menu"). Every magic row gets the same star at the end of its Magic_entry_horizontal: filled white while the
  spell is on the Magic wheel, and a click puts it on or takes it off the Magic wheel as Y does. The inventory column
  and this one share one module (Favourites.cpp); the owner confirmed the inventory's works and is interactive.
- **Fixed: items favourited back onto the inventory wheel stayed invisible** (the owner: after he "deselected the weapons
  from the wheel and tried favoriting them back, they didn't appear in the wheel menu"). The game keeps a slot's old
  picture after an item leaves its key, and a key with a picture and no item was read as a spell and its picture's
  opacity set to 0 on the game's wheel - never put back, so whatever went there next stayed invisible (04:18:54, the
  mace's old slot 7). A key now counts as a spell only when its picture is one of the game's magic icons (the texture's
  own path, /icons/magic/), and a blanked slot gets the game's opacity back as soon as it stops being one.
- The wheels stand down while Tween Menu's menu is open (TweenMenu_IsOpen, Tween Menu 1.0.1).
- **Fixed: the Magic wheel never drew, and the favourites column never appeared** (the owner's screenshots of 03:38:40
  and 03:38:46: the magic menu's wheel still showed a bow, a sword, a torch; the log: "the game's wheel picture is NOT
  FOUND (quickKeys_material)", "a row has no inv_entry_horizontal - no column"). GetWidgetFromName found neither widget
  in game. Both are now also found by walking the widget trees ourselves - each user widget's WidgetTree root, every
  panel's Slots[].Content, nested user widgets' trees - and comparing names in the plugin (ui::FindInTree).
- **Fixed: "Magic Wheel" stayed over the magic menu after its wheel panel was hidden** (the owner: "after you hide
  the wheel menu, the title of it still stays"). The panel is hidden through a parent, so the wheel's own IsVisible
  stayed true and the panel read as up again 0.1 s after B closed it. A wheel now counts as on screen only when it and
  every widget above it is visible and not faded out (ui::ShownOnScreen).
- **Added: the favourites column in the inventory** (the owner: "I still don't see the favorites column in the inventory
  interface ... Maybe we can build it just like we did the buttons for the SLS mod", and "each item have a button. In
  its column with an empty star shape then the star shape gets filled to be white whenever it's favorited").
  Favourites.cpp adds one more column at the end of every inventory row's inv_entry_horizontal: the game's own empty
  star (T_UI_star_default_D) over a white fill of ours (ImprovedWheelMenu\FavouriteStarFill.png - an original shape
  drawn for the mod, imported at run time with ImportFileAsTexture2D; Slate clamps a tint, so the game's blue star
  could not be tinted white), and the game's invisible button over both, as Simple Loadout System's boxes use it -
  a click toggles the favourite exactly as Y does (never a stop for the controller: IsFocusable off). Rows are reused
  as the list scrolls: each keeps its column, and only a row whose item or favourite state changed is redrawn, looked
  at every 100 ms while the inventory is open and only when a row or a wheel moved.
- **Changed: the Magic wheel is a wheel of its own** (the owner: "creating a second wheel, not just a renamed wheel, a
  second entirely different wheel, similar to how we created the ammo wheel. And this one needs to be specific to the
  magic inventory menu. Because when I go to inventory and magic, I still see all the same items. And only on the
  magic wheel do I hear a bunch of ticking noises as soon as it appears"). MagicWheel.cpp: its own image with its own
  instance of the game's wheel material (of the same parent as the game's), laid exactly over the game's wheel picture
  (quickKeys_material > Image, measured every 100 ms) while the Magic wheel is up - the magic menu's panel, or the HUD
  radial with Magic active - with the game's picture hidden under it (its opacity put back after). Its eight slots are
  the Magic wheel's spells, empty ones drawn empty; every scalar of the game's instance but the slots' own is copied
  onto ours, so the selector and its animation follow the game's exactly. Nothing is written into the game's view model
  any more - the old SetIcons redrew slots, and every redrawn slot ticked.
- **Changed: no magic on the inventory wheel** (the owner: "the inventory wheel menu still having magic on it"). A game
  key with a picture and no item - a spell the game kept from before - is drawn empty on the game's wheel (its slot's
  opacity on the game's own instance, set again whenever the game redraws), and closing the radial on it uses nothing.
- Spell pictures also come from the spell's first effect's own icon (TESIcon, the same path rule as items), so the
  Magic wheel has pictures before the magic menu has shown its rows.
- **Fixed: the ammo wheel showed the material's defaults** (the owner: no arrows, and "box number eight is highlighted
  permanently, and it doesn't respond to my right stick movement"). The primary's read with the wheel open found no
  instance of ours - only the HUD's two - so every parameter written went nowhere. Each open now asks the wheel's image
  what its brush draws; when it is not our instance, the instance is made again and set (a warning names what it
  found). With nothing pointed - an empty wheel - the selector goes to slot 3 in the hidden right half, instead of the
  material's default slot 8.
- **Fixed: no arrow pictures until the inventory had shown them.** An item's picture now also comes from its own form:
  its icon path (TESIcon, "Weapons\IronArrow.dds") names the remaster's texture - /Game/Art/UI/Icons/Dynamic_Icons/
  menus/icons/weapons/T_ironarrow, the folder in lower case, "T_" and the lower-case stem (the primary session's reads
  and the paks) - loaded when first asked. Arrows, weapons, books, misc items, apparatus, ingredients, potions and
  lights; a "rows: icon for ..." line names each one loaded, and a failure is logged once and not retried.
  - The 02:37 empty wheel was the owner's Y presses in the inventory removing both arrows (Y toggles a favourite).
- **Changed: the ammo wheel is the game's own wheel, sliced in half** (the owner: "the arrows icons and slot outline is
  too small for my liking and the highlighting effect is too dim ... basically just a carbon copy of the game's wheel
  menu just sliced in half").
  - The HUD wheel is one image drawn by a dynamic instance of MIC_UI_QuickKeys (the primary session's reads). The ammo
    wheel is a second instance of it, 634 across like the HUD's, clipped just past its middle, so the left five slots
    stay whole: 1 at the top, 8, 7 at the left, 6, 5 at the bottom.
  - The arrows go into those slots' ID textures, and the game's own selector (SelectorRotator, SelectorArrowAlpha)
    points at the chosen one.
  - Entries fill from the middle: one at 7, two at 8 and 6, up to five arrow types.
  - The drawn circles remain as the fallback if the material can't be instanced.
- **Added: the wheel's name over the wheel on screen**: "Inventory Wheel" or "Magic Wheel" over the HUD radial (the
  active wheel) and over the inventory's and the magic menu's assign panels (the owner: "The wheel menus aren't named in
  the inventory when they should be").
  - The last session's log shows the magic menu's wheel WAS drawn as the Magic wheel ("0 of 8 slots hold a spell ... the
    rest drawn empty"). An unnamed empty wheel read as "no Magic wheel" ("I still haven't seen the wheeler wheel for the
    magic menu").
  - The label is the game's own text prefab, placed from the wheel widget's cached geometry (Minimap Menu's compass
    placement). It is checked ten times a second, and its world-context call is fault-guarded.
- Still open, waiting on in-game reads: the favourites column (the list rows' widget layout), and keeping spells off
  the inventory wheel (where the game stores a spell's own quick key has not been found yet).
- **Changed: the ammo wheel divides the half circle evenly, every entry the same size** (the owner, testing 465ca21: "The
  iron and steel arrow overlap in the ammo wheel, and there are different sizes too, which is weird. It should be dividing
  up the circumference evenly not pushing them both into the center").
  - n entries each take one of n equal sectors from straight up to straight down, sitting at its middle: one in the
    middle, two at 135 and 225 degrees, eight 22.5 degrees apart. This replaces the centred 22.5-degree spacing.
  - The pointed entry is lit, not enlarged.
- **Changed: no centre rest snap on the ammo wheel** (the owner: "otherwise it's inconvenient to use it"). The pointed
  arrows stay pointed when the stick is let go. The main radial keeps its snap.
- **Changed: the ammo wheel lays out as the Skyrim Perfected Wheeler's does** (the owner: "if you only have one arrow
  selected in the ammo wheel or one arrow favorited to it, then it centers to the middle of the semicircle and for every
  additional favorited arrow type it adds an additional radial entry and each arrow should have the same circle art as
  the main wheel for each arrow slot").
  - One entry per favourited arrow type, centred on the middle of the half circle and 22.5 degrees apart, closer only
    when the entries would reach the screen's edge.
  - Every entry sits on the main wheel's own slot circle, which grows and warms when pointed. The primary session's
    ue.tree showed the HUD wheel is ONE image drawn by the material MIC_UI_QuickKeys. The circle it draws each slot
    with, T_QuickKeys_SingleCircle_D (a gold rim, a dark translucent inside), was found in the paks with uetex and is
    loaded by path (KismetSystemLibrary's LoadAsset_Blocking, Minimap Menu's loader). If it can't be loaded, a drawn
    disc stands in. The circles show 0.2 R across, so eight entries never overlap.
  - The right stick picks the nearest entry, and D-pad up / down step through the entries.
- **Fixed: the magic menu showed the inventory wheel's pictures** (the owner: "I still don't see the magic wheel in the
  magic inventory" - "the magic menu wheel is a distinct separate wheel from the inventory wheel. And just like the
  inventory wheel, even if it's empty, it's still visible and it still draws. It would just be empty").
  - Cause: the owner's Magic wheel is empty (0 entries), and the widget's own SetQuickKeyByIndex keeps the old picture
    when given none, so every empty Magic slot went on showing the inventory wheel's item.
  - Fix: the Magic wheel is now written into the wheel's view model (SetIcons), where the game's own drawing shows an
    empty key as empty. It is written again whenever the game pushes its own pictures, and only then, because each
    redraw clicks. The game's eight pictures are kept and written back when the Magic wheel leaves the screen.
  - The log says how many Magic slots hold a spell and how many of those have their picture known.
- **Fixed: iron arrows duplicated in the inventory** (the owner: "There seems to be a weird duplication bug with the
  iron arrows in the inventory"). Every close of the ammo wheel with A or its button equipped the pointed stack again,
  even when it was already worn: 40 whole-stack equips of the same iron arrows in one session. Arrows already worn are
  now left alone, and every real equip logs the stack's data lists afterwards (how many, how many worn, their counts), so
  a split stack shows in the log. A stack already split in this save may need a reload.
- **Fixed: the arrows sat partly off screen on the ammo wheel** (the owner: "the arrows appear slightly off screen in
  the arrow ammo wheel"). The eight arrows were spread over the whole half circle, so the top and bottom ones touched
  the screen's edge. They now sit on the arc from 112.5 to 247.5 degrees, at 62% of the radius, and the right stick
  picks the nearest arrow on that arc.
- **Changed: the wheels stand down while the Apocrypha Menu Framework's window is open** (the owner: treat it like the
  game's own wheel, apart from the bow condition). IWM reads the pad on the game's import before the framework's gate
  hides it from the game, so D-pad down and D-pad right still opened the wheels with the framework's window up. IWM now
  looks up the framework's AMF_IsMenuOpen export (AMF OR 1.0.5+). While the window is open, no wheel rule runs, an open
  ammo wheel closes and a latched wheel lets go. A framework without the export leaves the rules as they were.

### Round 2 (the owner's report, 2026-09-30)
- **Fixed: the ammo wheel never appeared.** The owner: "I don't see the ammo wheel while pressing D-pad right". The log
  showed every press arriving with a bow held, followed by "the wheel cannot be built yet". The player-controller
  lookup matched the engine's PlayerController class exactly. The game's controller is a subclass, so the lookup never
  found it and the widget was never created. It now accepts subclasses.
- **Added: an "Ammo Wheel" row on the game's Controller Controls page** (the owner: "make sure that you build the system
  row and that the game recognizes the key presses properly").
  - How it works: the plugin creates a runtime input action (IA_IWM_AmmoWheel) and maps it in IMC_Game_Default. The
    row is placed after the game's quick keys row, its default is D-pad right, and it is modelled on Tween Menu's
    Controls row.
  - Rebinding: the row can be rebound there to any controller button. The rebind is kept in [AmmoWheel] uButton,
    because the game's save cannot restore a runtime action. If the game re-applies its map without our action, the
    action is put back.
  - The press itself is still read from the controller.
- **Fixed: the Magic wheel now appears in the magic menu when you switch tabs to it from the inventory with the wheel
  panel showing.** The owner: "I switched tabs to the magic and the magic wheel doesn't appear."
  - Cause: the magic menu's panel reports no visibility change, so the plugin only learned it was showing from its own
    D-pad-down toggle, and the tab switch carries the panel across without one.
  - Fix: the panel's visibility is now read from the wheel widgets themselves (UWidget::IsVisible) five times a second
    while the magic menu is open.
- **Changed: a spell goes on one slot of the Magic wheel only**, as items already do on the Equipment wheel (the owner:
  "if I assign magic to a slot, I shouldn't be able to assign it the same magic to two other slots. Because it's
  redundant"). Assigning a spell to a slot moves it there from any other slot.

### Added
- the ammo wheel (the owner, 2026-09-29): a half wheel of up to eight arrow kinds locked to the middle of the right-hand
  edge, opened with D-pad right while a bow is held, in gameplay only ("It would default to D-pad right while holding a
  bow"). The right stick or D-pad up / down points (it opens on the arrows worn now, drawn gold); D-pad right again or A
  equips the pointed arrows, B closes without. The equip runs on the TES thread (TesThread.cpp, from Simple Loadout
  System - the equipment path traps every other thread) with no lock. `[AmmoWheel] bEnabled / uButton / uScalePercent`.
  Built from the owner's description; Wheeler Refined's ammo wheel source is GPL-3.0-only and was not read (rule 62).
  Its Controls-page row comes with the next round - the button is an INI setting until then.
- Y on arrows in the inventory puts them on the ammo wheel, never the Equipment wheel, and arrows already on the
  Equipment wheel (or put on a key by the game's own assign) move over, taking the game's key with them, and the
  radial's pictures are patched so nothing stale is left on it. Each wheel holds only its own kind: items, spells, arrows.
  Favourited arrows cannot be dropped or sold, as other favourites.
- the centre rest snap (the owner: "we'd have to add the center rest snap feature or you might select items
  mistakenly"): on the HUD radial and the ammo wheel, once the right stick has pointed, bringing it back to rest in the
  middle for `uRestSnapMs` (150 ms) points at no slot, so letting go then uses nothing. `[Wheel] bCentreRestSnap`.

### Changed
- taking an item off the wheel the normal way unfavourites it (the owner, 2026-09-29: "I didn't press Y to unfavorite
  the item. I just removed the item from the wheel the normal way. Which should be considered going forward."). A removal
  through the assign panel (assign again on the item, or on an entry the slot holds but is not showing) now takes it off
  every slot of that wheel, and so does Y off (it took the item off the first slot holding it only). The case: the
  Revealer of Iniquity, taken off slots 7 and 1, stayed an undroppable favourite as the first of Equipment slot 3's three
  entries, where nothing showed it.

### Fixed
- the crash of 2026-09-29 09:15:50 when an item was dropped with X (TestBench crash record: ImprovedWheelMenu.dll+0x1B668,
  `reflect::IsLive` called from `rows::HighlightedItem`, reading freed memory at the row's internalIndex). The wheel
  kept inventory and magic rows, their icons and the quick keys view model from earlier frames and checked them by
  reading the object's own index - garbage once the row is freed. Every object kept across frames is now held with the
  object-array slot it was found in (`reflect::Handle`) and checked by asking the slot, never by reading the object; a
  highlighted row that is gone reads as "no item".
- (1445bc7, earlier the same day) `reflect::Text` / `TextKey` refuse a zeroed FText - a list row never given an item.
