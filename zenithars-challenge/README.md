# Zenithar's Challenge

A referee for Zenithar's Challenge in the LoreRim modlist (5.0.4.8): craft everything you use. The
challenge was written by Great Mighty p1ss on the LoreRim Discord. One SKSE plugin and one small
light ESP (two spells, written by `tools/make_esp.py`); no scripts, and no interface files changed.

**Status: in play-testing.** Not released. The site for it is the LoreRim Planner's `/challenge` pages.

## How it plays

A new character is asked once, when LoreRim's own setup is over. A plain box says what the
challenge is, with two buttons: **Take the challenge** or **Standard playthrough**. The answer
holds for the run.

- Standard playthrough: the mod stands aside for that character. No messages, no marks, no run file.
- Take the challenge: six cards follow, one for each rule the challenge leaves to the player. Tick
  a card to allow it: looted gems, looted soul gems, coin purse gems, quest rewards, starting gear,
  others' kills. All six unticked is the challenge as written. From then on the game refuses what
  the rules forbid.

## What counts as yours

| You did this | Counted as |
| --- | --- |
| Made an item at a forge, smelter, tanning rack or other "make" bench | crafted |
| Cooked food | cooked |
| Brewed a potion or poison | brewed |
| Mined ore or gems from a vein | mined |
| Chopped firewood at a block | chopped |
| Took hides, meat or parts from an animal you killed | hunted |
| Caught a fish on your own line | hunted |

Everything else that enters your pack is not yours, unless one of the six rules you allowed says
it is. Hunting gives hides, meat and parts: gear, gems, soul gems, scrolls and potions a creature
was carrying are loot like any other. A body you have put something into no longer counts as hunted.

A new character keeps the clothes it starts in, the food it has, and the food it picks up before
it leaves its starting room. The weapons, armor, jewelry and other gear an alternate start hands
over are not its own, unless Starting Gear was allowed.

Free however you get them: septims, ingredients, raw fruit and vegetables, flour, salt, water,
provisions, a pickaxe, a woodcutter's axe and a fishing rod. Ingredients and produce you pick from a plant
yourself are your own work as well, and may be sold.

Skyrim merges looted and made copies of one item into a single stack, so the mod keeps
counts rather than marking items. Your copies are followed into chests and back, onto the ground
and back, and out of your bow and back. Tempering or enchanting an item does not change whose it is.

## What is refused

- Wearing or wielding gear that is not yours: weapons, armor, shields, ammunition, clothing,
  jewelry, cloaks and bags. It comes back off. A stack that is only partly yours is refused until
  the extras are stored.
- Eating food you did not cook and drinking potions you did not brew. From a stack that is partly
  yours, your own copies are used first.
- Spells, scrolls and staves. The hand is emptied.
- Selling what is not yours, loot of every kind included. The sale is undone and the septims go back.
- Missive boards.

Looting is not refused: you may carry anything. Using and selling are what the rules are about.

Some things cannot be stopped from where the mod stands. They are written to the run as a broken
rule, with a line on screen at the time:

- Crafting, smelting or tempering with ore, ingots, firewood, hides or gems you did not gather.
- Melting down or crafting with gear that is not yours.
- Putting a poison you did not brew on a weapon from a hotkey or the wheel (from the inventory it
  is stopped at the press).
- Taking a missive, and adding an item with the console.

## What you see

- A small struck-through anvil and a dull red name on anything not yours, in the inventory, on
  your side of a merchant's screen, in a chest's screen and in the QuickLoot window. A mixed stack
  says how many, e.g. `Iron Arrow [3 not yours]`. Scrolls and staves carry the mark whoever made them.
- "Not made by you." on the item card, or "Magic is not allowed." for a scroll or staff.
- A dull red name under the crosshair for an item lying in the world that would not be yours.
- `Zenithar: Iron Dagger x1 crafted` when something is counted, and a line when something is refused.
- "Zenithar's Challenge" under Active Effects while the challenge is on, and the lesser power
  "Zenithar's Ledger", which shows the run so far.

## The run file

Every save writes `Documents\My Games\Skyrim Special Edition\SKSE\ZenitharsChallenge\<character>.json`:
the rules chosen, days passed, the day the challenge was taken, what was made and how, every
broken rule, kills, deaths, distance travelled, and the game's own totals (places discovered,
quests completed, gold found and so on). It is sealed, so the leaderboard refuses a file changed by hand. Nothing is sent
anywhere; the player uploads the file.

## Settings

`SKSE\Plugins\ZenitharsChallenge.ini`, each line explained there: which messages and marks are
shown. Your own changes go in `ZenitharsChallenge_Custom.ini` beside it, which survives updates.
The rules themselves are not settings, so a run file always says what was played.

`bAskOnLoad = 1` asks a character made before the mod was installed. It joins with nothing: what it
carries is not its own work, and its run counts its days from the day it joined.

## Requirements

- [SKSE64](https://skse.silverlock.org/) and [Address Library for SKSE Plugins](https://www.nexusmods.com/skyrimspecialedition/mods/32444)
- LoreRim's B612 traits window (`b612.esp`), for the six rule cards. LoreRim already has it. Without
  it the challenge can still be taken, with all six rules left not allowed.

## Building

See the repository [README](../README.md). The ledger is stored per character in the SKSE co-save.
