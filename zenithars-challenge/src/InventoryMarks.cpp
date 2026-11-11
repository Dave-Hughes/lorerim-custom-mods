#include "InventoryMarks.h"

#include "Events.h"
#include "Ledger.h"
#include "Rules.h"
#include "Settings.h"

#include <cstring>

namespace
{
	// The first rows seen are written to the log, to show what the screen hands over.
	constexpr int    kMaxLogged = 40;
	std::atomic_int  g_logged{ 0 };
	std::atomic_int  g_loggedOther{ 0 };

	// ---- the mark itself ----
	//
	// LoreRim's item lists are SkyUI's (Norden UI is a reskin of them). A row is drawn by the list's
	// InventoryListEntry class: formatName writes the name and lines up the small icons after it
	// (favorite, stolen, enchanted...), formatColor colours the text. Those icons are a fixed set
	// baked into the list's file, so ours is added from here: both functions are wrapped once per
	// screen, and after the original has run the wrapper draws a small struck-through anvil after
	// the row's other icons and tints the name. Nothing in any interface file is changed.

	constexpr auto kMarkName = "zenitharMark";
	constexpr auto kMarkDepth = 7310.0;          // above the row's own art, which sits at negative depths
	constexpr auto kMarkBox = 16.0;              // the mark is drawn in a 16 by 16 box, then scaled to the row's icons
	constexpr auto kNameColour = 0xD9907C;       // a dull red, for the name of something wholly not yours
	constexpr auto kAnvilColour = 0xC9A66B;
	constexpr auto kStrikeColour = 0xD0463B;

	[[nodiscard]] double Number(const RE::GFxValue& a_object, const char* a_name, double a_default = 0.0)
	{
		RE::GFxValue value;
		if (a_object.IsObject() && a_object.GetMember(a_name, &value) && value.IsNumber()) {
			return value.GetNumber();
		}
		return a_default;
	}

	[[nodiscard]] bool Flag(const RE::GFxValue& a_object, const char* a_name)
	{
		RE::GFxValue value;
		return a_object.IsObject() && a_object.GetMember(a_name, &value) && value.IsBool() && value.GetBool();
	}

	void Call(RE::GFxValue& a_clip, const char* a_function, std::initializer_list<double> a_numbers)
	{
		std::vector<RE::GFxValue> args;
		args.reserve(a_numbers.size());
		for (const auto number : a_numbers) {
			args.emplace_back(number);
		}
		a_clip.Invoke(a_function, nullptr, args.data(), args.size());
	}

	// An anvil seen from the side, struck through.
	void DrawMark(RE::GFxValue& a_clip)
	{
		static constexpr std::array<std::pair<double, double>, 13> anvil{ {
			{ 0, 4 }, { 15, 4 }, { 15, 7 }, { 11, 7 }, { 10, 9 }, { 12, 12 }, { 12, 14 }, { 3, 14 }, { 3, 12 }, { 5, 9 }, { 4, 7 }, { 2, 6.5 }, { 0, 5 },
		} };
		a_clip.Invoke("clear");
		Call(a_clip, "beginFill", { static_cast<double>(kAnvilColour), 100.0 });
		Call(a_clip, "moveTo", { anvil[0].first, anvil[0].second });
		for (std::size_t i = 1; i < anvil.size(); ++i) {
			Call(a_clip, "lineTo", { anvil[i].first, anvil[i].second });
		}
		a_clip.Invoke("endFill");
		Call(a_clip, "lineStyle", { 2.0, static_cast<double>(kStrikeColour), 100.0 });
		Call(a_clip, "moveTo", { 1.0, 15.0 });
		Call(a_clip, "lineTo", { 15.0, 1.0 });
	}

