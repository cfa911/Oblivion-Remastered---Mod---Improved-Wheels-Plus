// Unit tests for src/TextUtil.h. Build and run:  xmake f -m releasedbg && xmake build Tests && xmake run Tests
// (no game needed). Exit code 0 = all passed.

#include "core/TextUtil.h"
#include "wheels/WheelStorage.h"
#include <sstream>

#include <cstdio>

namespace
{
	int g_failed = 0;
	int g_run = 0;

	void Check(bool a_ok, const char* a_what)
	{
		++g_run;
		if (!a_ok) {
			++g_failed;
			std::printf("FAIL: %s\n", a_what);
		}
	}
}

int ReviewFixTests();

int main()
{
	using textutil::IconAssetPath;
	using textutil::Wide;

	// Wide: UTF-8 in, UTF-16 out (the bytes are written as escapes so the source file's encoding cannot matter)
	Check(Wide("") == L"", "Wide: empty");
	Check(textutil::Lower("") == "", "Lower: empty");
	Check(textutil::Lower("/Game/Art/UI/Icons/Magic/T_Fire") == "/game/art/ui/icons/magic/t_fire", "Lower: ASCII letters, the rest kept");
	Check(textutil::Lower("GrÃ¶Ã") == "grÃ¶Ã", "Lower: UTF-8 bytes above 0x7F unchanged");
	Check(Wide("Greater Heal") == L"Greater Heal", "Wide: ASCII unchanged");
	Check(Wide("Gr\xC3\xB6\xC3\x9F" "ere") == L"Gr\u00F6\u00DF" L"ere", "Wide: o-umlaut and sz (Groessere)");
	Check(Wide("\xC3\x84\xC3\x96\xC3\x9C\xC3\xA4\xC3\xB6\xC3\xBC\xC3\x9F") == L"\u00C4\u00D6\u00DC\u00E4\u00F6\u00FC\u00DF", "Wide: all German letters");
	Check(Wide("Gr\xC3\xB6\xC3\x9F" "ere").size() == 7, "Wide: one wchar per letter, not one per byte");
	Check(Wide("\xE2\x80\x99s") == L"\u2019s", "Wide: three-byte character");
	Check(Wide("\xF0\x9F\x98\x80") == L"\xD83D\xDE00", "Wide: four-byte character becomes a surrogate pair");
	{
		const auto bad = Wide("a\xC3z");   // a lone lead byte: must not crash, and keeps the neighbours
		Check(bad.size() == 3 && bad.front() == L'a' && bad.back() == L'z', "Wide: malformed byte is replaced, neighbours kept");
	}
	{
		const std::string withNul("a\0b", 3);   // the length decides, not a terminator
		Check(Wide(withNul).size() == 3, "Wide: embedded NUL kept");
	}

	// IconAssetPath: the paks' naming rule (folder lower case, T_ prefix, lower-case stem)
	Check(IconAssetPath("Weapons\\IronArrow.dds") ==
			  "/Game/Art/UI/Icons/Dynamic_Icons/menus/icons/weapons/T_ironarrow.T_ironarrow",
		"IconAssetPath: ammo from the owner's reads");
	Check(IconAssetPath("Armor\\Iron\\Cuirass.dds") ==
			  "/Game/Art/UI/Icons/Dynamic_Icons/menus/icons/armor/iron/T_cuirass.T_cuirass",
		"IconAssetPath: nested armor folder");
	Check(IconAssetPath("Clothes\\Shirt01.dds") ==
			  "/Game/Art/UI/Icons/Dynamic_Icons/menus/icons/clothes/T_shirt01.T_shirt01",
		"IconAssetPath: clothing");
	Check(IconAssetPath("Potion") == "/Game/Art/UI/Icons/Dynamic_Icons/menus/icons/T_potion.T_potion", "IconAssetPath: no folder, no extension");
	Check(IconAssetPath("a/b.c/Stem.dds") == "/Game/Art/UI/Icons/Dynamic_Icons/menus/icons/a/b.c/T_stem.T_stem", "IconAssetPath: only the last dot is the extension");
	Check(IconAssetPath("").empty(), "IconAssetPath: empty path");
	Check(IconAssetPath("Weapons\\").empty(), "IconAssetPath: folder only");

	// Assignment ownership: accept only this format and this exact character.
	{
		std::ostringstream saved;
		wheelstorage::WriteHeader(saved, "Hero #1234");
		std::istringstream own(saved.str());
		Check(wheelstorage::ReadHeader(own, "Hero #1234"), "wheel storage: own header round trip");
		std::istringstream otherCharacter(saved.str());
		Check(!wheelstorage::ReadHeader(otherCharacter, "Hero #5678"), "wheel storage: other character rejected");
		for (const char* foreign : { "", "# Improved Wheel Menu - the wheels of Hero #1234\n", "Equipment 1 active=1 0x00000123\n", "# ImprovedWheelsPlus assignments v2\n# Character: Hero #1234\n", "# ImprovedWheelsPlus assignments v1\n" }) {
			std::istringstream input(foreign);
			Check(!wheelstorage::ReadHeader(input, "Hero #1234"), "wheel storage: foreign, unsupported or incomplete header rejected");
		}
	}

	// Spell names are per character, in the same folder, with their own signature.
	{
		Check(wheelstorage::SafeName("Hero #1234") == "Hero _1234", "wheel storage: file stem keeps letters, digits, space, - and _");
		Check(wheelstorage::SafeName("a/b\\c:d") == "a_b_c_d", "wheel storage: path characters replaced");
		std::ostringstream names;
		wheelstorage::WriteHeader(names, "Hero #1234", wheelstorage::kSpellNamesSignature);
		std::istringstream own(names.str());
		Check(wheelstorage::ReadHeader(own, "Hero #1234", wheelstorage::kSpellNamesSignature), "spell names: own header round trip");
		std::istringstream otherCharacter(names.str());
		Check(!wheelstorage::ReadHeader(otherCharacter, "Hero #5678", wheelstorage::kSpellNamesSignature), "spell names: other character rejected");
		std::istringstream asAssignments(names.str());
		Check(!wheelstorage::ReadHeader(asAssignments, "Hero #1234"), "spell names: a names file is not an assignments file");
		std::ostringstream assignments;
		wheelstorage::WriteHeader(assignments, "Hero #1234");
		std::istringstream asNames(assignments.str());
		Check(!wheelstorage::ReadHeader(asNames, "Hero #1234", wheelstorage::kSpellNamesSignature), "spell names: an assignments file is not a names file");
		std::istringstream legacy("FF003586\t[Illu] Minor Invisibility\n");
		Check(!wheelstorage::ReadHeader(legacy, "Hero #1234", wheelstorage::kSpellNamesSignature), "spell names: the old shared SpellNames.txt is not read");
	}

	std::printf("%d checks, %d failed\n", g_run, g_failed);
	return (ReviewFixTests() == 0 && g_failed == 0) ? 0 : 1;
}
