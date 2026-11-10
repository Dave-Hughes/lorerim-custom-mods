#include "Status.h"

#include "Events.h"
#include "Ledger.h"

namespace
{
	constexpr auto       kPlugin = "ZenitharsChallenge.esp"sv;
	constexpr RE::FormID kAbility = 0x801;
	constexpr RE::FormID kPower = 0x803;

	RE::SpellItem* g_ability{ nullptr };
	RE::SpellItem* g_power{ nullptr };

	// The run so far, for the box the power opens.
	[[nodiscard]] std::string Summary()
	{
		const auto snapshot = Ledger::Get().TakeSnapshot();
		Ledger::MadeCounts totals{};
		for (const auto& [form, counts] : snapshot.made) {
			for (std::size_t i = 0; i < totals.size(); ++i) {
				totals[i] += counts[i];
			}
		}
		std::string allowed;
		const auto  add = [&](std::uint32_t a_bit, std::string_view a_name) {
			if ((snapshot.options & a_bit) != 0) {
				allowed += allowed.empty() ? "" : ", ";
				allowed += a_name;
			}
		};
		add(Option::kLootGems, "looted gems"sv);
		add(Option::kLootSoulGems, "looted soul gems"sv);
		add(Option::kCoinPurseGems, "coin purse gems"sv);
		add(Option::kQuestRewards, "quest rewards"sv);
		add(Option::kStartingItems, "starting gear"sv);
		add(Option::kOthersKills, "others' kills"sv);

		const auto calendar = RE::Calendar::GetSingleton();
		return std::format(
			"Zenithar's Challenge\n\n"
			"Day {:.0f}\n"
			"Crafted {}, cooked {}, brewed {}\n"
			"Mined {}, chopped {}, hunted {}\n\n"
			"Rules broken: {}\n"
			"Allowed: {}\n\n"
			"Deaths: {}. Miles travelled: {:.1f}",
			calendar ? calendar->GetDaysPassed() : 0.0F,
			totals[static_cast<std::size_t>(Source::kCrafted)], totals[static_cast<std::size_t>(Source::kCooked)], totals[static_cast<std::size_t>(Source::kBrewed)],
			totals[static_cast<std::size_t>(Source::kMined)], totals[static_cast<std::size_t>(Source::kChopped)], totals[static_cast<std::size_t>(Source::kHunted)],
			snapshot.violations.size(),
			allowed.empty() ? "nothing, the challenge as written" : allowed,
			snapshot.deaths, snapshot.distance / 112640.0);
	}

	class CastSink : public RE::BSTEventSink<RE::TESSpellCastEvent>
	{
	public:
		RE::BSEventNotifyControl ProcessEvent(const RE::TESSpellCastEvent* a_event, RE::BSTEventSource<RE::TESSpellCastEvent>*) override
		{
			if (a_event && g_power && a_event->spell == g_power->GetFormID() && a_event->object && a_event->object->IsPlayerRef()) {
				SKSE::GetTaskInterface()->AddTask([]() { RE::DebugMessageBox(Summary().c_str()); });
			}
			return RE::BSEventNotifyControl::kContinue;
		}
	};
}

void Status::OnDataLoaded()
{
	if (const auto data = RE::TESDataHandler::GetSingleton()) {
		g_ability = data->LookupForm<RE::SpellItem>(kAbility, kPlugin);
		g_power = data->LookupForm<RE::SpellItem>(kPower, kPlugin);
	}
	if (!g_ability || !g_power) {
		logger::warn("{} is not loaded: the challenge has no Active Effects entry and no Ledger power", kPlugin);
		return;
	}
	static CastSink sink;
	if (const auto holder = RE::ScriptEventSourceHolder::GetSingleton()) {
		holder->AddEventSink<RE::TESSpellCastEvent>(std::addressof(sink));
	}
	logger::info("{} found: ability {:08X}, power {:08X}", kPlugin, g_ability->GetFormID(), g_power->GetFormID());
}

void Status::Sync()
{
	const auto player = RE::PlayerCharacter::GetSingleton();
	if (!player || !g_ability || !g_power) {
		return;
	}
	const auto on = Events::OnChallenge();
	for (const auto spell : { g_ability, g_power }) {
		if (on && !player->HasSpell(spell)) {
			player->AddSpell(spell);
		} else if (!on && player->HasSpell(spell)) {
			player->RemoveSpell(spell);
		}
	}
}