	// After the row's own formatName: put the mark after its other icons, or hide it.
	void PlaceMark(RE::GFxValue& a_row, const RE::GFxValue& a_field, const RE::GFxValue& a_entry, const RE::GFxValue& a_state)
	{
		RE::GFxValue mark;
		const auto   has = a_row.GetMember(kMarkName, &mark) && mark.IsDisplayObject();
		if (Number(a_entry, "zenitharNotYours") <= 0.0) {
			if (has) {
				mark.SetMember("_visible", RE::GFxValue(false));
			}
			return;
		}
		if (!has) {
			RE::GFxValue name;
			name.SetString(kMarkName);
			const std::array<RE::GFxValue, 2> args{ name, RE::GFxValue(kMarkDepth) };
			a_row.Invoke("createEmptyMovieClip", &mark, args.data(), args.size());
			if (!mark.IsDisplayObject()) {
				return;
			}
			DrawMark(mark);
		}

		// The same sums the row's own script does for the icons it places.
		RE::GFxValue best;
		a_row.GetMember("bestIcon", &best);
		const auto width = std::max(8.0, Number(best, "_width", kMarkBox));
		const auto height = std::max(8.0, Number(best, "_height", kMarkBox));
		const auto space = width * 1.25;
		auto       x = Number(a_field, "_x") + Number(a_field, "_width") + 5.0;
		for (const auto shown : { Flag(a_entry, "bestInClass"), Flag(a_entry, "favorite"), Flag(a_entry, "isPoisoned"),
				 (Flag(a_entry, "isStolen") || Flag(a_entry, "isStealing")) && Flag(a_state, "showStolenIcon"), Flag(a_entry, "isEnchanted"), Flag(a_entry, "isRead") }) {
			if (shown) {
				x += space;
			}
		}
		mark.SetMember("_x", RE::GFxValue(x));
		mark.SetMember("_y", RE::GFxValue(Number(best, "_y")));
		mark.SetMember("_xscale", RE::GFxValue(height / kMarkBox * 100.0));
		mark.SetMember("_yscale", RE::GFxValue(height / kMarkBox * 100.0));
		mark.SetMember("_visible", RE::GFxValue(true));
	}

	class NameHandler : public RE::GFxFunctionHandler
	{
	public:
		void Call(Params& a_params) override
		{
			if (!a_params.thisPtr) {
				return;
			}
			a_params.thisPtr->Invoke("zenitharFormatName", nullptr, a_params.args, a_params.argCount);
			if (a_params.argCount >= 3) {
				PlaceMark(*a_params.thisPtr, a_params.args[0], a_params.args[1], a_params.args[2]);
			}
		}
	};

	class ColourHandler : public RE::GFxFunctionHandler
	{
	public:
		void Call(Params& a_params) override
		{
			if (!a_params.thisPtr) {
				return;
			}
			a_params.thisPtr->Invoke("zenitharFormatColor", nullptr, a_params.args, a_params.argCount);
			if (a_params.argCount >= 2 && Flag(a_params.args[1], "zenitharAll")) {
				RE::GFxValue enabled;
				const auto   greyed = a_params.args[1].GetMember("enabled", &enabled) && enabled.IsBool() && !enabled.GetBool();
				if (!greyed) {
					a_params.args[0].SetMember("textColor", RE::GFxValue(static_cast<double>(kNameColour)));
				}
			}
		}
	};

	// ---- the item card ----
	// The card on the right is filled by the screen's UpdateItemCardInfo. It is wrapped on the screen
	// itself: when the selected row is not yours, a line is added to the text the card shows under
	// the item (its "effects"), before the card reads it.
	class CardHandler : public RE::GFxFunctionHandler
	{
	public:
		void Call(Params& a_params) override
		{
			if (!a_params.thisPtr) {
				return;
			}
			if (a_params.argCount >= 1 && a_params.args[0].IsObject() && a_params.movie) {
				RE::GFxValue lists;
				RE::GFxValue list;
				RE::GFxValue selected;
				if (a_params.thisPtr->GetMember("inventoryLists", &lists) && lists.IsObject() && lists.GetMember("itemList", &list) && list.IsObject() &&
					list.GetMember("selectedEntry", &selected) && selected.IsObject()) {
					const auto notYours = static_cast<int>(Number(selected, "zenitharNotYours"));
					if (notYours > 0) {
						RE::GFxValue effects;
						std::string  text;
						if (a_params.args[0].GetMember("effects", &effects) && effects.IsString()) {
							text = effects.GetString();
						}
						if (!text.empty()) {
							text += "\n";
						}
						text += Flag(selected, "zenitharMagic") ? "<font color='#D9907C'>Magic is not allowed.</font>" :
								Flag(selected, "zenitharAll") ? "<font color='#D9907C'>Not made by you.</font>" :
																std::format("<font color='#D9907C'>{} not made by you.</font>", notYours);
						RE::GFxValue value;
						a_params.movie->CreateString(&value, text.c_str());
						a_params.args[0].SetMember("effects", value);
					}
				}
			}
			a_params.thisPtr->Invoke("zenitharCard", nullptr, a_params.args, a_params.argCount);
		}
	};

