#include "favourites/Favourites.h"

#include "game/Inventory.h"
#include "game/Menus.h"
#include "engine/PEHook.h"
#include "game/QuickKeys.h"
#include "engine/Reflect.h"
#include "game/Rows.h"
#include "core/Settings.h"
#include "engine/Ui.h"
#include "wheels/Wheels.h"

#include <unordered_map>

namespace favourites
{
	namespace
	{
		constexpr const wchar_t* kStarOutline = L"/Game/Art/UI/Common/T_UI_star_default_D.T_UI_star_default_D";   // the game's empty star
		constexpr const wchar_t* kButtonClass = L"/Game/UI/Modern/Prefabs/Buttons/WBP_ModernPrefab_InvisibleButton.WBP_ModernPrefab_InvisibleButton_C";
		constexpr double         kStarW = 38.0, kStarH = 19.0;   // the 64 x 32 star at the row's text height

		struct Column
		{
			std::int32_t    rowSlot = -1;   // the row's object-array slot when the column was added
			reflect::Handle overlay, fill, outline, button;
			std::string     key;            // what the row's name read when its item was last looked up
			std::uint32_t   form = 0;
			int             shown = -1;     // the fill as drawn: 1 favourite, 0 not, -1 not drawn yet
			bool            magic = false;  // a magic menu row (its star is the Magic wheel's)
		};
		std::unordered_map<UE::UObject*, Column> g_columns;   // by row; resolved through its slot before use

		reflect::Handle g_fillTexture, g_outlineTexture;
		bool            g_fillFailed = false;
		std::uint32_t   g_drawnGeneration = 0;
		ULONGLONG       g_next = 0;
		int             g_built = 0, g_clicks = 0;
		bool            g_watching = false;
		std::string     g_status = "the inventory has not been open yet";
		menus::Menu     g_lastMenu = menus::Menu::kNone;
		bool            g_lastContainer = false;

		// ours, from the plugin folder: a texture the engine imports from the PNG (KismetRenderingLibrary)
		UE::UObject* FillTexture()
		{
			if (auto* t = reflect::Get(g_fillTexture)) return t;
			if (g_fillFailed) return nullptr;
			const auto path = settings::PluginFolder() / L"ImprovedWheelsPlus" / L"FavouriteStarFill.png";
			static auto* lib = ui::Class(L"/Script/Engine.KismetRenderingLibrary");
			ui::Call c(lib ? lib->GetDefaultObject(false) : nullptr, L"ImportFileAsTexture2D");
			void* file = c.At("Filename");
			UE::UObject* tex = nullptr;
			if (c && file && std::filesystem::exists(path)) {
				c.Set("WorldContextObject", ui::PlayerController());
				auto* s = new (file) UE::FString(path.wstring().c_str());
				tex = c.RunGuarded() ? c.Get<UE::UObject*>("ReturnValue") : nullptr;
				s->~FString();
			}
			if (!tex) {
				g_fillFailed = true;
				logger::warn("favourites: the star's fill could not be loaded ({}{}) - a favourite shows the game's filled star instead",
					path.string(), std::filesystem::exists(path) ? "" : ", missing");
				return nullptr;
			}
			g_fillTexture = reflect::Hold(tex);
			logger::info("favourites: the star's fill is loaded from {}", path.filename().string());
			return tex;
		}

		UE::UObject* OutlineTexture()
		{
			if (auto* t = reflect::Get(g_outlineTexture)) return t;
			auto* t = ui::Load(kStarOutline);
			if (t) g_outlineTexture = reflect::Hold(t);
			return t;
		}

		UE::UObject* Prop(UE::UObject* a_o, std::string_view a_name)
		{
			const auto off = a_o && reflect::Ok() ? reflect::Offset(reinterpret_cast<UE::UStruct*>(a_o->GetClass()), a_name) : -1;
			return off >= 0 ? *reinterpret_cast<UE::UObject* const*>(reinterpret_cast<const std::uint8_t*>(a_o) + off) : nullptr;
		}

