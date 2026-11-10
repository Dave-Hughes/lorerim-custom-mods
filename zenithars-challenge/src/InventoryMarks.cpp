#include "InventoryMarks.h"

#include "Ledger.h"
#include "Rules.h"
#include "Settings.h"

namespace
{
	// The first rows seen are written to the log, to show what the screen hands over.
	constexpr int    kMaxLogged = 40;
	std::atomic_int  g_logged{ 0 };
	std::atomic_int  g_loggedOther{ 0 };

	[[nodiscard]] bool IsPlayersEntry(const RE::InventoryEntryData* a_entry)
	{
		const auto player = RE::PlayerCharacter::GetSingleton();
		const auto changes = player ? player->GetInventoryChanges() : nullptr;
		if (!changes || !changes->entryList) {
			return false;
		}
		for (const auto entry : *changes->entryList) {
			if (entry == a_entry) {
				return true;
			}
		}
		return false;
	}

	// Called by SKSE for every row the game builds for an item list.
	void OnInventoryEntry(RE::GFxMovieView* a_view, RE::GFxValue* a_object, RE::InventoryEntryData* a_item)
	{
		if (!Settings::bMarkInventory.GetValue() || !a_view || !a_object || !a_object->IsObject() || !a_item) {
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
		if (!ownScreen && g_loggedOther < kMaxLogged) {
			++g_loggedOther;
			logger::debug("row on \"{}\": item [{:08X}] is {} the player's pack", url, a_item->object ? a_item->object->GetFormID() : 0, IsPlayersEntry(a_item) ? "in" : "not in");
		}
		// A merchant's screen lists both sides. A row is the player's when its entry is one of the
		// entries in the player's own pack.
		if (!ownScreen && !(Rules::ContainsNoCase(url, "bartermenu"sv) && IsPlayersEntry(a_item))) {
			return;
		}
		const RE::TESBoundObject* item = a_item->object;  // not GetObject(): windows.h defines that name as a macro
		if (!item || !Rules::MustBeOwn(item)) {
			return;
		}

		RE::GFxValue countValue;
		std::int32_t count = std::max(1, a_item->countDelta);
		if (a_object->GetMember("count", &countValue) && countValue.IsNumber()) {
			count = static_cast<std::int32_t>(countValue.GetNumber());
		}
		const auto earned = Ledger::Get().Earned(Ledger::kPlayer, item->GetFormID());
		const auto notYours = count - std::min(count, earned);
		if (notYours <= 0) {
			return;
		}

		// A row that is wholly not yours gets the screen's own "not the player's" look: the stolen icon
		// after the name and the stolen text colour. SkyUI-style lists (LoreRim's Norden UI is one)
		// read both from isStealing, which nothing else sets on these screens. iconColor is no use:
		// the screen's own script overwrites it after this runs.
		a_object->SetMember("zenitharNotYours", RE::GFxValue(static_cast<double>(notYours)));
		if (Settings::bMarkWithIcon.GetValue() && notYours >= count) {
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
