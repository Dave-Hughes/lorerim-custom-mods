#include "Stats.h"

#include "Events.h"

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

void Stats::Start()
{
	if (g_started.exchange(true)) {
		return;
	}
	std::thread([]() {
		for (;;) {
			std::this_thread::sleep_for(60s);
			SKSE::GetTaskInterface()->AddTask([]() { Stats::Refresh(); });
		}
	}).detach();
}

void Stats::Clear()
{
	++g_generation;
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
