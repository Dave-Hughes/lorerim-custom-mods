#pragma once

// Marks items that are not the player's own wherever items are listed: the inventory, a merchant's
// screen, a chest's screen, the quick-loot window, and the item card. Off with bMarkInventory = 0.
namespace InventoryMarks
{
	void Register();
	// Once the game's data has loaded: starts following what the crosshair is on (for the quick-loot window).
	void OnDataLoaded();
	// A menu is opening (any thread).
	void OnMenuOpening(std::string_view a_menu);
}
