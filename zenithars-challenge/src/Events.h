#pragma once

// Listens to the game's own "an item changed hands" and "an actor died" events and keeps the
// ledger in step. Nothing here changes the game; it only watches.
class Events :
	public RE::BSTEventSink<RE::TESContainerChangedEvent>,
	public RE::BSTEventSink<RE::TESDeathEvent>,
	public RE::BSTEventSink<RE::TESActivateEvent>,
	public RE::BSTEventSink<RE::TESEquipEvent>
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

	RE::BSEventNotifyControl ProcessEvent(const RE::TESContainerChangedEvent* a_event, RE::BSTEventSource<RE::TESContainerChangedEvent>* a_source) override;
	RE::BSEventNotifyControl ProcessEvent(const RE::TESDeathEvent* a_event, RE::BSTEventSource<RE::TESDeathEvent>* a_source) override;
	RE::BSEventNotifyControl ProcessEvent(const RE::TESActivateEvent* a_event, RE::BSTEventSource<RE::TESActivateEvent>* a_source) override;
	RE::BSEventNotifyControl ProcessEvent(const RE::TESEquipEvent* a_event, RE::BSTEventSource<RE::TESEquipEvent>* a_source) override;

private:
	void OnAddedToPack(const RE::TESContainerChangedEvent& a_event, RE::TESForm* a_item);
	void OnLeftPack(const RE::TESContainerChangedEvent& a_event, RE::TESForm* a_item);
};
