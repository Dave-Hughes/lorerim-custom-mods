#include "Stats.h"

#include "Events.h"
#include "Ledger.h"

namespace
{
	constexpr std::array kEntries{
		Stats::Entry{ "locationsDiscovered"sv, "Locations Discovered"sv },
		Stats::Entry{ "dungeonsCleared"sv, "Dungeons Cleared"sv },
		Stats::Entry{ "standingStonesFound"sv, "Standing Stones Found"sv },
		Stats::Entry{ "chestsLooted"sv, "Chests Looted"sv },
		Stats::Entry{ "questsCompleted"sv, "Quests Completed"sv },
		Stats::Entry{ "miscObjectivesCompleted"sv, "Misc Objectives Completed"sv },
		Stats::Entry{ "goldFound"sv, "Gold Found"sv },
		Stats::Entry{ "mostGoldCarried"sv, "Most Gold Carried"sv },
		Stats::Entry{ "barters"sv, "Barters"sv },
		Stats::Entry{ "peopleKilled"sv, "People Killed"sv },
		Stats::Entry{ "animalsKilled"sv, "Animals Killed"sv },
		Stats::Entry{ "creaturesKilled"sv, "Creatures Killed"sv },
		Stats::Entry{ "undeadKilled"sv, "Undead Killed"sv },
		Stats::Entry{ "daedraKilled"sv, "Daedra Killed"sv },
		Stats::Entry{ "automatonsKilled"sv, "Automatons Killed"sv },
		Stats::Entry{ "dragonSoulsCollected"sv, "Dragon Souls Collected"sv },
		Stats::Entry{ "weaponsMade"sv, "Weapons Made"sv },
		Stats::Entry{ "armorMade"sv, "Armor Made"sv },
		Stats::Entry{ "potionsMixed"sv, "Potions Mixed"sv },
		Stats::Entry{ "poisonsMixed"sv, "Poisons Mixed"sv },
		Stats::Entry{ "ingredientsHarvested"sv, "Ingredients Harvested"sv },
		Stats::Entry{ "foodEaten"sv, "Food Eaten"sv },
		Stats::Entry{ "booksRead"sv, "Books Read"sv },
		Stats::Entry{ "skillIncreases"sv, "Skill Increases"sv },
		Stats::Entry{ "hoursSlept"sv, "Hours Slept"sv },
		Stats::Entry{ "locksPicked"sv, "Locks Picked"sv },
		Stats::Entry{ "sneakAttacks"sv, "Sneak Attacks"sv },
		Stats::Entry{ "criticalStrikes"sv, "Critical Strikes"sv },
	};

	constexpr std::int32_t kUnknown = -1;

	std::mutex                                 g_lock;
	std::array<std::int32_t, kEntries.size()>  g_values = []() {
		std::array<std::int32_t, kEntries.size()> values{};
		values.fill(kUnknown);
		return values;
	}();
	std::atomic_uint32_t g_generation{ 0 };
	std::atomic_bool     g_started{ false };

	// Receives one answer from the script system.
	class Answer : public RE::BSScript::IStackCallbackFunctor
	{
	public:
		Answer(std::size_t a_index, std::uint32_t a_generation) :
			_index(a_index), _generation(a_generation)
		{}

		void operator()(RE::BSScript::Variable a_result) override
		{
			if (_generation != g_generation || !a_result.IsInt()) {
				return;
			}
			std::scoped_lock lock(g_lock);
			g_values[_index] = a_result.GetSInt();
		}

		void SetObject(const RE::BSTSmartPointer<RE::BSScript::Object>&) override {}

	private:
		std::size_t   _index;
		std::uint32_t _generation;
	};
}

void Stats::Refresh()
{
	const auto vm = RE::BSScript::Internal::VirtualMachine::GetSingleton();
	const auto player = RE::PlayerCharacter::GetSingleton();
	if (!vm || !player || !player->Is3DLoaded() || !Events::OnChallenge()) {
		return;
	}
	const auto generation = g_generation.load();
	for (std::size_t i = 0; i < kEntries.size(); ++i) {
		RE::BSTSmartPointer<RE::BSScript::IStackCallbackFunctor> callback{ new Answer(i, generation) };
		const auto args = RE::MakeFunctionArguments(RE::BSFixedString(kEntries[i].stat));
		vm->DispatchStaticCall("Game", "QueryStat", args, callback);
	}
}

namespace
{
	// Where the character was a second ago, and whether it was on its feet. Main thread only.
	struct Step
	{
		bool         known{ false };
		RE::NiPoint3 at{};
		RE::FormID   space{ 0 };  // the worldspace, or the interior cell
		bool         down{ false };
	};
	Step g_step;

	// Further than this in one second is a door, a cart or fast travel, not travel on foot or horse.
	constexpr float kMaxStep = 2500.0F;

	void Walk()
	{
		const auto player = RE::PlayerCharacter::GetSingleton();
		const auto ui = RE::UI::GetSingleton();
		if (!player || !ui || !player->Is3DLoaded() || !Events::OnChallenge()) {
			g_step.known = false;
			return;
		}

		// Going down: dead, dying, or dropped (LoreRim's respawn mod drops the character and brings it back).
		const auto state = player->AsActorState();
		const auto life = state ? state->GetLifeState() : RE::ACTOR_LIFE_STATE::kAlive;
		const auto down = life == RE::ACTOR_LIFE_STATE::kDying || life == RE::ACTOR_LIFE_STATE::kDead || life == RE::ACTOR_LIFE_STATE::kEssentialDown ||
						  life == RE::ACTOR_LIFE_STATE::kBleedout;
		if (down && !g_step.down) {
			Ledger::Get().NoteDeath();
			logger::info("the character went down (life state {})", static_cast<int>(life));
		}
		g_step.down = down;

		if (ui->GameIsPaused()) {
			return;
		}
		const auto world = player->GetWorldspace();
		const auto cell = player->GetParentCell();
		const auto space = world ? world->GetFormID() : (cell ? cell->GetFormID() : 0);
		const auto at = player->GetPosition();
		if (g_step.known && space == g_step.space) {
			const auto moved = at.GetDistance(g_step.at);
			if (moved > 1.0F && moved <= kMaxStep) {
				Ledger::Get().AddDistance(moved);
			}
		}
		g_step.known = true;
		g_step.at = at;
		g_step.space = space;
	}
}

void Stats::Start()
{
	if (g_started.exchange(true)) {
		return;
	}
	std::thread([]() {
		for (int second = 1;; ++second) {
			std::this_thread::sleep_for(1s);
			SKSE::GetTaskInterface()->AddTask([]() { Walk(); });
			if (second % 60 == 0) {
				SKSE::GetTaskInterface()->AddTask([]() { Stats::Refresh(); });
			}
		}
	}).detach();
}

void Stats::Clear()
{
	++g_generation;
	SKSE::GetTaskInterface()->AddTask([]() { g_step = {}; });
	std::scoped_lock lock(g_lock);
	g_values.fill(kUnknown);
}

std::vector<std::pair<std::string_view, std::int32_t>> Stats::Latest()
{
	std::vector<std::pair<std::string_view, std::int32_t>> result;
	std::scoped_lock                                       lock(g_lock);
	for (std::size_t i = 0; i < kEntries.size(); ++i) {
		if (g_values[i] != kUnknown) {
			result.emplace_back(kEntries[i].key, g_values[i]);
		}
	}
	return result;
}
