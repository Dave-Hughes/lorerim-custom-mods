# Zenithar's Challenge

A referee for Zenithar's Challenge in the LoreRim modlist (5.0.4.8): craft everything you use. The
challenge was written by Great Mighty p1ss on the LoreRim Discord. One SKSE plugin and one small
light ESP (two spells, written by `tools/make_esp.py`); no scripts, and no interface files changed.

**Status: in play-testing.** Not released. The site for it is the LoreRim Planner's `/challenge` pages.

## How it plays

A new character is asked once, when LoreRim's own setup is over: **Zenithar's Challenge** or
**Standard Playthrough**. The answer holds for the run.

- Standard playthrough: the mod stands aside for that character. No messages, no marks, no run file.
- The challenge: seven rules the challenge leaves to the player are chosen next (looted gems, looted
  soul gems, coin purse gems, quest rewards, starting gear, starting clothes,
  others' kills), and from then on the
  game refuses what the rules forbid.

## What counts as yours

| You did this | Counted as |
| --- | --- |
| Made an item at a forge, smelter, tanning rack or other "make" bench | crafted |
| Cooked food | cooked |
| Brewed a potion or poison | brewed |
| Mined ore or gems from a vein | mined |
| Chopped firewood at a block | chopped |
| Took hides, meat or parts from an animal you killed | hunted |

Everything else that enters your pack is not yours, unless one of the six rules you allowed says
it is. Two of them cover what a new character starts with: Starting Clothes (the clothing only)
and Starting Gear (everything). With neither, nothing you start with counts.

Skyrim merges looted and made copies of one item into a single stack, so the mod keeps
counts rather than marking items. Your copies are followed into chests and back, onto the ground
and back, and out of your bow and back. Tempering or enchanting an item does not change whose it is.

Water, provisions, a pickaxe and a woodcutter's axe are free however you get them.

## What is refused

- Wearing or wielding gear that is not yours: weapons, armor, shields, ammunition, clothing,
  jewelry, cloaks and bags (`bRestrictClothing = 0` narrows this to armor). It comes back off. A
  stack that is only partly yours is refused until the extras are stored.
- Eating food you did not cook and drinking potions you did not brew.
- Spells, scrolls and staves. The hand is emptied.
- Selling what is not yours. The sale is undone and the septims go back.
- Missive boards.

Crafting with ore, firewood or hides you did not gather cannot be stopped; it is written to the
run as a broken rule.

## What you see

- A small struck-through anvil and a dull red name on anything not yours, in the inventory, on
  your side of a merchant's screen, in a chest's screen and in the QuickLoot window. A mixed stack
  says how many, e.g. `Iron Arrow [3 not yours]`.
- "Not made by you." on the item card.
- `Zenithar: Iron Dagger x1 crafted` when something is counted, and a line when something is refused.
- "Zenithar's Challenge" under Active Effects while the challenge is on, and the lesser power
  "Zenithar's Ledger", which shows the run so far.

## The run file

Every save writes `Documents\My Games\Skyrim Special Edition\SKSE\ZenitharsChallenge\<character>.json`:
the rules chosen, days passed, what was made and how, every broken rule, kills, deaths,
distance travelled, and the game's own totals (places discovered, quests completed, gold found
and so on). It is sealed, so the leaderboard refuses a file changed by hand. Nothing is sent
anywhere; the player uploads the file.

## Settings

`SKSE\Plugins\ZenitharsChallenge.ini`, each line explained there. Your own changes go in
`ZenitharsChallenge_Custom.ini` beside it, which survives updates.

## Requirements

- [SKSE64](https://skse.silverlock.org/) and [Address Library for SKSE Plugins](https://www.nexusmods.com/skyrimspecialedition/mods/32444)
- LoreRim's B612 traits window (`b612.esp`), for the choice cards. LoreRim already has it.

## Building

See the repository [README](../README.md). The ledger is stored per character in the SKSE co-save.
