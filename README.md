# Mega Man 6 — NESRecomp

NESRecomp cycle-backend project for the original USA cartridge. Supply your own ROM; cartridge data is excluded from source and releases.

Development takes place on the feature branch. Native gameplay is the baseline for the character, co-op, controls and adaptive widescreen implementation.

```text
cmake -S . -B build -DNESRECOMP_ROM="/path/to/Mega Man 6 (USA).nes"
cmake --build build --config Release --parallel 2
```

Use `NESRECOMP_ROOT` and `NESRECOMP_RECOMP_UI` to select existing engine and launcher checkouts. Runtime tests use isolated configuration and explicit ROM paths.

The feature branch contains native gameplay, character and co-op development. Native Mega Man 6 mechanics, including slide, charge and Rush adapters, remain the behavioral reference. Full gameplay parity and release acceptance are still in progress.

## Launcher selections and online sessions

The launcher exposes Proto Man, co-op, adaptive widescreen and slowdown reduction alongside weapon switching and complete sprite presentation. Fit to screen, weapon switching, complete sprites and slowdown reduction are enabled in a fresh catalog. Proto Man and co-op are optional. Both characters retain native charge, slide and Rush adapter restrictions.

The online session profile adopts the host's roster, character behavior, native abilities, weapon controls, sprite policy, CPU budget and camera class before gameplay starts. It also verifies the cartridge and embedded character artwork, including the adapter-head policy. Shared online play uses a fixed common logical canvas; the physical windows can have different sizes. Independent views retain native 256-pixel grids and allow local Split, Player one and Player two choices within that camera class. Runtime changes cannot switch the agreed camera class. Leaving a match restores the prior runtime settings, catalog preferences, command-line precedence and video geometry.

Netplay is built by default. `MM6_NETPLAY_QUALIFIED` remains `OFF` while broader game integration is unfinished; qualification builds explicitly enable it. The representative Shared profile has an actual delayed-input PC/server native/interpreter check with matching canonical state, rollback and current-build continuation evidence. Separate-view UI changes have targeted checks; a complete moving online Split run is not claimed by this slice.

A 5,220-frame route through stage selection, teleport, movement, camera scrolling, enemies and the inventory matches the interpreter at every frame. This is representative baseline evidence, not complete stage or enhancement acceptance.

```text
python tools/qa_route.py tests/routes/boot-play.route --exe build/Release/MegaMan6Recomp.exe --rom "/path/to/Mega Man 6 (USA).nes" --out qa/baseline
```

The route helper is adapted from MegaMan2NESRecomp under the included license and additional permissions. No game cartridge bytes or extracted donor assets are included.

## Development adaptive widescreen

`--wide on --aspect fit` extends horizontal terrain from the native layout, block, metatile and palette tables. Authored director records bound connected horizontal rooms; vertical and unrecognized scene layouts retain the native view. The original viewport remains intact, with its clipped left edge filled when that edge moves inside the wider picture.

```text
MegaMan6Recomp.exe "/path/to/Mega Man 6 (USA).nes" --wide on --aspect fit --no-flicker on
```

Ordinary authored enemies in the added horizontal area use their original constructor, AI, projectiles and collision routines. The stage director stays on its native camera. Signed world positions place complete sprite packets without byte-wrapped duplicates in the original viewport. A killed authored enemy stays absent until its spawn position leaves the complete view, including when the native camera later reaches that position. Failed native allocations remain eligible for retry.

Fit has targeted checks at 256, 426, 568 and 854 pixels, with native/interpreter agreement and current-build moving-scene continuation. Terrain conformance retains the original game's edge differences separately: each such pixel must be reproduced from live native CIRAM. An assisted combat route activates six authored actors, damages and kills two margin enemies through original shots, and retains their identities without immediate respawn. The 740-frame cold continuation reproduces the first picture and PCM; the sprite-limit option changes presentation while hardware execution and audio stay identical. Stock width matches the disabled extension. Actor pool pressure, all scenes and fresh launcher defaults remain in progress; these targeted checks do not establish complete widescreen acceptance.

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