	void WrapCard(RE::GFxMovieView* a_view)
	{
		static const auto cardHandler = RE::make_gptr<CardHandler>();
		RE::GFxValue      menu;
		if (!a_view->GetVariable(&menu, "_root.Menu_mc") || !menu.IsObject() || Flag(menu, "zenitharCardWrapped")) {
			return;
		}
		RE::GFxValue original;
		if (!menu.GetMember("UpdateItemCardInfo", &original) || !original.IsObject()) {
			return;
		}
		RE::GFxValue wrapped;
		menu.SetMember("zenitharCard", original);
		a_view->CreateFunction(&wrapped, cardHandler.get());
		menu.SetMember("UpdateItemCardInfo", wrapped);
		menu.SetMember("zenitharCardWrapped", RE::GFxValue(true));
		logger::info("item card wrapped for the not-yours line");
	}

	// ---- using a not-yours potion or food from the inventory ----
	// The refusal itself happens deep in the game, after LoreRim's potion animation mod has already
	// started the drinking animation. From the inventory screen the press can be stopped at the
	// screen instead, before anything starts: the screen's onItemSelect is wrapped, and a press on
	// food or a potion that is wholly not yours goes no further. (A hotkey or the wheel still goes
	// the long way round: the animation plays and the drink is refused at the end of it.)
	constexpr auto kFormTypeAlchemy = 46.0;

	class SelectHandler : public RE::GFxFunctionHandler
	{
	public:
		void Call(Params& a_params) override
		{
			if (!a_params.thisPtr) {
				return;
			}
			if (a_params.argCount >= 1 && a_params.args[0].IsObject() && Events::OnChallenge()) {
				RE::GFxValue entry;
				if (a_params.args[0].GetMember("entry", &entry) && entry.IsObject() && Flag(entry, "zenitharAll") &&
					Number(entry, "formType") == kFormTypeAlchemy) {
					RE::GFxValue name;
					const auto   text = entry.GetMember("zenitharName", &name) && name.IsString() ? std::string(name.GetString()) : "That"s;
					RE::SendHUDMessage::ShowHUDMessage(std::format("Zenithar: {} was not made by you.", text).c_str());
					logger::info("refused at the inventory screen: {}", text);
					return;
				}
			}
			a_params.thisPtr->Invoke("zenitharSelect", nullptr, a_params.args, a_params.argCount);
		}
	};

	// The keys and clicks that use an item reach the screen as AttemptEquip, which acts on the selected row.
	class EquipHandler : public RE::GFxFunctionHandler
	{
	public:
		void Call(Params& a_params) override
		{
			if (!a_params.thisPtr) {
				return;
			}
			if (Events::OnChallenge()) {
				RE::GFxValue lists;
				RE::GFxValue list;
				RE::GFxValue entry;
				if (a_params.thisPtr->GetMember("inventoryLists", &lists) && lists.IsObject() && lists.GetMember("itemList", &list) && list.IsObject() &&
					list.GetMember("selectedEntry", &entry) && entry.IsObject() && Flag(entry, "zenitharAll") && Number(entry, "formType") == kFormTypeAlchemy) {
					RE::GFxValue name;
					const auto   text = entry.GetMember("zenitharName", &name) && name.IsString() ? std::string(name.GetString()) : "That"s;
					RE::SendHUDMessage::ShowHUDMessage(std::format("Zenithar: {} was not made by you.", text).c_str());
					logger::info("refused at the inventory screen (use key): {}", text);
					return;
				}
			}
			a_params.thisPtr->Invoke("zenitharEquip", nullptr, a_params.args, a_params.argCount);
		}
	};

