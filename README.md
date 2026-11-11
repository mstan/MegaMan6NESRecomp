# Mega Man 6 — NESRecomp

NESRecomp cycle-backend project for the original USA cartridge. Supply your own ROM; cartridge data is excluded from source and releases.

Development takes place on the feature branch. Native gameplay is the baseline for the character, co-op, controls and adaptive widescreen implementation.

```text
cmake -S . -B build -DNESRECOMP_ROM="/path/to/Mega Man 6 (USA).nes"
cmake --build build --config Release --parallel 2
```

Use `NESRECOMP_ROOT` and `NESRECOMP_RECOMP_UI` to select existing engine and launcher checkouts. Runtime tests use isolated configuration and explicit ROM paths.

The feature branch currently contains a qualified native gameplay baseline; the Proto Man, co-op/netplay and adaptive world extensions are in progress. Native Mega Man 6 mechanics, including slide, charge and Rush adapters, remain the behavioral reference.

A 5,220-frame route through stage selection, teleport, movement, camera scrolling, enemies and the inventory matches the interpreter at every frame. This is representative baseline evidence, not complete stage or enhancement acceptance.

```text
python tools/qa_route.py tests/routes/boot-play.route --exe build/Release/MegaMan6Recomp.exe --rom "/path/to/Mega Man 6 (USA).nes" --out qa/baseline
```

The route helper is adapted from MegaMan2NESRecomp under the included license and additional permissions. No game cartridge bytes or extracted donor assets are included.

## Quick weapon switching

The default-on optional package maps L/R (LB/RB) to the previous/next acquired weapon or Beat in normal form. It wraps, skips locked equipment and includes empty weapons. Held presses change once; simultaneous shoulders cancel. The native inventory chooses Jet and Power adapters, whose native weapon restrictions are retained. Native graphics, palettes and personal shot cleanup come from the supplied cartridge's own tables.

```text
python tools/qa_controls.py --exe build/Release/MegaMan6Recomp.exe --rom "/path/to/Mega Man 6 (USA).nes" --out qa/controls
```

The regression checks compare native/interpreter execution, ordinary gameplay with the feature idle or disabled, acquired/locked/empty equipment, native inventory graphics and palettes, player/menu input isolation, and held-input state continuation. This controls slice does not establish full adapter, character, co-op or stage acceptance.
