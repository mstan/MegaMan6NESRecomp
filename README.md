# Mega Man 6 — NESRecomp

NESRecomp cycle-backend project for the original USA cartridge. Supply your own ROM; cartridge data is excluded from source and releases.

Development takes place on the feature branch. Native gameplay is the baseline for the character, co-op, controls and adaptive widescreen implementation.

```text
cmake -S . -B build -DNESRECOMP_ROM="/path/to/Mega Man 6 (USA).nes"
cmake --build build --config Release --parallel 2
```

Use `NESRECOMP_ROOT` and `NESRECOMP_RECOMP_UI` to select existing engine and launcher checkouts. Runtime tests use isolated configuration and explicit ROM paths.

The feature branch currently contains qualified native gameplay and initial solo character slices. Co-op/netplay and adaptive world extensions are in progress. Native Mega Man 6 mechanics, including slide, charge and Rush adapters, remain the behavioral reference.

A 5,220-frame route through stage selection, teleport, movement, camera scrolling, enemies and the inventory matches the interpreter at every frame. This is representative baseline evidence, not complete stage or enhancement acceptance.

```text
python tools/qa_route.py tests/routes/boot-play.route --exe build/Release/MegaMan6Recomp.exe --rom "/path/to/Mega Man 6 (USA).nes" --out qa/baseline
```

The route helper is adapted from MegaMan2NESRecomp under the included license and additional permissions. No game cartridge bytes or extracted donor assets are included.

## Development adaptive terrain

`--wide on --aspect fit` extends horizontal terrain from the native layout, block, metatile and palette tables. Authored director records bound connected horizontal rooms; vertical and unrecognized scene layouts retain the native view. The original viewport remains intact, with its clipped left edge filled when that edge moves inside the wider picture.

```text
MegaMan6Recomp.exe "/path/to/Mega Man 6 (USA).nes" --wide on --aspect fit --no-flicker on
```

Fit has targeted checks at 256, 426, 568 and 854 pixels, with native/interpreter agreement and current-build moving-scene continuation. Terrain conformance retains the original game's edge differences separately: each such pixel must be reproduced from live native CIRAM. Wider actor activation/culling, all scenes and fresh launcher defaults remain in progress; this initial terrain slice does not establish complete widescreen acceptance.

## Quick weapon switching

The default-on optional package maps L/R (LB/RB) to the previous/next acquired weapon or Beat in normal form. It wraps, skips locked equipment and includes empty weapons. Held presses change once; simultaneous shoulders cancel. The native inventory chooses Jet and Power adapters, whose native weapon restrictions are retained. Native graphics, palettes and personal shot cleanup come from the supplied cartridge's own tables.

Presses during recoil are discarded. A new press after recovery works normally, including at the native life meter's last unit.

```text
python tools/qa_controls.py --exe build/Release/MegaMan6Recomp.exe --rom "/path/to/Mega Man 6 (USA).nes" --out qa/controls
```

The regression checks compare native/interpreter execution, ordinary gameplay with the feature idle or disabled, acquired/locked/empty equipment, native inventory graphics and palettes, player/menu input isolation, and held-input state continuation. This controls slice does not establish full adapter, character, co-op or stage acceptance.

## Development Proto Man

The command line exposes the initial optional solo implementation:

```text
MegaMan6Recomp.exe "/path/to/Mega Man 6 (USA).nes" --protoman on --proto-mode protoman
```

Select swaps characters using a 32-frame teleport. Proto Man retains the native charge and slide mechanisms, limits ordinary Buster shots to two, takes double native contact damage with normal recoil, and reflects the native small pellets (types 14/1A) while airborne and facing them without firing. `--proto-mode megaman` retains Mega Man's traits with Proto Man's artwork. `--proto-start megaman` starts as Mega Man.

Native actor ownership identifies the body OAM parts; the compositor replaces those parts with the credited immutable sprite atlas. Native entry/effect animations and adapter forms currently retain their original artwork. Proto-themed Jet/Power forms, complete pose and projectile coverage, inventory portraits and launcher activation remain unfinished.

The initial gallery matches native/interpreter execution for 4,800 frames in both modes. Mid-swap and mid-recoil loads reproduce memory and presentation; actual contact deals 6 damage with Proto Man traits and 3 in reskin mode with the same recoil. A real native pellet reflects into a native Buster shot, which damages an enemy while a special weapon is selected. Rear, grounded, firing and reskin cases retain native damage. Mid-reflection continuation matches the uninterrupted picture, memory and PCM. These are targeted checks, not complete game acceptance.

```text
python tools/qa_route.py tests/routes/proto-gallery.route --frames 4800 --exe build/Release/MegaMan6Recomp.exe --rom "/path/to/Mega Man 6 (USA).nes" --out qa/proto --extra=--protoman --extra=on
python tools/qa_route.py tests/routes/proto-reflected-target.route --frames 4040 --exe build/Release/MegaMan6Recomp.exe --rom "/path/to/Mega Man 6 (USA).nes" --out qa/reflection --extra=--protoman --extra=on
```

