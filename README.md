# LoreRim custom mods

Dave Hughes' add-on mods for Skyrim / the LoreRim modlist. Each mod lives in its own folder and
builds on its own; shared libraries live under `lib/`.

| Mod | What it does | Status |
| --- | --- | --- |
| [journal-declutter](journal-declutter/) | Hide unwanted quests from the journal with a hotkey. SKSE plugin, no ESP, no MCM. | in development |

## Building

The SKSE plugins use [CommonLibSSE-NG](https://github.com/alandtse/CommonLibSSE-NG) as a git
submodule (`lib/commonlibsse-ng`, pinned to a release tag). Clone with submodules:

```
git clone --recurse-submodules https://github.com/Dave-Hughes/lorerim-custom-mods.git
```

Native builds need Visual Studio 2022 (Desktop development with C++), CMake 3.22+ and vcpkg with
`VCPKG_ROOT` set. Each mod folder has a `CMakePresets.json`; run `cmake --preset release-msvc` then
`cmake --build --preset release-msvc` from inside it. GitHub Actions builds every push on
`windows-latest` and uploads the packaged mod as an artifact; tags of the form `<mod>-v1.2.3` also
create a GitHub release with a zip.

## License

GPL-3.0-or-later unless a mod folder says otherwise (CommonLibSSE-NG requires GPL-compatible plugins).
