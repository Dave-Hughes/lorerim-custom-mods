#pragma once

// The choice made once per character: Oath or Honor, then the rules the challenge leaves to the
// player. Shown as card windows (the B612 traits window that LoreRim uses for traits and
// birthsigns), once LoreRim's own character setup is over.
namespace Choice
{
	void Register();
	// A new game began: the character will be asked once setup is over.
	void OnNewGame(std::uint32_t a_turn);
	// A save finished loading: resumes a choice that was still pending, or asks if the test switch says so.
	void OnLoaded(std::uint32_t a_turn);
	// The current game session; a pending question from an earlier session is dropped.
	void SetTurn(std::uint32_t a_turn);
}
