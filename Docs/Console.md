# Консольные команды

Каталог собран скриптом `Scripts/os_console_catalog.py` из регистраций в
`Source/` (`FAutoConsoleCommand*`, `TAutoConsoleVariable`,
`IConsoleManager::Get().RegisterConsole*` и обёрток над ними). Руками не
править: изменил команду или её строку помощи — перезапусти скрипт;
`python Scripts/os_console_catalog.py --check` падает, если каталог отстал
от кода ([Probes](Probes.md#каталог-команд)). Аргументы — начало строки
помощи после имени (до двоеточия), если она так начинается. «не в Shipping»
— регистрация под `#if !UE_BUILD_SHIPPING`.

Что проверяет каждая проба и на какой карте её гонять — [Probes](Probes.md).

Всего: 112 команд, 88 переменных.

## `os.Attach`

| Команда | Аргументы | Что делает | Файл |
|---|---|---|---|
| `os.Attach.Cost` | — | os.Attach.Cost: what the laser (its tick: a trace, the dot and the thread) and the clear magazine's rebuilds cost since the last call, then start over. *не в Shipping* | `Source/MindKore/OSWeaponAttach.cpp` |
| `os.Attach.Give` | `<Id\|all> [bag]` | os.Attach.Give <Id\|all> [bag]: into the pouch; onto the gun in hand when a free place fits (bag: the pouch only). *не в Shipping* | `Source/MindKore/OSWeaponAttach.cpp` |
| `os.Attach.List` | — | os.Attach.List: the guns, their places, what is on them, the pouch. *не в Shipping* | `Source/MindKore/OSWeaponAttach.cpp` |
| `os.Attach.Mount` | `<Id> [slot\|gun Id]` | os.Attach.Mount <Id> [slot\|gun Id]: onto the gun at once, no motion (default the gun in hand); given first if not carried. *не в Shipping* | `Source/MindKore/OSWeaponAttach.cpp` |
| `os.Attach.Probe` | — | os.Attach.Probe: the attachment menu, seating, the laser, the torch, the suppressor, the RMB-93 swap and the 20-round magazine, step by step (Lvl_OS_TestRoom, -nullrhi); PASS/FAIL per line. *не в Shipping* | `Source/MindKore/OSAttachProbe.cpp` |
| `os.Attach.Stills` | `[X Y Z Pitch Yaw Roll]` | os.Attach.Stills [X Y Z Pitch Yaw Roll]: HighResShot frames of the attachment menu, the laser rolling on, its dot on a wall, the gun's side with the suppressor and the clear magazine at 20 / 7 / 0 rounds; os.Attach.Cost over 3 s of the laser (Lvl_OS_TestRoom, real RHI). Six numbers try a pistol menu pose (OffsetCm, Turn) for the run. *не в Shipping* | `Source/MindKore/OSAttachProbe.cpp` |
| `os.Attach.Take` | `<Id\|all>` | os.Attach.Take <Id\|all>: off the gun at once and out of the pouch. *не в Shipping* | `Source/MindKore/OSWeaponAttach.cpp` |
| `os.Attach.Unmount` | `<Id\|all>` | os.Attach.Unmount <Id\|all>: off its gun at once, into the pouch. *не в Shipping* | `Source/MindKore/OSWeaponAttach.cpp` |

## `os.Audio`

| Команда | Аргументы | Что делает | Файл |
|---|---|---|---|
| `os.Audio.LimiterProbe` | `[Gain]` | os.Audio.LimiterProbe [Gain]: log the master submix's effect chain and record the master output under a pistol burst and three SV-98 shots. | `Source/MindKore/OSAudioProbe.cpp` |

## `os.Debug`

| Команда | Аргументы | Что делает | Файл |
|---|---|---|---|
| `os.Debug.Export` | `[name]` | os.Debug.Export [name]: the config to Saved/OSDebug/Presets/<name>.json and the clipboard. | `Source/MindKore/OSDebug.cpp` |
| `os.Debug.FixedStep` | `<Hz\|0>` | os.Debug.FixedStep <Hz\|0>: fixed simulation step (FApp), 0 = off. | `Source/MindKore/OSDebug.cpp` |
| `os.Debug.Fps` | `<N\|0>` | os.Debug.Fps <N\|0>: frame-rate cap (UGameUserSettings + t.MaxFPS), 0 = none. | `Source/MindKore/OSDebug.cpp` |
| `os.Debug.Get` | `<key>` | os.Debug.Get <key>: a parameter's value, and its original if changed. | `Source/MindKore/OSDebug.cpp` |
| `os.Debug.GiveGun` | `<pistol\|shotgun\|sv98\|rmb93\|all>` | os.Debug.GiveGun <pistol\|shotgun\|sv98\|rmb93\|all>: the player gets that gun on any map (the menu's «Оружие» buttons). | `Source/MindKore/OSDebug.cpp` |
| `os.Debug.Gravity` | `<scale>` | os.Debug.Gravity <scale>: every zone's gravity times this (0 = zero-G everywhere). | `Source/MindKore/OSDebug.cpp` |
| `os.Debug.Group` | — | Debug menu: open the groups of the current tab whose name has this text; 'all' opens them all, 'none' folds every group (remembered in Saved/OSDebug/OpenGroups.txt). | `Source/MindKore/OSDebug.cpp` |
| `os.Debug.Hitch` | `<ms> <s>` | os.Debug.Hitch <ms> <s>: a frame of <ms> every <s> seconds of real time (0 ms = off). | `Source/MindKore/OSDebug.cpp` |
| `os.Debug.Import` | `<file\|clipboard>` | os.Debug.Import <file\|clipboard>: replace the config; unknown keys are skipped and listed. | `Source/MindKore/OSDebug.cpp` |
| `os.Debug.Menu` | — | Debug menu (F6): toggle; or open on a tab (Главное, Система, Мир, Игрок, Оружие, Враги, Пресеты; Закреплено = Главное), 'pin' (Shift+F6), 'close'. | `Source/MindKore/OSDebug.cpp` |
| `os.Debug.Pause` | `[0\|1\|step]` | os.Debug.Pause [0\|1\|step]: pause, resume, or one frame while paused. | `Source/MindKore/OSDebug.cpp` |
| `os.Debug.Preset` | `<name>` | os.Debug.Preset <name>: apply a preset (asset or Saved JSON) instead of the current config. | `Source/MindKore/OSDebug.cpp` |
| `os.Debug.Probe` | — | Debug menu checks (headless, without -UseFixedTimeStep): set/get/reset/undo, export-reset-import, skipped keys, default pins, class defaults untouched, the gravity multiplier on the pawn and a body (Lvl_OS_Ship), the fixed step, the FPS cap. Ends with 'Debug probe: done'. | `Source/MindKore/OSDebugProbe.cpp` |
| `os.Debug.Redo` | — | Debug menu: redo (Ctrl+Y). | `Source/MindKore/OSDebug.cpp` |
| `os.Debug.Reset` | `[key\|owner\|all]` | os.Debug.Reset [key\|owner\|all]: back to the originals (default all). | `Source/MindKore/OSDebug.cpp` |
| `os.Debug.SaveAsset` | `[name]` | os.Debug.SaveAsset [name]: the config into /Game/OS/Debug/Presets/DA_Test_<name> (editor builds only). | `Source/MindKore/OSDebug.cpp` |
| `os.Debug.Search` | — | Debug menu: put this text into the search (empty: clear); 'changed' toggles «только изменённые». | `Source/MindKore/OSDebug.cpp` |
| `os.Debug.Set` | `<key> <value>` | os.Debug.Set <key> <value>: a parameter, as UE property text (Pawn.ArmLengthCm 72, Weapon[Дробовик].RecoilImpulse 20000). | `Source/MindKore/OSDebug.cpp` |
| `os.Debug.Shots` | — | Debug menu frames (real RHI): the landing view, a tab with folded groups, one open group with a changed and a pinned row, a search, an import's skipped list, the pinned overlay; Saved/Screenshots. | `Source/MindKore/OSDebugProbe.cpp` |
| `os.Debug.TimeScale` | `<0.1..2>` | os.Debug.TimeScale <0.1..2>: game speed. | `Source/MindKore/OSDebug.cpp` |
| `os.Debug.Undo` | — | Debug menu: undo the last change (Ctrl+Z). | `Source/MindKore/OSDebug.cpp` |
| `os.Debug.VSync` | `0\|1` | os.Debug.VSync 0\|1 | `Source/MindKore/OSDebug.cpp` |

## `os.Flyer`

| Команда | Аргументы | Что делает | Файл |
|---|---|---|---|
| `os.Flyer.BiteShot` | — | Rendered stills of a flyer bite: the visor blood at +0.2, +1.2, +2.5 s (Shot; needs a real RHI). | `Source/MindKore/OSFlyer.cpp` |
| `os.Flyer.Clip` | `[Seconds] [Ffmpeg] [shotgun] [stat] [roost\|wall]` | os.Flyer.Clip [Seconds] [Ffmpeg] [shotgun] [stat] [roost\|wall]: a recorded take (video and sound) of a fly-by attack - or the roost waking and hunting, or the flesh wall's pain call and the flock coming - the voices in the log; Saved/Clips (needs a real RHI and a window). | `Source/MindKore/OSFlyer.cpp` |
| `os.Flyer.CorpseShot` | — | A rendered still of a flyer killed over the planetoid, lying where it fell (Shot; needs a real RHI). | `Source/MindKore/OSFlyer.cpp` |
| `os.Flyer.Cost` | `[Seconds]` | os.Flyer.Cost [Seconds]: the flyers' tick cost on the game thread (clinging and the rest), per frame and per tick. | `Source/MindKore/OSFlyer.cpp` |
| `os.Flyer.EncounterProbe` | — | os.Flyer.EncounterProbe: the airlock to the linked flesh wall with shots into space (unheard), the shotgun into the wall (the pain call), back to the airlock (where the flock meets the player), a shot in the planetoid's air (heard). | `Source/MindKore/OSFlyer.cpp` |
| `os.Flyer.LogFlaps` (переменная) | значение (умолч. `false`) | Log every audible wing beat of every flyer (os.Flyer.Probe turns it on). | `Source/MindKore/OSFlyer.cpp` |
| `os.Flyer.MixCheck` | `[DistanceCm] [noatt] [shotgun]` | os.Flyer.MixCheck [DistanceCm] [noatt] [shotgun]: record the player's shot and, apart, a round (or a blast's pellets) into a flyer held that far ahead (Saved/Clips/FlyerMixCheck.wav; needs sound). | `Source/MindKore/OSFlyer.cpp` |
| `os.Flyer.Portraits` | `[Count] [Habitat]` | os.Flyer.Portraits [Count] [Habitat]: side stills of clinging flyers (the roost pose; Shot; needs a real RHI). | `Source/MindKore/OSFlyer.cpp` |
| `os.Flyer.Probe` | `[all] [Habitat]` | os.Flyer.Probe [all] [Habitat]: one flyer of the first (or the named) habitat - wander, hearing (vacuum, air), the ladder (curious dart, aim: stalking, aim: hunting and the cascade), dive tokens, bites, a stagger, flight, calming down, the player hides and leaves (de-aggro), shot dead, the corpse. 'all' lets its flockmates fly too. | `Source/MindKore/OSFlyer.cpp` |
| `os.Flyer.Rays` | `x y z` | os.Flyer.Rays x y z: 98 rays and 40 cm sweeps out of a point - what they hit how far (finding a way in). | `Source/MindKore/OSFlyer.cpp` |
| `os.Flyer.RoostProbe` | `[Habitat] [airlock\|cabin]` | os.Flyer.RoostProbe [Habitat] [airlock\|cabin]: the first EVA - the roost at rest and its cost, the unarmed way from the airlock to the roost and on to the pistol (no bite; a bump at the roost), the pistol, a shot into space, a round into the roost's hull: the cascade; then the player hides 120 s (where the fight left him, inside the airlock, or in the cabin nearest the roost): no new cascade. | `Source/MindKore/OSFlyer.cpp` |
| `os.Flyer.Shots` | — | Rendered stills riding along a flyer (ahead above, below, behind above, wider) and over its flock (Shot; needs a real RHI). | `Source/MindKore/OSFlyer.cpp` |
| `os.Flyer.Views` | `<file> [blind]` | os.Flyer.Views <file> [blind]: stills from listed views (name x y z tx ty tz [seconds]; Shot; needs a real RHI); blind: no flyer notices the player. | `Source/MindKore/OSFlyer.cpp` |
| `os.Flyer.WakeStrip` | `[x y z tx ty tz]` | os.Flyer.WakeStrip [x y z tx ty tz]: frames every 0.25 s of the roost waking in its cascade after a round into its hull (Shot; needs a real RHI). | `Source/MindKore/OSFlyer.cpp` |
| `os.Flyer.WallShots` | — | Stills of the encounter: the linked flesh wall blasted (the scream and the vent), then the flock gathering and coming (Shot; needs a real RHI). | `Source/MindKore/OSFlyer.cpp` |

## `os.Fx`

| Команда | Аргументы | Что делает | Файл |
|---|---|---|---|
| `os.Fx.ClearSmoke` | — | Remove every muzzle smoke cloud of the player (stills). | `Source/MindKore/OSMuzzleFx.cpp` |
| `os.Fx.HoleLive` (переменная) | значение (умолч. `1`) | 1 (v16c): a shroud hole's pop and wrap leave with the hole's live velocity, 0.6 of it, given to the air over 0.18 s - a short soft tail behind each hole on the move, nothing riding on and stopping dead; 0: the pop and wrap ride with the gun (cling) at the shot's velocity. | `Source/MindKore/OSMuzzleFx.cpp` |
| `os.Fx.HoleWrap` (переменная) | значение (умолч. `1`) | 1 (v16): a shroud hole's pop rides with the gun 0.2 s, then every hole lets a slow wisp out and back along the shroud that climbs round it; the seep leaves along its hole and rises. 0: the pop left in the world at once, threads straight up out of 4 holes, the seep straight up. | `Source/MindKore/OSMuzzleFx.cpp` |
| `os.Fx.MoverStill` | `[after s]` | [after s] Wait for the player's next smoke cloud (0.6 s old), throw the nearest simulating prop through it across the view at 3.5 m/s, HighResShot OSMover_0100/0250/0500ms. | `Source/MindKore/OSMuzzleFx.cpp` |
| `os.Fx.SmokeCost` | — | Log what the smoke grids cost since the last call - a step (on a worker thread): mean / max us; the game thread in a frame with a grid stepping (waiting for the steps, the box, the upload): mean / max us - then start over. *не в Shipping* | `Source/MindKore/OSMuzzleFx.cpp` |
| `os.Fx.SmokeStamp` (переменная) | значение (умолч. `1`) | 1: a shot's first frame of gas is laid into its grid on the game thread (seen on the shot's frame); 0: only the grid's step lays it (a frame later). | `Source/MindKore/OSMuzzleFx.cpp` |
| `os.Fx.SmokeTune` (переменная) | значение (умолч. `TEXT("")`) | Tuning overrides: 'Field=Value ...' on every gun's FOSMuzzleFx (Sim.Buoyancy=300) and mat.<Param>=Value (or R/G/B) on the cloud material. *не в Shipping* | `Source/MindKore/OSMuzzleFx.cpp` |
| `os.Fx.TunnelStill` | `[after s]` | [after s] Wait (from world time 'after') for the player's next smoke cloud (0.6 s old), carve a 3 x 3 grid of tunnels along the view through it (CarveRay, 7 cm), HighResShot OSTunnels_0/1000/2000ms. *не в Shipping* | `Source/MindKore/OSMuzzleFx.cpp` |
| `os.Fx.VolumetricSmoke` (переменная) | значение (умолч. `1`) | 1: muzzle smoke as volumetric voxel clouds (heterogeneous volumes); 0: card puffs. Both live in the world. | `Source/MindKore/OSMuzzleFx.cpp` |

## `os.Gas`

| Команда | Аргументы | Что делает | Файл |
|---|---|---|---|
| `os.Gas.Albedo` (переменная) | значение (умолч. `1.0f`) | os.Gas.Light 1: the gas's albedo, x its look's colour. | `Source/MindKore/OSGas.cpp` |
| `os.Gas.AmbientScale` (переменная) | значение (умолч. `0.75f`) | os.Gas.Light 1: the room's light on the gas, x (unlit gas about 0.5-0.6 of the walls round it). | `Source/MindKore/OSGas.cpp` |
| `os.Gas.Bands` (переменная) | значение (умолч. `4`) | The gas's opacity levels (4; 8 a finer variant). | `Source/MindKore/OSGas.cpp` |
| `os.Gas.BlockDiv` (переменная) | значение (умолч. `360.0f`) | The gas's screen block: round(height / BlockDiv) px (360: 4 px at 1440p, 3 at 1080p; 270 before v12 7d). | `Source/MindKore/OSGas.cpp` |
| `os.Gas.Capture` (переменная) | значение (умолч. `0`) | 1: os.Gas.WalkProbe saves every frame from 0.2 to 3.2 s (a self-check). *не в Shipping* | `Source/MindKore/OSMuzzleFx.cpp` |
| `os.Gas.Cards` (переменная) | значение (умолч. `0`) | 1: the old card wisps (shroud, muzzle stream) are drawn too; 0 (v11): the gas is parcels only. | `Source/MindKore/OSGas.cpp` |
| `os.Gas.Debug` (переменная) | значение (умолч. `0`) | 1: draw every cloud's box (white), parcels (points, red heavy to blue light), bodies (yellow, velocity arrow), tunnels (cyan), wall/floor planes (green) and sources (magenta); 2: also each cloud's key lamp in the log once a second. | `Source/MindKore/OSGas.cpp` |
| `os.Gas.DebugAlpha` (переменная) | значение (умолч. `0`) | 1: the gas post-process draws only its opacity, grey (os.Gas.WalkProbe measures its jumps a frame); 3: the block less the pixel's own march; 4: the room's light the gas takes (os.Gas.Light 1). | `Source/MindKore/OSGas.cpp` |
| `os.Gas.Dither` (переменная) | значение (умолч. `2`) | 2 (v13): the Bayer dither on the view sphere (a turn leaves the dots on the smoke), the block's opacity held against its last frame's, a world grain; 1: an ordered 4x4 Bayer on the screen (v12); 0: a hash per block, the tail from 0.12 (before v12). | `Source/MindKore/OSGas.cpp` |
| `os.Gas.DitherGrain` (переменная) | значение (умолч. `0.5f`) | os.Gas.Dither 2: the world grain's nudge of the threshold, x half a level. | `Source/MindKore/OSGas.cpp` |
| `os.Gas.DitherHold` (переменная) | значение (умолч. `0.15f`) | os.Gas.Dither 2: a pixel keeps its last frame's level while the new opacity has crossed into the next by less than this share of a step (the opacity itself is always this frame's). | `Source/MindKore/OSGas.cpp` |
| `os.Gas.DitherPx` (переменная) | значение (умолч. `0.0f`) | The Bayer threshold's cell, px (0: half a block, 1 px at least) - a finer lattice inside a block; the opacity is still the block's. | `Source/MindKore/OSGas.cpp` |
| `os.Gas.DriftCm` (переменная) | значение (умолч. `3.0f`) | The zone's air drift (v12 7a.3): a slow divergence-free field, 1.5 m across, changing over ~20 s - its top speed, cm/s. | `Source/MindKore/OSGas.cpp` |
| `os.Gas.FlashReach` (переменная) | значение (умолч. `2.5f`) | The muzzle's flash for the gas reaches this x its radius (v13 13b: the whole room's haze shows on the shot's frames); 1: as the lamp (80e4fec). | `Source/MindKore/OSGas.cpp` |
| `os.Gas.Fog` (переменная) | значение (умолч. `1`) | 1: zones with a fog keep it in their air (v15: seeded over the room's grid, held against ventilation, asleep out of reach); 0: as without one. | `Source/MindKore/OSGas.cpp` |
| `os.Gas.FogAmbient` (переменная) | значение (умолч. `0.35f`) | A fog's share of the room's light (the haze's: os.Gas.ZoneAmbient). | `Source/MindKore/OSGas.cpp` |
| `os.Gas.FogFallScale` (переменная) | значение (умолч. `1.0f`) | A floor fog's settling x (its layer's height). | `Source/MindKore/OSGas.cpp` |
| `os.Gas.FogG` (переменная) | значение (умолч. `0.65f`) | A fog's Henyey-Greenstein g toward the lamps (a glow round them). | `Source/MindKore/OSGas.cpp` |
| `os.Gas.FogLook` (переменная) | значение (умолч. `1`) | 1: the haze's look mixes toward its fog's by the fog's share of its mass (v15: its colour, a dimmer room light, a forward scatter); 0: as in v14. | `Source/MindKore/OSGas.cpp` |
| `os.Gas.FogMass` (переменная) | значение (умолч. `9.0f`) | A fog's background parcel mass at density 1 (calibrated: TestRoom, a floor fog 0.5 - CentreT ~0.35). | `Source/MindKore/OSGas.cpp` |
| `os.Gas.FogRefill` (переменная) | значение (умолч. `4.0f`) | A fog refills a furrow cut in it over this, s. | `Source/MindKore/OSGas.cpp` |
| `os.Gas.FogSet` | `<floor\|haze\|off> [density 0-1] [height cm]` | <floor\|haze\|off> [density 0-1] [height cm]: the fog of the zone the player stands in, for this play only (the map is untouched). *не в Shipping* | `Source/MindKore/OSGas.cpp` |
| `os.Gas.FogSource` (переменная) | значение (умолч. `1`) | 1: a source with bIntoHaze (the fog and steam presets) lets its gas straight into the zone's haze (v15); 0: through a cloud slot of its own, as a weld. | `Source/MindKore/OSGas.cpp` |
| `os.Gas.FogSplit` (переменная) | значение (умолч. `1`) | 1: a zone's haze with a fog in it is marched in two goes round the other clouds - in front of a puff over it, behind it under it (v15d; smoke alone as before); 0: in one go. | `Source/MindKore/OSGas.cpp` |
| `os.Gas.FogStir` (переменная) | значение (умолч. `0.25f`) | A floor fog's vertical drift and wander x (its layer stays down). | `Source/MindKore/OSGas.cpp` |
| `os.Gas.FogWakeCm` (переменная) | значение (умолч. `1200.0f`) | A zone's fog wakes within this of the camera, cm (sleeps past 1.25 x). | `Source/MindKore/OSGas.cpp` |
| `os.Gas.Forward` (переменная) | значение (умолч. `0.7f`) | os.Gas.Light 1: the forward-scattering share of the lamps' light (Henyey-Greenstein g 0.55; the rest isotropic) - smoke against a lamp glows at its rim. | `Source/MindKore/OSGas.cpp` |
| `os.Gas.HazeDiffuse` (переменная) | значение (умолч. `500.0f`) | The haze spreads through the room by a slow turbulent wander (v14), cm^2/s: 200 denser and slower, 400 faster and paler; 0: stays where it was (cb97b4e). | `Source/MindKore/OSGas.cpp` |
| `os.Gas.HazeDiffuseZ` (переменная) | значение (умолч. `0.2f`) | The haze's wander: its vertical share (warm air over cool is stable). | `Source/MindKore/OSGas.cpp` |
| `os.Gas.HazeDiffuseZeroG` (переменная) | значение (умолч. `0.5f`) | The haze's wander in zero-G, x (isotropic: no convection mixes it). | `Source/MindKore/OSGas.cpp` |
| `os.Gas.HazeDriftCm` (переменная) | значение (умолч. `6.0f`) | The haze rides the room's slow currents: their top speed, cm/s (shots' gas keeps os.Gas.DriftCm). | `Source/MindKore/OSGas.cpp` |
| `os.Gas.HazeDriftScaleCm` (переменная) | значение (умолч. `300.0f`) | The room's currents for the haze: their size, cm. | `Source/MindKore/OSGas.cpp` |
| `os.Gas.HazeDriftZ` (переменная) | значение (умолч. `0.4f`) | The vertical of the room's currents for the haze in gravity x this (v14c; a floor fog: the least of this and os.Gas.FogStir); 1 off. | `Source/MindKore/OSGas.cpp` |
| `os.Gas.HazeEddyCm` (переменная) | значение (умолч. `80.0f`) | The haze's wander comes mostly from a world-anchored eddy field this size, cm (neighbours move together: strands, not puffs). | `Source/MindKore/OSGas.cpp` |
| `os.Gas.HazeEddySeconds` (переменная) | значение (умолч. `10.0f`) | The haze's wander: how long a parcel keeps its eddy's velocity, s. | `Source/MindKore/OSGas.cpp` |
| `os.Gas.HazeFillMs` (переменная) | значение (умолч. `0.15f`) | The haze rooms' flood: game thread time a frame, ms. | `Source/MindKore/OSGas.cpp` |
| `os.Gas.HazeFlat` (переменная) | значение (умолч. `0.6f`) | The haze's and a fog's kernels in gravity are squashed along it (v14c): vertical x this, horizontal / its root, volume kept; 1 balls. | `Source/MindKore/OSGas.cpp` |
| `os.Gas.HazeNoiseCut` (переменная) | значение (умолч. `0.15f`) | The haze's noise threshold (0.3: as a shot's gas, holes at the kernels' rims). | `Source/MindKore/OSGas.cpp` |
| `os.Gas.HazeRadius` (переменная) | значение (умолч. `60.0f`) | The haze's parcels diffuse up to this radius, cm (60: a veil; 20: as before 24.10). | `Source/MindKore/OSGas.cpp` |
| `os.Gas.HazeRadiusMax` (переменная) | значение (умолч. `250.0f`) | A spreading haze parcel's radius grows with the haze's spread up to this, cm (overlapping into a veil, not round puffs); its matter kept. | `Source/MindKore/OSGas.cpp` |
| `os.Gas.HazeRoom` (переменная) | значение (умолч. `1`) | 1: the haze knows its room - a 25 cm grid flooded from the first shot by overlap tests: its walls hold it, open doors let it through, vacuum stops it (v14); 0: only the zone box's faces and the shots' planes. | `Source/MindKore/OSGas.cpp` |
| `os.Gas.HazeSamples` (переменная) | значение (умолч. `32`) | The haze's march samples at most. | `Source/MindKore/OSGas.cpp` |
| `os.Gas.HazeSlump` (переменная) | значение (умолч. `10.0f`) | The haze (and a fog) spreads as a layer in gravity (v14c): the thick flows to the thin at up to this, cm/s, and stops as it thins; 0 off. | `Source/MindKore/OSGas.cpp` |
| `os.Gas.HazeVents` (переменная) | значение (умолч. `1`) | 1: actors tagged GasVent draw the haze in (15 cm/s within 1 m) and take it 4x faster within 50 cm - its last gas goes there (v14); 0 off. | `Source/MindKore/OSGas.cpp` |
| `os.Gas.HazeVentScale` (переменная) | значение (умолч. `1.0f`) | The zones' haze half-life x (1: as set on the zone, 45 s; 2: 90 s). | `Source/MindKore/OSGas.cpp` |
| `os.Gas.HazeWake` (переменная) | значение (умолч. `0.15f`) | A body moving through the haze leaves a turbulent wake: its wander kicked by this x the body's speed (30 cm/s at most); 0 off (v14). | `Source/MindKore/OSGas.cpp` |
| `os.Gas.HazeWanderOwn` (переменная) | значение (умолч. `0.25f`) | The share of the haze's wander that is each parcel's own (1: all its own, the round puffs of v14). | `Source/MindKore/OSGas.cpp` |
| `os.Gas.Light` (переменная) | значение (умолч. `1`) | 1: the gas lit by the room (the frame before it, 1/32 and blurred) and the key lamp's full colour, a darker albedo (v11 stage 6); 0: the light before stage 6 (A/B). | `Source/MindKore/OSGas.cpp` |
| `os.Gas.LightScale` (переменная) | значение (умолч. `2.0f`) | os.Gas.Light 1: the lamps' light on the gas, x. | `Source/MindKore/OSGas.cpp` |
| `os.Gas.LowRes` (переменная) | значение (умолч. `1`) | 1: the gas drawn once per screen block by its own pass after the tonemapper (v11 stage 5); 0: by M_OS_GasPost in every pixel (the old way, kept until the merge). | `Source/MindKore/OSGas.cpp` |
| `os.Gas.NixieLamp` (переменная) | значение (умолч. `1.0f`) | The Nixie counter's orange glow on the smoke round the gun (a lamp for the gas only), x; 0 off. | `Source/MindKore/OSGas.cpp` |
| `os.Gas.ProbeSeconds` (переменная) | значение (умолч. `0.0f`) | > 0: os.Gas.WalkProbe runs this long and takes a HighResShot (OSWalk_4_end) half a second and a ProfileGPU 2 s before its end (the haze's frames and cost). *не в Shipping* | `Source/MindKore/OSMuzzleFx.cpp` |
| `os.Gas.ProbeSpawn` (переменная) | значение (умолч. `TEXT("")`) | A preset: os.Gas.WalkProbe near spawns it (os.Gas.Spawn) instead of each shot, and runs 24 s. *не в Shipping* | `Source/MindKore/OSMuzzleFx.cpp` |
| `os.Gas.ProbeSpawnLate` (переменная) | значение (умолч. `TEXT("")`) | A preset (or a whole os. command): os.Gas.WalkProbe spawns (runs) it once at 5 s (after its shots). *не в Shipping* | `Source/MindKore/OSMuzzleFx.cpp` |
| `os.Gas.ProbeSuppressor` (переменная) | значение (умолч. `0`) | 1: os.Gas.WalkProbe mounts the suppressor first. *не в Shipping* | `Source/MindKore/OSMuzzleFx.cpp` |
| `os.Gas.Rise` (переменная) | значение (умолч. `25.0f`) | A shot's warm gas rises as a thermal: this x sqrt(1 s / its age), cm/s (keeps rising till the ceiling, slowing as it mixes; at least os.Gas.WarmLift); 0: os.Gas.WarmLift alone, from 1 s. | `Source/MindKore/OSGas.cpp` |
| `os.Gas.Shafts` (переменная) | значение (умолч. `1`) | 1: a sun (directional) or a spot/rect lamp tagged GasShaft lights the haze in layers with the shadows of frames and jambs - its light traced to the haze's parcels, an eighth a step (v13 13c); 0: off (80e4fec). | `Source/MindKore/OSGas.cpp` |
| `os.Gas.Shimmer` (переменная) | значение (умолч. `2.0f`) | The air's shimmer behind heat (v12 7b.3), x; 0 off. | `Source/MindKore/OSGas.cpp` |
| `os.Gas.SmoothAlpha` (переменная) | значение (умолч. `0`) | 1: the block's result (light and opacity) read bilinearly between the blocks' centres (edge blocks as before) - finer still. | `Source/MindKore/OSGas.cpp` |
| `os.Gas.SourceTest` | `<preset> <pulse\|burst\|rate1>` | <preset> <pulse\|burst\|rate1>: a gas source 1.5 m along the view run by a pattern (on/off, bursts every 2.5 s, 1 a second). *не в Shipping* | `Source/MindKore/OSGasSource.cpp` |
| `os.Gas.Spawn` | `<muzzle\|port\|weld\|breach\|rcs\|dust\|fog\|steam\|list> [x y z]` | <muzzle\|port\|weld\|breach\|rcs\|dust\|fog\|steam\|list> [x y z]: a gas source of the preset at the point (1.5 m along the view by default) for os.Gas.SpawnSeconds, along the view. *не в Shipping* | `Source/MindKore/OSGasSource.cpp` |
| `os.Gas.SpawnSeconds` (переменная) | значение (умолч. `4.0f`) | How long an os.Gas.Spawn source lives, s (4). *не в Shipping* | `Source/MindKore/OSGasSource.cpp` |
| `os.Gas.SpreadLog` (переменная) | значение (умолч. `0`) | 1: every 5 s each haze logs its parcels, mass, spread across and up (mass-weighted sigma, cm), CentreT - the spreading's probe (v14). | `Source/MindKore/OSGas.cpp` |
| `os.Gas.StampAsync` (переменная) | значение (умолч. `1`) | 1: a shot's stamp (its first parcels into the atlas) starts on a worker the moment the gun has let its gas out; the gas tick only joins it - the same atlas on the same frame; 0: splatted in the gas tick. | `Source/MindKore/OSGas.cpp` |
| `os.Gas.StampPriority` (переменная) | значение (умолч. `1`) | 1: a shot's first splat on the game thread runs its parallel loops at normal priority (the frame waits for it); 0: at background priority, as the steps. | `Source/MindKore/OSGas.cpp` |
| `os.Gas.TestShaft` | `[distance cm]` | [distance cm]: a transient GasShaft spot lamp that far ahead of the view, aimed back (the haze's shafts on a map without a sun in its windows). *не в Shipping* | `Source/MindKore/OSGas.cpp` |
| `os.Gas.TickHz` (переменная) | значение (умолч. `0.0f`) | > 0: the gas's own evolution shown in steps of 1/TickHz s (stop motion); its place and the camera stay smooth. 0: off. | `Source/MindKore/OSGas.cpp` |
| `os.Gas.TileLamps` (переменная) | значение (умолч. `1`) | 1: a haze takes up to os.Gas.ZoneLamps of the zone's lamps and each screen block its own strongest at its gas (v13 13d), the player's lamps apart; 0: 4 lamps for the whole haze (80e4fec). | `Source/MindKore/OSGas.cpp` |
| `os.Gas.TileSlots` (переменная) | значение (умолч. `4`) | The zone's lamps a screen block takes (4-6; the last one soft - no seam where the 4th and 5th swap). | `Source/MindKore/OSGas.cpp` |
| `os.Gas.TorchShadow` (переменная) | значение (умолч. `1`) | 1: the torch's cone cuts the smoke with the shadows of what stands in it (a map of traces, v13 13a); 0: off (80e4fec). | `Source/MindKore/OSGas.cpp` |
| `os.Gas.TorchShadowEase` (переменная) | значение (умолч. `0.08f`) | The torch's shadow map: each texel's distance eases to its trace over this, s. | `Source/MindKore/OSGas.cpp` |
| `os.Gas.TorchShadowRes` (переменная) | значение (умолч. `16`) | The torch's shadow map for the gas, texels a side (16, up to 32). | `Source/MindKore/OSGas.cpp` |
| `os.Gas.TorchTracesPerFrame` (переменная) | значение (умолч. `64`) | The torch's shadow map: traces a frame (texels in turn; 64: a 16x16 map in 4 frames). | `Source/MindKore/OSGas.cpp` |
| `os.Gas.WalkProbe` | `[after s]` | [after s] Shoot while walking, stop, step back, turn, walk round the cloud; log the cloud's centroid every 0.25 s (and HighResShot OSWalk_*). *не в Shipping* | `Source/MindKore/OSMuzzleFx.cpp` |
| `os.Gas.WarmLift` (переменная) | значение (умолч. `4.0f`) | Residual warmth's lift at full gravity, cm/s (4; try 10-15: the haze gathers under the ceiling sooner). | `Source/MindKore/OSGas.cpp` |
| `os.Gas.ZoneAlbedo` (переменная) | значение (умолч. `0.8f`) | The haze's albedo it ages to over 20 s (old smoke coagulates and whitens: a light veil in the light, dark in the shade). | `Source/MindKore/OSGas.cpp` |
| `os.Gas.ZoneAlbedoSeconds` (переменная) | значение (умолч. `8.0f`) | The haze whitens to os.Gas.ZoneAlbedo over its smoke's this, s (a thin haze in a lit room is light grey, not a black veil; 20 as before). | `Source/MindKore/OSGas.cpp` |
| `os.Gas.ZoneAmbient` (переменная) | значение (умолч. `1.0f`) | The zone's haze: the room's light on it, x (it is seen by the lamps, not by the room). | `Source/MindKore/OSGas.cpp` |
| `os.Gas.ZoneDensity` (переменная) | значение (умолч. `0.6f`) | The zone's haze: the gas handed to it, x its mass. | `Source/MindKore/OSGas.cpp` |
| `os.Gas.ZoneLamps` (переменная) | значение (умолч. `16`) | The haze's lamps a block chooses from (4-16). | `Source/MindKore/OSGas.cpp` |

## `os.Gravity`

| Команда | Аргументы | Что делает | Файл |
|---|---|---|---|
| `os.Gravity.Cost` | — | Log what the zones' pull on loose bodies (OSPullBodies) cost per frame since the last call, then start over. | `Source/MindKore/OSGravity.cpp` |
| `os.Gravity.Probe` | — | Loose bodies in the gravity zones of Lvl_OS_Ship: casing, opt-out, throws against their prediction, a corpse that sleeps, the cost. | `Source/MindKore/OSGravity.cpp` |
| `os.Gravity.Stills` | — | Rendered stills of a throw in the cabin: aiming with the predicted arc, in flight, landed (Shot; needs a real RHI). | `Source/MindKore/OSGravity.cpp` |

## `os.Guns`

| Команда | Аргументы | Что делает | Файл |
|---|---|---|---|
| `os.Guns.Give` | `<slot\|Id\|name\|all>` | os.Guns.Give <slot\|Id\|name\|all>: open a gun (1..3, Pistol, Shotgun, Rmb93 - swaps it into 2 -, Sniper). *не в Shipping* | `Source/MindKore/OSWeaponAttach.cpp` |
| `os.Guns.RmbProbe` | — | The RMB rule step by step: aim with a gun out, grab (unarmed) with a loose body in reach, the hands when unarmed, a gun drawn while holding (Lvl_OS_ZeroG). | `Source/MindKore/OSScope.cpp` |
| `os.Guns.Swap` | `<slot> <Rmb93\|Slot>` | os.Guns.Swap <slot> <Rmb93\|Slot>: another gun into the slot, as the RMB-93 pickup does (2 Rmb93; Slot: the slot's own back). *не в Shipping* | `Source/MindKore/OSWeaponAttach.cpp` |
| `os.Guns.Take` | `<slot\|Id\|name\|all>` | os.Guns.Take <slot\|Id\|name\|all>: close a gun - put away if in hand; its rounds and attachments stay with it. *не в Shipping* | `Source/MindKore/OSWeaponAttach.cpp` |

## `os.Hands`

| Команда | Аргументы | Что делает | Файл |
|---|---|---|---|
| `os.Hands.Probe` | — | Lvl_OS_Ship: a hull tool taken mid-reload abandons it (no magazine, no rounds); a tool in hand has no gun (GetWeapon, the HUD's RMB line); then every hands key (LMB, RMB, MMB, wheel, R, 1, 2, the tool keys) injected through its input action with each thing in hand (gun, strap, a held body, nothing) in Walk, Hang, Ladder and Float, against the README controls table (PASS/FAIL each). | `Source/MindKore/OSHandsProbe.cpp` |

## `os.Hang`

| Команда | Аргументы | Что делает | Файл |
|---|---|---|---|
| `os.Hang.Probe` | — | The arm: on Lvl_OS_ZeroG a block edge caught in passing, a pumped swing let go, the same with a pull mid-swing, a kick while holding, a heavy body grabbed in passing; on Lvl_OS_Ship hanging under the cabin ceiling. Logs positions, velocities and the arm length over time; "shots" adds HighResShot frames of the catch / the hang (needs a real RHI). | `Source/MindKore/OSHangProbe.cpp` |

## `os.Head`

| Команда | Аргументы | Что делает | Файл |
|---|---|---|---|
| `os.Head.Probe` | — | The head against geometry (review #44) and the hold at rest: on Lvl_OS_TestRoom / Lvl_OS_Ship hold on to the ceiling and pull up with Space and W, swing (the camera's clearance every tick), walk under a spawned low slab; on Lvl_OS_Ship also hold on to a cabin wall, each door's leaf (both faces, closed and open) and the planetoid (flat and a slope) and rest (speed, contacts, shakes, reversals). PASS/FAIL each. "shots": the pull-up only, with HighResShot frames. Run at -UseFixedTimeStep -FPS=60. | `Source/MindKore/OSHeadProbe.cpp` |

## `os.Hull`

| Команда | Аргументы | Что делает | Файл |
|---|---|---|---|
| `os.Hull.Probe` | `[Index]` | os.Hull.Probe [Index]: hull repair logic on one breach - strap, junk, weld, tack, seal, cut, cling, death and the revival in the cabin (what it keeps and drops). | `Source/MindKore/OSShip.cpp` |
| `os.Hull.Stills` | `[Index]` | os.Hull.Stills [Index]: rendered stills of the suit projector weld frame on one breach - strapped with a gap, junk inside, tacked, half welded, sealed - from 2 m and 8 m, then ProfileGPU lit and dark (HighResShot; real RHI). | `Source/MindKore/OSShip.cpp` |

## `os.Ladder`

| Команда | Аргументы | Что делает | Файл |
|---|---|---|---|
| `os.Ladder.Probe` | — | The first ladder: grab, climb by the view, S, dead zone, strafe, over the top, down to the floor, jump off, strafe off; logs position and speed. 'shots': stills (needs a real RHI); 'trace': a line every frame (mode, ground, CanEnter, contacts). | `Source/MindKore/OSLadder.cpp` |

## `os.Laser`

| Команда | Аргументы | Что делает | Файл |
|---|---|---|---|
| `os.Laser.Mask` (переменная) | значение (умолч. `0`) | 1: show the laser's view of the gun on the HUD - the gun's mask over the beam in blue, the lens in green (yellow: no mask, the boxes' fallback). | `Source/MindKore/OSLaserSight.cpp` |
| `os.Laser.Tint` | `[N]` | os.Laser.Tint [N]: the laser's colour from its palette (0-5: red, green, blue, sodium, black light, IR); no N: the next one. | `Source/MindKore/OSLaserSight.cpp` |

## `os.Move`

| Команда | Аргументы | Что делает | Файл |
|---|---|---|---|
| `os.Move.Probe` | — | Lvl_OS_Ship: the shotgun and the SV-98 fired at the feet lift the walker, the grapple lifts it off the floor, an 800 kg body held slows the walk, holding a wall with the feet on the floor swings freely, and a view kick right after entering gravity from a rolled zero-G view does not drift (PASS/FAIL each, with the frame time). | `Source/MindKore/OSMoveProbe.cpp` |

## `os.Nixie`

| Команда | Аргументы | Что делает | Файл |
|---|---|---|---|
| `os.Nixie.Probe` | — | Per-instance Nixie MID, digit/OFF, binding and lifetime checks in PIE or -game (NullRHI supported). Optional material object path; default MI_OS_Nixie_RMB93. Does not change shared assets. *не в Shipping* | `Source/MindKore/OSNixieCounter.cpp` |
| `os.Nixie.WeaponProbe` | — | Runtime pickup -> production RMB93 in hands -> actual fire/reload -> switch/holster/replacement -> restore inventory. Takes ~5s; use PIE/-game in Lvl_OS_TestRoom or ZeroG; NullRHI supported. No assets saved. *не в Shipping* | `Source/MindKore/OSNixieWeaponProbe.cpp` |

## `os.Rmb93`

| Команда | Аргументы | Что делает | Файл |
|---|---|---|---|
| `os.Rmb93.Cost` | — | Log what the RMB-93's lid and shroud smoke (UpdateLid + UpdatePortSmoke) cost per frame with it in view since the last call, then start over. *не в Shipping* | `Source/MindKore/OSWeaponLid.cpp` |
| `os.Rmb93.Probe` | — | The RMB-93 through its handling on world time: shot, hull, shroud puff, reload with the lid, a broken-off reload, the lid in zero-G and gravity, a lid-only reload, a still. ~20 s; Lvl_OS_TestRoom or Lvl_OS_ZeroG, NullRHI fine; ends "Rmb93 probe: done". *не в Shipping* | `Source/MindKore/OSRmb93.cpp` |

## `os.Scope`

| Команда | Аргументы | Что делает | Файл |
|---|---|---|---|
| `os.Scope.AimFrames` | — | Every gun's raise, a shot aimed and the lowering every 33 ms of game time (time at a tenth), os.Scope.Trace on (HighResShot; needs a real RHI). | `Source/MindKore/OSScope.cpp` |
| `os.Scope.AimStills` | `<gun> <eye` | os.Scope.AimStills <gun> <eye:fov> ...: a settled aimed still for each eye distance / gun field of view (HighResShot; needs a real RHI). | `Source/MindKore/OSScope.cpp` |
| `os.Scope.Shots` | — | Stills of the SV-98's scope at a living wall ~20 m away: hip, raising, aimed (centre, edge), a shot through the bolt cycle, hip again, the pistol sight dots; GPU/frame time at the hip and aimed (HighResShot; needs a real RHI). | `Source/MindKore/OSScope.cpp` |
| `os.Scope.SwitchStills` | `[Gun] [To]` | os.Scope.SwitchStills [Gun] [To]: aimed with Gun (2), then To (0) selected with the aim held - stills and a per-tick log of the aim and the gun's field of view (HighResShot; needs a real RHI). | `Source/MindKore/OSScope.cpp` |
| `os.Scope.Trace` (переменная) | значение (умолч. `0`) | Log the scope's aim springs every tick (Scope trace: t, place, turn, alpha) - for plotting the curves. | `Source/MindKore/OSScope.cpp` |

## `os.Ship`

| Команда | Аргументы | Что делает | Файл |
|---|---|---|---|
| `os.Ship.Caves` | `[Crater] [Plug]` | os.Ship.Caves [Crater] [Plug]: shotgun a crater membrane and drop through; walk at a cave plug, then shoot it and walk through. | `Source/MindKore/OSShip.cpp` |
| `os.Ship.CaveShots` | — | Rendered stills of the planetoid: from afar, on the relief, a crater membrane before/after the shotgun, a cave plug (HighResShot; real RHI). | `Source/MindKore/OSShip.cpp` |
| `os.Ship.JetExit` | — | On the planetoid: jump, then Space in the air - the jet leap that leaves its gravity; logs the state. | `Source/MindKore/OSShip.cpp` |
| `os.Ship.Land` | `[Pitch] [Roll] [Speed]` | os.Ship.Land [Pitch] [Roll] [Speed]: fall onto the planetoid from a free, rolled view and log the landing every ~33 ms. | `Source/MindKore/OSShip.cpp` |
| `os.Ship.Lap` | — | Walk the player straight ahead around the planetoid for 35 s, logging angle, up vectors and view roll. | `Source/MindKore/OSShip.cpp` |
| `os.Ship.PlanetShots` | — | Two HighResShot stills standing on the planetoid, top and side (needs a real RHI). | `Source/MindKore/OSShip.cpp` |
| `os.Ship.Probe` | — | Ship: standing, walking and jumping in the cabin, the suit and the airlock, zero-G outside, the planetoid; a plate; every part in, the lever. | `Source/MindKore/OSShip.cpp` |
| `os.Ship.Shots` | — | Rendered stills of Lvl_OS_Ship from six viewpoints (HighResShot; needs a real RHI). | `Source/MindKore/OSShip.cpp` |
| `os.Ship.Where` | — | Logs location and speed of every actor with the given tag (default Junk.Foreign). | `Source/MindKore/OSShip.cpp` |

## `os.Sync`

| Команда | Аргументы | Что делает | Файл |
|---|---|---|---|
| `os.Sync.Probe` | `[Shots] [Ffmpeg]` | os.Sync.Probe [Shots] [Ffmpeg]: single shots per gun, recorded (video, master submix, per-frame gun pose, mixed onsets) for the shot/sound sync check. | `Source/MindKore/OSSyncProbe.cpp` |

## `os.Wall`

| Команда | Аргументы | Что делает | Файл |
|---|---|---|---|
| `os.Wall.BlastFrames` | `[DistanceCm]` | os.Wall.BlastFrames [DistanceCm]: a shotgun blast into the probe wall from 10 m, a still every frame for 0.5 s (HighResShot; real RHI, -UseFixedTimeStep -FPS=60). | `Source/MindKore/OSLivingWall.cpp` |
| `os.Wall.HitLag` | — | Fire to the living wall's wound for each gun at 5 / 10 / 20 m: first and last round landing, the wound (world ms; use -UseFixedTimeStep -FPS=N). | `Source/MindKore/OSLivingWall.cpp` |
| `os.Wall.Probe` | `[Name]` | os.Wall.Probe [Name]: pistol and shotgun at a living wall - torn cells, the passable window, close times, the pawn flown through. | `Source/MindKore/OSLivingWall.cpp` |
| `os.Wall.Seal` | — | Sweep the pawn sphere and a 15 cm sphere through a grid over every whole living wall (round ones: and around their edge); logs any pass. | `Source/MindKore/OSLivingWall.cpp` |
| `os.Wall.Shots` | `[Name\|Tag]` | os.Wall.Shots [Name\|Tag]: rendered stills of a living wall: whole, right after a shotgun blast, closing, a second blast (HighResShot; needs a real RHI). | `Source/MindKore/OSLivingWall.cpp` |

## `os.Weapons`

| Команда | Аргументы | Что делает | Файл |
|---|---|---|---|
| `os.Weapons.AirTake` | `[B\|sync]` | os.Weapons.AirTake [B\|sync]: on Lvl_OS_Ship, the guns' medium from the cabin through the airlock (PASS / FAIL), then the same sequence in the cabin and outside recorded (Saved/Clips/AirTake_<A\|B>.wav). B: the flyers' room. sync: 16 pistol shots, cabin and outside in turn (AirTake_sync.wav). | `Source/MindKore/OSAirTake.cpp` |
| `os.Weapons.Cycles` | — | The guns' moving parts frame by frame (os.Weapons.PoseAt), a live shot each and the fire gate (HighResShot; needs a real RHI). | `Source/MindKore/OSWeapons.cpp` |
| `os.Weapons.DrawPoseAt` (переменная) | значение (умолч. `-1.0f`) | Hold the gun in view this many seconds into its draw; < 0: live. | `Source/MindKore/OSWeaponParts.cpp` |
| `os.Weapons.Frames` | — | Every gun's cycle and reload as stills every 33 ms (os.Weapons.PoseAt / ReloadPoseAt; HighResShot, needs a real RHI). | `Source/MindKore/OSWeaponParts.cpp` |
| `os.Weapons.Fx` | — | First-person stills of each gun's muzzle flash frame by frame (60 fps frames 0-12 at time dilation 0.01), its smoke over 3 s and two more bursts (HighResShot; needs a real RHI). 'space': look away from the wall. | `Source/MindKore/OSMuzzleFx.cpp` |
| `os.Weapons.MagFrames` | — | The USP's and the SV-98's magazine drop and insert in first person at 60 fps (HighResShot; needs a real RHI). | `Source/MindKore/OSWeaponParts.cpp` |
| `os.Weapons.MagTrace` | — | Log the magazines' pose on the gun every 1/60 s through the reload. | `Source/MindKore/OSWeaponParts.cpp` |
| `os.Weapons.PortSmoke` | — | The RMB-93's shroud smoke up close on Lvl_OS_TestRoom (HighResShot OSPorts_*; needs a real RHI, -UseFixedTimeStep -FPS=60): three quick shots, stills 0.2-2 s after, two more and a turn. *не в Shipping* | `Source/MindKore/OSPortSmokeStand.cpp` |
| `os.Weapons.PortSmokeSide` | `[side -1\|1] [distance cm]` | [side -1\|1] [distance cm]: the RMB-93 from outside, 3/4 beside its shroud (style stills): one shot, HighResShot OSPortsSide_* 0.35 and 1 s after. The view stays on that camera. *не в Shipping* | `Source/MindKore/OSPortSmokeStand.cpp` |
| `os.Weapons.PoseAt` (переменная) | значение (умолч. `-1.0f`) | Hold the gun in view (kick and moving part) this many seconds after its last shot; < 0: live. | `Source/MindKore/OSWeapons.cpp` |
| `os.Weapons.PoseTrace` | — | Log the guns' pose springs and part curves every 10 ms through a cycle and a reload. | `Source/MindKore/OSWeaponParts.cpp` |
| `os.Weapons.ReloadPoseAt` (переменная) | значение (умолч. `-1.0f`) | Hold the gun in view this many seconds into a reload (a round-by-round gun loads two rounds); < 0: live. | `Source/MindKore/OSWeaponParts.cpp` |
| `os.Weapons.Reloads` | — | The reloads frame by frame (os.Weapons.ReloadPoseAt), spent cases and dropped magazines live (HighResShot; needs a real RHI). | `Source/MindKore/OSWeaponParts.cpp` |
| `os.Weapons.Shots` | — | First-person stills of the pistol and the shotgun, then the SV-98 at rest: at rest, +0.05 s after a shot, then +0.12 s (pistol) / +0.45 s (shotgun, the hole) (HighResShot; needs a real RHI). | `Source/MindKore/OSWeapons.cpp` |
| `os.Weapons.SlideFrames` | — | The USP's slide locking open on the last round and released in the empty reload, stills every 33 ms (HighResShot; needs a real RHI). | `Source/MindKore/OSWeaponParts.cpp` |
| `os.Weapons.SlideProbe` | — | The USP's slide stop live: locked open on the last round, a dead trigger, switches, the empty reload's release, the tactical reload unchanged (PASS / FAIL). | `Source/MindKore/OSWeaponParts.cpp` |
| `os.Weapons.SlideTake` | — | The USP's slide stop recorded for listening: the last shot, the empty reload with the release, the release only (Saved/Clips/SlideTake.wav and .csv). | `Source/MindKore/OSWeaponParts.cpp` |
| `os.Weapons.Smoke` | — | Stills of the smoke's moments in play on Lvl_OS_Range (run with -UseFixedTimeStep -FPS=60): on foot, holding on, a pistol burst, an SV-98 shot drifting off and back (or only those named: foot, cling, burst, sv98). 'budget': three shotgun clouds on the os.Weapons.Fx stand, then ProfileGPU. 'room': the RMB-93 at the floor of Lvl_OS_TestRoom, 'roomspace': the same shot on Lvl_OS_ZeroG into the black. | `Source/MindKore/OSMuzzleFx.cpp` |
| `os.Weapons.StowPoseAt` (переменная) | значение (умолч. `-1.0f`) | Hold the gun in view this many seconds into its put-away; < 0: live. | `Source/MindKore/OSWeaponParts.cpp` |
| `os.Weapons.SwitchFrames` | — | Every gun put away and drawn, stills every 33 ms (os.Weapons.StowPoseAt / DrawPoseAt; HighResShot, needs a real RHI). | `Source/MindKore/OSWeaponParts.cpp` |
| `os.Weapons.SwitchProbe` | — | Live switching with quick changes of mind and a holster/draw, logged every frame with the largest pose jump. | `Source/MindKore/OSWeaponParts.cpp` |

## `os.Weld`

| Команда | Аргументы | Что делает | Файл |
|---|---|---|---|
| `os.Weld.Curves` | — | The weld projector's appear curves (each group's flight, the signs typed) every 10 ms to Saved/Logs/WeldCurves.csv. *не в Shipping* | `Source/MindKore/OSWeldProjector.cpp` |

## `os.ZeroG`

| Команда | Аргументы | Что делает | Файл |
|---|---|---|---|
| `os.ZeroG.Grapple` | — | Zero-G pawn: hook a fixed block, then a light loose prop (logging both ends after 1 s), then a 45 deg roll input. | `Source/MindKore/OSZeroGPawn.cpp` |
| `os.ZeroG.Probe` | — | Zero-G pawn: push off what is in view, then shoot; logs the pawn velocity around each. | `Source/MindKore/OSZeroGPawn.cpp` |
| `os.ZeroG.Trace` (переменная) | значение (умолч. `0`) | 1: the player's pawn logs a line every tick (mode, body, velocity, eye, arm, contacts and shakes so far) and every body hit. | `Source/MindKore/OSZeroGPawn.cpp` |
