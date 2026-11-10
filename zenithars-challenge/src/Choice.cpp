#include "Choice.h"

#include "Events.h"
#include "Ledger.h"
#include "Settings.h"
#include "Status.h"

#include <filesystem>

namespace
{
	struct Card
	{
		std::string title;
		std::string text;
		std::string image;  // a path under Interface/
	};

	using Done = std::function<void(std::vector<int>)>;

	constexpr auto kMenu = "CustomMenu"sv;
	constexpr auto kSwf = "b612_traitsmenut"sv;
	constexpr auto kCloseEvent = "b612_TraitsMenu_Close"sv;
	// A card's own picture is Interface/TraitPics/Zenithar_<name>.dds. Until that file exists the
	// card borrows one of LoreRim's trait pictures, so artwork can be dropped in without a new build.
	constexpr auto kBorrowed = "TraitPics/LoreTraits_SelfTaughtAb.dds"sv;

	[[nodiscard]] std::string Picture(std::string_view a_name)
	{
		const auto      own = std::format("TraitPics/Zenithar_{}.dds", a_name);
		std::error_code error;
		if (std::filesystem::exists(std::filesystem::path("Data/Interface") / own, error)) {
			return own;
		}
		return std::string(kBorrowed);
	}

	std::atomic_uint32_t g_turn{ 0 };
	std::atomic_bool     g_asking{ false };

	// The window now open and waiting for the player. Main thread, except g_waiting.
	std::atomic_bool g_waiting{ false };
	Done             g_done;

	// Runs a_fn on the main thread after a delay.
	void After(std::chrono::milliseconds a_delay, std::function<void()> a_fn)
	{
		std::thread([a_delay, fn = std::move(a_fn)]() mutable {
			std::this_thread::sleep_for(a_delay);
			SKSE::GetTaskInterface()->AddTask(std::move(fn));
		}).detach();
	}

	void CallUI(const char* a_function, RE::BSScript::IFunctionArguments* a_args)
	{
		const auto vm = RE::BSScript::Internal::VirtualMachine::GetSingleton();
		if (!vm) {
			delete a_args;
			return;
		}
		RE::BSTSmartPointer<RE::BSScript::IStackCallbackFunctor> callback;
		vm->DispatchStaticCall("UI", a_function, a_args, callback);
	}

	[[nodiscard]] bool HasWindowFile()
	{
		const auto data = RE::TESDataHandler::GetSingleton();
		return data && data->LookupModByName("b612.esp"sv);
	}

	[[nodiscard]] bool MenuOpen(std::string_view a_name)
	{
		const auto ui = RE::UI::GetSingleton();
		return ui && ui->IsMenuOpen(a_name);
	}

	[[nodiscard]] std::vector<int> ParseIndexes(std::string_view a_text)
	{
		std::vector<int> result;
		int              value = -1;
		for (const auto ch : a_text) {
			if (ch >= '0' && ch <= '9') {
				value = (value < 0 ? 0 : value * 10) + (ch - '0');
			} else if (value >= 0) {
				result.push_back(value);
				value = -1;
			}
		}
		if (value >= 0) {
			result.push_back(value);
		}
		return result;
	}

	// Fills the open window with its cards and shows it. False if the window is not ready yet.
	[[nodiscard]] bool Populate(const std::vector<Card>& a_cards, int a_max, int a_min)
	{
		const auto                       ui = RE::UI::GetSingleton();
		RE::GPtr<RE::GFxMovieView> movie;
		if (ui) {
			movie = ui->GetMovieView(kMenu);
		}
		if (!movie) {
			return false;
		}
		RE::GFxValue loaded;
		if (!movie->GetVariable(&loaded, "_root.isLoaded") || !loaded.IsBool() || !loaded.GetBool()) {
			return false;
		}
		for (const auto& card : a_cards) {
			std::array<RE::GFxValue, 3> args;
			movie->CreateString(&args[0], card.title.c_str());
			movie->CreateString(&args[1], card.text.c_str());
			movie->CreateString(&args[2], card.image.c_str());
			movie->Invoke("_root.Traits_mc.addItem", nullptr, args.data(), 3);
		}
		const RE::GFxValue maxValue{ static_cast<double>(a_max) };
		const RE::GFxValue minValue{ static_cast<double>(a_min) };
		movie->Invoke("_root.Traits_mc.setMaxSelection", nullptr, &maxValue, 1);
		movie->Invoke("_root.Traits_mc.setMinSelection", nullptr, &minValue, 1);
		movie->Invoke("_root.Traits_mc.render", nullptr, nullptr, 0);
		return true;
	}

