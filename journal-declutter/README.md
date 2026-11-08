# Journal Declutter

Hide the quests you don't care about, straight from the journal. No plugin file, no MCM.

Open the journal, highlight a quest, press **Delete**. It's gone from the list and its markers
leave your compass and map. The quest itself keeps running underneath, exactly as before — if
you ever walk into its dungeon or talk to its NPC, it advances normally, and when it reaches a
new objective it simply isn't shown. Nothing is cancelled or broken; you only control what the
journal shows you.

Works with Skyrim Special Edition, Anniversary Edition (1.6.1130 / 1.6.1170 / 1.7.x) and VR
from one DLL, through the Address Library.

## Keys (keyboard; all rebindable in the INI)

| Key | In the journal, quests tab |
| --- | --- |
| **Delete** | Hide the highlighted quest. On the *Miscellaneous* page with an objective highlighted, hide that objective. |
| **Insert** | Show hidden entries in place, marked `[HIDDEN]`. Press Delete on one to bring it back. Insert again to tuck them away. |
| **Shift + Delete** | Restore every hidden quest and objective for this character. |

Hover with the mouse or move with the arrow keys to highlight — the same highlight that
*Toggle Active* and *Show on Map* use. Hidden quests are remembered per character in the SKSE
co-save (the `.skse` file next to your save), so they travel with the save and survive
load-order changes.

## Requirements

- [SKSE64](https://skse.silverlock.org/)
- [Address Library for SKSE Plugins](https://www.nexusmods.com/skyrimspecialedition/mods/32444) (or the VR Address Library on VR)
- A SkyUI-based journal. SkyUI itself, Dear Diary (Dark Mode), Untarnished UI, Norden UI, Edge UI and
  similar skins keep the vanilla journal structure and work. *Quest Journal Overhaul* replaces the
  journal file with a different layout and is **not** supported.

## Install

Drop `SKSE/Plugins/JournalDeclutter.dll` and `JournalDeclutter.ini` into your `Data` folder, or install
the archive with your mod manager. Safe to add or remove mid-playthrough; removing it brings every
hidden quest back.

## Settings

`Data/SKSE/Plugins/JournalDeclutter.ini` holds the keys and behaviour, with comments. Put your
changes in `JournalDeclutter_Custom.ini` in the same folder so they survive updates. Key codes are
DirectInput scan codes, the same numbers SkyUI MCMs display.

## How it works (for the curious)

Skyrim's quest journal is a Flash movie (`quest_journal.swf`). When its quests page opens it asks the
game for the quest list through a callback named `RequestQuestsData`, and for a quest's objectives
through `RequestObjectivesData`. This plugin wraps those two callbacks: after the game has filled the
lists, it removes the entries you've hidden and tells the page to redraw. The removed entries are
parked on the page object itself, so *Show hidden* can put them back instantly and the parked data
dies with the menu. Hotkeys are read from the game's input stream while the Journal Menu is open.

Because it hooks the callbacks by name rather than by code address, it needs no per-version offsets
and does not touch the journal's files, so UI skins keep working.

## Building

See the repository root. In short: Visual Studio 2022, CMake, vcpkg, and the CommonLibSSE-NG submodule;
`cmake --preset release-msvc && cmake --build --preset release-msvc` inside this folder. GitHub Actions
builds every push and attaches a zip to tags named `journal-declutter-v*`.

## Credits

- Written by Dave Hughes, with Claude.
- Built on [CommonLibSSE-NG](https://github.com/alandtse/CommonLibSSE-NG) (GPL-3.0-or-later with exceptions).
- The idea of hiding futile quests was done first by vagonumero12's *Hide Those Futile Quests*; this is an
  independent implementation with no code or assets from it.

## License

GPL-3.0-or-later. See `LICENSE`.