		UE::UObject* Add(UE::UObject* a_panel, const wchar_t* a_fn, UE::UObject* a_child)
		{
			ui::Call c(a_panel, a_fn);
			if (!c) return nullptr;
			c.Set("Content", a_child);
			return c.Run() ? c.Get<UE::UObject*>("ReturnValue") : nullptr;
		}

		void Align(UE::UObject* a_slot, std::uint8_t a_h, std::uint8_t a_v)
		{
			ui::CallFirst(a_slot, L"SetHorizontalAlignment", &a_h, sizeof(a_h));
			ui::CallFirst(a_slot, L"SetVerticalAlignment", &a_v, sizeof(a_v));
		}

		UE::UObject* StarImage(UE::UObject* a_tree, UE::UObject* a_overlay, const std::wstring& a_name, UE::UObject* a_texture)
		{
			static auto* imageClass = ui::Class(L"/Script/UMG.Image");
			auto* img = imageClass ? UE::NewObject<UE::UObject>(a_tree, imageClass, UE::FName(a_name.c_str())) : nullptr;
			auto* slot = img ? Add(a_overlay, L"AddChildToOverlay", img) : nullptr;
			if (!slot) return nullptr;
			Align(slot, 2, 2);   // centred
			if (a_texture) {
				ui::Call b(img, L"SetBrushFromTexture");
				b.Set("Texture", a_texture);
				b.Set("bMatchSize", false);
				b.Run();
			}
			ui::Vec2(img, L"SetDesiredSizeOverride", kStarW, kStarH);
			ui::Visible(img, true);
			return img;
		}

		void OnButton(UE::UObject* a_obj, UE::UFunction* a_fn, void*);

