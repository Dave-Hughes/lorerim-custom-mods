#pragma once

// The run file: a small sealed JSON file with the story of the run, written each time the game
// saves. The player uploads it to the site; nothing is sent anywhere by the mod itself.
// It lands next to the SKSE logs: Documents/My Games/Skyrim Special Edition/SKSE/ZenitharsChallenge/<character>.json
namespace RunFile
{
	void Write();
}
