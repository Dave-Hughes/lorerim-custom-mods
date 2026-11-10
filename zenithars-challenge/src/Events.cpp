#include "Events.h"

#include "Ledger.h"
#include "Rules.h"
#include "Settings.h"

namespace
{
	using namespace Rules;

	constexpr RE::FormID kGold = 0x0000000F;
	constexpr RE::FormID kLockpick = 0x0000000A;
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

	// Rules are judged once the game has settled after loading, and not before a new character
	// has been asked for its mode (its starting gear is put on before that).
	[[nodiscard]] bool Judging()
	{
		if (!g_armed) {
			return false;
		}
		const auto& ledger = Ledger::Get();
		return !(ledger.ChoicePending() && ledger.StoredMode() == Mode::kUnset);
	}

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
		return Settings::bCountOthersKills.GetValue() || (Ledger::Get().Options() & Option::kOthersKills) != 0 ||
			   Ledger::Get().WasKilledByPlayer(a_container);
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
		holder->AddEventSink<RE::TESCellAttachDetachEvent>(GetSingleton());
		if (const auto ui = RE::UI::GetSingleton()) {
			ui->AddEventSink<RE::MenuOpenCloseEvent>(GetSingleton());
		}
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

Mode Events::CurrentMode()
{
	const auto forced = Settings::sForceMode.GetValue();
	if (EqualsNoCase(forced, "oath"sv)) {
		return Mode::kOath;
	}
	if (EqualsNoCase(forced, "honor"sv)) {
		return Mode::kHonor;
	}
	const auto stored = Ledger::Get().StoredMode();
	return stored == Mode::kOath ? Mode::kOath : Mode::kHonor;
}

namespace
{
	void ShowLine(const std::string& a_text)
	{
		if (Settings::bShowWarnings.GetValue()) {
			const auto text = std::format("Zenithar: {}", a_text);
			RE::SendHUDMessage::ShowHUDMessage(text.c_str());
		}
	}

	// Oath mode: food and potions that are not the player's own are refused before they are used.
	// Actor::DrinkPotion is what the game calls for both.
	struct DrinkPotionHook
	{
		static bool thunk(RE::Actor* a_this, RE::AlchemyItem* a_potion, RE::ExtraDataList* a_extraList)
		{
			if (a_potion && Judging() && Events::CurrentMode() == Mode::kOath && MustBeOwn(a_potion) &&
				Ledger::Get().Earned(Ledger::kPlayer, a_potion->GetFormID()) <= 0) {
				const auto name = ItemName(a_potion);
				logger::info("OATH refused: {} [{:08X}] not consumed", name, a_potion->GetFormID());
				ShowLine(a_potion->IsFood() ? std::format("{} was not cooked by you. Your oath holds.", name) :
											  std::format("{} was not brewed by you. Your oath holds.", name));
				return false;
			}
			return func(a_this, a_potion, a_extraList);
		}

		static inline REL::Relocation<decltype(thunk)> func;
		static constexpr std::size_t                   kIndex = 0x10F;
	};

	// Oath mode: takes back off what the player just put on.
	void TakeOff(RE::FormID a_form)
	{
		SKSE::GetTaskInterface()->AddTask([a_form]() {
			const auto player = RE::PlayerCharacter::GetSingleton();
			const auto item = RE::TESForm::LookupByID(a_form);
			if (!player || !item) {
				return;
			}
			if (const auto spell = item->As<RE::SpellItem>(); spell && !item->Is(RE::FormType::Scroll)) {
				player->DeselectSpell(spell);
				return;
			}
			if (const auto object = item->As<RE::TESBoundObject>()) {
				if (const auto manager = RE::ActorEquipManager::GetSingleton()) {
					manager->UnequipObject(player, object, nullptr, 1, nullptr, true, false, false, true);
				}
			}
		});
	}
}

namespace
{
	// Septims the player was just paid in the barter menu, so a refused sale can hand them back.
	struct GoldIn
	{
		std::int32_t      count{ 0 };
		Clock::time_point when{};
	};
	std::mutex          g_goldLock;
	std::vector<GoldIn> g_goldIn;
	constexpr auto      kSaleWindow = 400ms;

	void NoteGoldIn(std::int32_t a_count)
	{
		std::scoped_lock lock(g_goldLock);
		const auto       now = Clock::now();
		std::erase_if(g_goldIn, [now](const GoldIn& a_entry) { return now - a_entry.when > kSaleWindow; });
		g_goldIn.push_back({ a_count, now });
	}

	[[nodiscard]] std::int32_t TakeRecentGold()
	{
		std::scoped_lock lock(g_goldLock);
		const auto       now = Clock::now();
		std::int32_t     total = 0;
		for (const auto& entry : g_goldIn) {
			if (now - entry.when <= kSaleWindow) {
				total += entry.count;
			}
		}
		g_goldIn.clear();
		return total;
	}