	void WrapSelect(RE::GFxMovieView* a_view)
	{
		static const auto selectHandler = RE::make_gptr<SelectHandler>();
		RE::GFxValue      menu;
		if (!a_view->GetVariable(&menu, "_root.Menu_mc") || !menu.IsObject() || Flag(menu, "zenitharSelectWrapped")) {
			return;
		}
		RE::GFxValue original;
		if (!menu.GetMember("onItemSelect", &original) || !original.IsObject()) {
			return;
		}
		RE::GFxValue wrapped;
		menu.SetMember("zenitharSelect", original);
		a_view->CreateFunction(&wrapped, selectHandler.get());
		menu.SetMember("onItemSelect", wrapped);
		static const auto equipHandler = RE::make_gptr<EquipHandler>();
		RE::GFxValue      attempt;
		if (menu.GetMember("AttemptEquip", &attempt) && attempt.IsObject()) {
			RE::GFxValue wrappedAttempt;
			menu.SetMember("zenitharEquip", attempt);
			a_view->CreateFunction(&wrappedAttempt, equipHandler.get());
			menu.SetMember("AttemptEquip", wrappedAttempt);
		}
		menu.SetMember("zenitharSelectWrapped", RE::GFxValue(true));
		logger::info("inventory screen wrapped to stop not-yours food and potions at the press");
	}

	// ---- the quick-loot window (QuickLoot IE) ----
	// Its rows are its own (QuickLoot.ListItemRenderer) and carry only a name and a count, so the
	// item is found by its name in whatever the crosshair is on.
	std::atomic<RE::FormID> g_crosshairRef{ 0 };
	std::atomic_bool        g_lootWrappedOnce{ false };

	// An item lying in the world: the name under the crosshair is given the not-yours colour, so a
	// cheese on a table says what it is before it is picked up.
	void TintRollover(RE::FormID a_ref)
	{
		const auto ui = RE::UI::GetSingleton();
		const auto view = ui ? ui->GetMovieView(RE::HUDMenu::MENU_NAME) : nullptr;
		RE::GFxValue rollover;
		if (!view || !view->GetVariable(&rollover, "_root.HUDMovieBaseInstance.RolloverText") || !rollover.IsDisplayObject()) {
			static std::atomic_bool told{ false };
			if (!told.exchange(true)) {
				logger::warn("crosshair name: the HUD has no RolloverText to colour");
			}
			return;
		}
		static double plain = -1.0;
		static bool   tinted = false;
		if (!tinted) {
			plain = Number(rollover, "textColor", 16777215.0);
		}
		bool notYours = false;
		const auto ref = a_ref != 0 ? RE::TESForm::LookupByID<RE::TESObjectREFR>(a_ref) : nullptr;
		const auto base = ref ? ref->GetBaseObject() : nullptr;
		if (base && Events::OnChallenge() && Settings::bMarkInventory.GetValue() && !base->Is(RE::FormType::Container) && !base->Is(RE::FormType::NPC)) {
			notYours = Rules::IsMagicItem(base) || Events::NotYoursIfTaken(0xFFFFFFFF, base, 1) > 0;  // 0xFFFFFFFF: things on the ground
			static std::atomic_int logged{ 0 };
			if (notYours && logged < 5) {
				++logged;
				logger::debug("crosshair name: {} [{:08X}] lying in the world is not yours, coloured", Rules::ItemName(base), base->GetFormID());
			}
		}
		if (notYours || tinted) {
			rollover.SetMember("textColor", RE::GFxValue(notYours ? static_cast<double>(kNameColour) : plain));
			tinted = notYours;
		}
	}

	class CrosshairSink : public RE::BSTEventSink<SKSE::CrosshairRefEvent>
	{
	public:
		RE::BSEventNotifyControl ProcessEvent(const SKSE::CrosshairRefEvent* a_event, RE::BSTEventSource<SKSE::CrosshairRefEvent>*) override
		{
			const auto ref = a_event && a_event->crosshairRef ? a_event->crosshairRef->GetFormID() : 0;
			g_crosshairRef = ref;
			// After the game has written the new name.
			SKSE::GetTaskInterface()->AddUITask([ref]() { TintRollover(ref); });
			return RE::BSEventNotifyControl::kContinue;
		}
	};

	// "Iron Arrow (23)" -> "Iron Arrow": the window may give the name with its count on the end.
	[[nodiscard]] std::string WithoutCount(std::string a_name)
	{
		if (!a_name.empty() && a_name.back() == ')') {
			const auto open = a_name.rfind(" (");
			if (open != std::string::npos && a_name.find_first_not_of("0123456789", open + 2) == a_name.size() - 1) {
				a_name.erase(open);
			}
		}
		return a_name;
	}

