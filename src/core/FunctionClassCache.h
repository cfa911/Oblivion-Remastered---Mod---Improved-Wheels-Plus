#pragma once

#include <unordered_map>
#include <utility>

// Keep both interesting and uninteresting classifications. Identity distinguishes
// a new object at a reused address; callers supply live metadata, never stale reads.
template <class Key, class Identity, class Value>
class FunctionClassCache
{
	struct Entry { Identity identity; Value value; };
	std::unordered_map<Key, Entry> entries;

public:
	template <class Resolve>
	const Value& Get(Key a_key, Identity a_identity, Resolve&& a_resolve)
	{
		auto it = entries.find(a_key);
		if (it != entries.end() && it->second.identity == a_identity) return it->second.value;
		return entries.insert_or_assign(a_key, Entry{ a_identity, std::forward<Resolve>(a_resolve)() }).first->second.value;
	}
};
