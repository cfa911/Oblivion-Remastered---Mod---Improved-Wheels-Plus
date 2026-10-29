#pragma once

// The name the menus show for a spell the player made: the game keeps only a placeholder ("UI_UserInputText_...") in
// the form, the menu row carries the text. Learned from the rows and kept per character, across sessions, in
// ImprovedWheelsPlus\Assignments\<character>.spells (next to the character's .iwp): a spell made in the game has an id
// 0xFF......, which is another spell in another character's save, so one shared file would mix the characters up.
// Game thread.

namespace spellnames
{
	std::string Find(std::uint32_t a_formID);                           // the name learned for that spell (empty = none)
	void        Remember(std::uint32_t a_formID, const std::string& a_name);   // a row showed it: kept, and the file written
	void        Reset();                                                // a character loaded or left
}
