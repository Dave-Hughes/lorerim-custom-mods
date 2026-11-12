#include "Rules.h"

namespace Rules
{
	bool HasKeyword(const RE::TESForm* a_item, std::string_view a_editorID)
	{
		const auto keywords = a_item ? a_item->As<RE::BGSKeywordForm>() : nullptr;
		return keywords && keywords->HasKeywordString(a_editorID);
	}

	bool IsOreOrGem(const RE::TESForm* a_item)
	{
		return HasKeyword(a_item, "VendorItemOreIngot"sv) || HasKeyword(a_item, "VendorItemGem"sv);
	}

	bool IsMagicItem(const RE::TESForm* a_item)
	{
		if (!a_item) {
			return false;
		}
		if (a_item->Is(RE::FormType::Scroll)) {
			return true;
		}
		const auto weapon = a_item->As<RE::TESObjectWEAP>();
		return weapon && weapon->IsStaff();
	}

	bool IsGem(const RE::TESForm* a_item)
	{
		return HasKeyword(a_item, "VendorItemGem"sv);
	}

	bool IsSoulGem(const RE::TESForm* a_item)
	{
		return a_item && a_item->Is(RE::FormType::SoulGem);
	}

	bool IsGear(const RE::TESForm* a_item)
	{
		return a_item && (a_item->Is(RE::FormType::Weapon) || a_item->Is(RE::FormType::Armor) || a_item->Is(RE::FormType::Ammo));
	}

	std::string ItemName(const RE::TESForm* a_item)
	{
		const auto name = a_item ? a_item->GetName() : nullptr;
		if (name && name[0] != '\0') {
			return name;
		}
		return std::format("item {:08X}", a_item ? a_item->GetFormID() : 0);
	}

	bool ContainsNoCase(std::string_view a_text, std::string_view a_part)
	{
		const auto it = std::search(a_text.begin(), a_text.end(), a_part.begin(), a_part.end(), [](char a_lhs, char a_rhs) {
			return std::tolower(static_cast<unsigned char>(a_lhs)) == std::tolower(static_cast<unsigned char>(a_rhs));
		});
		return it != a_text.end();
	}

	bool EqualsNoCase(std::string_view a_lhs, std::string_view a_rhs)
	{
		return a_lhs.size() == a_rhs.size() && ContainsNoCase(a_lhs, a_rhs);
	}

	// The plugin that first defined the item ("" for items made while playing, such as brewed potions).
	std::string_view DefiningPlugin(const RE::TESForm* a_item)
	{
		const auto file = a_item ? a_item->GetFile(0) : nullptr;
		return file ? file->GetFilename() : ""sv;
	}

	// Water and provisions: may be consumed whoever made them. These are LoreRim's plugins for them.
	// Not a setting: a list a player could add to would let any food or potion through unseen.
	bool IsFromAllowedPlugin(const RE::TESForm* a_item)
	{
		static constexpr std::array free{ "Waterskin.esp"sv, "LoreRim Respawn Provision.esp"sv };
		const auto plugin = DefiningPlugin(a_item);
		return !plugin.empty() && std::ranges::any_of(free, [plugin](std::string_view a_name) { return EqualsNoCase(plugin, a_name); });
	}

	bool IsMissive(const RE::TESForm* a_item)
	{
		return a_item && a_item->Is(RE::FormType::Book) && ContainsNoCase(DefiningPlugin(a_item), "missive"sv);
	}

	// The challenge lets the player get a pickaxe however they like; the woodcutter's axe is treated the same.
	bool IsFishingRod(const RE::TESForm* a_item)
	{
		return a_item && a_item->Is(RE::FormType::Weapon) &&
			   (HasKeyword(a_item, "ccBGSSSE001_FishingPoleKW"sv) || ContainsNoCase(ItemName(a_item), "fishing rod"sv));
	}

	bool IsTool(const RE::TESForm* a_item)
	{
		const auto name = ItemName(a_item);
		return ContainsNoCase(name, "pickaxe"sv) || ContainsNoCase(name, "woodcutter"sv) || IsFishingRod(a_item);
	}

	// Raw materials the challenge says the player must gather: ore and ingots, firewood, hides and leather.
	bool IsGatheredMaterial(const RE::TESForm* a_item)
	{
		return a_item && (a_item->GetFormID() == kFirewood || HasKeyword(a_item, "VendorItemOreIngot"sv) ||
							 HasKeyword(a_item, "VendorItemAnimalHide"sv));
	}

	namespace
	{
		[[nodiscard]] bool IsLetter(char a_ch)
		{
			return std::isalpha(static_cast<unsigned char>(a_ch)) != 0;
		}

		[[nodiscard]] char Lower(char a_ch)
		{
			return static_cast<char>(std::tolower(static_cast<unsigned char>(a_ch)));
		}

