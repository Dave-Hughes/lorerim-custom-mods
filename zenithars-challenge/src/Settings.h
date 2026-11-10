#pragma once

// All settings live in Data/SKSE/Plugins/ZenitharsChallenge.ini.
// Users may override any value in ZenitharsChallenge_Custom.ini (same folder), which survives updates.
namespace Settings
{
	// Rules
	inline REX::INI::Bool bCountOthersKills{ "Rules", "bCountOthersKills", false };

	// Items from these plugins may be eaten or drunk whoever made them (water, provisions). Separate names with |
	inline REX::INI::Str sAllowedPlugins{ "Rules", "sAllowedPlugins", "Waterskin.esp|LoreRim Respawn Provision.esp" };

	// Testing aid until the character-creation choice exists: "honor" or "oath" overrides the saved mode. Empty = use the save's.
	inline REX::INI::Str sForceMode{ "Rules", "sForceMode", "" };

	// Testing aid: ask an existing character for its mode after loading, if it has none yet.
	inline REX::INI::Bool bAskOnLoad{ "Rules", "bAskOnLoad", false };

	// Messages
	inline REX::INI::Bool bShowWarnings{ "Messages", "bShowWarnings", true };
	inline REX::INI::Bool bShowMadeMessages{ "Messages", "bShowMadeMessages", true };
	inline REX::INI::Bool bShowNotYoursMessages{ "Messages", "bShowNotYoursMessages", false };
	// Experiment: tag rows in the inventory screen that hold items which are not yours.
	inline REX::INI::Bool bMarkInventory{ "Messages", "bMarkInventory", true };

	// Debug
	inline REX::INI::Bool bVerboseLog{ "Debug", "bVerboseLog", true };

	void Load();
}