	// Oath mode: a sale of something that was not the player's own is reversed. The item comes
	// back and the septims paid for it go back to the merchant. Runs two frames later, so the
	// payment has been reported by then whichever order the game reports them in.
	void UndoSale(RE::FormID a_merchant, RE::FormID a_form, std::int32_t a_back, std::int32_t a_sold)
	{
		SKSE::GetTaskInterface()->AddTask([=]() {
			SKSE::GetTaskInterface()->AddTask([=]() {
				const auto player = RE::PlayerCharacter::GetSingleton();
				const auto merchant = RE::TESForm::LookupByID<RE::TESObjectREFR>(a_merchant);
				const auto object = RE::TESForm::LookupByID<RE::TESBoundObject>(a_form);
				const auto gold = RE::TESForm::LookupByID<RE::TESBoundObject>(kGold);
				if (!player || !merchant || !object) {
					return;
				}
				const auto paid = TakeRecentGold();
				const auto refund = a_sold > 0 ? static_cast<std::int32_t>(static_cast<std::int64_t>(paid) * a_back / a_sold) : 0;
				merchant->RemoveItem(object, a_back, RE::ITEM_REMOVE_REASON::kStoreInContainer, nullptr, player);
				if (refund > 0 && gold) {
					player->RemoveItem(gold, refund, RE::ITEM_REMOVE_REASON::kStoreInContainer, nullptr, merchant);
				}
				RE::SendUIMessage::SendInventoryUpdateMessage(player, nullptr);
				RE::SendUIMessage::SendInventoryUpdateMessage(merchant, nullptr);
				logger::info("OATH refused: sale of {} x{} reversed, {} septims returned to the merchant", ItemName(object), a_back, refund);
			});
		});
	}

