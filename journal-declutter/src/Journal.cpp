#include "Journal.h"

#include "HiddenStore.h"
#include "Settings.h"

namespace Journal
{
	namespace
	{
		using RE::GFxValue;

		struct EntryKey
		{
			RE::FormID    formID{ 0 };
			std::uint32_t instance{ 0 };
		};

		bool IsObj(const GFxValue& a_val)
		{
			return a_val.IsObject();  // kObject, kArray or kDisplayObject
		}

		bool GetNumber(const GFxValue& a_obj, const char* a_name, double& a_out)
		{
			GFxValue v;
			if (!a_obj.GetMember(a_name, &v) || !v.IsNumber()) {
				return false;
			}
			a_out = v.GetNumber();
			return true;
		}

		bool GetBool(const GFxValue& a_obj, const char* a_name)
		{
			GFxValue v;
			if (!a_obj.GetMember(a_name, &v)) {
				return false;
			}
			if (v.IsBool()) {
				return v.GetBool();
			}
			if (v.IsNumber()) {
				return v.GetNumber() != 0.0;
			}
			return false;
		}

		bool GetArrayMember(const GFxValue& a_obj, const char* a_name, GFxValue& a_out)
		{
			return a_obj.GetMember(a_name, &a_out) && a_out.IsArray();
		}

		bool IsDivider(const GFxValue& a_entry)
		{
			return GetBool(a_entry, "divider");
		}

		bool IsCompleted(const GFxValue& a_entry)
		{
			return GetBool(a_entry, "completed");
		}

		// Returns nothing for dividers and malformed entries. The Miscellaneous header is a
		// valid entry with formID 0; callers decide what to do with it.
		std::optional<EntryKey> KeyOf(const GFxValue& a_entry)
		{
			if (!IsObj(a_entry) || IsDivider(a_entry)) {
				return std::nullopt;
			}
			double formID = 0.0;
			if (!GetNumber(a_entry, "formID", formID)) {
				return std::nullopt;
			}
			double instance = 0.0;
			GetNumber(a_entry, "instance", instance);
			return EntryKey{
				static_cast<RE::FormID>(static_cast<std::uint64_t>(formID)),
				static_cast<std::uint32_t>(static_cast<std::uint64_t>(instance))
			};
		}

		int GetSelectedIndex(const GFxValue& a_list)
		{
			double d = -1.0;
			return GetNumber(a_list, "selectedIndex", d) ? static_cast<int>(d) : -1;
		}

		void SetSelectedIndex(GFxValue& a_list, int a_idx)
		{
			a_list.SetMember("selectedIndex", GFxValue(static_cast<double>(a_idx)));
		}

		std::optional<EntryKey> SelectedKey(const GFxValue& a_list)
		{
			GFxValue sel;
			if (!a_list.GetMember("selectedEntry", &sel) || !IsObj(sel)) {
				return std::nullopt;
			}
			return KeyOf(sel);
		}

		int FindIndex(GFxValue& a_entryList, const EntryKey& a_key)
		{
			const auto size = a_entryList.GetArraySize();
			for (std::uint32_t i = 0; i < size; ++i) {
				GFxValue e;
				if (!a_entryList.GetElement(i, &e)) {
					continue;
				}
				const auto k = KeyOf(e);
				if (k && k->formID == a_key.formID && k->instance == a_key.instance) {
					return static_cast<int>(i);
				}
			}
			return -1;
		}

		GFxValue GetOrCreateArray(Context& a_ctx, const char* a_name, bool a_reset)
		{
			GFxValue arr;
			if (!a_reset && a_ctx.page.GetMember(a_name, &arr) && arr.IsArray()) {
				return arr;
			}
			a_ctx.movie->CreateArray(&arr);
			a_ctx.page.SetMember(a_name, arr);
			return arr;
		}

		void ClearArray(GFxValue& a_arr)
		{
			a_arr.ClearElements();
		}

