#pragma once

// Small UMG text helpers shared by the Menu Search and Search Keyboard plugins: colour, text, and the game's own UI font.
// Game thread only.

namespace uix
{
	// a TextBlock's colour (linear RGBA): SetColor (the game's text prefab) and SetColorAndOpacity (a plain TextBlock)
	void Ink(UE::UObject* a_label, const float (&a_rgba)[4]);
	// its shadow colour (linear RGBA) and offset in pixels
	void Shadow(UE::UObject* a_label, const float (&a_rgba)[4], double a_offset);
	// the texts inside the wheels (the ammo label, the slot counters): a soft grey, not white (white read too bright), no shadow
	void InsideWheelInk(UE::UObject* a_label);

	// SetText on a TextBlock: an ASCII string laid into the call's FText parameter
	bool SetText(UE::UObject* a_widget, const std::string& a_text);
	// the same for an item's or spell's name: a full-name key ("LOC_FN_...") through the game's ST_FullNames table, as the
	// game's own UI shows it; anything else (a custom name) as it is
	bool SetItemName(UE::UObject* a_widget, const std::string& a_text);

	// The game's own UI font (Font_Robinson_Localized, the one the lists use) at a size in font units, put on a text block's Font property in
	// place and then through SetFont so the widget redraws. False when the layout is not as expected (the text keeps the default font).
	bool ApplyFont(UE::UObject* a_widget, double a_size);   // a_size 0 = keep the size the text block has now
	// the same, bold: an outline (a_rgba, linear, a_outlineSize px) round every stroke: the text's own colour thickens it, black helps it read on a bright scene - the font has no bold face to ask for
	bool ApplyBoldFont(UE::UObject* a_widget, double a_size, const float* a_rgba, int a_outlineSize = 1);
}
