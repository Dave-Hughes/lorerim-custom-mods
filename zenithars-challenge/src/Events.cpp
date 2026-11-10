#include "Events.h"

#include "Ledger.h"
#include "Settings.h"

namespace
{
	constexpr RE::FormID kGold = 0x0000000F;
	constexpr RE::FormID kLockpick = 0x0000000A;
	constexpr RE::FormID kFirewood = 0x0006F993;  // Skyrim.esm Firewood01
	constexpr RE::FormID kUnarmed = 0x000001F4;   // Skyrim.esm Unarmed (fists)

	// The game does not say which reference a dropped item became, so earned copies lying on the
	// ground are pooled per item under one stand-in holder. Picking the item up off the ground
	// draws from the pool. The number of earned copies never grows this way.
	constexpr RE::FormID kGround = 0xFFFFFFFF;

	enum class Bench
	{
		kNone,     // no crafting menu open
		kMake,     // forge, smelter, tanning rack, cooking pot, alchemy lab: new items
		kImprove,  // grindstone, armor workbench, enchanter: the same item comes back
	};

	using Clock = std::chrono::steady_clock;

	std::mutex                                   g_benchLock;
	std::unordered_map<RE::FormID, std::int32_t> g_benchPending;  // item -> earned copies put on an improvement bench

	// The game reports an item picked up off the ground as arriving from nowhere. The activation
	// just before it names the reference, so the last world item the player activated is kept.
	struct Activated
	{
		RE::FormID        ref{ 0 };
		RE::FormID        base{ 0 };
		Clock::time_point when{};
	};
	std::mutex g_activatedLock;
	Activated  g_activated;
	constexpr auto kPickupWindow = 3s;

	// Eating or drinking is reported twice a few milliseconds apart for a single item (seen in
	// LoreRim 5.0.4.8). The second report within this window is ignored.
	struct UsedUp
	{
		RE::FormID        form{ 0 };
		std::int32_t      count{ 0 };
		Clock::time_point when{};
	};
	std::mutex g_usedLock;
	UsedUp     g_lastUsed;
	constexpr auto kDoubleReportWindow = 150ms;

	std::atomic_bool g_armed{ false };

	// The same equip can be reported more than once; a repeat within this window is ignored.
	struct Equipped
	{
		RE::FormID        form{ 0 };
		Clock::time_point when{};
	};
	std::mutex g_equipLock;
	Equipped   g_lastEquip;
	constexpr auto kEquipRepeatWindow = 2s;

	[[nodiscard]] RE::TESObjectREFRPtr OccupiedFurniture()
	{
		const auto player = RE::PlayerCharacter::GetSingleton();
		if (!player) {
			return nullptr;
		}
		return player->GetOccupiedFurniture().get();
	}

	[[nodiscard]] Bench CurrentBench()
	{
		const auto ui = RE::UI::GetSingleton();
		if (!ui || !ui->IsMenuOpen(RE::CraftingMenu::MENU_NAME)) {
			return Bench::kNone;
		}
		const auto ref = OccupiedFurniture();
		const auto base = ref ? ref->GetBaseObject() : nullptr;
		const auto furniture = base ? base->As<RE::TESFurniture>() : nullptr;
		if (!furniture) {
			return Bench::kMake;
		}
		using BenchType = RE::TESFurniture::WorkBenchData::BenchType;
		switch (furniture->workBenchData.benchType.get()) {
		case BenchType::kSmithingWeapon:
		case BenchType::kSmithingArmor:
		case BenchType::kEnchanting:
		case BenchType::kEnchantingExperiment:
			return Bench::kImprove;
		default:
			return Bench::kMake;
		}
	}

	[[nodiscard]] bool HasKeyword(const RE::TESForm* a_item, std::string_view a_editorID)
	{
		const auto keywords = a_item ? a_item->As<RE::BGSKeywordForm>() : nullptr;
		return keywords && keywords->HasKeywordString(a_editorID);
	}

	[[nodiscard]] bool IsOreOrGem(const RE::TESForm* a_item)
	{
		return HasKeyword(a_item, "VendorItemOreIngot"sv) || HasKeyword(a_item, "VendorItemGem"sv);
	}

	[[nodiscard]] bool IsGear(const RE::TESForm* a_item)
	{
		return a_item && (a_item->Is(RE::FormType::Weapon) || a_item->Is(RE::FormType::Armor) || a_item->Is(RE::FormType::Ammo));
	}

