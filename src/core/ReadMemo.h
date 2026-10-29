#pragma once

#include <cstdint>
#include <optional>
#include <utility>

namespace readmemo
{
	inline thread_local std::uint64_t serial = 0, current = 0;
	struct Scope
	{
		std::uint64_t previous = current;
		Scope() { current = ++serial; }
		~Scope() { current = previous; }
		Scope(const Scope&) = delete;
		Scope& operator=(const Scope&) = delete;
	};

	template <class Key, class Value>
	class Memo
	{
		std::uint64_t epoch = 0;
		std::optional<Key> key;
		std::optional<Value> value;

	public:
		template <class Resolve>
		Value Get(Key a_key, Resolve&& a_resolve)
		{
			if (!current) return std::forward<Resolve>(a_resolve)();
			if (epoch != current || !key || *key != a_key) {
				value = std::forward<Resolve>(a_resolve)();
				key = a_key;
				epoch = current;
			}
			return *value;
		}
	};
}
