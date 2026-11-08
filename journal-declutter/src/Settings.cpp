#include "Settings.h"

namespace Settings
{
	void Load()
	{
		const auto store = REX::INI::SettingStore::GetSingleton();
		store->Init(
			"Data/SKSE/Plugins/JournalDeclutter.ini",
			"Data/SKSE/Plugins/JournalDeclutter_Custom.ini");
		store->Load();

		if (bVerboseLog.GetValue()) {
			spdlog::set_level(spdlog::level::debug);
			spdlog::flush_on(spdlog::level::debug);
		}

		logger::info("Settings: hide={} showHidden={} restoreAll={} (shift={}) deactivate={} sounds={} prefix=\"{}\"",
			iHideKey.GetValue(), iShowHiddenKey.GetValue(), iRestoreAllKey.GetValue(), bRestoreAllNeedsShift.GetValue(),
			bDeactivateHiddenQuests.GetValue(), bPlaySounds.GetValue(), sHiddenPrefix.GetValue());
	}
}
