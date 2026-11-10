#pragma once

// All settings live in Data/SKSE/Plugins/ZenitharsChallenge.ini.
// Users may override any value in ZenitharsChallenge_Custom.ini (same folder), which survives updates.
namespace Settings
{
	// Rules
	inline REX::INI::Bool bCountOthersKills{ "Rules", "bCountOthersKills", false };

	// Clothing, jewelry, cloaks and bags must be the player's own too, not only armor.
	inline REX::INI::Bool bRestrictClothing{ "Rules", "bRestrictClothing", true };

	// Items from these plugins may be eaten or drunk whoever made them (water, provisions). Separate names with |
	inline REX::INI::Str sAllowedPlugins{ "Rules", "sAllowedPlugins", "Waterskin.esp|LoreRim Respawn Provision.esp" };

	// Testing aid: "oath" (on the challenge) or "off" (standard playthrough) overrides the save's own choice. Empty = use the save's.
	inline REX::INI::Str sForceMode{ "Rules", "sForceMode", "" };

	// Testing aid: ask an existing character whether it takes the challenge after loading, if it has not been asked.
	inline REX::INI::Bool bAskOnLoad{ "Rules", "bAskOnLoad", false };

	// Messages
	inline REX::INI::Bool bShowWarnings{ "Messages", "bShowWarnings", true };
	inline REX::INI::Bool bShowMadeMessages{ "Messages", "bShowMadeMessages", true };
	inline REX::INI::Bool bShowNotYoursMessages{ "Messages", "bShowNotYoursMessages", false };
	// Mark rows in the inventory and barter screens that hold items which are not yours.
	inline REX::INI::Bool bMarkInventory{ "Messages", "bMarkInventory", true };
	// How: the screen's stolen icon and colour, and/or the words "[not yours]" after the name.
	inline REX::INI::Bool bMarkWithIcon{ "Messages", "bMarkWithIcon", true };
	inline REX::INI::Bool bMarkWithText{ "Messages", "bMarkWithText", false };

	// Debug
	inline REX::INI::Bool bVerboseLog{ "Debug", "bVerboseLog", true };

	void Load();
}
