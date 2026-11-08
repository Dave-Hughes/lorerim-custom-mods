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

		constexpr std::string_view kOrigUpdateButtonsVar = "jdc_origUpdateButtons";
		constexpr const char*      kHideLabel = "Hide";
		constexpr const char*      kShowHiddenLabel = "Show Hidden";
		constexpr const char*      kRestoreAllLabel = "Restore All";
		constexpr const char*      kHiddenViewTitle = "HIDDEN QUESTS";
		constexpr const char*      kHiddenViewDescription =
			"Quests and objectives you have hidden. Highlight one on the right and press the Hide key to bring it back.";

		// ---- small GFx helpers ---------------------------------------------------------------

		bool IsObj(const GFxValue& a_val)
		{
			return a_val.IsObject();  // kObject, kArray or kDisplayObject
		}

		bool GetNumber(const GFxValue& a_obj, const char* a_name, double& a_out)
		{
			if (!IsObj(a_obj)) {
				return false;
			}
			GFxValue v;
			if (!a_obj.GetMember(a_name, &v) || !v.IsNumber()) {
				return false;
			}
			a_out = v.GetNumber();
			return true;
		}

		bool GetBool(const GFxValue& a_obj, const char* a_name)
		{
			if (!IsObj(a_obj)) {
				return false;
			}
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

		std::string GetString(const GFxValue& a_obj, const char* a_name)
		{
			if (!IsObj(a_obj)) {
				return {};
			}
			GFxValue v;
			if (!a_obj.GetMember(a_name, &v) || !v.IsString() || !v.GetString()) {
				return {};
			}
			return v.GetString();
		}

		bool GetArrayMember(const GFxValue& a_obj, const char* a_name, GFxValue& a_out)
		{
			return IsObj(a_obj) && a_obj.GetMember(a_name, &a_out) && a_out.IsArray();
		}

		void SetString(Context& a_ctx, GFxValue& a_obj, const char* a_name, const std::string& a_text)
		{
			GFxValue s;
			a_ctx.movie->CreateString(&s, a_text.c_str());
			a_obj.SetMember(a_name, s);
		}

		bool IsDivider(const GFxValue& a_entry)
		{
			return GetBool(a_entry, "divider");
		}

		bool IsCompleted(const GFxValue& a_entry)
		{
			return GetBool(a_entry, "completed");
		}

		bool IsHiddenView(const GFxValue& a_entry)
		{
			return GetBool(a_entry, kHiddenViewFlag);
		}

		// Returns nothing for dividers, our pseudo-entry and malformed entries. The
		// Miscellaneous header is a valid entry with formID 0; callers decide what to do with it.
		std::optional<EntryKey> KeyOf(const GFxValue& a_entry)
		{
			if (!IsObj(a_entry) || IsDivider(a_entry) || IsHiddenView(a_entry)) {
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

		bool GetSelectedEntry(const GFxValue& a_list, GFxValue& a_out)
		{
			return IsObj(a_list) && a_list.GetMember("selectedEntry", &a_out) && IsObj(a_out);
		}

		std::optional<EntryKey> SelectedKey(const GFxValue& a_list)
		{
			GFxValue sel;
			return GetSelectedEntry(a_list, sel) ? KeyOf(sel) : std::nullopt;
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

		int FindHiddenViewIndex(GFxValue& a_entryList)
		{
			const auto size = a_entryList.GetArraySize();
			for (std::uint32_t i = 0; i < size; ++i) {
				GFxValue e;
				if (a_entryList.GetElement(i, &e) && IsHiddenView(e)) {
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

		void SetPrefix(Context& a_ctx, GFxValue& a_entry)
		{
			const auto& prefix = Settings::sHiddenPrefix.GetValue();
			if (prefix.empty()) {
				return;
			}
			const std::string text = GetString(a_entry, "text");
			if (text.empty() || text.starts_with(prefix)) {
				return;
			}
			SetString(a_ctx, a_entry, "text", prefix + text);
		}

		void StripPrefix(Context& a_ctx, GFxValue& a_entry)
		{
			const auto& prefix = Settings::sHiddenPrefix.GetValue();
			if (prefix.empty()) {
				return;
			}
			const std::string text = GetString(a_entry, "text");
			if (!text.starts_with(prefix)) {
				return;
			}
			SetString(a_ctx, a_entry, "text", text.substr(prefix.size()));
		}

		std::string TitleWithoutPrefix(const GFxValue& a_entry)
		{
			std::string text = GetString(a_entry, "text");
			const auto& prefix = Settings::sHiddenPrefix.GetValue();
			if (!prefix.empty() && text.starts_with(prefix)) {
				text.erase(0, prefix.size());
			}
			return text;
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
			if (!a_entryList.GetElement(static_cast<std::uint32_t>(a_idx), &e) || !IsDivider(e)) {
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

		bool IsViewingHidden(Context& a_ctx)
		{
			GFxValue sel;
			return GetSelectedEntry(a_ctx.titleList, sel) && IsHiddenView(sel);
		}

		// ---- divider & order -----------------------------------------------------------------

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

		// ---- the HIDDEN QUESTS pseudo-entry --------------------------------------------------

		// Display text for a hidden misc objective, looked up in the player's objective list.
		std::string MiscObjectiveText(const EntryKey& a_key)
		{
			const auto player = RE::PlayerCharacter::GetSingleton();
			if (!player) {
				return {};
			}
			const RE::BSTArray<RE::BGSInstancedQuestObjective>* objectives = nullptr;
			if (REL::Module::IsVR()) {
				if (const auto vr = player->GetVRPlayerRuntimeData()) {
					objectives = &vr->objectives;
				}
			} else {
				objectives = &player->GetPlayerRuntimeData().objectives;
			}
			if (!objectives) {
				return {};
			}
			for (const auto& inst : *objectives) {
				if (!inst.Objective || !inst.Objective->ownerQuest || inst.instanceID != a_key.instance) {
					continue;
				}
				if (inst.Objective->ownerQuest->GetFormID() != a_key.formID) {
					continue;
				}
				if (inst.InstanceState != RE::QUEST_OBJECTIVE_STATE::kDisplayed) {
					continue;
				}
				const auto text = inst.Objective->displayText.c_str();
				if (text && *text) {
					return text;
				}
			}
			return {};
		}

		void AddHiddenRow(Context& a_ctx, GFxValue& a_rows, const std::string& a_text, const EntryKey& a_key, int a_kind)
		{
			GFxValue row;
			a_ctx.movie->CreateObject(&row);
			SetString(a_ctx, row, "text", a_text);
			row.SetMember("formID", GFxValue(static_cast<double>(a_key.formID)));
			row.SetMember("instance", GFxValue(static_cast<double>(a_key.instance)));
			row.SetMember("active", GFxValue(false));
			row.SetMember("completed", GFxValue(false));
			row.SetMember(kHiddenKindVar, GFxValue(static_cast<double>(a_kind)));
			a_rows.PushBack(row);
		}

		// Removes the pseudo-entry and, while anything is hidden (and not being shown in place),
		// rebuilds it right after the last active quest. Returns its index, or -1.
		int RebuildHiddenView(Context& a_ctx, GFxValue& a_entryList)
		{
			const int old = FindHiddenViewIndex(a_entryList);
			if (old >= 0) {
				a_entryList.RemoveElements(static_cast<std::uint32_t>(old), 1);
			}

			auto& store = HiddenStore::Get();
			if (store.showHidden) {
				return -1;
			}

			GFxValue rows;
			a_ctx.movie->CreateArray(&rows);

			GFxValue parked;
			if (GetArrayMember(a_ctx.page, kHiddenTitlesVar, parked)) {
				const auto n = parked.GetArraySize();
				for (std::uint32_t i = 0; i < n; ++i) {
					GFxValue e;
					if (!parked.GetElement(i, &e)) {
						continue;
					}
					const auto key = KeyOf(e);
					if (!key) {
						continue;
					}
					auto text = TitleWithoutPrefix(e);
					if (text.empty()) {
						text = "(Quest)";
					}
					AddHiddenRow(a_ctx, rows, text, *key, 0);
				}
			}

			for (const auto raw : store.MiscKeys()) {
				const EntryKey key{ static_cast<RE::FormID>(raw >> 32), static_cast<std::uint32_t>(raw & 0xFFFFFFFFu) };
				auto text = MiscObjectiveText(key);
				if (text.empty()) {
					continue;  // not currently in the player's journal; nothing to show or restore
				}
				AddHiddenRow(a_ctx, rows, text, key, 1);
			}

			if (rows.GetArraySize() == 0) {
				return -1;
			}

			GFxValue entry;
			a_ctx.movie->CreateObject(&entry);
			SetString(a_ctx, entry, "text", kHiddenViewTitle);
			SetString(a_ctx, entry, "description", kHiddenViewDescription);
			entry.SetMember("formID", GFxValue(kHiddenViewFormID));
			entry.SetMember("instance", GFxValue(0.0));
			entry.SetMember("active", GFxValue(false));
			entry.SetMember("completed", GFxValue(false));
			entry.SetMember("type", GFxValue(0.0));
			entry.SetMember("timeIndex", GFxValue(1.0e300));
			entry.SetMember("objectives", rows);
			entry.SetMember(kHiddenViewFlag, GFxValue(true));

			// After the last unfinished quest (the Miscellaneous header included), before any divider.
			int insertAt = 0;
			const auto size = a_entryList.GetArraySize();
			for (std::uint32_t i = 0; i < size; ++i) {
				GFxValue e;
				if (!a_entryList.GetElement(i, &e) || !IsObj(e)) {
					continue;
				}
				if (IsDivider(e) || IsCompleted(e)) {
					break;
				}
				insertAt = static_cast<int>(i) + 1;
			}

			// GFx has no insert; append and rotate into place.
			a_entryList.PushBack(entry);
			for (auto i = static_cast<int>(a_entryList.GetArraySize()) - 1; i > insertAt; --i) {
				GFxValue prev;
				a_entryList.GetElement(static_cast<std::uint32_t>(i - 1), &prev);
				a_entryList.SetElement(static_cast<std::uint32_t>(i), prev);
			}
			a_entryList.SetElement(static_cast<std::uint32_t>(insertAt), entry);
			return insertAt;
		}

		// ---- redraw ----------------------------------------------------------------------------

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
		void InvalidateMiscObjectives(GFxValue& a_entryList)
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

		// ---- bottom bar ------------------------------------------------------------------------

		bool PanelHasLabel(const GFxValue& a_panel, const char* a_label)
		{
			GFxValue buttons;
			if (!GetArrayMember(a_panel, "buttons", buttons)) {
				return false;
			}
			const auto n = buttons.GetArraySize();
			for (std::uint32_t i = 0; i < n; ++i) {
				GFxValue b;
				if (buttons.GetElement(i, &b) && GetString(b, "label") == a_label) {
					return true;
				}
			}
			return false;
		}

		void AddPanelButton(RE::GFxMovie* a_movie, GFxValue& a_panel, const char* a_label, std::initializer_list<int> a_keys)
		{
			GFxValue controls;
			a_movie->CreateArray(&controls);
			for (const int key : a_keys) {
				GFxValue c;
				a_movie->CreateObject(&c);
				c.SetMember("keyCode", GFxValue(static_cast<double>(key)));
				controls.PushBack(c);
			}
			GFxValue data;
			a_movie->CreateObject(&data);
			GFxValue text;
			a_movie->CreateString(&text, a_label);
			data.SetMember("text", text);
			data.SetMember("controls", controls);
			a_panel.Invoke("addButton", nullptr, &data, 1);
		}

		// Replaces ButtonPanel.updateButtons on the journal's bottom bar: after the page has laid
		// out its own buttons (Toggle Active, Show on Map) we append ours. The page calls this
		// every time the quests tab is shown, so the hints follow the tab.
		class UpdateButtonsHandler : public RE::GFxFunctionHandler
		{
		public:
			void Call(Params& a_params) override
			{
				if (!a_params.thisPtr || !IsObj(*a_params.thisPtr) || !a_params.movie) {
					return;
				}
				GFxValue& panel = *a_params.thisPtr;
				panel.Invoke(kOrigUpdateButtonsVar.data(), a_params.retVal, a_params.args, a_params.argCount);

				if (!IsQuestsTabActive(a_params.movie)) {
					return;
				}
				if (!PanelHasLabel(panel, "$Toggle Active") || PanelHasLabel(panel, kHideLabel)) {
					return;
				}

				const int hideKey = Settings::iHideKey.GetValue();
				const int showKey = Settings::iShowHiddenKey.GetValue();
				const int restoreKey = Settings::iRestoreAllKey.GetValue();
				const bool shift = Settings::bRestoreAllNeedsShift.GetValue() || restoreKey == hideKey;

				AddPanelButton(a_params.movie, panel, kHideLabel, { hideKey });
				if (showKey > 0) {
					AddPanelButton(a_params.movie, panel, kShowHiddenLabel, { showKey });
				}
				if (shift) {
					AddPanelButton(a_params.movie, panel, kRestoreAllLabel, { 42, restoreKey });
				} else {
					AddPanelButton(a_params.movie, panel, kRestoreAllLabel, { restoreKey });
				}

				GFxValue instant(true);
				panel.Invoke(kOrigUpdateButtonsVar.data(), nullptr, &instant, 1);
			}
		};

		void InstallButtonHints(Context& a_ctx)
		{
			GFxValue bar;
			if (!(a_ctx.page.GetMember("_bottomBar", &bar) && IsObj(bar))) {
				if (!(a_ctx.movie->GetVariable(&bar, kBottomBarPath.data()) && IsObj(bar))) {
					logger::debug("bottom bar not found; no button hints");
					return;
				}
			}
			GFxValue panel;
			if (!(bar.GetMember("buttonPanel", &panel) && IsObj(panel))) {
				logger::debug("buttonPanel not found; no button hints");
				return;
			}
			GFxValue already;
			if (panel.GetMember(kOrigUpdateButtonsVar.data(), &already) && !already.IsUndefined()) {
				return;  // this movie is already wired
			}
			GFxValue orig;
			if (!panel.GetMember("updateButtons", &orig) || orig.IsUndefined()) {
				logger::debug("ButtonPanel.updateButtons not found; no button hints");
				return;
			}
			panel.SetMember(kOrigUpdateButtonsVar.data(), orig);
			GFxValue fn;
			a_ctx.movie->CreateFunction(&fn, new UpdateButtonsHandler());
			panel.SetMember("updateButtons", fn);
		}

		// ---- hide / restore primitives --------------------------------------------------------

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

			// The pseudo-entry gained a row; rebuild it without disturbing the current view.
			GFxValue entryList;
			if (GetArrayMember(a_ctx.titleList, "entryList", entryList)) {
				const int sel = GetSelectedIndex(a_ctx.titleList);
				RebuildHiddenView(a_ctx, entryList);
				a_ctx.titleList.Invoke("InvalidateData");
				SetSelectedIndex(a_ctx.titleList, sel);
				a_ctx.titleList.Invoke("UpdateList");
			}
			Sound("UIQuestInactive");
		}

		// Delete pressed on a row of the HIDDEN QUESTS view: bring that one back.
		void RestoreFromHiddenView(Context& a_ctx, int a_rowIdx)
		{
			GFxValue rows;
			if (!GetArrayMember(a_ctx.objectiveList, "entryList", rows)) {
				Sound("UIMenuCancel");
				return;
			}
			if (a_rowIdx < 0 || static_cast<std::uint32_t>(a_rowIdx) >= rows.GetArraySize()) {
				Sound("UIMenuCancel");
				return;
			}
			GFxValue row;
			rows.GetElement(static_cast<std::uint32_t>(a_rowIdx), &row);
			const auto key = KeyOf(row);
			double kind = 0.0;
			GetNumber(row, kHiddenKindVar, kind);
			if (!key) {
				Sound("UIMenuCancel");
				return;
			}

			GFxValue entryList;
			if (!GetArrayMember(a_ctx.titleList, "entryList", entryList)) {
				return;
			}
			auto& store = HiddenStore::Get();

			if (kind == 0.0) {
				store.Unhide(key->formID, key->instance);
				GFxValue parked;
				if (GetArrayMember(a_ctx.page, kHiddenTitlesVar, parked)) {
					for (auto i = static_cast<int>(parked.GetArraySize()) - 1; i >= 0; --i) {
						GFxValue e;
						if (!parked.GetElement(static_cast<std::uint32_t>(i), &e)) {
							continue;
						}
						const auto k = KeyOf(e);
						if (k && k->formID == key->formID && k->instance == key->instance) {
							StripPrefix(a_ctx, e);
							entryList.PushBack(e);
							parked.RemoveElements(static_cast<std::uint32_t>(i), 1);
						}
					}
				}
				// Our pseudo-entry must not take part in the sort; it is re-inserted below.
				const int old = FindHiddenViewIndex(entryList);
				if (old >= 0) {
					entryList.RemoveElements(static_cast<std::uint32_t>(old), 1);
				}
				FixDivider(a_ctx, entryList);
				SortEntries(a_ctx, entryList);
			} else {
				store.UnhideMisc(key->formID, key->instance);
				InvalidateMiscObjectives(entryList);
			}

			// Stay on the hidden view if it still exists, otherwise land on the restored quest.
			const int viewIdx = RebuildHiddenView(a_ctx, entryList);
			if (viewIdx >= 0) {
				RefreshTitles(a_ctx, entryList, std::nullopt, viewIdx);
				// Keep the right-hand focus roughly where it was.
				GFxValue newRows;
				if (GetArrayMember(a_ctx.objectiveList, "entryList", newRows) && newRows.GetArraySize() > 0) {
					SetSelectedIndex(a_ctx.objectiveList, std::clamp(a_rowIdx, 0, static_cast<int>(newRows.GetArraySize()) - 1));
					a_ctx.objectiveList.Invoke("UpdateList");
				}
			} else {
				std::optional<EntryKey> landOn;
				if (kind == 0.0) {
					landOn = *key;
				}
				RefreshTitles(a_ctx, entryList, landOn, 0);
			}
			Sound("UIQuestActive");
		}
	}

	// ---- public ----------------------------------------------------------------------------

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

	bool IsQuestsTabActive(RE::GFxMovie* a_movie)
	{
		GFxValue tab;
		if (!a_movie || !a_movie->GetVariable(&tab, kTabPath.data()) || !tab.IsNumber()) {
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
		InstallButtonHints(a_ctx);

		if (store.QuestCount() == 0 && store.MiscCount() == 0) {
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

		FixDivider(a_ctx, entryList);
		const int viewIdx = RebuildHiddenView(a_ctx, entryList);
		if (removed > 0 || marked > 0 || viewIdx >= 0) {
			RefreshTitles(a_ctx, entryList, selKey, fallbackIdx);
		}
		logger::debug("quests populated: {} removed, {} marked, hidden view at {}, {} shown",
			removed, marked, viewIdx, entryList.GetArraySize());
	}

	void OnObjectivesPopulated(Context& a_ctx)
	{
		if (!IsViewingMisc(a_ctx)) {
			return;
		}
		auto& store = HiddenStore::Get();
		if (store.MiscCount() == 0) {
			return;
		}

		GFxValue sel;
		if (!GetSelectedEntry(a_ctx.titleList, sel)) {
			return;
		}
		GFxValue objectives;
		if (!GetArrayMember(sel, "objectives", objectives)) {
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

		if (IsObj(a_ctx.objectiveList)) {
			const int objIdx = GetSelectedIndex(a_ctx.objectiveList);
			if (objIdx >= 0 && IsViewingHidden(a_ctx)) {
				RestoreFromHiddenView(a_ctx, objIdx);
				return;
			}
			if (objIdx >= 0 && IsViewingMisc(a_ctx)) {
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
		if (!key || key->formID == 0) {  // divider, our pseudo-entry, or the Miscellaneous header
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
		RebuildHiddenView(a_ctx, entryList);
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

		const int old = FindHiddenViewIndex(entryList);
		if (old >= 0) {
			entryList.RemoveElements(static_cast<std::uint32_t>(old), 1);
		}

		if (store.showHidden) {
			const auto n = parked.GetArraySize();
			for (std::uint32_t i = 0; i < n; ++i) {
				GFxValue e;
				if (parked.GetElement(i, &e)) {
					SetPrefix(a_ctx, e);
					entryList.PushBack(e);
				}
			}
			parked.ClearElements();
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

		InvalidateMiscObjectives(entryList);
		RebuildHiddenView(a_ctx, entryList);
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

		const int old = FindHiddenViewIndex(entryList);
		if (old >= 0) {
			entryList.RemoveElements(static_cast<std::uint32_t>(old), 1);
		}

		auto       parked = GetOrCreateArray(a_ctx, kHiddenTitlesVar, false);
		const auto n = parked.GetArraySize();
		for (std::uint32_t i = 0; i < n; ++i) {
			GFxValue e;
			if (parked.GetElement(i, &e)) {
				StripPrefix(a_ctx, e);
				entryList.PushBack(e);
			}
		}
		parked.ClearElements();

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
		InvalidateMiscObjectives(entryList);
		RefreshTitles(a_ctx, entryList, selKey, fallbackIdx);
		Sound("UIQuestActive");
		logger::info("restored all: {} quests, {} misc objectives", hadQuests, hadMisc);
	}
}