	[[nodiscard]] std::int32_t NotYoursInTarget(const std::string& a_name, std::int32_t a_count)
	{
		const auto target = RE::TESForm::LookupByID<RE::TESObjectREFR>(g_crosshairRef.load());
		if (!target || a_name.empty()) {
			return 0;
		}
		const auto wanted = WithoutCount(a_name);
		for (const auto& [object, data] : target->GetInventory()) {
			const auto& entry = data.second;
			const auto  shown = entry ? entry->GetDisplayName() : nullptr;
			const auto  base = object ? object->GetName() : nullptr;
			if (object && ((shown && Rules::EqualsNoCase(shown, wanted)) || (base && Rules::EqualsNoCase(base, wanted)))) {
				return Events::NotYoursIfTaken(target->GetFormID(), object, std::max(1, a_count));
			}
		}
		return 0;
	}

	class LootRowHandler : public RE::GFxFunctionHandler
	{
	public:
		void Call(Params& a_params) override
		{
			if (!a_params.thisPtr) {
				return;
			}
			a_params.thisPtr->Invoke("zenitharSetData", nullptr, a_params.args, a_params.argCount);

			auto&        row = *a_params.thisPtr;
			RE::GFxValue mark;
			const auto   has = row.GetMember(kMarkName, &mark) && mark.IsDisplayObject();
			std::int32_t notYours = 0;
			std::string  shownName;
			if (a_params.argCount >= 1 && a_params.args[0].IsObject() && Events::OnChallenge() && Settings::bMarkInventory.GetValue()) {
				for (const auto member : { "displayName", "text", "name", "label" }) {
					RE::GFxValue name;
					if (a_params.args[0].GetMember(member, &name) && name.IsString()) {
						shownName = name.GetString();
						break;
					}
				}
				const auto count = static_cast<std::int32_t>(Number(a_params.args[0], "count", 1.0));
				// The row says which item it is; the name is only a fallback, since an enchanted or
				// renamed item is not shown under the name its record has.
				const auto formID = static_cast<RE::FormID>(Number(a_params.args[0], "formId", 0.0));
				const auto target = RE::TESForm::LookupByID<RE::TESObjectREFR>(g_crosshairRef.load());
				const auto object = formID != 0 ? RE::TESForm::LookupByID<RE::TESBoundObject>(formID) : nullptr;
				if (object && Rules::IsMagicItem(object)) {
					notYours = std::max(1, count);
				} else if (target && object) {
					notYours = Events::NotYoursIfTaken(target->GetFormID(), object, std::max(1, count));
				} else {
					notYours = NotYoursInTarget(shownName, count);
				}
			}
			// The first rows are written to the log in full, to see what the window hands over.
			static std::atomic_int logged{ 0 };
			if (logged < 12) {
				++logged;
				std::string members;
				if (a_params.argCount >= 1 && a_params.args[0].IsObject()) {
					a_params.args[0].VisitMembers([&members](const char* a_key, const RE::GFxValue& a_value) {
						members += std::format(" {}={}", a_key ? a_key : "?",
							a_value.IsString() ? a_value.GetString() : a_value.IsNumber() ? std::to_string(a_value.GetNumber()) : a_value.IsBool() ? (a_value.GetBool() ? "true" : "false") : "(object)");
					});
				}
				logger::debug("quick-loot row: {} argument(s), name \"{}\", looking at {:08X}, {} not yours;{}", a_params.argCount, shownName, g_crosshairRef.load(), notYours, members);
			}
			if (notYours <= 0) {
				if (has) {
					mark.SetMember("_visible", RE::GFxValue(false));
				}
				return;
			}
			// The row's name field: asked for under the names this window's row class uses, then found
			// by looking for the text field that shows the name.
			RE::GFxValue field;
			std::string  fieldName;
			for (const auto candidate : { "itemName", "textField", "label", "nameField" }) {
				RE::GFxValue value;
				if (row.GetMember(candidate, &value) && value.IsDisplayObject()) {
					field = value;
					fieldName = candidate;
					break;
				}
			}
			if (!field.IsDisplayObject()) {
				row.VisitMembers([&](const char* a_key, const RE::GFxValue& a_value) {
					if (field.IsDisplayObject() || !a_key || !a_value.IsDisplayObject()) {
						return;
					}
					RE::GFxValue text;
					if (a_value.GetMember("text", &text) && text.IsString() && Rules::ContainsNoCase(text.GetString(), shownName)) {
						field = a_value;
						fieldName = a_key;
					}
				});
			}
			static std::atomic_int told{ 0 };
			if (told < 3) {
				++told;
				std::string members;
				row.VisitMembers([&members](const char* a_key, const RE::GFxValue& a_value) {
					if (a_key && a_value.IsDisplayObject()) {
						members += std::format(" {}", a_key);
					}
				});
				logger::debug("quick-loot row for \"{}\": name field \"{}\"; display objects on the row:{}", shownName, fieldName, members);
			}
			if (!field.IsDisplayObject()) {
				return;
			}
			field.SetMember("textColor", RE::GFxValue(static_cast<double>(kNameColour)));
			if (!has) {
				RE::GFxValue markName;
				markName.SetString(kMarkName);
				const std::array<RE::GFxValue, 2> args{ markName, RE::GFxValue(kMarkDepth) };
				row.Invoke("createEmptyMovieClip", &mark, args.data(), args.size());
				if (!mark.IsDisplayObject()) {
					return;
				}
				DrawMark(mark);
			}
			const auto size = std::max(10.0, Number(field, "textHeight", 18.0) * 0.8);
			const auto markX = Number(field, "_x") + Number(field, "textWidth") + Number(row, "_totalIconWidth") + 12.0;
			// A name that fills its field leaves no room: the mark would sit on the value beside it.
			// The name's colour says it alone then.
			// The name field is only as wide as its text, so the limit is where the value column starts
			// in this window's row (measured from Dave's screenshots: the value's digits begin near 365).
			const auto fieldEnd = 350.0;
			static std::atomic_int placed{ 0 };
			if (placed < 6) {
				++placed;
				logger::debug("quick-loot mark for \"{}\": at {:.0f}, size {:.0f}, limit {:.0f}", shownName, markX, size, fieldEnd);
			}
			if (markX + size > fieldEnd) {
				mark.SetMember("_visible", RE::GFxValue(false));
				return;
			}
			mark.SetMember("_x", RE::GFxValue(markX));
			mark.SetMember("_y", RE::GFxValue(Number(field, "_y") + (Number(field, "textHeight", 18.0) - size) / 2.0 + 2.0));
			mark.SetMember("_xscale", RE::GFxValue(size / kMarkBox * 100.0));
			mark.SetMember("_yscale", RE::GFxValue(size / kMarkBox * 100.0));
			mark.SetMember("_visible", RE::GFxValue(true));
		}
	};

