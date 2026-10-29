#pragma once

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#include <atomic>
#include <filesystem>
#include <string>
#include <string_view>
#include <algorithm>

namespace atomicfile
{
	// Same-directory replacement: the previous file remains until every byte is flushed and closed.
	inline bool Write(const std::filesystem::path& path, std::string_view bytes)
	{
		static std::atomic<unsigned long long> sequence{ 0 };
		std::filesystem::path temporary;
		HANDLE file = INVALID_HANDLE_VALUE;
		for (int attempt = 0; attempt < 32; ++attempt) {
			temporary = path;
			temporary += L".tmp-" + std::to_wstring(GetCurrentProcessId()) + L"-" + std::to_wstring(sequence.fetch_add(1));
			file = CreateFileW(temporary.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
			if (file != INVALID_HANDLE_VALUE) break;
			if (GetLastError() != ERROR_FILE_EXISTS && GetLastError() != ERROR_ALREADY_EXISTS) return false;
		}
		if (file == INVALID_HANDLE_VALUE) return false;
		bool ok = true;
		while (!bytes.empty()) {
			const auto count = static_cast<DWORD>((std::min)(bytes.size(), static_cast<std::size_t>(1024 * 1024)));
			DWORD written = 0;
			if (!WriteFile(file, bytes.data(), count, &written, nullptr) || written == 0) { ok = false; break; }
			bytes.remove_prefix(written);
		}
		if (ok) ok = FlushFileBuffers(file) != 0;
		if (!CloseHandle(file)) ok = false;
		if (ok) ok = MoveFileExW(temporary.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != 0;
		if (!ok) DeleteFileW(temporary.c_str());
		return ok;
	}
}
