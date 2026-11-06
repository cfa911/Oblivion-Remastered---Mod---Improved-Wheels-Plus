"""Wheel and spell-name files edited by hand must still read (code review 2026-11-06, finding 11).

An editor can save with a UTF-8 BOM (EF BB BF before the first line) or with CRLF line endings. The exact header
comparison then refused the file (storage rejected, wheels empty for that character, retried every 5 s), a CRLF blank
line failed the whole parse, and .spells names kept a trailing CR. The real WheelStorage.h and the .spells read loop
from SpellNames.cpp are compiled and fed such files.
"""
from pathlib import Path
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]
storage = (root / 'src/wheels/WheelStorage.h').read_text(encoding='utf-8').replace('\r\n', '\n').replace('#pragma once', '')
names = (root / 'src/game/SpellNames.cpp').read_text(encoding='utf-8').replace('\r\n', '\n')
start = names.index('\t\t\tstd::string line;', names.index('kSpellNamesSignature)) {'))
loop = names[start:names.index('\t\t\treturn true;', start)]

program = r'''
#include <cstdio>
#include <map>
#include <sstream>
''' + storage + r'''
std::map<std::uint32_t, std::string> g_displayNames;
void ReadNames(std::istream& f)
{
''' + loop + r'''
}
int failures = 0;
void Check(bool a_ok, const char* a_what) { std::printf("%s: %s\n", a_ok ? "PASS" : "FAIL", a_what); if (!a_ok) ++failures; }
const std::string kId = "4A1A9262-487462B9-F49E7884-5B878754";
std::string File(const char* a_bom, const char* a_nl, bool a_blank)
{
	std::string nl = a_nl;
	return std::string(a_bom) + "# ImprovedWheelsPlus assignments v1" + nl + "# Character: " + kId + nl +
		"# <wheel> <slot 1-8> active=<entry, 0 = none> <formID>..." + nl + "Equipment 1 active=1 0x00027319 0x000BE335" + nl +
		(a_blank ? nl : std::string()) + "Magic 2 active=0 0x0001C4C0" + nl;
}
bool Reads(const std::string& a_text)
{
	std::istringstream in(a_text);
	wheelstorage::Model m{};
	return wheelstorage::ReadAssignments(in, kId, m) && m[0][0].entries.size() == 2 && m[0][0].active == 0 &&
		m[0][0].entries[1] == 0x000BE335 && m[1][1].entries.size() == 1 && m[1][1].active == -1;
}
int main() {
	const char* bom = "\xEF\xBB\xBF";
	Check(Reads(File("", "\n", false)), "a plain file reads (as written by the mod)");
	Check(Reads(File(bom, "\n", false)), "a file with a UTF-8 BOM reads");
	Check(Reads(File("", "\r\n", false)), "a CRLF file reads");
	Check(Reads(File(bom, "\r\n", true)), "a BOM + CRLF file with a blank line reads");
	{
		std::istringstream in(File(bom, "\r\n", false));
		std::string who;
		Check(wheelstorage::ReadIdentity(in, who) && who == kId, "ReadIdentity names the character without BOM or CR");
	}
	{
		std::istringstream in(std::string(bom) + "# ImprovedWheelsPlus spell names v1\r\n# Character: " + kId + "\r\n0001C4C0\tFire Bolt\r\n");
		const bool header = wheelstorage::ReadHeader(in, kId, wheelstorage::kSpellNamesSignature);
		Check(header, "a BOM + CRLF .spells header reads");
		if (header) ReadNames(in);
		Check(g_displayNames[0x1C4C0] == "Fire Bolt", "a CRLF .spells name has no trailing CR");
	}
	{
		std::istringstream in(File("", "\n", false));
		Check(!wheelstorage::ReadAssignments(in, "00000000-00000000-00000000-00000001", *new wheelstorage::Model{}),
			"a file naming another character is still refused");
	}
	return failures ? 1 : 0;
}
'''

with tempfile.TemporaryDirectory() as folder:
    folder = Path(folder)
    (folder / 'test.cpp').write_text(program, encoding='utf-8')
    (folder / 'build.bat').write_text(
        '@echo off\ncall F:\\modbuild\\tools\\msvc\\setup_x64.bat >nul\ncl /nologo /EHsc /std:c++20 /utf-8 test.cpp /Fe:test.exe >build.log\nif errorlevel 1 type build.log\n',
        encoding='utf-8')
    subprocess.run(['cmd', '/c', str(folder / 'build.bat')], cwd=folder, check=True)
    if not (folder / 'test.exe').exists():
        raise SystemExit('BUILD FAILED')
    raise SystemExit(subprocess.run([str(folder / 'test.exe')], cwd=folder).returncode)
