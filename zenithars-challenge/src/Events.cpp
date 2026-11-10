#include "Events.h"

#include "InventoryMarks.h"

#include "Ledger.h"
#include "Rules.h"
#include "Settings.h"

namespace
{
	using namespace Rules;

	constexpr RE::FormID kGold = 0x0000000F;
	constexpr RE::FormID kLockpick = 0x0000000A;
	constexpr RE::FormID kUnarmed = 0x000001F4;   // Skyrim.esm Unarmed (fists)
	constexpr RE::FormID kDummyWeapon = 0x00020163;    // Skyrim.esm hidden dummy weapon, used to empty a hand
	constexpr RE::FormID kRightHandSlot = 0x00013F42;  // Skyrim.esm RightHand equip slot
	constexpr RE::FormID kLeftHandSlot = 0x00013F43;   // Skyrim.esm LeftHand equip slot

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
		bool              purse{ false };  // the thing activated was a coin purse
	};

	// Quest rewards. A reward is handed over by a script, so it arrives "from nowhere", within a
	// few seconds of the quest moving to a new stage, on either side of it. Arrivals are kept that
	// long so a stage change that follows them can still claim them.
	struct Arrival
	{
		RE::FormID        form{ 0 };
		std::int32_t      count{ 0 };
		Clock::time_point when{};
	};
	std::mutex           g_questLock;
	Clock::time_point    g_lastStage{};
	std::vector<Arrival> g_arrivals;
	constexpr auto       kRewardWindow = 4s;
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

	// Rules are judged only for a character that took the challenge, and only once the game has
	// settled after loading.
	[[nodiscard]] bool Judging()
	{
		return g_armed && Events::OnChallenge();
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

	// The up-to-you rules that let a looted item count as the player's own.
	[[nodiscard]] bool LootAllowed(const RE::TESForm* a_item)
	{
		const auto options = Ledger::Get().Options();
		return (IsGem(a_item) && (options & Option::kLootGems) != 0) || (IsSoulGem(a_item) && (options & Option::kLootSoulGems) != 0);
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
		if (Settings::bShowMadeMessages.GetValue() && Events::OnChallenge()) {
			const auto text = std::format("Zenithar: {} x{} {}", ItemName(a_item), a_count, SourceVerb(a_source));
			RE::SendHUDMessage::ShowHUDMessage(text.c_str());
		}
	}

	void ShowNotYours(const RE::TESForm* a_item, std::int32_t a_count)
	{
		if (a_count > 0 && Settings::bShowNotYoursMessages.GetValue() && Events::OnChallenge() && IsRestricted(a_item)) {
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
		holder->AddEventSink<RE::TESQuestStageEvent>(GetSingleton());
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
	if (EqualsNoCase(forced, "off"sv)) {
		return Mode::kOff;
	}
	const auto stored = Ledger::Get().StoredMode();
	return stored == Mode::kOath ? Mode::kOath : Mode::kOff;
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

	// food and potions that are not the player's own are refused before they are used.
	// Actor::DrinkPotion is what the game calls for both.
	struct DrinkPotionHook
	{
		static bool thunk(RE::Actor* a_this, RE::AlchemyItem* a_potion, RE::ExtraDataList* a_extraList)
		{
			if (a_potion && Judging() && MustBeOwn(a_potion) &&
				Ledger::Get().Earned(Ledger::kPlayer, a_potion->GetFormID()) <= 0) {
				const auto name = ItemName(a_potion);
				logger::info("refused: {} [{:08X}] not consumed", name, a_potion->GetFormID());
				ShowLine(a_potion->IsFood() ? std::format("{} was not cooked by you.", name) :
											  std::format("{} was not brewed by you.", name));
				return false;
			}
			return func(a_this, a_potion, a_extraList);
		}

		static inline REL::Relocation<decltype(thunk)> func;
		static constexpr std::size_t                   kIndex = 0x10F;
	};

	void RefreshItemScreens(RE::PlayerCharacter* a_player);

	[[nodiscard]] bool IsForbiddenMagic(const RE::TESForm* a_form)
	{
		if (!a_form) {
			return false;
		}
		if (a_form->Is(RE::FormType::Scroll)) {
			return true;
		}
		if (const auto spell = a_form->As<RE::SpellItem>()) {
			return spell->GetSpellType() == RE::MagicSystem::SpellType::kSpell;
		}
		if (const auto weapon = a_form->As<RE::TESObjectWEAP>()) {
			return weapon->IsStaff();
		}
		return false;
	}

	// empties any hand that holds a spell, a scroll or a staff. A spell has no "unequip"
	// of its own, so the hand is cleared the way hotkey mods do it: a hidden dummy weapon is put in
	// the hand and taken straight back out. Main thread only.
	void ClearMagicHandsNow()
	{
		const auto player = RE::PlayerCharacter::GetSingleton();
		const auto manager = RE::ActorEquipManager::GetSingleton();
		if (!player || !manager) {
			return;
		}
		const auto dummy = RE::TESForm::LookupByID<RE::TESObjectWEAP>(kDummyWeapon);
		auto       cleared = false;
		for (const bool left : { true, false }) {
			const auto held = player->GetEquippedObject(left);
			if (!IsForbiddenMagic(held)) {
				continue;
			}
			const auto slot = RE::TESForm::LookupByID<RE::BGSEquipSlot>(left ? kLeftHandSlot : kRightHandSlot);
			logger::info("{} hand cleared of {} [{:08X}]", left ? "left" : "right", held->GetName(), held->GetFormID());
			if (const auto spell = held->As<RE::SpellItem>(); spell && !held->Is(RE::FormType::Scroll)) {
				player->DeselectSpell(spell);
			}
			if (dummy && slot) {
				manager->EquipObject(player, dummy, nullptr, 1, slot, false, true, false, true);
				manager->UnequipObject(player, dummy, nullptr, 1, slot, false, true, false, true);
			} else if (const auto object = held->As<RE::TESBoundObject>()) {
				manager->UnequipObject(player, object, nullptr, 1, slot, false, true, false, true);
			}
			cleared = true;
		}
		if (cleared) {
			RefreshItemScreens(player);
		}
	}

	// Now, and once more a frame later: the game finishes an equip after reporting it, and a second
	// click on the same spell can land in between.
	void ClearMagicHands()
	{
		SKSE::GetTaskInterface()->AddTask([]() {
			ClearMagicHandsNow();
			SKSE::GetTaskInterface()->AddTask([]() { ClearMagicHandsNow(); });
		});
	}

	// takes back off the gear the player just put on.
	// The copy of an item the player is wearing or holding: its own extra data, or null.
	[[nodiscard]] RE::ExtraDataList* WornCopy(RE::PlayerCharacter* a_player, const RE::TESBoundObject* a_object)
	{
		const auto changes = a_player->GetInventoryChanges();
		if (!changes || !changes->entryList) {
			return nullptr;
		}
		for (const auto entry : *changes->entryList) {
			if (!entry || entry->object != a_object || !entry->extraLists) {
				continue;
			}
			for (const auto list : *entry->extraLists) {
				if (list && (list->HasType(RE::ExtraDataType::kWorn) || list->HasType(RE::ExtraDataType::kWornLeft))) {
					return list;
				}
			}
		}
		return nullptr;
	}

	// An open item screen keeps its own copy of the pack. After the mod changes what is worn behind
	// its back, the screen is told to read the pack again; a stale copy is a known way to crash it.
	void RefreshItemScreens(RE::PlayerCharacter* a_player)
	{
		const auto ui = RE::UI::GetSingleton();
		if (ui && (ui->IsMenuOpen(RE::InventoryMenu::MENU_NAME) || ui->IsMenuOpen(RE::ContainerMenu::MENU_NAME) || ui->IsMenuOpen(RE::BarterMenu::MENU_NAME) ||
					  ui->IsMenuOpen(RE::FavoritesMenu::MENU_NAME))) {
			RE::SendUIMessage::SendInventoryUpdateMessage(a_player, nullptr);
		}
	}

	// Takes off every worn or held copy of gear that is not the player's own (or, for a mixed stack,
	// every copy of it). Names each worn copy and its hand outright: a second click puts a weapon in
	// the other hand, which a plain "unequip this item" does not reach. Main thread only.
	void SweepGearNow()
	{
		const auto player = RE::PlayerCharacter::GetSingleton();
		const auto manager = RE::ActorEquipManager::GetSingleton();
		const auto changes = player ? player->GetInventoryChanges() : nullptr;
		if (!manager || !changes || !changes->entryList) {
			return;
		}
		struct Worn
		{
			RE::TESBoundObject* object;
			RE::ExtraDataList*  list;
			bool                left;
		};
		std::vector<Worn> off;
		auto&             ledger = Ledger::Get();
		for (const auto entry : *changes->entryList) {
			if (!entry || !entry->object || !entry->extraLists || !MustBeOwnGear(entry->object)) {
				continue;
			}
			const auto form = entry->object->GetFormID();
			if (ledger.Earned(Ledger::kPlayer, form) > 0 && ledger.UnearnedInPack(form) <= 0) {
				continue;
			}
			for (const auto list : *entry->extraLists) {
				if (!list) {
					continue;
				}
				if (list->HasType(RE::ExtraDataType::kWornLeft)) {
					off.push_back({ entry->object, list, true });
				} else if (list->HasType(RE::ExtraDataType::kWorn)) {
					off.push_back({ entry->object, list, false });
				}
			}
		}
		for (const auto& worn : off) {
			const RE::BGSEquipSlot* slot = nullptr;
			if (worn.object->Is(RE::FormType::Weapon)) {
				slot = RE::TESForm::LookupByID<RE::BGSEquipSlot>(worn.left ? kLeftHandSlot : kRightHandSlot);
			}
			manager->UnequipObject(player, worn.object, worn.list, 1, slot, true, false, false, true);
		}
		if (!off.empty()) {
			RefreshItemScreens(player);
		}
	}

	// Now, and again on the next two frames: the game finishes an equip after reporting it.
	void TakeOff(RE::FormID)
	{
		SKSE::GetTaskInterface()->AddTask([]() {
			SweepGearNow();
			SKSE::GetTaskInterface()->AddTask([]() {
				SweepGearNow();
				SKSE::GetTaskInterface()->AddTask([]() { SweepGearNow(); });
			});
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

	// a sale of something that was not the player's own is reversed. The item comes
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
				logger::info("refused: sale of {} x{} reversed, {} septims returned to the merchant", ItemName(object), a_back, refund);
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
		auto earned = ledger.Transfer(a_event.oldContainer, Ledger::kPlayer, form, count);
		if (earned < count && a_item->Is(RE::FormType::Ammo)) {
			// Arrows pulled out of a body: the player's own shot arrows come back as their own.
			const auto back = ledger.Consume(kGround, form, count - earned);
			earned += ledger.Promote(form, back);
		}
		if (earned < count && LootAllowed(a_item)) {
			// A rule the player chose to allow: looted or bought gems or soul gems count as their own.
			ledger.EarnPack(form, ledger.Earned(Ledger::kPlayer, form) + ledger.UnearnedInPack(form));
			earned = count;
		}
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
	const auto options = ledger.Options();
	const auto now = Clock::now();

	// A gem out of a coin purse, if the player allowed those.
	if (IsGem(a_item) && (options & Option::kCoinPurseGems) != 0) {
		std::scoped_lock lock(g_activatedLock);
		if (g_activated.purse && now - g_activated.when <= kPickupWindow) {
			ledger.Promote(form, count);
			logger::debug("pack +{} {} [{:08X}] from a coin purse: yours (allowed)", count, ItemName(a_item), form);
			return;
		}
	}
	// A quest reward, if the player allowed those.
	if ((options & Option::kQuestRewards) != 0) {
		std::scoped_lock lock(g_questLock);
		if (now - g_lastStage <= kRewardWindow) {
			ledger.Promote(form, count);
			logger::debug("pack +{} {} [{:08X}] as a quest moved on: yours (allowed quest reward)", count, ItemName(a_item), form);
			return;
		}
		std::erase_if(g_arrivals, [now](const Arrival& a_entry) { return now - a_entry.when > kRewardWindow; });
		g_arrivals.push_back({ form, count, now });
	}
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
			ShowLine(std::format("{} x{} was not yours to sell.", ItemName(a_item), count - earned));
			UndoSale(a_event.newContainer, form, count - earned, count);
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
	// the game reports food and potions as used up a moment before it asks to use them, and
	// the refusal comes at that second step. Nothing was eaten, so nothing is counted or recorded.
	if (bench == Bench::kNone && a_item->Is(RE::FormType::AlchemyItem) && Judging() && MustBeOwn(a_item) &&
		ledger.Earned(Ledger::kPlayer, form) <= 0) {
		logger::debug("pack -{} {} [{:08X}] reported used, about to be refused: not counted", count, ItemName(a_item), form);
		return;
	}
	if (bench == Bench::kNone && a_item->Is(RE::FormType::Ammo)) {
		// An arrow shot. It is not gone: it lies in the world or sticks in a body, and it is still the
		// player's own when picked back up. Shot arrows are kept with the things dropped on the ground.
		const auto own = ledger.Transfer(Ledger::kPlayer, kGround, form, count, true);
		logger::debug("pack -{} {} [{:08X}] shot: {} yours", count, ItemName(a_item), form, own);
		return;
	}
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
	if (Judging() && IsMissiveBoard(a_event->objectActivated.get())) {
		// The Missives mod unblocks its board whenever it refreshes it, so the board is blocked
		// again here and the list of notes is closed if it managed to open.
		logger::info("refused: missive board {:08X}", a_event->objectActivated->GetFormID());
		ShowLine("Missives are not allowed.");
		a_event->objectActivated->SetActivationBlocked(true);
		SKSE::GetTaskInterface()->AddTask([]() {
			if (const auto queue = RE::UIMessageQueue::GetSingleton()) {
				queue->AddMessage(RE::ContainerMenu::MENU_NAME, RE::UI_MESSAGE_TYPE::kHide, nullptr);
			}
		});
		return RE::BSEventNotifyControl::kContinue;
	}
	std::scoped_lock lock(g_activatedLock);
	g_activated = { a_event->objectActivated->GetFormID(), base->GetFormID(), Clock::now(), ContainsNoCase(ItemName(base), "purse"sv) };
	return RE::BSEventNotifyControl::kContinue;
}

RE::BSEventNotifyControl Events::ProcessEvent(const RE::TESEquipEvent* a_event, RE::BSTEventSource<RE::TESEquipEvent>*)
{
	if (!Judging() || !a_event || !a_event->equipped || !a_event->actor || !a_event->actor->IsPlayerRef()) {
		return RE::BSEventNotifyControl::kContinue;
	}
	const auto form = a_event->baseObject;
	if (form == 0 || form == kUnarmed || form == kDummyWeapon) {
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
	if (IsForbiddenMagic(item)) {
		// Asked first: a scroll is a spell underneath, and a staff is a weapon.
		kind = Kind::kMagic;
	} else if (MustBeOwnGear(item)) {
		kind = Kind::kGear;
	}
	if (kind == Kind::kNone) {
		return RE::BSEventNotifyControl::kContinue;
	}

	if (kind == Kind::kMagic) {
		// Every time, even for a repeat: the second click on a spell is what used to stick.
		ClearMagicHands();
	} else {
		// The same for gear: a second click puts a weapon in the other hand.
		TakeOff(form);
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
		logger::info("refused: {} [{:08X}] (magic) taken off", name, form);
		ShowLine(std::format("{} is magic. Only enchanting is allowed.", name));
		return RE::BSEventNotifyControl::kContinue;
	}

	auto&      ledger = Ledger::Get();
	const auto earned = ledger.Earned(Ledger::kPlayer, form);
	const auto unearned = ledger.UnearnedInPack(form);
	if (earned <= 0) {
		logger::info("refused: {} [{:08X}] taken off", name, form);
		ShowLine(std::format("{} was not made by your hand.", name));
		TakeOff(form);
	} else if (unearned > 0) {
		// A mixed stack: some copies are the player's own and some are not.
		logger::info("refused: {} [{:08X}] mixed stack ({} not yours) taken off", name, form, unearned);
		ShowLine(std::format("{} of your {} are not yours. Store them first.", unearned, name));
		TakeOff(form);
	}
	return RE::BSEventNotifyControl::kContinue;
}

RE::BSEventNotifyControl Events::ProcessEvent(const RE::MenuOpenCloseEvent* a_event, RE::BSTEventSource<RE::MenuOpenCloseEvent>*)
{
	if (a_event && a_event->opening && a_event->menuName == RE::Console::MENU_NAME) {
		Ledger::Get().NoteConsole();
	}
	// whatever was done in a menu, no hand leaves it holding magic.
	if (a_event && a_event->opening) {
		InventoryMarks::OnMenuOpening(a_event->menuName.c_str());
	}
	if (a_event && !a_event->opening && Judging() &&
		(a_event->menuName == RE::InventoryMenu::MENU_NAME || a_event->menuName == RE::MagicMenu::MENU_NAME || a_event->menuName == RE::FavoritesMenu::MENU_NAME)) {
		ClearMagicHands();
		TakeOff(0);
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
	} else if (Judging()) {
		a_event->reference->SetActivationBlocked(true);
	}
	return RE::BSEventNotifyControl::kContinue;
}

RE::BSEventNotifyControl Events::ProcessEvent(const RE::TESQuestStageEvent* a_event, RE::BSTEventSource<RE::TESQuestStageEvent>*)
{
	if (!a_event) {
		return RE::BSEventNotifyControl::kContinue;
	}
	std::vector<Arrival> claimed;
	{
		std::scoped_lock lock(g_questLock);
		const auto       now = Clock::now();
		g_lastStage = now;
		for (const auto& arrival : g_arrivals) {
			if (now - arrival.when <= kRewardWindow) {
				claimed.push_back(arrival);
			}
		}
		g_arrivals.clear();
	}
	if ((Ledger::Get().Options() & Option::kQuestRewards) != 0) {
		for (const auto& arrival : claimed) {
			const auto moved = Ledger::Get().Promote(arrival.form, arrival.count);
			logger::debug("quest {:08X} moved to stage {}: {} x{} just handed over counts as yours (allowed quest reward)", a_event->formID, a_event->stage,
				ItemName(RE::TESForm::LookupByID(arrival.form)), moved);
		}
	}
	return RE::BSEventNotifyControl::kContinue;
}

std::int32_t Events::NotYoursIfTaken(RE::FormID a_holder, const RE::TESForm* a_item, std::int32_t a_count)
{
	if (!a_item || a_count <= 0 || !MustBeOwn(a_item)) {
		return 0;
	}
	if ((!IsGear(a_item) && IsHuntedBody(a_holder)) || LootAllowed(a_item)) {
		return 0;
	}
	return a_count - std::min(a_count, Ledger::Get().Earned(a_holder, a_item->GetFormID()));
}