		// the column, added once to a row
		bool Build(UE::UObject* a_row, Column& a_col, const char* a_horizontal, bool a_magic)
		{
			static auto* overlayClass = ui::Class(L"/Script/UMG.Overlay");
			auto* horizontal = Prop(a_row, a_horizontal);
			if (!horizontal) horizontal = ui::FindInTree(a_row, a_horizontal);
			auto* tree = Prop(a_row, "WidgetTree");
			if (!overlayClass || !horizontal || !tree) {
				static bool logged = false;
				if (!logged) logger::warn("favourites: a row has no {} - no column", !horizontal ? a_horizontal : "widget tree");
				if (!logged && !horizontal) logger::warn("favourites: the row's tree root is {}", tree ? pe::Utf8(tree->GetFName().ToString()) : std::string("none"));
				logged = true;
				return false;
			}
			const std::wstring n = std::to_wstring(++g_built);
			auto* overlay = UE::NewObject<UE::UObject>(tree, overlayClass, UE::FName((L"IwmFavourite" + n).c_str()));
			// An inventory row's star sits in the margin before the item's icon (the old column at the row's right edge stood under the sort
			// headers and read as a sorted column). The row's widgets lie on a canvas (inv_entry_panel: the focus box, the list background and the
			// row's horizontal box), so the star is a canvas child of its own - anchored to the row's left edge, centred on its height - and the
			// row's columns stay where the headers are. bFavouriteStarAtLeft=0 keeps the old column; iFavouriteStarX is measured from where the row's content starts.
			const int    left = settings::Get().starX;
			bool         onCanvas = false;
			UE::UObject* slot = nullptr;
			if (settings::Get().starAtLeft && overlay) {
				const char* canvasName = a_magic ? "Magic_entry_panel" : "inv_entry_panel";
				auto*       canvas = Prop(a_row, canvasName);
				if (!canvas) canvas = ui::FindInTree(a_row, canvasName);
				slot = canvas ? Add(canvas, L"AddChildToCanvas", overlay) : nullptr;
				if (slot) {
					const double anchors[4] = { 0.0, 0.5, 0.0, 0.5 };   // minimum (x, y), maximum (x, y): the left edge, mid-height
					ui::CallFirst(slot, L"SetAnchors", anchors, sizeof(anchors));
					ui::Vec2(slot, L"SetAlignment", 0.0, 0.5);
					ui::Vec2(slot, L"SetPosition", static_cast<double>(left), 0.0);
					ui::Vec2(slot, L"SetSize", kStarW, kStarH);
					const std::uint8_t noAuto = 0;
					ui::CallFirst(slot, L"SetAutoSize", &noAuto, sizeof(noAuto));
					onCanvas = true;
					// The row's own bounds start at its icon, and anything outside them is clipped: the row's content (icon, name, columns) is moved
					// right by iFavouriteStarSpace - a left padding on its first child, once (read first: a row that is built again keeps it) - and the
					// star sits in the room that makes.
					const float space = static_cast<float>(settings::Get().starSpace);
					const char* firstName = a_magic ? "Spacer_97" : "inv_entry_icon_overlay";   // the first child of the row's box
					auto*       first = Prop(a_row, firstName);
					if (!first) first = ui::FindInTree(a_row, firstName);
					auto* firstSlot = first ? Prop(first, "Slot") : nullptr;
					const auto padOff = firstSlot ? reflect::Offset(reinterpret_cast<UE::UStruct*>(firstSlot->GetClass()), "Padding") : -1;
					if (space > 0.0f && padOff >= 0) {
						float* m = reinterpret_cast<float*>(reinterpret_cast<std::uint8_t*>(firstSlot) + padOff);   // left, top, right, bottom
						if (m[0] < space - 0.5f) {
							const float pad[4] = { space, m[1], m[2], m[3] };
							ui::CallFirst(firstSlot, L"SetPadding", pad, sizeof(pad));
						}
					}
				}
				static bool logged = false;
				if (!logged) {
					logged = true;
					logger::info("favourites: the inventory star is placed {} px from the row's left edge ({})", left, onCanvas ? "on the row's canvas" : "the row has no canvas - it stays at the right");
				}
			}
			if (!slot) slot = overlay ? Add(horizontal, L"AddChildToHorizontalBox", overlay) : nullptr;
			if (!slot) return false;
			if (!onCanvas) {
				Align(slot, 2, 2);
				const float pad[4] = { 10.0f, 0.0f, 6.0f, 0.0f };   // left, top, right, bottom
				ui::CallFirst(slot, L"SetPadding", pad, sizeof(pad));
			}

			auto* fillTex = FillTexture();
			auto* fill = StarImage(tree, overlay, L"IwmFavouriteFill" + n, fillTex ? fillTex : ui::Load(L"/Game/Art/UI/Common/T_UI_star_D.T_UI_star_D"));
			auto* outline = StarImage(tree, overlay, L"IwmFavouriteStar" + n, OutlineTexture());
			if (fill) ui::Visible(fill, false);

			// the click surface: the game's own invisible button, as Simple Loadout System's boxes use it; never a stop for
			// the controller's navigation (IsFocusable off before its Slate widget is built)
			auto* button = ui::CreateWidget(kButtonClass);
			if (button) {
				static const auto focusable = reflect::Offset(reinterpret_cast<UE::UStruct*>(button->GetClass()), "IsFocusable");
				if (focusable >= 0) *(reinterpret_cast<std::uint8_t*>(button) + focusable) = 0;
				if (auto* bs = Add(overlay, L"AddChildToOverlay", button)) {
					Align(bs, 0, 0);   // fills the star
				}
				if (!g_watching) {
					g_watching = pe::Watch(button->GetClass(), &OnButton);
					logger::info("favourites: {} the star buttons' clicks", g_watching ? "watching" : "could NOT watch");
				}
			}
			a_col.overlay = reflect::Hold(overlay);
			a_col.fill = fill ? reflect::Hold(fill) : reflect::Handle{};
			a_col.outline = outline ? reflect::Hold(outline) : reflect::Handle{};
			a_col.button = button ? reflect::Hold(button) : reflect::Handle{};
			a_col.rowSlot = a_row->internalIndex;
			a_col.shown = -1;
			a_col.key.clear();
			static bool firstLogged = false;
			if (!firstLogged) {
				firstLogged = true;
				logger::info("favourites: the first row's column is built (fill {}, outline {}, button {})", fill ? "yes" : "NO", outline ? "yes" : "NO",
					button ? "yes" : "NO");
			}
			return true;
		}

