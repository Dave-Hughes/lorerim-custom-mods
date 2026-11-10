#include "InventoryMarks.h"

#include "Ledger.h"
#include "Rules.h"
#include "Settings.h"

namespace
{
	// The first rows seen are written to the log, to show what the screen hands over.
	constexpr int    kMaxLogged = 40;
	std::atomic_int  g_logged{ 0 };

	// Called by SKSE for every row the game builds for an item list.
	void OnInventoryEntry(RE::GFxMovieView* a_view, RE::GFxValue* a_object, RE::InventoryEntryData* a_item)
	{
		if (!Settings::bMarkInventory.GetValue() || !a_view || !a_object || !a_object->IsObject() || !a_item) {
			return;
		}
		// Only the player's own inventory screen: there every row is the player's. Container and
		// barter screens list both sides and the row does not say which side it is on. The screen is
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
		if (!url || !Rules::ContainsNoCase(url, "inventorymenu"sv)) {
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

		RE::GFxValue textValue;
		if (!a_object->GetMember("text", &textValue) || !textValue.IsString()) {
			return;
		}
		const std::string name = textValue.GetString();
		const auto        marked = notYours >= count ? std::format("{} [not yours]", name) : std::format("{} [{} not yours]", name, notYours);

		RE::GFxValue markedValue;
		a_view->CreateString(&markedValue, marked.c_str());
		a_object->SetMember("text", markedValue);
		a_object->SetMember("zenitharNotYours", RE::GFxValue(static_cast<double>(notYours)));
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
