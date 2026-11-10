#pragma once

// How an item came to be the player's own.
enum class Source : std::uint8_t
{
	kCrafted = 0,  // forge, smelter, tanning rack and other "make" benches
	kCooked,       // food from a cooking pot
	kBrewed,       // potions and poisons
	kMined,        // ore and gems from a vein
	kChopped,      // firewood from a chopping block
	kHunted,       // anything taken from an animal the player killed

	kTotal
};

[[nodiscard]] std::string_view SourceVerb(Source a_source) noexcept;

// The count of "yours" copies of every item, wherever they are.
//
// Skyrim merges looted and made copies of one item into a single stack, so the ledger keeps
// counts instead of marking items. Every copy the player makes or gathers is "earned". Earned
// copies are followed as they move: pack -> chest -> pack, pack -> ground -> pack. A copy is
// never earned by moving it; only making or gathering mints one.
//
// A "holder" is whoever has the copies: the player (0x14), a container or actor reference, or a
// dropped item's own reference.
//
// Which copies move when a mixed stack is split:
//   - leaving the pack for a container or the ground: the not-yours copies go first, so a player
//     can clear a mixed stack by storing the extras;
//   - used up (eaten, drunk, spent as crafting material): the earned copies go first;
//   - coming back out of a container: the earned copies come first.
//
// Persisted in the SKSE co-save, so it travels with the save and survives load-order changes.
class Ledger
{
public:
	static constexpr RE::FormID kPlayer = 0x14;

	static Ledger& Get();

	// n copies the player made or gathered arrived in the pack.
	void Mint(RE::FormID a_form, std::int32_t a_count, Source a_source);
	// n copies that are not the player's arrived in the pack.
	void AddUnearned(RE::FormID a_form, std::int32_t a_count);
	// n earned copies return to the pack without being made anew (an improved or enchanted item).
	void AddEarned(RE::FormID a_form, std::int32_t a_count);
	// n earned copies now sit with a holder (used when a dropped item's reference is known only a moment later).
	void Place(RE::FormID a_holder, RE::FormID a_form, std::int32_t a_count);
	// n copies moved between holders. Returns how many of them were earned.
	std::int32_t Transfer(RE::FormID a_from, RE::FormID a_to, RE::FormID a_form, std::int32_t a_count);
	// n copies left a holder and the world. Returns how many of them were earned.
	std::int32_t Consume(RE::FormID a_holder, RE::FormID a_form, std::int32_t a_count);

	[[nodiscard]] std::int32_t Earned(RE::FormID a_holder, RE::FormID a_form) const;
	[[nodiscard]] std::int32_t UnearnedInPack(RE::FormID a_form) const;
	[[nodiscard]] bool         Tracks(RE::FormID a_holder) const;
	[[nodiscard]] std::uint32_t TotalMade(Source a_source) const;
	[[nodiscard]] std::uint32_t TotalMade() const;

	// Animals the player killed, so their hides and meat count as hunted.
	void               NoteKill(RE::FormID a_actorRef);
	[[nodiscard]] bool WasKilledByPlayer(RE::FormID a_actorRef) const;

	void Clear();

	// SKSE serialization
	static void RegisterSerialization();

private:
	using Counts = std::unordered_map<RE::FormID, std::int32_t>;
	using MadeCounts = std::array<std::uint32_t, static_cast<std::size_t>(Source::kTotal)>;

	static void OnSave(SKSE::SerializationInterface* a_intfc);
	static void OnLoad(SKSE::SerializationInterface* a_intfc);
	static void OnRevert(SKSE::SerializationInterface* a_intfc);

	std::int32_t TakeEarned(RE::FormID a_holder, RE::FormID a_form, std::int32_t a_count);
	void         GiveEarned(RE::FormID a_holder, RE::FormID a_form, std::int32_t a_count);
	std::int32_t TakeUnearned(RE::FormID a_form, std::int32_t a_count);
	void         ClearLocked();

	mutable std::mutex                        _lock;
	std::unordered_map<RE::FormID, Counts>    _earned;    // holder -> item -> earned copies
	Counts                                    _unearned;  // item -> not-yours copies in the pack
	std::unordered_map<RE::FormID, MadeCounts> _made;     // item -> lifetime made, by source
	std::deque<RE::FormID>                    _killOrder;
	std::unordered_set<RE::FormID>            _kills;
};