		void SetPrefix(Context& a_ctx, GFxValue& a_entry)
		{
			const auto& prefix = Settings::sHiddenPrefix.GetValue();
			if (prefix.empty()) {
				return;
			}
			GFxValue t;
			if (!a_entry.GetMember("text", &t) || !t.IsString() || !t.GetString()) {
				return;
			}
			const std::string text = t.GetString();
			if (text.starts_with(prefix)) {
				return;
			}
			GFxValue ns;
			a_ctx.movie->CreateString(&ns, (prefix + text).c_str());
			a_entry.SetMember("text", ns);
		}

		void StripPrefix(Context& a_ctx, GFxValue& a_entry)
		{
			const auto& prefix = Settings::sHiddenPrefix.GetValue();
			if (prefix.empty()) {
				return;
			}
			GFxValue t;
			if (!a_entry.GetMember("text", &t) || !t.IsString() || !t.GetString()) {
				return;
			}
			const std::string text = t.GetString();
			if (!text.starts_with(prefix)) {
				return;
			}
			GFxValue ns;
			a_ctx.movie->CreateString(&ns, text.substr(prefix.size()).c_str());
			a_entry.SetMember("text", ns);
		}

		void Sound(const char* a_editorID)
		{
			if (Settings::bPlaySounds.GetValue()) {
				RE::PlaySound(a_editorID);
			}
		}

		// A hidden quest should not keep pointing at the compass either. The Flash entry carries
		// its own copy of the active flag (it draws the marker icon), so keep that in step.
		void Deactivate(GFxValue& a_entry, RE::FormID a_formID)
		{
			if (!Settings::bDeactivateHiddenQuests.GetValue() || a_formID == 0) {
				return;
			}
			if (const auto quest = RE::TESForm::LookupByID<RE::TESQuest>(a_formID)) {
				quest->data.flags.reset(RE::QuestFlag::kActive);
			}
			if (IsObj(a_entry)) {
				a_entry.SetMember("active", GFxValue(false));
			}
		}

		// The centred list makes whatever sits at the scroll position the selection, and the
		// game's own navigation never lands on the divider; neither should ours.
		int SelectableIndex(GFxValue& a_entryList, int a_idx)
		{
			const auto size = static_cast<int>(a_entryList.GetArraySize());
			if (size <= 0) {
				return -1;
			}
			a_idx = std::clamp(a_idx, 0, size - 1);
			GFxValue e;
			if (!a_entryList.GetElement(static_cast<std::uint32_t>(a_idx), &e) || !IsObj(e) || !IsDivider(e)) {
				return a_idx;
			}
			if (a_idx > 0) {
				return a_idx - 1;
			}
			return size > 1 ? 1 : -1;
		}

		bool IsViewingMisc(Context& a_ctx)
		{
			if (!GetBool(a_ctx.page, "bHasMiscQuests")) {
				return false;
			}
			const auto key = SelectedKey(a_ctx.titleList);
			return key && key->formID == 0;
		}

		// The page shows a divider between unfinished and finished quests only when both exist.
		// Hiding can empty one group; restoring can refill it. Keep the divider consistent.
		void FixDivider(Context& a_ctx, GFxValue& a_entryList)
		{
			int    dividerIdx = -1;
			int    open = 0;
			int    done = 0;
			double minDoneTime = 1.0e300;
			const auto size = a_entryList.GetArraySize();
			for (std::uint32_t i = 0; i < size; ++i) {
				GFxValue e;
				if (!a_entryList.GetElement(i, &e) || !IsObj(e)) {
					continue;
				}
				if (IsDivider(e)) {
					dividerIdx = static_cast<int>(i);
					continue;
				}
				if (!KeyOf(e)) {
					continue;
				}
				if (IsCompleted(e)) {
					++done;
					double t = 0.0;
					if (GetNumber(e, "timeIndex", t)) {
						minDoneTime = std::min(minDoneTime, t);
					}
				} else {
					++open;
				}
			}

			const bool need = open > 0 && done > 0;
			if (dividerIdx >= 0 && !need) {
				GFxValue d;
				if (a_entryList.GetElement(static_cast<std::uint32_t>(dividerIdx), &d)) {
					a_ctx.page.SetMember(kDividerVar, d);
				}
				a_entryList.RemoveElements(static_cast<std::uint32_t>(dividerIdx), 1);
			} else if (dividerIdx < 0 && need) {
				GFxValue d;
				if (!(a_ctx.page.GetMember(kDividerVar, &d) && IsObj(d))) {
					a_ctx.movie->CreateObject(&d);
					d.SetMember("divider", GFxValue(true));
					d.SetMember("completed", GFxValue(true));
				}
				if (minDoneTime < 1.0e299) {
					d.SetMember("timeIndex", GFxValue(minDoneTime - 0.5));
				}
				a_entryList.PushBack(d);
			}
		}

