#pragma once

// All settings live in Data/SKSE/Plugins/ZenitharsChallenge.ini.
// Users may override any value in ZenitharsChallenge_Custom.ini (same folder), which survives updates.
namespace Settings
{
	// Rules
	inline REX::INI::Bool bCountOthersKills{ "Rules", "bCountOthersKills", false };

	// Messages (test build: both on)
	inline REX::INI::Bool bShowMadeMessages{ "Messages", "bShowMadeMessages", true };
	inline REX::INI::Bool bShowNotYoursMessages{ "Messages", "bShowNotYoursMessages", true };

	// Debug
	inline REX::INI::Bool bVerboseLog{ "Debug", "bVerboseLog", true };

	void Load();
}
