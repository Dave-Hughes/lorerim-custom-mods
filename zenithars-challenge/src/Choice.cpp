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
	void OpenWindow(std::vector<Card> a_cards, int a_max, int a_min, Done a_done)
	{
		g_done = std::move(a_done);
		g_waiting = false;
		CallUI("OpenCustomMenu", RE::MakeFunctionArguments(RE::BSFixedString(kSwf), std::int32_t{ 0 }));
		WaitForWindow(std::move(a_cards), a_max, a_min, 100);
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

	void Box(const char* a_text, std::function<void()> a_next)
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
		Status::Sync();
		g_asking = false;
		logger::info("CHOSEN: {} options {:#04x}", a_mode == Mode::kOath ? "the challenge" : "standard playthrough", a_options);
		RE::DebugMessageBox(a_mode == Mode::kOath ?
				"Your oath is sworn.\n\nThe game will hold you to the rules of Zenithar's Challenge. Use the power Zenithar's Ledger to look over your run." :
				"A standard playthrough.\n\nZenithar's Challenge is off for this character.");
	}

	void AskOptions(Mode a_mode)
	{
		std::vector<Card> cards{
			{ "Looted Gems", "Ticked: gems you take from chests, bodies and shops count as yours.\n\nUnticked: only gems that drop when you mine a vein count.", Picture("LootedGems"sv) },
			{ "Looted Soul Gems", "Ticked: empty soul gems you take from chests, bodies and shops count as yours.\n\nUnticked: only soul gems you craft count.", Picture("LootedSoulGems"sv) },
			{ "Coin Purse Gems", "Ticked: gems that come out of coin purses count as yours.\n\nUnticked: they do not, so toss them.", Picture("CoinPurseGems"sv) },
			{ "Quest Rewards", "Ticked: items handed to you as quest rewards count as yours.\n\nUnticked: only the septims do.", Picture("QuestRewards"sv) },
			{ "Starting Gear", "Ticked: the weapons, armor, jewelry and other gear your start handed you count as yours.\n\nUnticked: they do not, so leave them behind.\n\nThe clothes you start in are yours either way.", Picture("StartingItems"sv) },
			{ "Others' Kills", "Ticked: hides and meat from any dead animal count as hunted.\n\nUnticked: only animals you killed yourself count.", Picture("OthersKills"sv) },
		};
		OpenWindow(std::move(cards), 6, 0, [a_mode](std::vector<int> a_picked) {
			std::uint32_t options = 0;
			for (const auto index : a_picked) {
				if (index >= 0 && index < 6) {
					options |= 1u << index;
				}
			}
			Finish(a_mode, options);
		});
	}

	void AskMode()
	{
		std::vector<Card> cards{
			{ "Zenithar's Challenge", "Craft everything you use, and the game holds you to it.\n\nGear, food and potions you did not make cannot be used. Spells, scrolls and staves cannot be used. What is not yours cannot be sold.\n\nThis cannot be changed later.", Picture("Challenge"sv) },
			{ "Standard Playthrough", "Play LoreRim as usual.\n\nThe challenge is off for this character and nothing is stopped.\n\nThis cannot be changed later.", Picture("Standard"sv) },
		};
		OpenWindow(std::move(cards), 1, 1, [](std::vector<int> a_picked) {
			if (a_picked.empty()) {
				// Confirmed with nothing ticked: ask again.
				AskMode();
				return;
			}
			if (a_picked.front() != 0) {
				Finish(Mode::kOff, 0);
				return;
			}
			const auto mode = Mode::kOath;
			Box("Now choose the rules the challenge leaves up to you.\n\nTick a card to allow it. You may tick none.", [mode]() { AskOptions(mode); });
		});
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
		AskMode();
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