		// Re-run the page's own comparator so re-inserted entries land where the game put them.
		void SortEntries(Context& a_ctx, GFxValue& a_entryList)
		{
			GFxValue fn;
			if (!a_ctx.page.GetMember("completedQuestSort", &fn) || fn.IsUndefined() || fn.IsNull()) {
				logger::warn("completedQuestSort not found on quests page; list left unsorted");
				return;
			}
			a_entryList.Invoke("sort", nullptr, &fn, 1);
		}

		void RefreshTitles(Context& a_ctx, GFxValue& a_entryList, const std::optional<EntryKey>& a_selKey, int a_fallbackIdx)
		{
			a_ctx.titleList.Invoke("InvalidateData");
			int idx = a_selKey ? FindIndex(a_entryList, *a_selKey) : -1;
			idx = SelectableIndex(a_entryList, idx < 0 ? a_fallbackIdx : idx);
			if (idx >= 0) {
				std::array<GFxValue, 2> args{ GFxValue(static_cast<double>(idx)), GFxValue(true) };
				a_ctx.titleList.Invoke("RestoreScrollPosition", nullptr, args.data(), args.size());
			}
			a_ctx.titleList.Invoke("UpdateList");
			a_ctx.page.Invoke("onQuestHighlight");
		}

		void RefreshObjectives(Context& a_ctx, GFxValue& a_objList, int a_fallbackIdx)
		{
			a_ctx.objectiveList.Invoke("InvalidateData");
			const auto size = static_cast<int>(a_objList.GetArraySize());
			SetSelectedIndex(a_ctx.objectiveList, size > 0 ? std::clamp(a_fallbackIdx, 0, size - 1) : -1);
			a_ctx.objectiveList.Invoke("UpdateList");
		}

		// Drops the cached objective list of the Miscellaneous entry so the page asks the game
		// for it again (through our hook) the next time it is highlighted.
		void InvalidateMiscObjectives(Context& a_ctx, GFxValue& a_entryList)
		{
			const auto size = a_entryList.GetArraySize();
			for (std::uint32_t i = 0; i < size; ++i) {
				GFxValue e;
				if (!a_entryList.GetElement(i, &e)) {
					continue;
				}
				const auto k = KeyOf(e);
				if (k && k->formID == 0) {
					e.DeleteMember("objectives");
				}
			}
		}

