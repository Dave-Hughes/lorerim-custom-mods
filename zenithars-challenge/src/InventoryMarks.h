#pragma once

// Marks rows in the inventory screen that hold items which are not the player's own,
// the way the game marks stolen items. An experiment: on by default, off with bMarkInventory = 0.
namespace InventoryMarks
{
	void Register();
}
