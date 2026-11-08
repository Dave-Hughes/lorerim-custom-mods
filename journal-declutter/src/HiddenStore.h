#pragma once

// The set of hidden journal entries for the current character.
// A key is (quest FormID, quest instance). Radiant quests reuse one FormID with a new
// instance per run, so hiding one bounty does not hide the next one.
// Titled quests and miscellaneous objectives are tracked in separate sets because the
// journal shows them in different lists.
// Persisted in the SKSE co-save, so it travels with the save and survives load-order changes.
class HiddenStore
{
public:
	using Key = std::uint64_t;

	static HiddenStore& Get();

	[[nodiscard]] static constexpr Key MakeKey(RE::FormID a_formID, std::uint32_t a_instance) noexcept
	{
		return (static_cast<Key>(a_formID) << 32) | a_instance;
	}

	[[nodiscard]] bool IsHidden(RE::FormID a_formID, std::uint32_t a_instance) const;
	[[nodiscard]] bool IsMiscHidden(RE::FormID a_formID, std::uint32_t a_instance) const;

	void Hide(RE::FormID a_formID, std::uint32_t a_instance);
	void Unhide(RE::FormID a_formID, std::uint32_t a_instance);
	void HideMisc(RE::FormID a_formID, std::uint32_t a_instance);
	void UnhideMisc(RE::FormID a_formID, std::uint32_t a_instance);
	void Clear();

	[[nodiscard]] std::size_t QuestCount() const { return _quests.size(); }
	[[nodiscard]] std::size_t MiscCount() const { return _misc.size(); }
	[[nodiscard]] const std::unordered_set<Key>& MiscKeys() const { return _misc; }

	// Session-only UI state: when true, hidden entries are listed with a prefix instead of removed.
	bool showHidden{ false };

	// SKSE serialization
	static void RegisterSerialization();

private:
	static void OnSave(SKSE::SerializationInterface* a_intfc);
	static void OnLoad(SKSE::SerializationInterface* a_intfc);
	static void OnRevert(SKSE::SerializationInterface* a_intfc);

	static void WriteSet(SKSE::SerializationInterface* a_intfc, std::uint32_t a_type, const std::unordered_set<Key>& a_set);
	static void ReadSet(SKSE::SerializationInterface* a_intfc, std::unordered_set<Key>& a_set);

	std::unordered_set<Key> _quests;
	std::unordered_set<Key> _misc;
};
