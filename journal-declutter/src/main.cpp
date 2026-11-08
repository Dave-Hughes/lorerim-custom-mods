#include "HiddenStore.h"
#include "Hooks.h"
#include "InputHandler.h"
#include "Settings.h"

namespace
{
	void MessageHandler(SKSE::MessagingInterface::Message* a_msg)
	{
		switch (a_msg->type) {
		case SKSE::MessagingInterface::kDataLoaded:
			Hooks::Install();
			InputHandler::Register();
			break;
		default:
			break;
		}
	}
}

SKSEPluginLoad(const SKSE::LoadInterface* a_skse)
{
	SKSE::Init(a_skse, { .log = true });

	const auto& module = REL::Module::get();
	logger::info("{} v{} loading on Skyrim {} ({})",
		SKSE::GetPluginName(), SKSE::GetPluginVersion().string(),
		module.version().string(), REL::Module::IsVR() ? "VR" : (REL::Module::IsAE() ? "AE" : "SE"));

	Settings::Load();
	HiddenStore::RegisterSerialization();

	if (!SKSE::GetMessagingInterface()->RegisterListener(MessageHandler)) {
		logger::critical("failed to register messaging listener");
		return false;
	}

	logger::info("loaded");
	return true;
}
