#include "engine/UiText.h"

#include "engine/PEHook.h"
#include "engine/Reflect.h"
#include "engine/Ui.h"

namespace uix
{
	namespace
	{
		constexpr const wchar_t* kFontAsset = L"/Game/UI/CommonStyle/Fonts/Font_Robinson_Localized.Font_Robinson_Localized";
		constexpr const wchar_t* kFullNames = L"/Game/Localization/StringTables/ST_FullNames.ST_FullNames";
	}

	void Ink(UE::UObject* a_label, const float (&a_rgba)[4])
	{
		struct SlateColor
		{
			float        rgba[4];
			std::uint8_t rule;   // ESlateColorStylingMode: 0 = the colour given
			std::uint8_t pad[7];
		} ink{ { a_rgba[0], a_rgba[1], a_rgba[2], a_rgba[3] }, 0, {} };
		ui::CallFirst(a_label, L"SetColor", &ink, sizeof(ink));   // the prefab's own (Tween Menu, BTPS)
		ui::CallFirst(a_label, L"SetColorAndOpacity", &ink, sizeof(ink));
	}

	void Shadow(UE::UObject* a_label, const float (&a_rgba)[4], double a_offset)
	{
		ui::CallFirst(a_label, L"SetShadowColorAndOpacity", a_rgba, sizeof(a_rgba));
		ui::Vec2(a_label, L"SetShadowOffset", a_offset, a_offset);
	}

	void InsideWheelInk(UE::UObject* a_label)
	{
		constexpr float kSoftGrey[4] = { 0.50f, 0.49f, 0.46f, 1.0f };   // linear
		constexpr float kNone[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
		Ink(a_label, kSoftGrey);
		Shadow(a_label, kNone, 0.0);
	}

	bool SetText(UE::UObject* a_widget, const std::string& a_text)
	{
		auto*      fn = a_widget ? a_widget->FindFunction(UE::FName(L"SetText", UE::EFindName::Find)) : nullptr;
		const auto params = fn ? reflect::Fields(reinterpret_cast<UE::UStruct*>(fn)) : std::vector<std::pair<std::string, std::int32_t>>{};
		if (params.empty()) return false;
		std::vector<std::uint8_t> buf(static_cast<std::size_t>(std::max(reinterpret_cast<UE::UStruct*>(fn)->propertiesSize, 0)) + 16, 0);
		const std::wstring w = pe::Wide(a_text);
		auto*              text = new (buf.data() + params.front().second) UE::FText(UE::FText::AsCultureInvariant(UE::FString(w.c_str())));
		a_widget->ProcessEvent(fn, buf.data());
		text->~FText();
		return true;
	}

	bool SetItemName(UE::UObject* a_widget, const std::string& a_text)
	{
		auto*      fn = a_widget ? a_widget->FindFunction(UE::FName(L"SetText", UE::EFindName::Find)) : nullptr;
		const auto params = fn ? reflect::Fields(reinterpret_cast<UE::UStruct*>(fn)) : std::vector<std::pair<std::string, std::int32_t>>{};
		if (params.empty()) return false;
		const std::wstring w = pe::Wide(a_text);
		std::vector<std::uint8_t> buf(static_cast<std::size_t>(std::max(reinterpret_cast<UE::UStruct*>(fn)->propertiesSize, 0)) + 16, 0);
		UE::FText* text = nullptr;
		if (a_text.starts_with("LOC_")) {
			static auto* lib = ui::Class(L"/Script/Engine.KismetTextLibrary");
			ui::Call t(lib ? lib->GetDefaultObject(false) : nullptr, L"TextFromStringTable");
			void* id = t.At("TableId");
			void* key = t.At("Key");
			void* ret = t.At("ReturnValue");
			if (id && key && ret) {
				new (id) UE::FName(kFullNames, UE::EFindName::Add);
				auto* k = new (key) UE::FString(w.c_str());
				t.Run();
				auto* got = static_cast<UE::FText*>(ret);
				text = new (buf.data() + params.front().second) UE::FText(*got);
				got->~FText();
				k->~FString();
			}
		}
		if (!text) {
			text = new (buf.data() + params.front().second) UE::FText(UE::FText::AsCultureInvariant(UE::FString(w.c_str())));
		}
		a_widget->ProcessEvent(fn, buf.data());
		text->~FText();
		return true;
	}

	namespace
	{
		bool Apply(UE::UObject* a_w, double a_size, const float* a_outline, int a_outlineSize = 1)
		{
		static auto*           fontInfo = reinterpret_cast<UE::UStruct*>(UE::StaticFindObject<UE::UObject>(nullptr, nullptr, L"/Script/SlateCore.SlateFontInfo"));
		static reflect::Handle s_font;
		auto*                  font = reflect::Get(s_font);
		if (!font) {
			font = ui::Load(kFontAsset);
			if (font) s_font = reflect::Hold(font);
		}
		if (!a_w || !a_w->GetClass() || !fontInfo || !font || !reflect::Ok()) return false;
		const auto propOff = reflect::Offset(a_w->GetClass(), "Font");
		const auto propSize = ui::SizeOf(a_w->GetClass(), "Font");
		const auto objOff = reflect::Offset(fontInfo, "FontObject");
		const auto sizeOff = reflect::Offset(fontInfo, "Size");
		const auto sizeSize = ui::SizeOf(fontInfo, "Size");
		if (propOff < 0 || propSize <= 0 || objOff < 0 || sizeOff < 0 || (sizeSize != 4 && sizeSize != 8)) return false;
		auto* b = reinterpret_cast<std::uint8_t*>(a_w) + propOff;
		*reinterpret_cast<UE::UObject**>(b + objOff) = font;
		if (a_size > 0) {
			if (sizeSize == 4) *reinterpret_cast<float*>(b + sizeOff) = static_cast<float>(a_size);
			else *reinterpret_cast<double*>(b + sizeOff) = a_size;
		}
		if (a_outline) {   // FSlateFontInfo::OutlineSettings { OutlineSize (int32 in UE 5.3), OutlineColor }
			static auto* outline = reinterpret_cast<UE::UStruct*>(UE::StaticFindObject<UE::UObject>(nullptr, nullptr, L"/Script/SlateCore.FontOutlineSettings"));
			const auto   osOff = reflect::Offset(fontInfo, "OutlineSettings");
			const auto   sizeOff2 = outline ? reflect::Offset(outline, "OutlineSize") : -1;
			const auto   colOff = outline ? reflect::Offset(outline, "OutlineColor") : -1;
			if (osOff >= 0 && sizeOff2 >= 0 && colOff >= 0 && ui::SizeOf(outline, "OutlineSize") == 4) {
				*reinterpret_cast<std::int32_t*>(b + osOff + sizeOff2) = a_outlineSize;
				std::memcpy(b + osOff + colOff, a_outline, 4 * sizeof(float));
			}
		}
		std::vector<std::uint8_t> copy(b, b + propSize);
		return ui::CallFirst(a_w, L"SetFont", copy.data(), copy.size());
		}
	}

	bool ApplyFont(UE::UObject* a_w, double a_size)
	{
		return Apply(a_w, a_size, nullptr);
	}

	bool ApplyBoldFont(UE::UObject* a_w, double a_size, const float* a_rgba, int a_outlineSize)
	{
		return Apply(a_w, a_size, a_rgba, a_outlineSize);
	}


}
