#include "Events.h"
#include "Ledger.h"
#include "Settings.h"

namespace
{
	void MessageHandler(SKSE::MessagingInterface::Message* a_msg)
	{
		switch (a_msg->type) {
		case SKSE::MessagingInterface::kDataLoaded:
			Events::Register();
			break;
		case SKSE::MessagingInterface::kNewGame:
			Events::GetSingleton()->Reset();
			break;
		case SKSE::MessagingInterface::kPostLoadGame:
			Events::GetSingleton()->Reset();
			if (Settings::bShowMadeMessages.GetValue()) {
				const auto text = std::format("Zenithar's ledger is running: {} items made so far", Ledger::Get().TotalMade());
				RE::SendHUDMessage::ShowHUDMessage(text.c_str());
			}
			break;
		default:
			break;
		}
	}
}

SKSE_PLUGIN_LOAD(const SKSE::LoadInterface* a_skse)
{
	SKSE::Init(a_skse, { .log = true });

	const auto& game = REL::Module::get();
	logger::info("{} v{} loading on Skyrim {} ({})",
		SKSE::GetPluginName(), SKSE::GetPluginVersion().string(),
		game.version().string(), REL::Module::IsVR() ? "VR" : (REL::Module::IsAE() ? "AE" : "SE"));

	Settings::Load();
	Ledger::RegisterSerialization();

	if (!SKSE::GetMessagingInterface()->RegisterListener(MessageHandler)) {
		logger::critical("failed to register messaging listener");
		return false;
	}

	logger::info("loaded");
	return true;
}
