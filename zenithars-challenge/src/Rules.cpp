#include "Rules.h"

#include "Settings.h"

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

	// Water and provisions: may be consumed whoever made them.
	bool IsFromAllowedPlugin(const RE::TESForm* a_item)
	{
		const auto plugin = DefiningPlugin(a_item);
		if (plugin.empty()) {
			return false;
		}
		const auto       list = Settings::sAllowedPlugins.GetValue();
		std::string_view rest{ list };
		while (!rest.empty()) {
			const auto bar = rest.find('|');
			auto       name = rest.substr(0, bar);
			rest = bar == std::string_view::npos ? ""sv : rest.substr(bar + 1);
			while (!name.empty() && name.front() == ' ') {
				name.remove_prefix(1);
			}
			while (!name.empty() && name.back() == ' ') {
				name.remove_suffix(1);
			}
			if (!name.empty() && EqualsNoCase(plugin, name)) {
				return true;
			}
		}
		return false;
	}

	bool IsMissive(const RE::TESForm* a_item)
	{
		return a_item && a_item->Is(RE::FormType::Book) && ContainsNoCase(DefiningPlugin(a_item), "missive"sv);
	}

	// The challenge lets the player get a pickaxe however they like; the woodcutter's axe is treated the same.
	bool IsTool(const RE::TESForm* a_item)
	{
		const auto name = ItemName(a_item);
		return ContainsNoCase(name, "pickaxe"sv) || ContainsNoCase(name, "woodcutter"sv);
	}

	// Raw materials the challenge says the player must gather: ore and ingots, firewood, hides and leather.
	bool IsGatheredMaterial(const RE::TESForm* a_item)
	{
		return a_item && (a_item->GetFormID() == kFirewood || HasKeyword(a_item, "VendorItemOreIngot"sv) ||
							 HasKeyword(a_item, "VendorItemAnimalHide"sv));
	}

	// Raw fruit and vegetables: anything a plant or tree in the game can be picked for, and anything
	// a food mod has tagged as fruit or vegetable. The challenge leaves these free.
	bool IsRawProduce(const RE::TESForm* a_item)
	{
		const auto food = a_item ? a_item->As<RE::AlchemyItem>() : nullptr;
		if (!food || !food->IsFood()) {
			return false;
		}
		static const auto picked = []() {
			std::unordered_set<RE::FormID> forms;
			if (const auto data = RE::TESDataHandler::GetSingleton()) {
				for (const auto flora : data->GetFormArray<RE::TESFlora>()) {
					if (flora && flora->produceItem) {
						forms.insert(flora->produceItem->GetFormID());
					}
				}
				for (const auto tree : data->GetFormArray<RE::TESObjectTREE>()) {
					if (tree && tree->produceItem) {
						forms.insert(tree->produceItem->GetFormID());
					}
				}
			}
			logger::info("raw produce: {} kinds of thing can be picked from plants and trees", forms.size());
			return forms;
		}();
		if (picked.contains(a_item->GetFormID()) || HasKeyword(a_item, "VendorItemFruit"sv) || HasKeyword(a_item, "VendorItemVegetable"sv)) {
			return true;
		}
		// The game's records do not tell a carrot from a cheese, and nothing grows carrots. So: food
		// named for a fruit or vegetable that no recipe makes. A recipe makes Apple Pie and Tomato
		// Soup; none makes an Apple or a Tomato.
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
		if (cooked.contains(a_item->GetFormID())) {
			return false;
		}
		static constexpr std::array produce{ "apple"sv, "carrot"sv, "cabbage"sv, "potato"sv, "leek"sv, "tomato"sv, "turnip"sv, "onion"sv, "pumpkin"sv,
			"gourd"sv, "pear"sv, "peach"sv, "plum"sv, "grape"sv, "berry"sv, "berries"sv, "melon"sv, "radish"sv, "beet"sv, "corn"sv, "lettuce"sv,
			"cucumber"sv, "orange"sv, "lemon"sv, "fig"sv, "olive"sv, "cherry"sv, "cherries"sv, "squash"sv, "garlic"sv, "yam"sv };
		const auto name = ItemName(a_item);
		return std::ranges::any_of(produce, [&name](std::string_view a_word) { return ContainsNoCase(name, a_word); });
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
			return Settings::bRestrictClothing.GetValue() && (a_item->GetFormFlags() & 0x4) == 0;
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
