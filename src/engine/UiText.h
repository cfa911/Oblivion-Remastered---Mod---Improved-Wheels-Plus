#pragma once

// Small UMG text helpers shared by the Menu Search and Search Keyboard plugins: colour, text, and the game's own UI font.
// Game thread only.

namespace uix
{
	// a TextBlock's colour (linear RGBA), with no shadow
	void Ink(UE::UObject* a_label, const float* a_rgba);

	// SetText on a TextBlock: an ASCII string laid into the call's FText parameter
	bool SetText(UE::UObject* a_widget, const std::string& a_text);

	// GetText on an editable or text widget (UTF-8)
	std::string GetText(UE::UObject* a_widget);

	// The game's own UI font (Font_Robinson_Localized, the one the lists use) at a size in font units, put on a text block's Font property in
	// place and then through SetFont so the widget redraws. False when the layout is not as expected (the text keeps the default font).
	bool ApplyFont(UE::UObject* a_widget, double a_size);   // a_size 0 = keep the size the text block has now
	// the same, bold: an outline (a_rgba, linear, a_outlineSize px) round every stroke: the text's own colour thickens it, black helps it read on a bright scene - the font has no bold face to ask for
	bool ApplyBoldFont(UE::UObject* a_widget, double a_size, const float* a_rgba, int a_outlineSize = 1);

	// The same font on a label made of the game's text prefab (WBP_AltarTextBlock: the text block inside it is called text_block), the size it has kept.
	bool ApplyPrefabFont(UE::UObject* a_label);
}