	void WaitForWindow(std::vector<Card> a_cards, int a_max, int a_min, int a_triesLeft)
	{
		After(100ms, [cards = std::move(a_cards), a_max, a_min, a_triesLeft]() mutable {
			if (Populate(cards, a_max, a_min)) {
				g_waiting = true;
				return;
			}
			if (a_triesLeft > 0) {
				WaitForWindow(std::move(cards), a_max, a_min, a_triesLeft - 1);
				return;
			}
			logger::warn("the choice window did not open; the character has not been asked");
			g_done = nullptr;
			g_asking = false;
		});
	}

	// Opens the card window. a_done gets the indexes of the cards the player ticked.
	// The card window is one shared window: LoreRim's traits use it, and so do other mods (Callings
	// asks its own question at the same moment a new character is ready). Two mods filling it at
	// once end up in one list. So ours waits its turn: the window and any message box must have
	// been closed for three seconds running before it opens.
	void OpenWhenQuiet(std::vector<Card> a_cards, int a_max, int a_min, int a_quietChecks)
	{
		After(500ms, [cards = std::move(a_cards), a_max, a_min, a_quietChecks]() mutable {
			const auto busy = MenuOpen(kMenu) || MenuOpen(RE::MessageBoxMenu::MENU_NAME);
			if (busy || a_quietChecks < 6) {
				OpenWhenQuiet(std::move(cards), a_max, a_min, busy ? 0 : a_quietChecks + 1);
				return;
			}
			CallUI("OpenCustomMenu", RE::MakeFunctionArguments(RE::BSFixedString(kSwf), std::int32_t{ 0 }));
			WaitForWindow(std::move(cards), a_max, a_min, 100);
		});
	}

	// Runs a_next once the card window and any message box have been closed for three seconds running.
	void WhenQuiet(std::function<void()> a_next, int a_quietChecks = 0)
	{
		After(500ms, [next = std::move(a_next), a_quietChecks]() mutable {
			const auto busy = MenuOpen(kMenu) || MenuOpen(RE::MessageBoxMenu::MENU_NAME);
			if (busy || a_quietChecks < 6) {
				WhenQuiet(std::move(next), busy ? 0 : a_quietChecks + 1);
				return;
			}
			next();
		});
	}

	void OpenWindow(std::vector<Card> a_cards, int a_max, int a_min, Done a_done)
	{
		g_done = std::move(a_done);
		g_waiting = false;
		OpenWhenQuiet(std::move(a_cards), a_max, a_min, 0);
	}

	// Shows a message box, then runs a_next once the player has closed it.
	void WaitForBoxClosed(std::function<void()> a_next, int a_triesLeft)
	{
		After(250ms, [next = std::move(a_next), a_triesLeft]() mutable {
			if (MenuOpen(RE::MessageBoxMenu::MENU_NAME) && a_triesLeft > 0) {
				WaitForBoxClosed(std::move(next), a_triesLeft - 1);
				return;
			}
			next();
		});
	}

	[[maybe_unused]] void Box(const char* a_text, std::function<void()> a_next)
	{
		RE::DebugMessageBox(a_text);
		// Give the box a moment to open before watching for it to close.
		After(500ms, [next = std::move(a_next)]() mutable { WaitForBoxClosed(std::move(next), 2400); });
	}

