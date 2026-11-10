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

// Which rule of the challenge was broken.
enum class Rule : std::uint8_t
{
	kGear = 0,   // equipped a weapon, armor or ammunition the player did not make
	kFood,       // ate food the player did not cook
	kPotion,     // drank or applied a potion or poison the player did not brew
	kSale,       // sold something that was not the player's own
	kMagic,      // equipped a spell, a scroll or a staff
	kMissive,    // took a missive from a board
	kMaterial,   // crafted with ore, firewood or hides the player did not gather

	kTotal
};

// Whether this character is playing Zenithar's Challenge. Chosen once, at character creation.
// (The values are what older saves hold: 1 was "Honor", a warn-only mode that no longer exists and
// now reads as a standard playthrough; 2 was "Oath", which is the challenge.)
enum class Mode : std::uint8_t
{
	kUnset = 0,  // not asked yet: nothing is judged
	kOff,        // a standard playthrough: the mod stays out of the way
	kOath,       // the challenge: breaking a rule is refused
};

// The rules the challenge leaves to the player, chosen with the mode. A set bit means "allowed".
namespace Option
{
	inline constexpr std::uint32_t kLootGems = 1u << 0;
	inline constexpr std::uint32_t kLootSoulGems = 1u << 1;
	inline constexpr std::uint32_t kCoinPurseGems = 1u << 2;
	inline constexpr std::uint32_t kQuestRewards = 1u << 3;
	inline constexpr std::uint32_t kStartingItems = 1u << 4;
	inline constexpr std::uint32_t kOthersKills = 1u << 5;
}

struct Violation
{
	Rule         rule{ Rule::kGear };
	RE::FormID   form{ 0 };
	std::int32_t count{ 0 };
	float        day{ 0.0F };  // in-game days passed when it happened
};

// The count of "yours" copies of every item, wherever they are.
//
// Skyrim merges looted and made copies of one item into a single stack, so the ledger keeps
// counts instead of marking items. Every copy the player makes or gathers is "earned". Earned
// copies are followed as they move: pack -> chest -> pack, pack -> ground -> pack. A copy is
// never earned by moving it; only making or gathering mints one.
//
// A "holder" is whoever has the copies: the player (0x14), a container or actor reference, or the
// ground (one stand-in holder for everything the player has dropped).
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
	using MadeCounts = std::array<std::uint32_t, static_cast<std::size_t>(Source::kTotal)>;

	// A copy of everything the run file reports.
	struct Snapshot
	{
		Mode                                             mode{ Mode::kUnset };
		std::uint32_t                                    options{ 0 };
		std::vector<std::pair<RE::FormID, MadeCounts>>   made;
		std::vector<Violation>                           violations;
		std::vector<std::pair<RE::FormID, std::uint32_t>> killsByRace;
		std::uint32_t                                    consoleOpened{ 0 };
	};

	static constexpr RE::FormID kPlayer = 0x14;

	static Ledger& Get();

	// n copies the player made or gathered arrived in the pack.
	void Mint(RE::FormID a_form, std::int32_t a_count, Source a_source);
	// n copies that are not the player's arrived in the pack.
	void AddUnearned(RE::FormID a_form, std::int32_t a_count);
	// n earned copies return to the pack without being made anew (an improved or enchanted item).
	void AddEarned(RE::FormID a_form, std::int32_t a_count);
	// n copies moved between holders. Returns how many of them were earned.
	// Leaving the pack, the not-yours copies go first unless a_earnedFirst (used for sales, where the
	// player is taken to be selling their own work first).
	std::int32_t Transfer(RE::FormID a_from, RE::FormID a_to, RE::FormID a_form, std::int32_t a_count, bool a_earnedFirst = false);
	// Brings the pack's counts for one item in line with what the pack really holds. Copies the
	// ledger never saw arrive are not yours; copies that left unseen come off the not-yours count
	// first, then the earned count. Returns the change in the not-yours count.
	std::int32_t ReconcilePack(RE::FormID a_form, std::int32_t a_actual);
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

	// A rule was broken. Returns how many have been broken so far, this one included.
	std::uint32_t               Break(Rule a_rule, RE::FormID a_form, std::int32_t a_count, float a_day);
	[[nodiscard]] std::uint32_t ViolationCount() const;

	// Every kill by the player, by the victim's race, for the run's story.
	void TallyKill(RE::FormID a_race);
	// The console was opened.
	void NoteConsole();
	[[nodiscard]] Snapshot TakeSnapshot() const;

	[[nodiscard]] Mode StoredMode() const;
	void               SetMode(Mode a_mode);
	[[nodiscard]] std::uint32_t Options() const;
	void                        SetOptions(std::uint32_t a_options);
	// A new character has not been asked for its mode yet.
	[[nodiscard]] bool ChoicePending() const;
	void               SetChoicePending(bool a_pending);
	// Up to n not-yours copies in the pack become the player's own (an allowed quest reward or coin purse gem). Returns how many.
	std::int32_t Promote(RE::FormID a_form, std::int32_t a_count);
	// Everything the pack holds of this item becomes the player's own (the "keep starting items" choice).
	void EarnPack(RE::FormID a_form, std::int32_t a_actual);

	void Clear();

	// SKSE serialization
	static void RegisterSerialization();

private:
	using Counts = std::unordered_map<RE::FormID, std::int32_t>;
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
	std::vector<Violation>                    _violations;
	Mode                                      _mode{ Mode::kUnset };
	std::unordered_map<RE::FormID, std::uint32_t> _killsByRace;
	std::uint32_t                             _consoleOpened{ 0 };
	std::uint32_t                             _options{ 0 };
	bool                                      _choicePending{ false };
};
