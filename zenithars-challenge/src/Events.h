#pragma once

// Listens to the game's own "an item changed hands" and "an actor died" events and keeps the
// ledger in step. Nothing here changes the game; it only watches.
class Events :
	public RE::BSTEventSink<RE::TESContainerChangedEvent>,
	public RE::BSTEventSink<RE::TESDeathEvent>
{
public:
	static Events* GetSingleton();
	static void    Register();

	// Forget anything remembered between two events (called when a save loads or a game starts).
	void Reset();

	RE::BSEventNotifyControl ProcessEvent(const RE::TESContainerChangedEvent* a_event, RE::BSTEventSource<RE::TESContainerChangedEvent>* a_source) override;
	RE::BSEventNotifyControl ProcessEvent(const RE::TESDeathEvent* a_event, RE::BSTEventSource<RE::TESDeathEvent>* a_source) override;

private:
	void OnAddedToPack(const RE::TESContainerChangedEvent& a_event, RE::TESForm* a_item);
	void OnLeftPack(const RE::TESContainerChangedEvent& a_event, RE::TESForm* a_item);
};
