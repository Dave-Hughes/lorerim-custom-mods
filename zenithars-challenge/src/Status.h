#pragma once

// The player's sign that the challenge is on: an entry under Active Effects ("Zenithar's Challenge")
// and a lesser power ("Zenithar's Ledger") that shows the run so far. Both records live in
// ZenitharsChallenge.esp; without that file the mod works as before, with no sign.
namespace Status
{
	// Once the game's data has loaded: finds the two spells and listens for the power.
	void OnDataLoaded();
	// Gives or takes away the sign to match whether this character is on the challenge. Main thread only.
	void Sync();
}