	void Finish(Mode a_mode, std::uint32_t a_options)
	{
		auto& ledger = Ledger::Get();
		ledger.SetMode(a_mode);
		ledger.SetOptions(a_options);
		ledger.SetChoicePending(false);
		// Whatever the character carries is counted first, then kept if the player chose that.
		Events::TakeStock();
		if ((a_options & Option::kStartingItems) != 0) {
			Events::KeepPack();
		} else if (a_mode == Mode::kOath) {
			// The clothes on the character's back are theirs either way. A start's weapons, armor,
			// jewelry and other gear are not.
			Events::KeepStartingClothes();
		}
		if (a_mode == Mode::kOath) {
			// A character has to eat: the food it has now, and what it picks up before leaving this room, is its own.
			Events::BeginStartingGrace();
		}
		Status::Sync();
		g_asking = false;
		logger::info("CHOSEN: {} options {:#04x}", a_mode == Mode::kOath ? "the challenge" : "standard playthrough", a_options);
		RE::SendHUDMessage::ShowHUDMessage(a_mode == Mode::kOath ? "Zenithar's Challenge: your oath is sworn." : "Zenithar's Challenge is off for this character.");
	}

	// Second step: the six rules the challenge leaves to the player, as cards. Each card is a
	// permission, worded the same way: tick to allow it.
	void AskOptions()
	{
		const auto card = [](const char* a_title, const char* a_allowed, const char* a_not, std::string_view a_picture) {
			return Card{ a_title, std::format("Tick this card to allow it.\n\nAllowed: {}\n\nNot allowed: {}", a_allowed, a_not), Picture(a_picture) };
		};
		std::vector<Card> cards{
			card("Allow looted gems", "gems you take from chests, bodies and shops are yours to use and sell.", "only gems you mine yourself are yours.", "LootedGems"sv),
			card("Allow looted soul gems", "soul gems you take from chests, bodies and shops are yours.", "only soul gems you make yourself are yours.", "LootedSoulGems"sv),
			card("Allow coin purse gems", "a gem that comes out of a coin purse is yours.", "it is not. Toss it.", "CoinPurseGems"sv),
			card("Allow quest rewards", "an item handed to you as a quest reward is yours.", "only the septims are.", "QuestRewards"sv),
			card("Allow starting gear", "the weapons, armor, jewelry and other gear your start gave you are yours.", "they are not. Leave them behind. Your clothes and your first food are yours either way.", "StartingItems"sv),
			card("Allow others' kills", "hides and meat from any dead animal count as hunted.", "only animals you killed yourself count.", "OthersKills"sv),
		};
		OpenWindow(std::move(cards), 6, 0, [](std::vector<int> a_picked) {
			std::uint32_t options = 0;
			for (const auto index : a_picked) {
				if (index >= 0 && index < 6) {
					options |= 1u << index;
				}
			}
			Finish(Mode::kOath, options);
		});
	}

	// The answer to the first box. The game calls this with the number of the button pressed.
	void OnIntroButton(std::uint8_t a_button)
	{
		logger::info("the first box was answered with button {}", a_button);
		SKSE::GetTaskInterface()->AddTask([a_button]() {
			if (a_button == 0) {
				AskOptions();
			} else {
				Finish(Mode::kOff, 0);
			}
		});
	}

	// First step: an ordinary message box that says what the challenge is and what comes next.
	void AskMode()
	{
		RE::MessageBoxMenu::Create(
			"Zenithar's Challenge\n\n"
			"You can only use what you craft. Gear, food and potions you did not make cannot be used or sold.\n\n"
			"You cannot use magic, scrolls, or staves.\n\n"
			"Next, turn on optional rules to make the challenge easier.",
			&OnIntroButton, 0, 4, 10, "Take the challenge", "Standard playthrough");
	}

