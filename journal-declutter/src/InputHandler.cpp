#include "InputHandler.h"

#include "Journal.h"
#include "Settings.h"

namespace
{
	constexpr std::uint32_t kLShift = 42;
	constexpr std::uint32_t kRShift = 54;
}

InputHandler* InputHandler::GetSingleton()
{
	static InputHandler instance;
	return &instance;
}

void InputHandler::Register()
{
	if (const auto manager = RE::BSInputDeviceManager::GetSingleton()) {
		manager->AddEventSink(GetSingleton());
		logger::info("input handler registered");
	} else {
		logger::error("BSInputDeviceManager not available; hotkeys disabled");
	}
}

RE::BSEventNotifyControl InputHandler::ProcessEvent(RE::InputEvent* const* a_event, RE::BSTEventSource<RE::InputEvent*>*)
{
	if (!a_event || !*a_event) {
		return RE::BSEventNotifyControl::kContinue;
	}

	const auto ui = RE::UI::GetSingleton();
	if (!ui || !ui->IsMenuOpen(RE::JournalMenu::MENU_NAME) || ui->IsMenuOpen(RE::Console::MENU_NAME)) {
		return RE::BSEventNotifyControl::kContinue;
	}

	// Modifier state comes from the same batch: a held key reports every frame.
	bool shift = false;
	for (auto e = *a_event; e; e = e->next) {
		if (e->GetEventType() != RE::INPUT_EVENT_TYPE::kButton || e->GetDevice() != RE::INPUT_DEVICE::kKeyboard) {
			continue;
		}
		const auto button = e->AsButtonEvent();
		if (button && button->IsPressed()) {
			const auto code = button->GetIDCode();
			if (code == kLShift || code == kRShift) {
				shift = true;
			}
		}
	}

	for (auto e = *a_event; e; e = e->next) {
		if (e->GetEventType() != RE::INPUT_EVENT_TYPE::kButton || e->GetDevice() != RE::INPUT_DEVICE::kKeyboard) {
			continue;
		}
		const auto button = e->AsButtonEvent();
		if (!button || !button->IsDown()) {
			continue;
		}
		HandleKey(button->GetIDCode(), shift);
	}
	return RE::BSEventNotifyControl::kContinue;
}

void InputHandler::HandleKey(std::uint32_t a_code, bool a_shift)
{
	const auto hideKey = static_cast<std::uint32_t>(Settings::iHideKey.GetValue());
	const auto showKey = static_cast<std::uint32_t>(Settings::iShowHiddenKey.GetValue());
	const auto restoreKey = static_cast<std::uint32_t>(Settings::iRestoreAllKey.GetValue());
	const bool restoreNeedsShift = Settings::bRestoreAllNeedsShift.GetValue() || restoreKey == hideKey;

	enum class Action
	{
		kNone,
		kHide,
		kShowHidden,
		kRestoreAll
	};
	auto action = Action::kNone;

	if (a_code == restoreKey && (a_shift || !restoreNeedsShift)) {
		action = Action::kRestoreAll;
	} else if (a_code == hideKey && !(a_shift && restoreKey == hideKey)) {
		action = Action::kHide;
	} else if (a_code == showKey) {
		action = Action::kShowHidden;
	}
	if (action == Action::kNone) {
		return;
	}

	Journal::Context ctx;
	if (!Journal::GetContext(ctx, nullptr) || !Journal::IsQuestsTabActive(ctx.movie)) {
		return;
	}

	switch (action) {
	case Action::kHide:
		Journal::HideSelected(ctx);
		break;
	case Action::kShowHidden:
		Journal::ToggleShowHidden(ctx);
		break;
	case Action::kRestoreAll:
		Journal::RestoreAll(ctx);
		break;
	default:
		break;
	}
}