	// True once the quick-loot window's rows are wrapped (or were already).
	[[nodiscard]] bool WrapLootRows(const std::string& a_menu, bool a_lastTry)
	{
		static const auto lootHandler = RE::make_gptr<LootRowHandler>();
		const auto        ui = RE::UI::GetSingleton();
		RE::GPtr<RE::GFxMovieView> view;
		if (ui) {
			view = ui->GetMovieView(a_menu);
		}
		if (!view) {
			if (a_lastTry && !g_lootWrappedOnce) {
				logger::warn("quick-loot window \"{}\": no movie to wrap", a_menu);
			}
			return false;
		}
		RE::GFxValue proto;
		if (!view->GetVariable(&proto, "_global.QuickLoot.ListItemRenderer.prototype") || !proto.IsObject()) {
			if (a_lastTry) {
				logger::warn("quick-loot window \"{}\": its row class was not found", a_menu);
			}
			return false;
		}
		if (Flag(proto, "zenitharWrapped")) {
			return true;
		}
		RE::GFxValue original;
		if (!proto.GetMember("setData", &original) || !original.IsObject()) {
			if (a_lastTry) {
				logger::warn("quick-loot window \"{}\": its rows have no setData to wrap", a_menu);
			}
			return false;
		}
		RE::GFxValue wrapped;
		proto.SetMember("zenitharSetData", original);
		view->CreateFunction(&wrapped, lootHandler.get());
		proto.SetMember("setData", wrapped);
		proto.SetMember("zenitharWrapped", RE::GFxValue(true));
		g_lootWrappedOnce = true;
		logger::info("quick-loot rows wrapped for the mark (\"{}\")", a_menu);
		return true;
	}