		// A row mid-construction or mid-destruction can hold garbage widget pointers: an access violation while its column is
		// read or built skips that row (built again on a later tick) instead of ending the game.
		bool BuildGuarded(UE::UObject* a_row, Column& a_col, const char* a_horizontal, bool a_magic, bool* a_faulted)
		{
			__try {
				return Build(a_row, a_col, a_horizontal, a_magic);
			} __except (GetExceptionCode() == EXCEPTION_ACCESS_VIOLATION ? EXCEPTION_EXECUTE_HANDLER : EXCEPTION_CONTINUE_SEARCH) {
				*a_faulted = true;
				return false;
			}
		}

		// a star's button clicked: its row's item on or off the wheel, as Y
		void OnButton(UE::UObject* a_obj, UE::UFunction* a_fn, void*)
		{
			if (!a_obj || !a_fn || pe::FunctionName(a_fn) != "BP_OnClicked") return;
			for (auto& [row, col] : g_columns) {
				if (reflect::Get(col.button) != a_obj) continue;
				auto* live = reflect::Get(row, col.rowSlot);
				const auto form = !live ? 0 : col.magic ? rows::MagicRowSpell(live) : rows::InventoryRowForm(live);
				if (!form) {
					logger::info("favourites: a star was clicked on a row with no {}", col.magic ? "spell of the player's" : "item");
					return;
				}
				++g_clicks;
				if (col.magic) {
					logger::info("favourites: star clicked on the spell {}", rows::SpellName(form));
					wheels::ToggleSpell(form);
				} else {
					logger::info("favourites: star clicked on {}", inventory::NameOf(form));
					wheels::ToggleItem(form);
				}
				g_next = 0;   // redrawn at once
				return;
			}
		}

			// A row is handed its item (the BP event OnListItemObjectSet): its star is set right after, in that frame and before anything is
			// drawn, so no star shows the item the row held before. The timer in Tick still builds the columns of new rows and catches
			// anything this misses.
			void UpdateExisting(UE::UObject* a_row, bool a_magic)
			{
				const auto it = g_columns.find(a_row);
				if (it == g_columns.end()) return;
				Column& col = it->second;
				if (col.rowSlot != a_row->internalIndex || !reflect::Get(col.overlay)) return;
				const std::string key = a_magic ? rows::MagicRowKey(a_row) : rows::InventoryRowKey(a_row);
				if (key != col.key) {
					col.key = key;
					col.form = a_magic ? rows::MagicRowSpell(a_row) : rows::InventoryRowForm(a_row);
					col.shown = -1;
				}
				const auto favs = a_magic ? wheels::MagicFavourites() : wheels::Favourites();
				const bool can = a_magic || !col.form || wheels::CanFavourite(col.form);
				const int  fav = !can ? 2 : col.form && favs.contains(col.form) ? 1 : 0;
				if (fav != col.shown) {
					ui::Visible(reflect::Get(col.overlay), fav != 2);
					ui::Visible(reflect::Get(col.fill), fav == 1);
					col.shown = fav;
				}
			}

			void OnRowPost(UE::UObject* a_obj, UE::UFunction* a_fn, void*)
			{
				static std::unordered_map<UE::UFunction*, bool> s_isItemSet;   // each distinct function is looked at by name once
					auto                                            it = s_isItemSet.find(a_fn);
				if (it == s_isItemSet.end()) {
					const auto name = pe::FunctionName(a_fn);
					it = s_isItemSet.emplace(a_fn, name == "OnListItemObjectSet").first;
				}
				if (!it->second || !a_obj) return;
				UpdateExisting(a_obj, a_obj->GetClass() == rows::MagicRowClass());
			}

			void RegisterRowHooks()
			{
				static bool s_done[2] = { false, false };
				if (!s_done[0]) {
					if (auto* c = rows::InventoryRowClass(); c && pe::WatchPost(c, &OnRowPost)) {
						s_done[0] = true;
						logger::info("favourites: the inventory's stars are set right after a row gets its item");
					}
				}
				if (!s_done[1]) {
					if (auto* c = rows::MagicRowClass(); c && pe::WatchPost(c, &OnRowPost)) {
						s_done[1] = true;
						logger::info("favourites: the magic menu's stars are set right after a row gets its item");
					}
				}
			}
	}

