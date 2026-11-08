#pragma once

// Everything that touches the quest journal's Scaleform (Flash) side.
//
// The journal is quest_journal.swf. Its quests page keeps two lists:
//   TitleList     - one entry per quest, plus a "Miscellaneous" pseudo-entry (formID 0)
//                   and, when both finished and unfinished quests exist, a divider entry.
//   ObjectiveList - objectives of the highlighted quest; for the Miscellaneous entry
//                   each objective belongs to a different quest and carries its formID.
// The game fills TitleList.entryList inside the "RequestQuestsData" callback and the
// highlighted quest's objectives inside "RequestObjectivesData". We run right after each
// and remove hidden entries before the lists are drawn. Removed quest entries are parked
// in an array attached to the page object, so they live and die with the movie and can be
// put back when the player wants to see or restore them. Misc objectives are simply
// re-requested from the game, which rebuilds that list on demand.
//
// We also add our own pseudo-entry, "HIDDEN QUESTS", after the active quests. Its
// objective list names every hidden quest and objective; Delete on one of them restores it.
namespace Journal
{
	constexpr std::string_view kMenuRootPath = "_root.QuestJournalFader.Menu_mc";
	constexpr std::string_view kPagePath = "_root.QuestJournalFader.Menu_mc.QuestsFader.Page_mc";
	constexpr std::string_view kTabPath = "_root.QuestJournalFader.Menu_mc.iCurrentTab";
	constexpr std::string_view kBottomBarPath = "_root.QuestJournalFader.Menu_mc.BottomBar_mc";

	// Page-attached parking arrays (names chosen not to collide with anything in the SWF).
	constexpr const char* kHiddenTitlesVar = "jdc_hiddenTitles";
	constexpr const char* kDividerVar = "jdc_divider";

	// Marks our pseudo-entry and the rows inside it.
	constexpr const char* kHiddenViewFlag = "jdc_hiddenView";
	constexpr const char* kHiddenKindVar = "jdc_kind";  // 0 = quest, 1 = misc objective

	// formID carried by the pseudo-entry; the game's ToggleQuestActiveStatus is told to ignore it.
	constexpr double kHiddenViewFormID = 4294967295.0;  // 0xFFFFFFFF

	struct Context
	{
		RE::GFxMovieView* movie{ nullptr };
		RE::GFxValue      page;
		RE::GFxValue      titleList;
		RE::GFxValue      objectiveList;
	};

	// Resolves the quests page and its two lists. a_movie may be null (the open Journal Menu
	// is used); a_titleListHint is the TitleList the game handed to RequestQuestsData and is
	// used as a fallback when the page path differs (other UI skins).
	bool GetContext(Context& a_out, RE::GFxMovieView* a_movie, const RE::GFxValue* a_titleListHint = nullptr);
	bool IsQuestsTabActive(RE::GFxMovie* a_movie);

	// Called from the hooked delegates.
	void OnQuestsPopulated(Context& a_ctx);
	void OnObjectivesPopulated(Context& a_ctx);

	// Called from the hotkeys.
	void HideSelected(Context& a_ctx);
	void ToggleShowHidden(Context& a_ctx);
	void RestoreAll(Context& a_ctx);
}