	// Items the challenge says must be the player's own to use: worth a "not yours" line.
	[[nodiscard]] bool IsRestricted(const RE::TESForm* a_item)
	{
		return a_item && (IsGear(a_item) || a_item->Is(RE::FormType::AlchemyItem) || a_item->GetFormID() == kFirewood ||
							 HasKeyword(a_item, "VendorItemOreIngot"sv));
	}

	[[nodiscard]] Source MadeSource(RE::TESForm* a_item)
	{
		if (const auto alch = a_item ? a_item->As<RE::AlchemyItem>() : nullptr) {
			return alch->IsFood() ? Source::kCooked : Source::kBrewed;
		}
		return Source::kCrafted;
	}

	[[nodiscard]] bool IsHuntable(RE::Actor* a_actor)
	{
		return a_actor && (a_actor->HasKeywordString("ActorTypeAnimal"sv) || a_actor->HasKeywordString("ActorTypeCreature"sv) ||
							  a_actor->HasKeywordString("ActorTypeDragon"sv));
	}

	// True when the container is the body of an animal the player hunted.
	[[nodiscard]] bool IsHuntedBody(RE::FormID a_container)
	{
		const auto actor = RE::TESForm::LookupByID<RE::Actor>(a_container);
		if (!actor || !actor->IsDead() || !IsHuntable(actor)) {
			return false;
		}
		return Settings::bCountOthersKills.GetValue() || Ledger::Get().WasKilledByPlayer(a_container);
	}

	[[nodiscard]] std::string ItemName(const RE::TESForm* a_item)
	{
		const auto name = a_item ? a_item->GetName() : nullptr;
		if (name && name[0] != '\0') {
			return name;
		}
		return std::format("item {:08X}", a_item ? a_item->GetFormID() : 0);
	}

	void ShowMade(const RE::TESForm* a_item, std::int32_t a_count, Source a_source)
	{
		if (Settings::bShowMadeMessages.GetValue()) {
			const auto text = std::format("Zenithar: {} x{} {}", ItemName(a_item), a_count, SourceVerb(a_source));
			RE::SendHUDMessage::ShowHUDMessage(text.c_str());
		}
	}

	void ShowNotYours(const RE::TESForm* a_item, std::int32_t a_count)
	{
		if (a_count > 0 && Settings::bShowNotYoursMessages.GetValue() && IsRestricted(a_item)) {
			const auto text = std::format("Zenithar: {} x{} is not yours", ItemName(a_item), a_count);
			RE::SendHUDMessage::ShowHUDMessage(text.c_str());
		}
	}

	void Mint(RE::TESForm* a_item, std::int32_t a_count, Source a_source)
	{
		Ledger::Get().Mint(a_item->GetFormID(), a_count, a_source);
		ShowMade(a_item, a_count, a_source);
		logger::debug("pack +{} {} [{:08X}] {} -> yours {}", a_count, ItemName(a_item), a_item->GetFormID(), SourceVerb(a_source),
			Ledger::Get().Earned(Ledger::kPlayer, a_item->GetFormID()));
	}

	[[nodiscard]] bool ContainsNoCase(std::string_view a_text, std::string_view a_part)
	{
		const auto it = std::search(a_text.begin(), a_text.end(), a_part.begin(), a_part.end(), [](char a_lhs, char a_rhs) {
			return std::tolower(static_cast<unsigned char>(a_lhs)) == std::tolower(static_cast<unsigned char>(a_rhs));
		});
		return it != a_text.end();
	}

	[[nodiscard]] bool EqualsNoCase(std::string_view a_lhs, std::string_view a_rhs)
	{
		return a_lhs.size() == a_rhs.size() && ContainsNoCase(a_lhs, a_rhs);
	}

	// The plugin that first defined the item ("" for items made while playing, such as brewed potions).
	[[nodiscard]] std::string_view DefiningPlugin(const RE::TESForm* a_item)
	{
		const auto file = a_item ? a_item->GetFile(0) : nullptr;
		return file ? file->GetFilename() : ""sv;
	}