	// The window loads its file a little after it opens, so this tries for a few seconds.
	void WrapLootRowsSoon(std::string a_menu, int a_triesLeft)
	{
		std::thread([menu = std::move(a_menu), a_triesLeft]() mutable {
			std::this_thread::sleep_for(150ms);
			SKSE::GetTaskInterface()->AddTask([menu = std::move(menu), a_triesLeft]() mutable {
				if (!WrapLootRows(menu, a_triesLeft <= 0) && a_triesLeft > 0) {
					WrapLootRowsSoon(std::move(menu), a_triesLeft - 1);
				}
			});
		}).detach();
	}

	// Wraps the list's two functions, once per screen. False when the screen's rows are not SkyUI's.
	[[nodiscard]] bool WrapRows(RE::GFxMovieView* a_view, bool a_ownScreen)
	{
		static const auto nameHandler = RE::make_gptr<NameHandler>();
		static const auto colourHandler = RE::make_gptr<ColourHandler>();

		RE::GFxValue proto;
		if (!a_view->GetVariable(&proto, "_global.InventoryListEntry.prototype") || !proto.IsObject()) {
			return false;
		}
		WrapCard(a_view);
		if (a_ownScreen) {
			WrapSelect(a_view);
		}
		if (Flag(proto, "zenitharWrapped")) {
			return true;
		}
		RE::GFxValue formatName;
		if (!proto.GetMember("formatName", &formatName) || !formatName.IsObject()) {
			static std::atomic_bool told{ false };
			if (!told.exchange(true)) {
				logger::warn("the row class is there but its formatName could not be wrapped; using the stolen icon instead");
			}
			return false;
		}
		RE::GFxValue wrapped;
		proto.SetMember("zenitharFormatName", formatName);
		a_view->CreateFunction(&wrapped, nameHandler.get());
		proto.SetMember("formatName", wrapped);

		RE::GFxValue formatColor;
		if (proto.GetMember("formatColor", &formatColor) && formatColor.IsObject()) {
			RE::GFxValue wrappedColour;
			proto.SetMember("zenitharFormatColor", formatColor);
			a_view->CreateFunction(&wrappedColour, colourHandler.get());
			proto.SetMember("formatColor", wrappedColour);
		}
		proto.SetMember("zenitharWrapped", RE::GFxValue(true));
		logger::info("inventory rows wrapped for the mark");
		return true;
	}

