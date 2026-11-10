#pragma once

// The game's own running totals for the character (places found, quests done, gold found...), for
// the run file. The game only gives them out through its script system, which answers a moment
// later, so they are asked for once a minute and the last answers are kept.
namespace Stats
{
	struct Entry
	{
		std::string_view key;   // the name in the run file
		std::string_view stat;  // the game's name for it
	};

	// Starts asking once a minute. Call once, when the game's data has loaded.
	void Start();
	// Ask now (main thread). Used just after a save has loaded.
	void Refresh();
	// Forget the answers: another character is being loaded.
	void Clear();
	// The last answers, in a fixed order. A stat the game has not answered yet is left out.
	[[nodiscard]] std::vector<std::pair<std::string_view, std::int32_t>> Latest();
}
