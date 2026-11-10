# Zenithar's Challenge (working title)

A referee for the Zenithar's Challenge run in the LoreRim modlist: craft everything you use, with
limited looting, buying and selling. SKSE plugin, no ESP yet.

**Status: phase 1, the ledger.** The plugin counts what you make and gather and follows those
items as they move. It blocks nothing and warns about nothing yet; it only shows test lines so the
counting can be checked in play.

## What counts as yours

| You did this | Counted as |
| --- | --- |
| Made an item at a forge, smelter, tanning rack or other "make" bench | crafted |
| Cooked food | cooked |
| Brewed a potion or poison | brewed |
| Mined ore or gems from a vein | mined |
| Chopped firewood at a block | chopped |
| Took hides, meat or parts from an animal you killed | hunted |

Everything else that enters your pack (loot, purchases, quest rewards, firewood picked up off a
stack, meat from hanging game) is not yours.

Skyrim merges looted and made copies of one item into a single stack, so the ledger keeps counts
rather than marking items. Your copies are followed into chests and back, and onto the ground and
back. Tempering or enchanting an item does not change whose it is.

## Test lines

- `Zenithar: Iron Dagger x1 crafted` when something is counted.
- `Zenithar: Iron Sword x1 is not yours` when gear, food, a potion, ore or firewood that is not
  yours enters your pack.
- `Zenithar's ledger is running: 12 items made so far` after a save loads.

Both kinds of line can be turned off in `ZenitharsChallenge.ini`.

## Requirements

- [SKSE64](https://skse.silverlock.org/)
- [Address Library for SKSE Plugins](https://www.nexusmods.com/skyrimspecialedition/mods/32444)

## Building

See the repository [README](../README.md). The ledger is stored per character in the SKSE co-save.
