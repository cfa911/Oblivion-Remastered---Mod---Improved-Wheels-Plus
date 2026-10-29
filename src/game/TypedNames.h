#pragma once

// The text a player typed for an item or spell (a name given at the spellmaker or the enchanter).
// The game keeps it in UVAltarUserInputTextSubsystem > UserInputTextSaveData > UserInputTextsMap (a TMap of FString key
// -> FText, keyed by the placeholder "UI_UserInputText_..." that the form's full name holds). The map is read raw: its
// element array first (TSparseArray data: pointer, count, max), each element a key string and a text, the element
// size found by trying the likely strides and keeping the one whose elements read as keys. Every read is fault-guarded.
// Game thread.

namespace typednames
{
	// the text typed for a placeholder name ("UI_UserInputText_...", the form's full name); empty = unknown. Read again every 2 s
	std::string Text(const std::string& a_placeholder);
	void        Reset();   // a character loaded or left
}
