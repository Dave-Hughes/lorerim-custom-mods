#pragma once

// Keyboard hotkeys while the Journal Menu's quests tab is showing.
class InputHandler : public RE::BSTEventSink<RE::InputEvent*>
{
public:
	static InputHandler* GetSingleton();
	static void          Register();

	RE::BSEventNotifyControl ProcessEvent(RE::InputEvent* const* a_event, RE::BSTEventSource<RE::InputEvent*>* a_source) override;

private:
	InputHandler() = default;

	void HandleKey(std::uint32_t a_code, bool a_shift);
};