		void HideMiscAt(Context& a_ctx, int a_idx)
		{
			GFxValue objList;
			if (!GetArrayMember(a_ctx.objectiveList, "entryList", objList)) {
				Sound("UIMenuCancel");
				return;
			}
			if (a_idx < 0 || static_cast<std::uint32_t>(a_idx) >= objList.GetArraySize()) {
				Sound("UIMenuCancel");
				return;
			}
			GFxValue e;
			objList.GetElement(static_cast<std::uint32_t>(a_idx), &e);
			const auto key = KeyOf(e);
			if (!key) {
				Sound("UIMenuCancel");
				return;
			}

			// One quest can list several objectives here; the game toggles them as a group
			// (see QuestsPage.onToggleQuestActive), so hide and restore them as a group too.
			const auto sameQuest = [&](const GFxValue& a_other) {
				const auto k = KeyOf(a_other);
				return k && k->formID == key->formID && k->instance == key->instance;
			};

			auto& store = HiddenStore::Get();
			if (store.showHidden) {
				const bool unhide = store.IsMiscHidden(key->formID, key->instance);
				if (unhide) {
					store.UnhideMisc(key->formID, key->instance);
				} else {
					store.HideMisc(key->formID, key->instance);
				}
				const auto size = objList.GetArraySize();
				for (std::uint32_t i = 0; i < size; ++i) {
					GFxValue o;
					if (!objList.GetElement(i, &o) || !sameQuest(o)) {
						continue;
					}
					if (unhide) {
						StripPrefix(a_ctx, o);
					} else {
						SetPrefix(a_ctx, o);
						Deactivate(o, key->formID);
					}
				}
				a_ctx.objectiveList.Invoke("InvalidateData");
				a_ctx.objectiveList.Invoke("UpdateList");
				Sound(unhide ? "UIQuestActive" : "UIQuestInactive");
				return;
			}

			store.HideMisc(key->formID, key->instance);
			for (auto i = static_cast<int>(objList.GetArraySize()) - 1; i >= 0; --i) {
				GFxValue o;
				if (objList.GetElement(static_cast<std::uint32_t>(i), &o) && sameQuest(o)) {
					Deactivate(o, key->formID);
					objList.RemoveElements(static_cast<std::uint32_t>(i), 1);
				}
			}
			RefreshObjectives(a_ctx, objList, a_idx);
			Sound("UIQuestInactive");
		}
	}

	bool GetContext(Context& a_out, RE::GFxMovieView* a_movie, const GFxValue* a_titleListHint)
	{
		if (!a_movie) {
			const auto ui = RE::UI::GetSingleton();
			if (!ui) {
				return false;
			}
			const auto menu = ui->GetMenu(RE::JournalMenu::MENU_NAME);
			if (!menu || !menu->uiMovie) {
				return false;
			}
			a_movie = menu->uiMovie.get();
		}
		a_out.movie = a_movie;

		GFxValue page;
		bool ok = a_movie->GetVariable(&page, kPagePath.data()) && IsObj(page);
		if (!ok && a_titleListHint && IsObj(*a_titleListHint)) {
			// TitleList lives at Page_mc.TitleList_mc.List_mc, so the page is two parents up.
			GFxValue p1;
			GFxValue p2;
			if (a_titleListHint->GetMember("_parent", &p1) && IsObj(p1) &&
				p1.GetMember("_parent", &p2) && IsObj(p2)) {
				page = p2;
				ok = true;
			}
		}
		if (!ok) {
			static bool warned = false;
			if (!warned) {
				warned = true;
				logger::warn("quests page not found at {}; is a non-SkyUI journal installed?", kPagePath);
			}
			return false;
		}
		a_out.page = page;

		if (!(page.GetMember("TitleList", &a_out.titleList) && IsObj(a_out.titleList))) {
			if (a_titleListHint && IsObj(*a_titleListHint)) {
				a_out.titleList = *a_titleListHint;
			} else {
				logger::warn("TitleList not found on quests page");
				return false;
			}
		}
		if (!(page.GetMember("ObjectiveList", &a_out.objectiveList) && IsObj(a_out.objectiveList))) {
			a_out.objectiveList.SetUndefined();
		}
		return true;
	}

	bool IsQuestsTabActive(const Context& a_ctx)
	{
		GFxValue tab;
		if (!a_ctx.movie->GetVariable(&tab, kTabPath.data()) || !tab.IsNumber()) {
			return true;  // skin without tabs: assume the quests page is what is showing
		}
		return tab.GetNumber() == 0.0;
	}

