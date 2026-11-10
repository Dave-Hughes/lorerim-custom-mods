#include "Choice.h"
#include "Events.h"
#include "InventoryMarks.h"
#include "Ledger.h"
#include "RunFile.h"
#include "Settings.h"

namespace
{
	std::atomic_uint32_t g_turn{ 0 };

	void MessageHandler(SKSE::MessagingInterface::Message* a_msg)
	{
		switch (a_msg->type) {
		case SKSE::MessagingInterface::kDataLoaded:
			Events::Register();
			Events::InstallHooks();
			Choice::Register();
			break;
		case SKSE::MessagingInterface::kSaveGame:
			RunFile::Write();
			break;
		case SKSE::MessagingInterface::kPreLoadGame:
			Events::SetArmed(false);
			break;
		case SKSE::MessagingInterface::kNewGame:
		case SKSE::MessagingInterface::kPostLoadGame:
			{
				Events::GetSingleton()->Reset();
				Events::SetArmed(false);
				const auto loaded = a_msg->type == SKSE::MessagingInterface::kPostLoadGame;
				const auto turn = ++g_turn;
				Choice::SetTurn(turn);
				// The HUD is not up yet while the loading screen is closing, and the game is still
				// dressing the character, so the rules and the hello line wait a few seconds.
				std::thread([loaded, turn]() {
					std::this_thread::sleep_for(6s);
					SKSE::GetTaskInterface()->AddTask([loaded, turn]() {
						if (turn != g_turn) {
							return;  // another save was loaded meanwhile
						}
						Events::TakeStock();
						Events::SetArmed(true);
						if (loaded) {
							Choice::OnLoaded(turn);
						} else {
							Choice::OnNewGame(turn);
						}
						if (loaded && Settings::bShowMadeMessages.GetValue()) {
							const auto text = std::format("Zenithar's Challenge ({}): {} items made, {} rules broken",
								Events::CurrentMode() == Mode::kOath ? "Oath" : "Honor", Ledger::Get().TotalMade(), Ledger::Get().ViolationCount());
							RE::SendHUDMessage::ShowHUDMessage(text.c_str());
						}
					});
				}).detach();
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
	InventoryMarks::Register();

	if (!SKSE::GetMessagingInterface()->RegisterListener(MessageHandler)) {
		logger::critical("failed to register messaging listener");
		return false;
	}

	logger::info("loaded");
	return true;
}
