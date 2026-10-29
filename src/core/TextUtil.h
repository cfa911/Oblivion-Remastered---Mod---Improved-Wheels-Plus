#pragma once

// Text helpers with no engine or game dependency, so tests/ can build them alone (xmake build Tests).

#include <cstddef>
#include <string>
#include <string_view>

#ifndef WIN32_LEAN_AND_MEAN
#	define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#	define NOMINMAX
#endif
#include <Windows.h>

namespace textutil
{
	// UTF-8 to UTF-16 (a name with umlauts or an sz must not be widened byte by byte). Malformed bytes become U+FFFD.
	inline std::wstring Wide(std::string_view a_s)
	{
		const int n = a_s.empty() ? 0 : MultiByteToWideChar(CP_UTF8, 0, a_s.data(), static_cast<int>(a_s.size()), nullptr, 0);
		std::wstring out(n > 0 ? static_cast<std::size_t>(n) : 0, L'\0');
		if (n > 0) {
			MultiByteToWideChar(CP_UTF8, 0, a_s.data(), static_cast<int>(a_s.size()), out.data(), n);
		}
		return out;
	}

	// The remaster's texture for a form's icon path (the primary session's reads, 2026-09-30: Arrow1Iron's icon is
	// "Weapons\IronArrow.dds", and the paks hold /Game/Art/UI/Icons/Dynamic_Icons/menus/icons/weapons/T_ironarrow -
	// the folder in lower case, "T_" and the lower-case stem; the HUD's own textures follow the same rule). Empty when no stem.
	inline std::string IconAssetPath(std::string_view a_icon)
	{
		std::string p(a_icon);
		for (auto& c : p) {
			c = c == '\\' ? '/' : static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
		}
		if (const auto dot = p.rfind('.'); dot != std::string::npos) p.resize(dot);
		const auto        slash = p.rfind('/');
		const std::string dir = slash == std::string::npos ? std::string() : p.substr(0, slash + 1);
		const std::string stem = slash == std::string::npos ? p : p.substr(slash + 1);
		if (stem.empty()) return {};
		return "/Game/Art/UI/Icons/Dynamic_Icons/menus/icons/" + dir + "T_" + stem + ".T_" + stem;
	}
}