	// Water and provisions: may be consumed whoever made them.
	[[nodiscard]] bool IsFromAllowedPlugin(const RE::TESForm* a_item)
	{
		const auto plugin = DefiningPlugin(a_item);
		if (plugin.empty()) {
			return false;
		}
		const auto       list = Settings::sAllowedPlugins.GetValue();
		std::string_view rest{ list };
		while (!rest.empty()) {
			const auto bar = rest.find('|');
			auto       name = rest.substr(0, bar);
			rest = bar == std::string_view::npos ? ""sv : rest.substr(bar + 1);
			while (!name.empty() && name.front() == ' ') {
				name.remove_prefix(1);
			}
			while (!name.empty() && name.back() == ' ') {
				name.remove_suffix(1);
			}
			if (!name.empty() && EqualsNoCase(plugin, name)) {
				return true;
			}
		}
		return false;
	}

	[[nodiscard]] bool IsMissive(const RE::TESForm* a_item)
	{
		return a_item && a_item->Is(RE::FormType::Book) && ContainsNoCase(DefiningPlugin(a_item), "missive"sv);
	}

	// The challenge lets the player get a pickaxe however they like; the woodcutter's axe is treated the same.
	[[nodiscard]] bool IsTool(const RE::TESForm* a_item)
	{
		const auto name = ItemName(a_item);
		return ContainsNoCase(name, "pickaxe"sv) || ContainsNoCase(name, "woodcutter"sv);
	}

	// Raw materials the challenge says the player must gather: ore and ingots, firewood, hides and leather.
	[[nodiscard]] bool IsGatheredMaterial(const RE::TESForm* a_item)
	{
		return a_item && (a_item->GetFormID() == kFirewood || HasKeyword(a_item, "VendorItemOreIngot"sv) ||
							 HasKeyword(a_item, "VendorItemAnimalHide"sv));
	}

	[[nodiscard]] float DaysPassed()
	{
		const auto calendar = RE::Calendar::GetSingleton();
		return calendar ? calendar->GetDaysPassed() : 0.0F;
	}

	// Records a broken rule and tells the player.
	void Break(Rule a_rule, const RE::TESForm* a_item, std::int32_t a_count, const std::string& a_text)
	{
		const auto total = Ledger::Get().Break(a_rule, a_item ? a_item->GetFormID() : 0, a_count, DaysPassed());
		logger::info("RULE BROKEN #{} (rule {}): {}", total, static_cast<int>(a_rule), a_text);
		if (Settings::bShowWarnings.GetValue()) {
			const auto text = std::format("Zenithar: {}", a_text);
			RE::SendHUDMessage::ShowHUDMessage(text.c_str());
		}
	}

	// The reference in the event, when the item is (or was) lying in the world.
	struct WorldRef
	{
		bool       present{ false };
		RE::FormID id{ 0 };
	};

	[[nodiscard]] WorldRef GetWorldRef(const RE::TESContainerChangedEvent& a_event)
	{
		WorldRef result;
		if (a_event.reference) {
			result.present = true;
			if (const auto ref = a_event.reference.get()) {
				result.id = ref->GetFormID();
			}
		}
		return result;
	}
}

Events* Events::GetSingleton()
{
	static Events instance;
	return std::addressof(instance);
}

void Events::Register()
{
	if (const auto holder = RE::ScriptEventSourceHolder::GetSingleton()) {
		holder->AddEventSink<RE::TESContainerChangedEvent>(GetSingleton());
		holder->AddEventSink<RE::TESDeathEvent>(GetSingleton());
		holder->AddEventSink<RE::TESActivateEvent>(GetSingleton());
		holder->AddEventSink<RE::TESEquipEvent>(GetSingleton());
		logger::info("event sinks registered");
	} else {
		logger::critical("ScriptEventSourceHolder not available; the ledger will not run");
	}
}

void Events::SetArmed(bool a_armed)
{
	g_armed = a_armed;
	logger::debug("rules {}", a_armed ? "armed" : "disarmed");
}

void Events::Reset()
{
	{
		std::scoped_lock lock(g_equipLock);
		g_lastEquip = {};
	}
	{
		std::scoped_lock lock(g_benchLock);
		g_benchPending.clear();
	}
	{
		std::scoped_lock lock(g_activatedLock);
		g_activated = {};
	}
	{
		std::scoped_lock lock(g_usedLock);
		g_lastUsed = {};
	}
}

