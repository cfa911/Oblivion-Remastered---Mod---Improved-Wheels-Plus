#pragma once

// ============================================================================================================
// Just enough Unreal reflection to read a property by NAME (CommonLibOB64 has no FProperty): walk a struct's
// FField chain (next +0x18, name +0x20, as CommonLibOB64's FField declares) and read FProperty::Offset_Internal
// at +0x44 (UE5's layout). reflect::SelfCheck() proves the offset on a property whose place is already known
// (VQuickKeysMenuViewModel::KeyIndex at 0xD0, read in game 2026-09-26) before anything else trusts it.
// Game thread only.
// ============================================================================================================

namespace reflect
{
	// -1 when the struct (or its supers) has no property of that name
	std::int32_t Offset(UE::UStruct* a_struct, std::string_view a_name);

	// a struct's own properties (a UFunction's parameters), in declaration order: name and offset
	std::vector<std::pair<std::string, std::int32_t>> Fields(UE::UStruct* a_struct);

	bool SelfCheck();   // true once the Offset_Internal layout is proven; everything reflected refuses to run until then
	bool Ok();

	bool        TextSet(const UE::FText& a_text);   // false for a zeroed FText (a list row never given an item)
	std::string Text(const UE::FText& a_text);   // an FText's display string (UTF-8); empty for a zeroed one

	// the string-table key an FText was made from ("LOC_FN_..." for a form's name), through the engine's own
	// KismetTextLibrary::StringTableIdAndKeyFromText; empty when the text is not from a table
	std::string TextKey(const UE::FText& a_text);

	template <class T>
	T* At(void* a_base, std::int32_t a_offset)
	{
		return a_base && a_offset >= 0 ? reinterpret_cast<T*>(static_cast<std::uint8_t*>(a_base) + a_offset) : nullptr;
	}

	// every live instance of a class (never the class default object)
	std::vector<UE::UObject*> Instances(UE::UClass* a_class);

	// For an object read THIS frame from a live owner only: it reads a_obj's own index, so a pointer kept from an
	// earlier frame must never come here (a freed row's index is garbage - the crash of 2026-09-29 09:15, dropping an
	// item with X: IsLive from HighlightedItem read freed memory). Kept pointers are Handles.
	bool IsLive(UE::UObject* a_obj);

	// A pointer kept across frames with the object-array slot it was found in. Get() asks the SLOT whether it still
	// holds that object and never reads the object itself.
	struct Handle
	{
		UE::UObject* ptr = nullptr;
		std::int32_t index = -1;
	};
	Handle       Hold(UE::UObject* a_live);   // a_live must be live now (handed to us by the engine this frame)
	UE::UObject* Get(const Handle& a_handle);   // nullptr once the slot holds anything else
	UE::UObject* Get(UE::UObject* a_ptr, std::int32_t a_index);

	// calls a UFunction by name through ProcessEvent (params laid out by the caller); false when it has none
	bool Call(UE::UObject* a_obj, const wchar_t* a_function, void* a_params);
}