	void OnQuestsPopulated(Context& a_ctx)
	{
		auto& store = HiddenStore::Get();

		GFxValue entryList;
		if (!GetArrayMember(a_ctx.titleList, "entryList", entryList)) {
			logger::warn("TitleList.entryList missing after RequestQuestsData");
			return;
		}

		// Fresh movie, fresh parking lot.
		auto parked = GetOrCreateArray(a_ctx, kHiddenTitlesVar, true);
		a_ctx.page.DeleteMember(kDividerVar);

		if (store.QuestCount() == 0) {
			return;
		}

		const auto selKey = SelectedKey(a_ctx.titleList);
		const int  fallbackIdx = GetSelectedIndex(a_ctx.titleList);

		int removed = 0;
		int marked = 0;
		for (auto i = static_cast<int>(entryList.GetArraySize()) - 1; i >= 0; --i) {
			GFxValue e;
			if (!entryList.GetElement(static_cast<std::uint32_t>(i), &e)) {
				continue;
			}
			const auto key = KeyOf(e);
			if (!key || !store.IsHidden(key->formID, key->instance)) {
				continue;
			}
			if (store.showHidden) {
				SetPrefix(a_ctx, e);
				++marked;
			} else {
				parked.PushBack(e);
				entryList.RemoveElements(static_cast<std::uint32_t>(i), 1);
				++removed;
			}
		}

		if (removed > 0 || marked > 0) {
			FixDivider(a_ctx, entryList);
			RefreshTitles(a_ctx, entryList, selKey, fallbackIdx);
		}
		logger::debug("quests populated: {} removed, {} marked, {} shown", removed, marked, entryList.GetArraySize());
	}

	void OnObjectivesPopulated(Context& a_ctx)
	{
		if (!IsViewingMisc(a_ctx)) {
			return;
		}
		auto& store = HiddenStore::Get();

		GFxValue sel;
		if (!a_ctx.titleList.GetMember("selectedEntry", &sel) || !IsObj(sel)) {
			return;
		}
		GFxValue objectives;
		if (!GetArrayMember(sel, "objectives", objectives)) {
			return;
		}

		if (store.MiscCount() == 0) {
			return;
		}

		int removed = 0;
		for (auto i = static_cast<int>(objectives.GetArraySize()) - 1; i >= 0; --i) {
			GFxValue e;
			if (!objectives.GetElement(static_cast<std::uint32_t>(i), &e)) {
				continue;
			}
			const auto key = KeyOf(e);
			if (!key || !store.IsMiscHidden(key->formID, key->instance)) {
				continue;
			}
			if (store.showHidden) {
				SetPrefix(a_ctx, e);
			} else {
				objectives.RemoveElements(static_cast<std::uint32_t>(i), 1);
				++removed;
			}
		}
		logger::debug("misc objectives populated: {} removed, {} shown", removed, objectives.GetArraySize());
	}

	void HideSelected(Context& a_ctx)
	{
		GFxValue entryList;
		if (!GetArrayMember(a_ctx.titleList, "entryList", entryList)) {
			return;
		}

		if (IsViewingMisc(a_ctx) && IsObj(a_ctx.objectiveList)) {
			const int objIdx = GetSelectedIndex(a_ctx.objectiveList);
			if (objIdx >= 0) {
				HideMiscAt(a_ctx, objIdx);
				return;
			}
		}

		const int  idx = GetSelectedIndex(a_ctx.titleList);
		const auto size = static_cast<int>(entryList.GetArraySize());
		if (idx < 0 || idx >= size) {
			Sound("UIMenuCancel");
			return;
		}
		GFxValue e;
		entryList.GetElement(static_cast<std::uint32_t>(idx), &e);
		const auto key = KeyOf(e);
		if (!key || key->formID == 0) {  // divider, or the Miscellaneous header itself
			Sound("UIMenuCancel");
			return;
		}

		auto& store = HiddenStore::Get();
		if (store.showHidden) {
			if (store.IsHidden(key->formID, key->instance)) {
				store.Unhide(key->formID, key->instance);
				StripPrefix(a_ctx, e);
				Sound("UIQuestActive");
			} else {
				store.Hide(key->formID, key->instance);
				SetPrefix(a_ctx, e);
				Deactivate(e, key->formID);
				Sound("UIQuestInactive");
			}
			a_ctx.titleList.Invoke("InvalidateData");
			a_ctx.titleList.Invoke("UpdateList");
			a_ctx.page.Invoke("onQuestHighlight");
			return;
		}

		store.Hide(key->formID, key->instance);
		Deactivate(e, key->formID);
		auto parked = GetOrCreateArray(a_ctx, kHiddenTitlesVar, false);
		parked.PushBack(e);
		entryList.RemoveElements(static_cast<std::uint32_t>(idx), 1);
		FixDivider(a_ctx, entryList);
		RefreshTitles(a_ctx, entryList, std::nullopt, idx);
		Sound("UIQuestInactive");
	}

