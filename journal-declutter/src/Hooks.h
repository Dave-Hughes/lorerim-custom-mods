#pragma once

namespace Hooks
{
	// Intercepts the quests tab's Scaleform callbacks ("RequestQuestsData", "RequestObjectivesData")
	// by wrapping the CallbackProcessor the game passes to Journal_QuestsTab::Accept.
	void Install();
}
