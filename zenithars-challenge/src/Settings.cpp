#include "Settings.h"

namespace Settings
{
	void Load()
	{
		const auto store = REX::INI::SettingStore::GetSingleton();
		store->Init(
			"Data/SKSE/Plugins/ZenitharsChallenge.ini",
			"Data/SKSE/Plugins/ZenitharsChallenge_Custom.ini");
		store->Load();

		if (bVerboseLog.GetValue()) {
			spdlog::set_level(spdlog::level::debug);
			spdlog::flush_on(spdlog::level::debug);
		}

		logger::info("Settings: othersKills={} warnings={} madeMessages={} notYoursMessages={} verbose={} allowedPlugins=\"{}\"",
			bCountOthersKills.GetValue(), bShowWarnings.GetValue(), bShowMadeMessages.GetValue(), bShowNotYoursMessages.GetValue(),
			bVerboseLog.GetValue(), sAllowedPlugins.GetValue());
	}
}