	void ToggleShowHidden(Context& a_ctx)
	{
		auto& store = HiddenStore::Get();
		GFxValue entryList;
		if (!GetArrayMember(a_ctx.titleList, "entryList", entryList)) {
			return;
		}
		store.showHidden = !store.showHidden;

		const auto selKey = SelectedKey(a_ctx.titleList);
		const int  fallbackIdx = GetSelectedIndex(a_ctx.titleList);
		auto       parked = GetOrCreateArray(a_ctx, kHiddenTitlesVar, false);

		if (store.showHidden) {
			const auto n = parked.GetArraySize();
			for (std::uint32_t i = 0; i < n; ++i) {
				GFxValue e;
				if (parked.GetElement(i, &e)) {
					SetPrefix(a_ctx, e);
					entryList.PushBack(e);
				}
			}
			ClearArray(parked);
			FixDivider(a_ctx, entryList);
			SortEntries(a_ctx, entryList);
		} else {
			for (auto i = static_cast<int>(entryList.GetArraySize()) - 1; i >= 0; --i) {
				GFxValue e;
				if (!entryList.GetElement(static_cast<std::uint32_t>(i), &e)) {
					continue;
				}
				const auto key = KeyOf(e);
				if (key && store.IsHidden(key->formID, key->instance)) {
					StripPrefix(a_ctx, e);
					parked.PushBack(e);
					entryList.RemoveElements(static_cast<std::uint32_t>(i), 1);
				}
			}
			FixDivider(a_ctx, entryList);
		}

		InvalidateMiscObjectives(a_ctx, entryList);
		RefreshTitles(a_ctx, entryList, selKey, fallbackIdx);
		Sound("UIMenuFocus");
		logger::debug("show hidden: {}", store.showHidden);
	}

	void RestoreAll(Context& a_ctx)
	{
		auto& store = HiddenStore::Get();
		GFxValue entryList;
		if (!GetArrayMember(a_ctx.titleList, "entryList", entryList)) {
			return;
		}
		const auto hadQuests = store.QuestCount();
		const auto hadMisc = store.MiscCount();
		store.Clear();

		const auto selKey = SelectedKey(a_ctx.titleList);
		const int  fallbackIdx = GetSelectedIndex(a_ctx.titleList);

		auto       parked = GetOrCreateArray(a_ctx, kHiddenTitlesVar, false);
		const auto n = parked.GetArraySize();
		for (std::uint32_t i = 0; i < n; ++i) {
			GFxValue e;
			if (parked.GetElement(i, &e)) {
				StripPrefix(a_ctx, e);
				entryList.PushBack(e);
			}
		}
		ClearArray(parked);

		// Entries shown with a prefix (show-hidden mode) are already in the list.
		const auto size = entryList.GetArraySize();
		for (std::uint32_t i = 0; i < size; ++i) {
			GFxValue e;
			if (entryList.GetElement(i, &e)) {
				StripPrefix(a_ctx, e);
			}
		}

		FixDivider(a_ctx, entryList);
		SortEntries(a_ctx, entryList);
		InvalidateMiscObjectives(a_ctx, entryList);
		RefreshTitles(a_ctx, entryList, selKey, fallbackIdx);
		Sound("UIQuestActive");
		logger::info("restored all: {} quests, {} misc objectives", hadQuests, hadMisc);
	}
}