Airborne firing shares the open-jump helmet and boot baseline. The native Buster starts at the matching cannon height in both directions, with ground and ladder origins retained.

Native actor ownership identifies the body OAM parts; the compositor replaces those parts with the credited immutable sprite atlas. Native entry/effect animations retain their original artwork. Jet and Power adapters keep the original animated armor, wings, punches and exhaust, with Proto Man's credited helmet, visor and scarf replacing the native head. Fuel, attacks, movement and adapter restrictions continue through the original game routines. Inventory portraits, complete pose/projectile coverage and launcher activation remain unfinished.

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

Either player can lead a native room transition. The room director and cartridge camera keep one owner, and the partner follows the original body continuation with personal HP, weapon and ammo retained. Vertical arrivals keep the native entry column. Native attacks retain their seat identity so a Robot Master finisher can own the original victory script. Weapon-get, password and selection screens then use global input, and the next native stage intro restores both player controllers.

The optional **Room transitions** setting waits for both players at shutters and vertical exits. The first player's body waits at its native contact point while the partner and shared world keep running. The original scroll starts when the partner approaches the same exit. A lone survivor can continue. Choose the policy in the Co-op package or the runtime Gameplay menu; `--coop-wait either|both` provides the command-line equivalent. Online matches adopt the host's policy before gameplay and keep it fixed for the match.

Targeted checks cover both leaders through Tomahawk Man's horizontal and vertical rooms, P2 opening both boss shutters, and a real P2 charged lethal hit through the native clear, password, stage selection and a fresh intro. Native/interpreter execution and cold loads during the vertical scroll and boss outro reproduce state, pictures and PCM. Other transport, boss and equipment variants remain part of the unfinished co-op acceptance work.

Personal ammo also survives the global clear and selection screens. Actual native weapon grants and refills update both inventories; the weapon demonstration keeps its original single native body without replacing the other player's retained ammo. A native new-game reset discards the retained bank. Distinct-ammo, native-refill and new-weapon cases cover these boundaries, including a cold load while the personal inventories are pending in the password screen.

Independent partner movement uses a personal 256-pixel viewport for the original controller's bounds and helper checks. It leaves the shared source camera, NMI and native coroutine clock intact. A targeted route crosses a screen boundary with the other player stationary, then opens the guest's native inventory, changes equipment and closes it. Each pane presents its own native inventory; only the initiating player resumes the common world. Actual distant source loading and room handoffs preserve both players' resources and match the nearby native terrain. Native/interpreter execution and cold loads during movement, inventory and source loading reproduce state, pixels and PCM.

Either controller can open and operate its own native inventory while the world pauses. The menu page, cursor, equipment and ammo belong to that player; E-tanks and stage progress remain shared. Native base/form and weapon graphics packets keep different equipped weapons from sharing the wrong patterns.

An individual death retains the native cue and particles while the partner keeps playing with stage music. The last death uses one shared life and the native retry, restoring both players to full HP and their own ammo with the native Buster selection. Both death orders, a load during the partner's actual in-flight death routine, and native/interpreter execution have targeted coverage. These assisted routes set health, inventory and projectile/position prerequisites; they leave deaths, the life decrement and retry to the native game.

Either controller can navigate the boot screens and accept Game Over. After selecting another stage, both players revive with native full HP, Buster selected and their own retained ammo. A living player's personal inventory continues to accept only its owner's controls. Native/interpreter execution and cold loads cover the Game Over menu and subsequent stage intro.

In co-op, hold Select for 90 gameplay updates to withdraw while the other player is alive. Release Select and press it again to return beside the survivor on safe ground. Departure and arrival each take 32 gameplay updates and pause with the native inventory. Returning preserves personal health, equipment, ammo and Jet fuel. Advancing a native checkpoint recovers a fallen partner with full health and Buster while preserving personal ammo; recovery waits for a safe landing. A withdrawn partner does not prevent the survivor's native death/retry from spending one shared life. Turning co-op off resumes solo play with the active survivor at the next native actor boundary.

Charge flashes belong to each character. Pausing freezes both effects; releasing a shot or changing equipment clears only that player's effect. A charged survivor retains the charge and its artwork after the partner dies. The small palette sequence reads the native cartridge's own packets; Buster movement, damage and allocation remain native.