void Events::OnAddedToPack(const RE::TESContainerChangedEvent& a_event, RE::TESForm* a_item)
{
	auto&      ledger = Ledger::Get();
	const auto form = a_event.baseObj;
	const auto count = a_event.itemCount;

	if (g_armed && IsMissive(a_item)) {
		Break(Rule::kMissive, a_item, count, "Missives are not allowed.");
	}

	// From a container, a merchant or a body.
	if (a_event.oldContainer != 0) {
		if (!IsGear(a_item) && IsHuntedBody(a_event.oldContainer)) {
			ledger.Consume(a_event.oldContainer, form, count);
			Mint(a_item, count, Source::kHunted);
			return;
		}
		const auto earned = ledger.Transfer(a_event.oldContainer, Ledger::kPlayer, form, count);
		logger::debug("pack +{} {} [{:08X}] from {:08X}: {} yours", count, ItemName(a_item), form, a_event.oldContainer, earned);
		ShowNotYours(a_item, count - earned);
		return;
	}

	// Picked up off the ground.
	if (const auto world = GetWorldRef(a_event); world.present) {
		const auto earned = ledger.Transfer(kGround, Ledger::kPlayer, form, count);
		logger::debug("pack +{} {} [{:08X}] picked up (ref {:08X}): {} yours", count, ItemName(a_item), form, world.id, earned);
		ShowNotYours(a_item, count - earned);
		return;
	}

	// Picked up off the ground, reported as from nowhere: the activation just before names the reference.
	{
		RE::FormID pickedRef = 0;
		{
			std::scoped_lock lock(g_activatedLock);
			if (g_activated.ref != 0 && g_activated.base == form && Clock::now() - g_activated.when <= kPickupWindow) {
				pickedRef = g_activated.ref;
				g_activated = {};
			}
		}
		if (pickedRef != 0) {
			const auto earned = ledger.Transfer(kGround, Ledger::kPlayer, form, count);
			logger::debug("pack +{} {} [{:08X}] picked up (activated ref {:08X}): {} yours", count, ItemName(a_item), form, pickedRef, earned);
			ShowNotYours(a_item, count - earned);
			return;
		}
	}

	// From nowhere: a bench, a vein, a chopping block, or a script.
	const auto bench = CurrentBench();
	if (bench == Bench::kImprove) {
		std::int32_t back = 0;
		{
			std::scoped_lock lock(g_benchLock);
			if (const auto it = g_benchPending.find(form); it != g_benchPending.end()) {
				back = std::min(count, it->second);
				it->second -= back;
				if (it->second <= 0) {
					g_benchPending.erase(it);
				}
			}
		}
		ledger.AddEarned(form, back);
		ledger.AddUnearned(form, count - back);
		logger::debug("pack +{} {} [{:08X}] back from an improvement bench: {} yours", count, ItemName(a_item), form, back);
		return;
	}
	if (bench == Bench::kMake) {
		Mint(a_item, count, MadeSource(a_item));
		return;
	}
	if (OccupiedFurniture()) {
		if (form == kFirewood) {
			Mint(a_item, count, Source::kChopped);
			return;
		}
		if (IsOreOrGem(a_item)) {
			Mint(a_item, count, Source::kMined);
			return;
		}
	}

	ledger.AddUnearned(form, count);
	logger::debug("pack +{} {} [{:08X}] from nowhere: not yours", count, ItemName(a_item), form);
	ShowNotYours(a_item, count);
}