		// True when a_word is in a_text ending where a word ends, give or take a plural: "tomato" is in
		// "Tomatoes" and "Raw Potato" has "potato", but "salt" is not in "Salted Beef". With
		// a_wholeWord it must start where a word starts as well: "ale" is not in "Kale".
		[[nodiscard]] bool HasWord(std::string_view a_text, std::string_view a_word, bool a_wholeWord)
		{
			if (a_word.empty() || a_text.size() < a_word.size()) {
				return false;
			}
			for (std::size_t at = 0; at + a_word.size() <= a_text.size(); ++at) {
				auto same = true;
				for (std::size_t i = 0; same && i < a_word.size(); ++i) {
					same = Lower(a_text[at + i]) == Lower(a_word[i]);
				}
				if (!same || (a_wholeWord && at > 0 && IsLetter(a_text[at - 1]))) {
					continue;
				}
				auto end = at + a_word.size();
				if (end + 1 < a_text.size() && Lower(a_text[end]) == 'e' && Lower(a_text[end + 1]) == 's') {
					if (end + 2 == a_text.size() || !IsLetter(a_text[end + 2])) {
						return true;
					}
				}
				if (end < a_text.size() && Lower(a_text[end]) == 's') {
					++end;
				}
				if (end == a_text.size() || !IsLetter(a_text[end])) {
					return true;
				}
			}
			return false;
		}
	}

	bool IsProduceName(std::string_view a_name)
	{
		// Cooking staples, free whether or not something can make them (Dave, 10 Nov).
		if (HasWord(a_name, "flour"sv, true) || HasWord(a_name, "salt"sv, true)) {
			return true;
		}
		// Something done to it: not raw, whatever it was made from.
		static constexpr std::array done{ "pie"sv, "soup"sv, "stew"sv, "baked"sv, "boiled"sv, "roasted"sv, "roast"sv, "grilled"sv, "fried"sv,
			"cooked"sv, "seared"sv, "steamed"sv, "mashed"sv, "stuffed"sv, "glazed"sv, "candied"sv, "pickled"sv, "dumpling"sv, "crostata"sv,
			"tart"sv, "cake"sv, "bread"sv, "jam"sv, "jelly"sv, "juice"sv, "cider"sv, "wine"sv, "mead"sv, "ale"sv, "brandy"sv, "pudding"sv,
			"salad"sv, "sauce"sv, "treat"sv, "pastry"sv, "strudel"sv, "cobbler"sv, "porridge"sv, "sweetroll"sv, "tea"sv };
		if (std::ranges::any_of(done, [a_name](std::string_view a_word) { return HasWord(a_name, a_word, true); })) {
			return false;
		}
		static constexpr std::array produce{ "apple"sv, "carrot"sv, "cabbage"sv, "potato"sv, "leek"sv, "tomato"sv, "turnip"sv, "onion"sv, "pumpkin"sv,
			"gourd"sv, "pear"sv, "peach"sv, "plum"sv, "grape"sv, "berry"sv, "berries"sv, "melon"sv, "radish"sv, "beet"sv, "corn"sv, "lettuce"sv,
			"cucumber"sv, "orange"sv, "lemon"sv, "fig"sv, "olive"sv, "cherry"sv, "cherries"sv, "squash"sv, "garlic"sv, "yam"sv };
		return std::ranges::any_of(produce, [a_name](std::string_view a_word) { return HasWord(a_name, a_word, false); });
	}

	// Raw fruit and vegetables, which the challenge leaves free, and flour and salt with them.
	//
	// The game's records do not tell a carrot from a cheese (both are VendorItemFood), and "what can
	// be picked" will not do either: hanging pheasants, rabbits and salmon are picked the same way as
	// a cabbage, and meat from hanging game does not count. So it goes by the name, and by there
	// being no recipe that makes it: a recipe makes Apple Pie; none makes an Apple.
	bool IsRawProduce(const RE::TESForm* a_item)
	{
		const auto food = a_item ? a_item->As<RE::AlchemyItem>() : nullptr;
		if (!food || !food->IsFood()) {
			return false;
		}
		if (HasKeyword(a_item, "VendorItemFruit"sv) || HasKeyword(a_item, "VendorItemVegetable"sv)) {
			return true;
		}
		const auto name = ItemName(a_item);
		if (!IsProduceName(name)) {
			return false;
		}
		if (HasWord(name, "flour"sv, true) || HasWord(name, "salt"sv, true)) {
			return true;
		}
		static const auto cooked = []() {
			std::unordered_set<RE::FormID> forms;
			if (const auto data = RE::TESDataHandler::GetSingleton()) {
				for (const auto recipe : data->GetFormArray<RE::BGSConstructibleObject>()) {
					if (recipe && recipe->createdItem) {
						forms.insert(recipe->createdItem->GetFormID());
					}
				}
			}
			return forms;
		}();
		return !cooked.contains(a_item->GetFormID());
	}

	bool MustBeOwnGear(const RE::TESForm* a_item)
	{
		if (!a_item) {
			return false;
		}
		if (const auto weapon = a_item->As<RE::TESObjectWEAP>()) {
			return !weapon->IsStaff() && !IsTool(a_item);
		}
		if (const auto armor = a_item->As<RE::TESObjectARMO>()) {
			if (armor->IsLightArmor() || armor->IsHeavyArmor()) {
				return true;
			}
			// Clothing, jewelry, cloaks and bags. Hidden pieces that mods put on the character are
			// flagged non-playable (record flag 0x4) and are left alone.
			return (a_item->GetFormFlags() & 0x4) == 0;
		}
		return a_item->Is(RE::FormType::Ammo);
	}

	bool MustBeOwn(const RE::TESForm* a_item)
	{
		if (!a_item) {
			return false;
		}
		if (a_item->Is(RE::FormType::AlchemyItem)) {
			return !IsFromAllowedPlugin(a_item) && !IsRawProduce(a_item);
		}
		return MustBeOwnGear(a_item) || IsGatheredMaterial(a_item) || IsGem(a_item) || IsSoulGem(a_item);
	}
}
