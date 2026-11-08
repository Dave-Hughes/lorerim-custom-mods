#pragma once

// All settings live in Data/SKSE/Plugins/JournalDeclutter.ini.
// Users may override any value in JournalDeclutter_Custom.ini (same folder), which survives updates.
// Key codes are DirectInput scan codes (the same numbers SkyUI MCMs show): Delete = 211, H = 35. 0 disables a key.
namespace Settings
{
	// Keys
	inline REX::INI::I32  iHideKey{ "Keys", "iHideKey", 211 };
	inline REX::INI::I32  iShowHiddenKey{ "Keys", "iShowHiddenKey", 35 };
	inline REX::INI::I32  iRestoreAllKey{ "Keys", "iRestoreAllKey", 211 };
	inline REX::INI::Bool bRestoreAllNeedsShift{ "Keys", "bRestoreAllNeedsShift", true };

	// Behavior
	inline REX::INI::Bool bDeactivateHiddenQuests{ "Behavior", "bDeactivateHiddenQuests", true };
	inline REX::INI::Bool bPlaySounds{ "Behavior", "bPlaySounds", true };
	inline REX::INI::Str  sHiddenPrefix{ "Behavior", "sHiddenPrefix", "[HIDDEN] " };

	// Debug
	inline REX::INI::Bool bVerboseLog{ "Debug", "bVerboseLog", false };

	void Load();
}
