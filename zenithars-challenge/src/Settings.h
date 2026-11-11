#pragma once

// All settings live in Data/SKSE/Plugins/ZenitharsChallenge.ini.
// Users may override any value in ZenitharsChallenge_Custom.ini (same folder), which survives updates.
namespace Settings
{
	// The rules themselves are not settings: anything that loosened them here would not show in the
	// run file. What the challenge leaves to the player is chosen in the game, once, and is written to the run.

	// Ask an existing character whether it takes the challenge after loading, if it has not been asked.
	// Such a character gets none of a new character's allowances, and its run says when it joined.
	inline REX::INI::Bool bAskOnLoad{ "Rules", "bAskOnLoad", false };

	// Messages
	inline REX::INI::Bool bShowWarnings{ "Messages", "bShowWarnings", true };
	inline REX::INI::Bool bShowMadeMessages{ "Messages", "bShowMadeMessages", true };
	inline REX::INI::Bool bShowNotYoursMessages{ "Messages", "bShowNotYoursMessages", false };
	// Mark rows in the inventory and barter screens that hold items which are not yours.
	inline REX::INI::Bool bMarkInventory{ "Messages", "bMarkInventory", true };
	// How: a struck-through anvil after the name and a dull red name, and/or the words "[not yours]" after the name.
	inline REX::INI::Bool bMarkWithIcon{ "Messages", "bMarkWithIcon", true };
	inline REX::INI::Bool bMarkWithText{ "Messages", "bMarkWithText", false };

	// Debug
	inline REX::INI::Bool bVerboseLog{ "Debug", "bVerboseLog", true };

	void Load();
}
