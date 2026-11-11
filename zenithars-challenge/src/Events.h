#pragma once

#include "Ledger.h"

// Listens to the game's own events (an item changed hands, an actor died, something was equipped or
// activated, a quest moved on) and keeps the ledger in step. For a character on the challenge it also
// acts: gear that is not the player's comes back off, magic leaves the hand, a sale of something not
// theirs is undone, food and potions not theirs are refused, and missive boards stay shut.
class Events :
	public RE::BSTEventSink<RE::TESContainerChangedEvent>,
	public RE::BSTEventSink<RE::TESDeathEvent>,
	public RE::BSTEventSink<RE::TESActivateEvent>,
	public RE::BSTEventSink<RE::TESEquipEvent>,
	public RE::BSTEventSink<RE::MenuOpenCloseEvent>,
	public RE::BSTEventSink<RE::TESCellAttachDetachEvent>,
	public RE::BSTEventSink<RE::TESQuestStageEvent>
{
public:
	static Events* GetSingleton();
	static void    Register();

	// Forget anything remembered between two events (called when a save loads or a game starts).
	void Reset();

	// Rules are only judged while armed. Disarmed while a save loads, so gear the game puts back
	// on the character is not read as the player equipping it.
	static void SetArmed(bool a_armed);

	// Counts what is really in the pack and corrects the ledger: anything it never saw arrive is
	// not yours. Run once the game has finished loading. Main thread only.
	static void TakeStock();

	// The clothes a new character starts in become the player's own (not jewelry, not armor, not weapons). Main thread only.
	static void KeepStartingClothes();
	// Food in the pack now is the player's own, and so is food picked up before leaving this room. Main thread only.
	static void BeginStartingGrace();
	// The "keep starting items" choice: everything in the pack right now becomes the player's own. Main thread only.
	static void KeepPack();

	// The mode in force: the character's own choice; not asked yet counts as off.
	[[nodiscard]] static Mode CurrentMode();
	// How many of a_count copies of an item held by someone else (a chest, a body) would not be the
	// player's own once taken. 0 for items the rules do not care about.
	[[nodiscard]] static std::int32_t NotYoursIfTaken(RE::FormID a_holder, const RE::TESForm* a_item, std::int32_t a_count);
	// True when this character is playing the challenge.
	[[nodiscard]] static bool OnChallenge() { return CurrentMode() == Mode::kOath; }
	// The hook that refuses food and potions before they are used.
	static void InstallHooks();

	RE::BSEventNotifyControl ProcessEvent(const RE::TESContainerChangedEvent* a_event, RE::BSTEventSource<RE::TESContainerChangedEvent>* a_source) override;
	RE::BSEventNotifyControl ProcessEvent(const RE::TESDeathEvent* a_event, RE::BSTEventSource<RE::TESDeathEvent>* a_source) override;
	RE::BSEventNotifyControl ProcessEvent(const RE::TESActivateEvent* a_event, RE::BSTEventSource<RE::TESActivateEvent>* a_source) override;
	RE::BSEventNotifyControl ProcessEvent(const RE::TESEquipEvent* a_event, RE::BSTEventSource<RE::TESEquipEvent>* a_source) override;
	RE::BSEventNotifyControl ProcessEvent(const RE::MenuOpenCloseEvent* a_event, RE::BSTEventSource<RE::MenuOpenCloseEvent>* a_source) override;
	RE::BSEventNotifyControl ProcessEvent(const RE::TESCellAttachDetachEvent* a_event, RE::BSTEventSource<RE::TESCellAttachDetachEvent>* a_source) override;
	RE::BSEventNotifyControl ProcessEvent(const RE::TESQuestStageEvent* a_event, RE::BSTEventSource<RE::TESQuestStageEvent>* a_source) override;

private:
	void OnAddedToPack(const RE::TESContainerChangedEvent& a_event, RE::TESForm* a_item);
	void OnLeftPack(const RE::TESContainerChangedEvent& a_event, RE::TESForm* a_item);
};