	// A missive board: the container the notes are taken from.
	[[nodiscard]] bool IsMissiveBoard(RE::TESObjectREFR* a_ref)
	{
		const auto base = a_ref ? a_ref->GetBaseObject() : nullptr;
		if (!base || !base->Is(RE::FormType::Container)) {
			return false;
		}
		return ContainsNoCase(DefiningPlugin(base), "missive"sv) || ContainsNoCase(ItemName(base), "missive"sv);
	}
}

void Events::InstallHooks()
{
	if (REL::Module::IsVR()) {
		logger::warn("VR: the food and potion hook is not installed");
		return;
	}
	REL::Relocation<std::uintptr_t> vtbl{ RE::VTABLE_PlayerCharacter[0] };
	DrinkPotionHook::func = vtbl.write_vfunc(DrinkPotionHook::kIndex, DrinkPotionHook::thunk);
	logger::info("food and potion hook installed");
}

void Events::KeepPack()
{
	const auto player = RE::PlayerCharacter::GetSingleton();
	if (!player) {
		return;
	}
	std::int32_t kinds = 0;
	for (const auto& [object, data] : player->GetInventory()) {
		if (!object || object->GetFormID() == kGold || object->GetFormID() == kLockpick) {
			continue;
		}
		Ledger::Get().EarnPack(object->GetFormID(), data.first);
		++kinds;
	}
	logger::info("starting items kept: {} kinds of item in the pack are now the player's own", kinds);
}

void Events::TakeStock()
{
	const auto player = RE::PlayerCharacter::GetSingleton();
	if (!player) {
		return;
	}
	auto&        ledger = Ledger::Get();
	std::int32_t items = 0;
	std::int32_t added = 0;
	std::int32_t removed = 0;
	for (const auto& [object, data] : player->GetInventory()) {
		if (!object || object->GetFormID() == kGold || object->GetFormID() == kLockpick) {
			continue;
		}
		++items;
		const auto change = ledger.ReconcilePack(object->GetFormID(), data.first);
		if (change > 0) {
			added += change;
		} else {
			removed -= change;
		}
	}
	logger::info("took stock of {} kinds of item in the pack: {} unseen copies marked not yours, {} stale ones cleared", items, added, removed);
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

	if (Judging() && IsMissive(a_item)) {
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
		if (selling && Judging() && earned < count) {
			if (CurrentMode() == Mode::kOath) {
				ShowLine(std::format("{} x{} was not yours to sell. Your oath holds.", ItemName(a_item), count - earned));
				UndoSale(a_event.newContainer, form, count - earned, count);
			} else {
				Break(Rule::kSale, a_item, count - earned, std::format("{} x{} was not yours to sell.", ItemName(a_item), count - earned));
			}
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

	if (!Judging() || earned >= count) {
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
	if (a_event->baseObj == kGold) {
		if (a_event->newContainer == Ledger::kPlayer) {
			if (const auto ui = RE::UI::GetSingleton(); ui && ui->IsMenuOpen(RE::BarterMenu::MENU_NAME)) {
				NoteGoldIn(a_event->itemCount);
			}
		}
		return RE::BSEventNotifyControl::kContinue;
	}
	if (a_event->baseObj == kLockpick) {
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
	if (victim && a_event->dead) {
		// The game reports a death twice (dying, then dead); the tally counts the second.
		if (const auto race = victim->GetRace()) {
			Ledger::Get().TallyKill(race->GetFormID());
		}
	}
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
	if (Judging() && CurrentMode() == Mode::kOath && IsMissiveBoard(a_event->objectActivated.get())) {
		// The Missives mod unblocks its board whenever it refreshes it, so the board is blocked
		// again here and the list of notes is closed if it managed to open.
		logger::info("OATH refused: missive board {:08X}", a_event->objectActivated->GetFormID());
		ShowLine("Missives are not allowed. Your oath holds.");
		a_event->objectActivated->SetActivationBlocked(true);
		SKSE::GetTaskInterface()->AddTask([]() {
			if (const auto queue = RE::UIMessageQueue::GetSingleton()) {
				queue->AddMessage(RE::ContainerMenu::MENU_NAME, RE::UI_MESSAGE_TYPE::kHide, nullptr);
			}
		});
		return RE::BSEventNotifyControl::kContinue;
	}
	std::scoped_lock lock(g_activatedLock);
	g_activated = { a_event->objectActivated->GetFormID(), base->GetFormID(), Clock::now() };
	return RE::BSEventNotifyControl::kContinue;
}

RE::BSEventNotifyControl Events::ProcessEvent(const RE::TESEquipEvent* a_event, RE::BSTEventSource<RE::TESEquipEvent>*)
{
	if (!Judging() || !a_event || !a_event->equipped || !a_event->actor || !a_event->actor->IsPlayerRef()) {
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

	const auto oath = CurrentMode() == Mode::kOath;

	if (kind == Kind::kMagic) {
		if (oath) {
			logger::info("OATH refused: {} [{:08X}] (magic) taken off", name, form);
			ShowLine(std::format("{} is magic. Only enchanting is allowed. Your oath holds.", name));
			TakeOff(form);
		} else {
			Break(Rule::kMagic, item, 1, std::format("{} is magic. Only enchanting is allowed.", name));
		}
		return RE::BSEventNotifyControl::kContinue;
	}

	auto&      ledger = Ledger::Get();
	const auto earned = ledger.Earned(Ledger::kPlayer, form);
	const auto unearned = ledger.UnearnedInPack(form);
	if (earned <= 0) {
		if (oath) {
			logger::info("OATH refused: {} [{:08X}] taken off", name, form);
			ShowLine(std::format("{} was not made by your hand. Your oath holds.", name));
			TakeOff(form);
		} else {
			Break(Rule::kGear, item, 1, std::format("{} was not made by your hand.", name));
		}
	} else if (unearned > 0) {
		// A mixed stack: some copies are the player's own and some are not.
		if (oath) {
			logger::info("OATH refused: {} [{:08X}] mixed stack ({} not yours) taken off", name, form, unearned);
			ShowLine(std::format("{} of your {} are not yours. Store them first.", unearned, name));
			TakeOff(form);
		} else {
			ShowLine(std::format("{} of your {} are not yours. Store them.", unearned, name));
		}
	}
	return RE::BSEventNotifyControl::kContinue;
}

RE::BSEventNotifyControl Events::ProcessEvent(const RE::MenuOpenCloseEvent* a_event, RE::BSTEventSource<RE::MenuOpenCloseEvent>*)
{
	if (a_event && a_event->opening && a_event->menuName == RE::Console::MENU_NAME) {
		Ledger::Get().NoteConsole();
	}
	return RE::BSEventNotifyControl::kContinue;
}

RE::BSEventNotifyControl Events::ProcessEvent(const RE::TESCellAttachDetachEvent* a_event, RE::BSTEventSource<RE::TESCellAttachDetachEvent>*)
{
	// Missive boards are blocked only while they are loaded and only under an oath, so nothing
	// stays blocked in the save if the mod is removed.
	if (!a_event || !a_event->reference || !IsMissiveBoard(a_event->reference.get())) {
		return RE::BSEventNotifyControl::kContinue;
	}
	if (!a_event->attached) {
		a_event->reference->SetActivationBlocked(false);
	} else if (Judging() && CurrentMode() == Mode::kOath) {
		a_event->reference->SetActivationBlocked(true);
	}
	return RE::BSEventNotifyControl::kContinue;
}
