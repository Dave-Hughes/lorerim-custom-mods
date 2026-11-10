#pragma once

#include "Ledger.h"

// Listens to the game's own "an item changed hands" and "an actor died" events and keeps the
// ledger in step. Nothing here changes the game; it only watches.
class Events :
	public RE::BSTEventSink<RE::TESContainerChangedEvent>,
	public RE::BSTEventSink<RE::TESDeathEvent>,
	public RE::BSTEventSink<RE::TESActivateEvent>,
	public RE::BSTEventSink<RE::TESEquipEvent>,
	public RE::BSTEventSink<RE::MenuOpenCloseEvent>,
	public RE::BSTEventSink<RE::TESCellAttachDetachEvent>
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

	// The "keep starting items" choice: everything in the pack right now becomes the player's own. Main thread only.
	static void KeepPack();

	// The mode in force: the ini override if set, else the character's own; not asked yet counts as off.
	[[nodiscard]] static Mode CurrentMode();
	// True when this character is playing the challenge.
	[[nodiscard]] static bool OnChallenge() { return CurrentMode() == Mode::kOath; }
	// Hooks that let Oath mode refuse food and potions before they are used.
	static void InstallHooks();

	RE::BSEventNotifyControl ProcessEvent(const RE::TESContainerChangedEvent* a_event, RE::BSTEventSource<RE::TESContainerChangedEvent>* a_source) override;
	RE::BSEventNotifyControl ProcessEvent(const RE::TESDeathEvent* a_event, RE::BSTEventSource<RE::TESDeathEvent>* a_source) override;
	RE::BSEventNotifyControl ProcessEvent(const RE::TESActivateEvent* a_event, RE::BSTEventSource<RE::TESActivateEvent>* a_source) override;
	RE::BSEventNotifyControl ProcessEvent(const RE::TESEquipEvent* a_event, RE::BSTEventSource<RE::TESEquipEvent>* a_source) override;
	RE::BSEventNotifyControl ProcessEvent(const RE::MenuOpenCloseEvent* a_event, RE::BSTEventSource<RE::MenuOpenCloseEvent>* a_source) override;
	RE::BSEventNotifyControl ProcessEvent(const RE::TESCellAttachDetachEvent* a_event, RE::BSTEventSource<RE::TESCellAttachDetachEvent>* a_source) override;

private:
	void OnAddedToPack(const RE::TESContainerChangedEvent& a_event, RE::TESForm* a_item);
	void OnLeftPack(const RE::TESContainerChangedEvent& a_event, RE::TESForm* a_item);
};