	void Tick()
	{
		RegisterRowHooks();
		// the inventory's rows (items: the Equipment and Ammo wheels) and the magic menu's (spells: the Magic wheel) - the
		// owner, 2026-09-30: "we need to add a star for favorites on the column in the magic menu"
		const auto menu = menus::Active();
		const bool magic = menu == menus::Menu::kMagic;
		// a container or barter menu (the game is in a menu, it is none of ours, and it is not the HUD radial): its list rows are the inventory's row widget,
		// the player's own side gets stars too
		const bool container = menu == menus::Menu::kNone && menus::AnyOpen() && !quickkeys::RadialOpen();
		if (menu != menus::Menu::kInventory && !magic && !container) {
			g_lastMenu = menu;
			g_lastContainer = false;
			return;
		}
		const ULONGLONG now = GetTickCount64();
		if (now < g_next) return;
		g_next = now + 100;
		const bool menuChanged = menu != g_lastMenu || container != g_lastContainer;
		g_lastMenu = menu;
		g_lastContainer = container;
		const bool rowsChanged = magic ? rows::TakeMagicRowsChanged() : rows::TakeInventoryRowsChanged();
		const auto generation = wheels::Generation();
		if (!menuChanged && !rowsChanged && generation == g_drawnGeneration) return;   // nothing moved: nothing to do
		g_drawnGeneration = generation;

		const auto live = magic ? rows::LiveMagicRows() : rows::LiveInventoryRows();
		const auto favs = magic ? wheels::MagicFavourites() : wheels::Favourites();
		int drawn = 0, built = 0;
		for (auto* row : live) {
			if (!magic) {
				// the other side of a container or barter list (the container's or the merchant's items) gets no star; a row of the Inventory menu left over
				// while a container is open, or the player's side of the container, is as usual
				const int side = rows::InventoryRowSide(row);
				if (side == 2) {
					if (const auto it = g_columns.find(row); it != g_columns.end() && it->second.rowSlot == row->internalIndex && it->second.shown != 2) {
						ui::Visible(reflect::Get(it->second.overlay), false);
						it->second.shown = 2;
					}
					continue;
				}
			}
			Column& col = g_columns[row];
			if (col.rowSlot != row->internalIndex || !reflect::Get(col.overlay)) {
				col = {};
				col.magic = magic;
				bool faulted = false;
				if (!BuildGuarded(row, col, magic ? "Magic_entry_horizontal" : "inv_entry_horizontal", magic, &faulted)) {
					if (faulted) {
						col = {};
						static bool warned = false;
						if (!warned) {
							warned = true;
							logger::warn("favourites: a row's widgets could not be read (access violation) - row skipped, not crashed");
						}
					}
					continue;
				}
				++built;
			}
			const std::string key = magic ? rows::MagicRowKey(row) : rows::InventoryRowKey(row);
			if (key != col.key) {   // the row shows another item or spell now
				col.key = key;
				col.form = magic ? rows::MagicRowSpell(row) : rows::InventoryRowForm(row);
				col.shown = -1;
			}
			// armour and clothing never go on the wheel (shields do): no star on their rows
			const bool can = magic || !col.form || wheels::CanFavourite(col.form);
			const int fav = !can ? 2 : col.form && favs.contains(col.form) ? 1 : 0;
			if (fav != col.shown) {
				ui::Visible(reflect::Get(col.overlay), fav != 2);
				ui::Visible(reflect::Get(col.fill), fav == 1);
				col.shown = fav;
				++drawn;
			}
		}
		// rows gone for good leave the map
		for (auto it = g_columns.begin(); it != g_columns.end();) {
			it = reflect::Get(it->first, it->second.rowSlot) ? std::next(it) : g_columns.erase(it);
		}
		if (built || drawn) {
			g_status = std::format("{} rows with a column ({} built now in the {}), {} stars redrawn, {} clicks so far", g_columns.size(), built,
				magic ? "magic menu" : container ? "container or barter menu" : "inventory", drawn, g_clicks);
			logger::debug("favourites: {}", g_status);
		}
	}

	std::string Status() { return g_status; }
}