## Development local co-op

```text
MegaMan6Recomp.exe "/path/to/Mega Man 6 (USA).nes" --coop on --weapon-cycle on
```

`--coop-roster protoman-megaman` reverses the two characters. Co-op enables the Proto Man dependency; `--proto-mode megaman` keeps both characters' native Mega Man traits. Each personal actor pool runs its original coroutine on the real game clock, while level actors run once. The partner uses the native animation and sprite emitter with personal weapon patterns and palettes. Controls, charge, slide, ordinary Buster limits, contact damage and airborne reflection belong to each seat.

Either controller can open and operate its own native inventory while the world pauses. The menu page, cursor, equipment and ammo belong to that player; E-tanks and stage progress remain shared. Native base/form and weapon graphics packets keep different equipped weapons from sharing the wrong patterns.

An individual death retains the native cue and particles while the partner keeps playing with stage music. The last death uses one shared life and the native retry, restoring both players to full HP and their own ammo with the native Buster selection. Both death orders, a load during the partner's actual in-flight death routine, and native/interpreter execution have targeted coverage. These assisted routes set health, inventory and projectile/position prerequisites; they leave deaths, the life decrement and retry to the native game.

Either controller can navigate the boot screens and accept Game Over. After selecting another stage, both players revive with native full HP, Buster selected and their own retained ammo. A living player's personal inventory continues to accept only its owner's controls. Native/interpreter execution and cold loads cover the Game Over menu and subsequent stage intro.

Charge flashes belong to each character. Pausing freezes both effects; releasing a shot or changing equipment clears only that player's effect. A charged survivor retains the charge and its artwork after the partner dies. The small palette sequence reads the native cartridge's own packets; Buster movement, damage and allocation remain native.

The optional, default-on **No sprite flicker** display package captures complete native sprite packets in the game's original priority. It draws parts beyond the hardware scanline and 64-part OAM limits without changing the native OAM, gameplay or audio. This keeps a surviving player visible during dense death effects. `--no-flicker off` restores the original hardware limits. The native inventory and other global screens retain their original rendering.

A targeted death-effect scene captures 109 native parts, including all nine survivor parts omitted by the full hardware OAM. On/off gameplay and PCM match; native/interpreter runs and a cold load reproduce the complete packets and first picture. A fresh packaged catalog enables the option, and a simple gameplay scene matches the original native pixels exactly. These checks do not establish complete stage coverage.

This is an initial local implementation. Partner HUD, complete weapon and adapter behavior, checkpoint revival, withdrawal/rejoin, complete stage transitions, all view modes, adaptive widescreen and online play remain unfinished. Online sessions are explicitly blocked while these development features are active.

```text
python tools/qa_route.py tests/routes/coop-controls.route --frames 4700 --exe build/Release/MegaMan6Recomp.exe --rom "/path/to/Mega Man 6 (USA).nes" --out qa/coop-controls --extra=--coop --extra=on
python tools/qa_route.py tests/routes/coop-traits.route --frames 4400 --exe build/Release/MegaMan6Recomp.exe --rom "/path/to/Mega Man 6 (USA).nes" --out qa/coop-traits --extra=--coop --extra=on
python tools/qa_route.py tests/routes/coop-weapons.route --frames 4850 --exe build/Release/MegaMan6Recomp.exe --rom "/path/to/Mega Man 6 (USA).nes" --out qa/coop-weapons --extra=--coop --extra=on --extra=--weapon-cycle --extra=on
python tools/qa_route.py tests/routes/coop-life.route --frames 5400 --exe build/Release/MegaMan6Recomp.exe --rom "/path/to/Mega Man 6 (USA).nes" --out qa/coop-life --extra=--coop --extra=on
python tools/qa_route.py tests/routes/coop-life-reverse.route --frames 5600 --exe build/Release/MegaMan6Recomp.exe --rom "/path/to/Mega Man 6 (USA).nes" --out qa/coop-life-reverse --extra=--coop --extra=on
python tools/qa_route.py tests/routes/coop-charge.route --frames 4600 --exe build/Release/MegaMan6Recomp.exe --rom "/path/to/Mega Man 6 (USA).nes" --out qa/coop-charge --extra=--coop --extra=on
python tools/qa_route.py tests/routes/coop-charge-handoff.route --frames 4700 --exe build/Release/MegaMan6Recomp.exe --rom "/path/to/Mega Man 6 (USA).nes" --out qa/sprite-overflow --extra=--coop --extra=on --extra=--coop-roster --extra=protoman-megaman --extra=--no-flicker --extra=on
python tools/qa_route.py tests/routes/coop-gameover.route --frames 7400 --exe build/Release/MegaMan6Recomp.exe --rom "/path/to/Mega Man 6 (USA).nes" --out qa/game-over --extra=--coop --extra=on
```
