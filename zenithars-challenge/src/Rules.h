#pragma once

// What the challenge's rules say about an item, with no reference to who holds it.
namespace Rules
{
	inline constexpr RE::FormID kFirewood = 0x0006F993;  // Skyrim.esm Firewood01

	[[nodiscard]] bool HasKeyword(const RE::TESForm* a_item, std::string_view a_editorID);
	[[nodiscard]] bool IsOreOrGem(const RE::TESForm* a_item);
	// Weapons, armor (clothing included) and ammunition.
	[[nodiscard]] bool IsGear(const RE::TESForm* a_item);
	[[nodiscard]] std::string ItemName(const RE::TESForm* a_item);
	[[nodiscard]] bool ContainsNoCase(std::string_view a_text, std::string_view a_part);
	[[nodiscard]] bool EqualsNoCase(std::string_view a_lhs, std::string_view a_rhs);
	// The plugin that first defined the item ("" for items made while playing, such as brewed potions).
	[[nodiscard]] std::string_view DefiningPlugin(const RE::TESForm* a_item);
	// Water and provisions: may be consumed whoever made them.
	[[nodiscard]] bool IsFromAllowedPlugin(const RE::TESForm* a_item);
	[[nodiscard]] bool IsMissive(const RE::TESForm* a_item);
	// Pickaxes and woodcutter's axes: the player may get these however they like.
	[[nodiscard]] bool IsTool(const RE::TESForm* a_item);
	// Raw materials the player must gather: ore and ingots, firewood, hides and leather.
	[[nodiscard]] bool IsGatheredMaterial(const RE::TESForm* a_item);

	[[nodiscard]] bool IsGem(const RE::TESForm* a_item);
	[[nodiscard]] bool IsSoulGem(const RE::TESForm* a_item);

	// Weapons (not staves, not tools), anything worn (armor, shields, clothing, jewelry, cloaks, bags) and
	// ammunition: must be made by the player to be used. bRestrictClothing=0 narrows "worn" to light and heavy armor.
	[[nodiscard]] bool MustBeOwnGear(const RE::TESForm* a_item);
	// Anything that must be the player's own: gear, food, potions, gathered materials, gems and soul gems.
	[[nodiscard]] bool MustBeOwn(const RE::TESForm* a_item);
}