	void Begin()
	{
		if (g_asking.exchange(true)) {
			return;
		}
		if (Ledger::Get().StoredMode() != Mode::kUnset) {
			g_asking = false;
			return;
		}
		if (!HasWindowFile()) {
			logger::warn("b612.esp is not loaded, so the choice window cannot be shown; the character has not been asked");
			g_asking = false;
			return;
		}
		logger::info("asking the character whether it takes the challenge");
		WhenQuiet([]() { AskMode(); });
	}

	// LoreRim's character setup ends by giving the player this spell for a short while.
	[[nodiscard]] bool SetupOver()
	{
		const auto player = RE::PlayerCharacter::GetSingleton();
		const auto data = RE::TESDataHandler::GetSingleton();
		if (!player || !data) {
			return false;
		}
		if (player->GetLevel() > 1) {
			return true;
		}
		const auto cue = data->LookupForm<RE::SpellItem>(0x804, "LoreRim Startup.esp"sv);
		return cue && player->HasSpell(cue);
	}

	// Checks every few seconds until the character can be asked.
	void Watch(std::uint32_t a_turn, int a_checksWithoutCue)
	{
		After(3s, [a_turn, a_checksWithoutCue]() {
			if (a_turn != g_turn || Ledger::Get().StoredMode() != Mode::kUnset) {
				return;
			}
			const auto ui = RE::UI::GetSingleton();
			const auto busy = !ui || ui->GameIsPaused() || ui->IsMenuOpen(RE::RaceSexMenu::MENU_NAME);
			const auto data = RE::TESDataHandler::GetSingleton();
			const auto lorerim = data && data->LookupModByName("LoreRim Startup.esp"sv);
			// Outside LoreRim there is no cue: ask about a minute and a half into the new game.
			const auto ready = lorerim ? SetupOver() : a_checksWithoutCue >= 30;
			if (!busy && ready) {
				After(4s, []() { Begin(); });
				return;
			}
			Watch(a_turn, a_checksWithoutCue + 1);
		});
	}

	class CloseSink : public RE::BSTEventSink<SKSE::ModCallbackEvent>
	{
	public:
		RE::BSEventNotifyControl ProcessEvent(const SKSE::ModCallbackEvent* a_event, RE::BSTEventSource<SKSE::ModCallbackEvent>*) override
		{
			if (!a_event || !g_waiting || a_event->eventName != kCloseEvent) {
				return RE::BSEventNotifyControl::kContinue;
			}
			g_waiting = false;
			auto picked = ParseIndexes(a_event->strArg.c_str());
			SKSE::GetTaskInterface()->AddTask([picked = std::move(picked)]() mutable {
				CallUI("CloseCustomMenu", RE::MakeFunctionArguments());
				auto done = std::move(g_done);
				g_done = nullptr;
				if (done) {
					// Let the window close before the next one opens.
					After(600ms, [done = std::move(done), picked = std::move(picked)]() mutable { done(std::move(picked)); });
				}
			});
			return RE::BSEventNotifyControl::kContinue;
		}
	};
}

void Choice::Register()
{
	static CloseSink sink;
	if (const auto source = SKSE::GetModCallbackEventSource()) {
		source->AddEventSink(std::addressof(sink));
	}
}

void Choice::SetTurn(std::uint32_t a_turn)
{
	g_turn = a_turn;
	g_asking = false;
	g_waiting = false;
}

void Choice::OnNewGame(std::uint32_t a_turn)
{
	Ledger::Get().SetChoicePending(true);
	Watch(a_turn, 0);
}

void Choice::OnLoaded(std::uint32_t a_turn)
{
	auto& ledger = Ledger::Get();
	if (ledger.StoredMode() != Mode::kUnset) {
		return;
	}
	if (ledger.ChoicePending()) {
		Watch(a_turn, 0);
	} else if (Settings::bAskOnLoad.GetValue()) {
		After(3s, []() { Begin(); });
	}
}
