#include "Events.h"

#include "Ledger.h"
#include "Settings.h"

namespace
{
	constexpr RE::FormID kGold = 0x0000000F;
	constexpr RE::FormID kLockpick = 0x0000000A;
	constexpr RE::FormID kFirewood = 0x0006F993;  // Skyrim.esm Firewood01

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
		logger::info("event sinks registered");
	} else {
		logger::critical("ScriptEventSourceHolder not available; the ledger will not run");
	}
}

void Events::Reset()
{
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
		const auto earned = ledger.Transfer(Ledger::kPlayer, a_event.newContainer, form, count);
		logger::debug("pack -{} {} [{:08X}] to {:08X}: {} yours", count, ItemName(a_item), form, a_event.newContainer, earned);
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
	const auto earned = ledger.Consume(Ledger::kPlayer, form, count);
	if (earned > 0 && CurrentBench() == Bench::kImprove) {
		std::scoped_lock lock(g_benchLock);
		g_benchPending[form] += earned;
	}
	logger::debug("pack -{} {} [{:08X}] used up: {} yours (uid {})", count, ItemName(a_item), form, earned, a_event.uniqueID);
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