	// Called by SKSE for every row the game builds for an item list.
	void OnInventoryEntry(RE::GFxMovieView* a_view, RE::GFxValue* a_object, RE::InventoryEntryData* a_item)
	{
		if (!Settings::bMarkInventory.GetValue() || !Events::OnChallenge() || !a_view || !a_object || !a_object->IsObject() || !a_item) {
			return;
		}
		// The player's own inventory screen and a merchant's. The screen is
		// told apart by its file name, because rows are first built before the menu counts as open.
		const auto def = a_view->GetMovieDef();
		const auto url = def ? def->GetFileURL() : nullptr;
		if (g_logged < kMaxLogged) {
			++g_logged;
			RE::GFxValue text;
			const auto   hasText = a_object->GetMember("text", &text) && text.IsString();
			logger::debug("inventory row: screen \"{}\" item [{:08X}] text {}", url ? url : "?", a_item->object ? a_item->object->GetFormID() : 0,
				hasText ? text.GetString() : "(none)");
		}
		if (!url) {
			return;
		}
		const auto ownScreen = Rules::ContainsNoCase(url, "inventorymenu"sv);
		auto       holder = Ledger::kPlayer;
		// A merchant's or a chest's screen lists both sides. The game puts the two sides in different
		// bits of the row's filterFlag: the low ten are the player's side.
		if (!ownScreen) {
			if (!Rules::ContainsNoCase(url, "bartermenu"sv) && !Rules::ContainsNoCase(url, "containermenu"sv)) {
				return;
			}
			RE::GFxValue flag;
			const auto   hasFlag = a_object->GetMember("filterFlag", &flag) && flag.IsNumber();
			const auto   bits = hasFlag ? static_cast<std::uint32_t>(flag.GetNumber()) : 0u;
			if (g_loggedOther < kMaxLogged) {
				++g_loggedOther;
				logger::debug("row on \"{}\": item [{:08X}] filterFlag {:#x}", url, a_item->object ? a_item->object->GetFormID() : 0, bits);
			}
			if ((bits & 0x3FFu) == 0) {
				// The other side. A merchant's wares are never yours, so marking them all says nothing;
				// a chest's contents are worth marking, because your own work may be stored in it.
				if (!Rules::ContainsNoCase(url, "containermenu"sv)) {
					return;
				}
				const auto target = RE::TESObjectREFR::LookupByHandle(RE::ContainerMenu::GetTargetRefHandle());
				if (!target) {
					return;
				}
				holder = target->GetFormID();
			}
		}
		const RE::TESBoundObject* item = a_item->object;  // not GetObject(): windows.h defines that name as a macro
		const auto magic = Rules::IsMagicItem(item);
		if (!item || (!magic && !Rules::MustBeOwn(item))) {
			return;
		}

		RE::GFxValue countValue;
		std::int32_t count = std::max(1, a_item->countDelta);
		if (a_object->GetMember("count", &countValue) && countValue.IsNumber()) {
			count = static_cast<std::int32_t>(countValue.GetNumber());
		}
		// A scroll or staff is marked whoever holds it: it cannot be used at all.
		const auto notYours = magic ? count :
							  holder == Ledger::kPlayer ? count - std::min(count, Ledger::Get().Earned(holder, item->GetFormID())) :
														  Events::NotYoursIfTaken(holder, item, count);
		if (notYours <= 0) {
			return;
		}
		if (magic) {
			a_object->SetMember("zenitharMagic", RE::GFxValue(true));
		}

		// What the wrapped row functions read: how many copies are not yours, and whether that is all of them.
		a_object->SetMember("zenitharNotYours", RE::GFxValue(static_cast<double>(notYours)));
		a_object->SetMember("zenitharAll", RE::GFxValue(notYours >= count));
		{
			RE::GFxValue plainName;
			a_view->CreateString(&plainName, Rules::ItemName(item).c_str());
			a_object->SetMember("zenitharName", plainName);
		}
		if (Settings::bMarkWithIcon.GetValue() && !WrapRows(a_view, ownScreen) && notYours >= count) {
			// Not a SkyUI-style list: fall back on the screen's own stolen icon and colour.
			a_object->SetMember("isStealing", RE::GFxValue(true));
		}
		// The words stay for a mixed stack (the count matters there), or for every row if asked.
		if (Settings::bMarkWithText.GetValue() || notYours < count) {
			RE::GFxValue textValue;
			if (!a_object->GetMember("text", &textValue) || !textValue.IsString()) {
				return;
			}
			const std::string name = textValue.GetString();
			const auto        marked = notYours >= count ? std::format("{} [not yours]", name) : std::format("{} [{} not yours]", name, notYours);
			RE::GFxValue      markedValue;
			a_view->CreateString(&markedValue, marked.c_str());
			a_object->SetMember("text", markedValue);
		}
	}
}

void InventoryMarks::Register()
{
	if (const auto scaleform = SKSE::GetScaleformInterface()) {
		scaleform->Register(OnInventoryEntry);
		logger::info("inventory marks registered");
	} else {
		logger::warn("no scaleform interface; inventory marks are off");
	}
}

void InventoryMarks::OnDataLoaded()
{
	static CrosshairSink sink;
	if (const auto source = SKSE::GetCrosshairRefEventSource()) {
		source->AddEventSink(std::addressof(sink));
	}
}

void InventoryMarks::OnMenuOpening(std::string_view a_menu)
{
	if (!Settings::bMarkInventory.GetValue() || !Rules::ContainsNoCase(a_menu, "lootmenu"sv)) {
		return;
	}
	static std::atomic_bool told{ false };
	if (!told.exchange(true)) {
		logger::info("quick-loot window opening as \"{}\"", a_menu);
	}
	WrapLootRowsSoon(std::string(a_menu), 20);
}
