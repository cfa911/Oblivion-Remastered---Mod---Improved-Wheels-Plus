#pragma once

// A byte-pattern scan over the game image's code sections ("49 3B C6 ?? 07 ..."; ?? matches any byte). The first
// match, or nullptr. Used where a function has no address-library ID: the TES main loop's call (TesThread.cpp).
namespace scan
{
	inline std::uint8_t* First(const char* a_pattern)
	{
		std::vector<int> pat;
		for (const char* p = a_pattern; *p;) {
			if (*p == ' ') {
				++p;
			} else if (*p == '?') {
				pat.push_back(-1);
				p += 2;
			} else {
				pat.push_back(static_cast<int>(std::strtoul(std::string(p, 2).c_str(), nullptr, 16)));
				p += 2;
			}
		}
		auto* base = reinterpret_cast<std::uint8_t*>(GetModuleHandleW(nullptr));
		auto* dos = reinterpret_cast<IMAGE_DOS_HEADER*>(base);
		auto* nt = reinterpret_cast<IMAGE_NT_HEADERS*>(base + dos->e_lfanew);
		auto* sec = IMAGE_FIRST_SECTION(nt);
		for (unsigned i = 0; i < nt->FileHeader.NumberOfSections; ++i, ++sec) {
			if (!(sec->Characteristics & IMAGE_SCN_MEM_EXECUTE)) {
				continue;
			}
			std::uint8_t* start = base + sec->VirtualAddress;
			const std::size_t size = sec->Misc.VirtualSize;
			for (std::size_t at = 0; at + pat.size() <= size; ++at) {
				std::size_t k = 0;
				for (; k < pat.size(); ++k) {
					if (pat[k] >= 0 && start[at + k] != static_cast<std::uint8_t>(pat[k])) {
						break;
					}
				}
				if (k == pat.size()) {
					return start + at;
				}
			}
		}
		return nullptr;
	}
}