void Events::OnLeftPack(const RE::TESContainerChangedEvent& a_event, RE::TESForm* a_item)
{
	auto&      ledger = Ledger::Get();
	const auto form = a_event.baseObj;
	const auto count = a_event.itemCount;

	// Into a container, a merchant or another actor.
	if (a_event.newContainer != 0) {
		const auto ui = RE::UI::GetSingleton();
		const auto selling = ui && ui->IsMenuOpen(RE::BarterMenu::MENU_NAME);
		const auto earned = ledger.Transfer(Ledger::kPlayer, a_event.newContainer, form, count, selling);
		logger::debug("pack -{} {} [{:08X}] to {:08X}{}: {} yours", count, ItemName(a_item), form, a_event.newContainer, selling ? " (sold)" : "", earned);
		if (selling && g_armed && earned < count) {
			Break(Rule::kSale, a_item, count - earned, std::format("{} x{} was not yours to sell.", ItemName(a_item), count - earned));
		}
		return;
	}

	// Dropped on the ground.
	if (const auto world = GetWorldRef(a_event); world.present) {
		const auto earned = ledger.Transfer(Ledger::kPlayer, kGround, form, count);
		logger::debug("pack -{} {} [{:08X}] dropped: {} yours", count, ItemName(a_item), form, earned);
		return;
	}

	// Used up: eaten, drunk, spent as material, or put on a bench.
	if (a_item->Is(RE::FormType::AlchemyItem) && CurrentBench() == Bench::kNone) {
		std::scoped_lock lock(g_usedLock);
		const auto       now = Clock::now();
		if (g_lastUsed.form == form && g_lastUsed.count == count && now - g_lastUsed.when <= kDoubleReportWindow) {
			g_lastUsed = {};
			logger::debug("pack -{} {} [{:08X}] used up: second report of the same item, ignored (uid {})", count, ItemName(a_item), form, a_event.uniqueID);
			return;
		}
		g_lastUsed = { form, count, now };
	}
	const auto bench = CurrentBench();
	const auto earned = ledger.Consume(Ledger::kPlayer, form, count);
	if (earned > 0 && bench == Bench::kImprove) {
		std::scoped_lock lock(g_benchLock);
		g_benchPending[form] += earned;
	}
	logger::debug("pack -{} {} [{:08X}] used up: {} yours (uid {})", count, ItemName(a_item), form, earned, a_event.uniqueID);

	if (!g_armed || earned >= count) {
		return;
	}
	if (bench == Bench::kNone) {
		// Eaten, drunk or applied.
		if (const auto alch = a_item->As<RE::AlchemyItem>(); alch && !IsFromAllowedPlugin(a_item)) {
			if (alch->IsFood()) {
				Break(Rule::kFood, a_item, count - earned, std::format("{} was not cooked by you.", ItemName(a_item)));
			} else {
				Break(Rule::kPotion, a_item, count - earned, std::format("{} was not brewed by you.", ItemName(a_item)));
			}
		}
	} else if (bench == Bench::kMake && IsGatheredMaterial(a_item)) {
		Break(Rule::kMaterial, a_item, count - earned, std::format("{} x{} was not gathered by you.", ItemName(a_item), count - earned));
	}
}

RE::BSEventNotifyControl Events::ProcessEvent(const RE::TESContainerChangedEvent* a_event, RE::BSTEventSource<RE::TESContainerChangedEvent>*)
{
	if (!a_event || a_event->itemCount <= 0 || a_event->baseObj == 0 || a_event->oldContainer == a_event->newContainer) {
		return RE::BSEventNotifyControl::kContinue;
	}
	if (a_event->baseObj == kGold || a_event->baseObj == kLockpick) {
		return RE::BSEventNotifyControl::kContinue;
	}

	// Anything waiting on an improvement bench is forgotten once the player has left it.
	if (CurrentBench() != Bench::kImprove) {
		std::scoped_lock lock(g_benchLock);
		g_benchPending.clear();
	}

	if (a_event->newContainer == Ledger::kPlayer || a_event->oldContainer == Ledger::kPlayer) {
		const auto item = RE::TESForm::LookupByID(a_event->baseObj);
		if (!item) {
			return RE::BSEventNotifyControl::kContinue;
		}
		if (a_event->newContainer == Ledger::kPlayer) {
			OnAddedToPack(*a_event, item);
		} else {
			OnLeftPack(*a_event, item);
		}
		return RE::BSEventNotifyControl::kContinue;
	}

	// Between two other holders (a follower emptying a chest, a body being looted by someone else).
	auto& ledger = Ledger::Get();
	if (a_event->oldContainer != 0 && ledger.Tracks(a_event->oldContainer)) {
		if (a_event->newContainer != 0) {
			ledger.Transfer(a_event->oldContainer, a_event->newContainer, a_event->baseObj, a_event->itemCount);
		} else if (const auto world = GetWorldRef(*a_event); world.present) {
			ledger.Transfer(a_event->oldContainer, kGround, a_event->baseObj, a_event->itemCount);
		} else {
			ledger.Consume(a_event->oldContainer, a_event->baseObj, a_event->itemCount);
		}
	}
	return RE::BSEventNotifyControl::kContinue;
}