Both players have health and equipment meters in MM2's stacked co-op layout, with Proto Man's health tint and a separate boss meter. The native meter routine supplies the original glyphs and amounts; completed packets latch with the displayed frame and survive current-build loads. Native personal menus keep their original picture. Jet fuel also receives its original passive recharge for the non-anchor player, with actual flight/recovery matching the original player's fuel trajectory. The adapter visuals work for either seat, in solo play and with either sprite-limit setting. Native hardware state, sprite packets and audio remain unchanged; picture differences are confined to the helmet and scarf. Targeted checks cover real inventory selection, flight, Power attacks, both directions and the first picture/audio after a current-build load.

The optional, default-on **No sprite flicker** display package captures complete native sprite packets in the game's original priority. It draws parts beyond the hardware scanline and 64-part OAM limits without changing the native OAM, gameplay or audio. This keeps a surviving player visible during dense death effects. `--no-flicker off` restores the original hardware limits. The native inventory and other global screens retain their original rendering.

A targeted death-effect scene captures 109 native parts, including all nine survivor parts omitted by the full hardware OAM. On/off gameplay and PCM match; native/interpreter runs and a cold load reproduce the complete packets and first picture. A fresh packaged catalog enables the option, and a simple gameplay scene matches the original native pixels exactly. These checks do not establish complete stage coverage.

`--coop-view split|player1|player2` provides independent 256-pixel horizontal views; split uses two adjacent panes. The in-game Display menu can switch views, including between the two individual views while paused. Logical view widths remain stable through Fit/aspect and window changes; returning to Shared restores the selected aspect policy. Each pane has its own native health/equipment meters. Connected horizontal terrain comes from the native tables, while vertical and scripted scenes retain their native camera.

Ordinary actors activate across the union of the two views, excluding the empty gap. Their native identities retire when they leave that union and can respawn after returning. The distant partner retains real movement, shots and native collisions. Enemies target the nearest living player through their original actor routines, with one shared update and each player's own body, health and inventory. Both target seats, a partner's native death, shared and independent layouts, sprite flicker removal on/off and current-build backend/cold continuation have targeted coverage. Native room scripts retain their leader and source-camera handling. When the anchor dies in a recognized horizontal room, the survivor takes ownership of the original player and camera without a shared retry or life loss. Both death orders, native/interpreter execution, current-build cold loads, terrain pixels and actor reentry have targeted coverage. Unprotected native enemy hits continue to damage the survivor normally.

```text
python tools/qa_views.py --exe build/Release/MegaMan6Recomp.exe --rom "/path/to/Mega Man 6 (USA).nes" --out qa/views
```

Split and individual views provide native personal inventories while the original pauser holds the common world clock. Each player can select equipment independently; only the pauser resumes play. Shared E-tanks are consumed in a deterministic order, with the pauser first when both request the last tank. A guest's native refill completes before play resumes. The pauser's own menu retains the original pixels, and Proto Man has matching header portraits for Normal, Jet and Power. Different weapon and adapter choices, both pausers, native/interpreter execution and current-build cold picture/state/audio continuation have targeted coverage.

Complete weapon and adapter behavior, checkpoint revival, withdrawal/rejoin, remaining stage transitions, scripted/vertical view coordination, complete adaptive widescreen and online qualification remain unfinished. Online sessions are explicitly blocked while these development features are active.

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

The adapter routes grant the native acquisition prerequisites, select each form through the original inventory, and exercise native movement and attacks:

```text
python tools/qa_route.py tests/routes/proto-adapter-jet.route --frames 5600 --exe build/Release/MegaMan6Recomp.exe --rom "/path/to/Mega Man 6 (USA).nes" --out qa/adapter-jet --extra=--protoman --extra=on --extra=--no-flicker --extra=off
python tools/qa_route.py tests/routes/proto-adapter-power.route --frames 5600 --exe build/Release/MegaMan6Recomp.exe --rom "/path/to/Mega Man 6 (USA).nes" --out qa/adapter-power --extra=--protoman --extra=on --extra=--no-flicker --extra=off
```