RE::BSEventNotifyControl Events::ProcessEvent(const RE::TESDeathEvent* a_event, RE::BSTEventSource<RE::TESDeathEvent>*)
{
	if (!a_event || !a_event->actorDying || !a_event->actorKiller) {
		return RE::BSEventNotifyControl::kContinue;
	}
	if (!a_event->actorKiller->IsPlayerRef()) {
		return RE::BSEventNotifyControl::kContinue;
	}
	const auto victim = a_event->actorDying->As<RE::Actor>();
	if (IsHuntable(victim)) {
		Ledger::Get().NoteKill(victim->GetFormID());
		logger::debug("hunted {} [{:08X}]", victim->GetName(), victim->GetFormID());
	}
	return RE::BSEventNotifyControl::kContinue;
}

RE::BSEventNotifyControl Events::ProcessEvent(const RE::TESActivateEvent* a_event, RE::BSTEventSource<RE::TESActivateEvent>*)
{
	if (!a_event || !a_event->objectActivated || !a_event->actionRef || !a_event->actionRef->IsPlayerRef()) {
		return RE::BSEventNotifyControl::kContinue;
	}
	const auto base = a_event->objectActivated->GetBaseObject();
	if (!base) {
		return RE::BSEventNotifyControl::kContinue;
	}
	std::scoped_lock lock(g_activatedLock);
	g_activated = { a_event->objectActivated->GetFormID(), base->GetFormID(), Clock::now() };
	return RE::BSEventNotifyControl::kContinue;
}

RE::BSEventNotifyControl Events::ProcessEvent(const RE::TESEquipEvent* a_event, RE::BSTEventSource<RE::TESEquipEvent>*)
{
	if (!g_armed || !a_event || !a_event->equipped || !a_event->actor || !a_event->actor->IsPlayerRef()) {
		return RE::BSEventNotifyControl::kContinue;
	}
	const auto form = a_event->baseObject;
	if (form == 0 || form == kUnarmed) {
		return RE::BSEventNotifyControl::kContinue;
	}
	const auto item = RE::TESForm::LookupByID(form);
	const auto name = item ? item->GetName() : nullptr;
	if (!item || !name || name[0] == '\0') {
		// Hidden helper items that mods equip have no name.
		return RE::BSEventNotifyControl::kContinue;
	}

	enum class Kind
	{
		kNone,
		kGear,
		kMagic
	};
	auto kind = Kind::kNone;
	if (const auto weapon = item->As<RE::TESObjectWEAP>()) {
		if (weapon->IsStaff()) {
			kind = Kind::kMagic;
		} else if (!IsTool(item)) {
			kind = Kind::kGear;
		}
	} else if (const auto armor = item->As<RE::TESObjectARMO>()) {
		// Clothing and jewelry are left alone for now; armor and shields must be the player's own.
		if (armor->IsLightArmor() || armor->IsHeavyArmor()) {
			kind = Kind::kGear;
		}
	} else if (item->Is(RE::FormType::Ammo)) {
		kind = Kind::kGear;
	} else if (const auto spell = item->As<RE::SpellItem>()) {
		if (spell->GetSpellType() == RE::MagicSystem::SpellType::kSpell) {
			kind = Kind::kMagic;
		}
	} else if (item->Is(RE::FormType::Scroll)) {
		kind = Kind::kMagic;
	}
	if (kind == Kind::kNone) {
		return RE::BSEventNotifyControl::kContinue;
	}

	{
		std::scoped_lock lock(g_equipLock);
		const auto       now = Clock::now();
		if (g_lastEquip.form == form && now - g_lastEquip.when <= kEquipRepeatWindow) {
			return RE::BSEventNotifyControl::kContinue;
		}
		g_lastEquip = { form, now };
	}

	if (kind == Kind::kMagic) {
		Break(Rule::kMagic, item, 1, std::format("{} is magic. Only enchanting is allowed.", name));
		return RE::BSEventNotifyControl::kContinue;
	}

	auto&      ledger = Ledger::Get();
	const auto earned = ledger.Earned(Ledger::kPlayer, form);
	if (earned <= 0) {
		Break(Rule::kGear, item, 1, std::format("{} was not made by your hand.", name));
	} else if (const auto unearned = ledger.UnearnedInPack(form); unearned > 0 && Settings::bShowWarnings.GetValue()) {
		// A mixed stack: a caution, not a broken rule.
		const auto text = std::format("Zenithar: {} of your {} are not yours. Store them.", unearned, name);
		RE::SendHUDMessage::ShowHUDMessage(text.c_str());
	}
	return RE::BSEventNotifyControl::kContinue;
}
