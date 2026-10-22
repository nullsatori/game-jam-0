# Jam Nixie counter

**v5 (21.10):** the face now hides a whole tube: a hexagonal anode mesh, ten cathodes stacked at their own depths in IN-14 order (the lit one shadowed by the wires in front, neighbours catching its glow), gas seen at three depths, Fresnel through the glass and dust lit from inside. Digit changes and power switches play from world-time stamps the component writes once per event: the discharge runs along the new cathode from its lead, the old one cools red, power-on strikes, drops out and restrikes. The atlas gained an A channel (distance along the wire). `preview_nixie.py` is a numpy mirror of the shader for offline before/after sheets. Rebuild order is unchanged: `build_sdf.py`, then `build_ue_nixie.py` in the Editor. See `Docs/Nixie.md`. The v4 notes below remain for history.

Reusable UE5.8 instrument material with the project's authored numerals. The active wire has a narrow hot core, orange gas sheath, and local glow that follows its strokes. Dark inactive cathodes occupy different virtual depths. A physical clearcoat supplies the visible glass reflection. Four subpixel samples stabilize the active wire at native ADS size without changing the glyph paths.

Saved assets live in `/Game/JamArt/Materials/Instruments/Nixie`: `M_Jam_Nixie`, `MI_Jam_Nixie_RMB93`, `BP_Jam_NixieDisplay`, and five shared textures. Their dependencies contain no preview folder. The extra distance atlas is a shared feature dependency; it does not replace any of the nine RMB93 PBR maps.

## Use

Place `BP_Jam_NixieDisplay`. Its engine Plane is 2.4 × 1.8 cm, faces actor -X, and has U toward +Y / V toward -Z. The saved `NixieCounter` component binds slot 0 of `DisplaySurface`. Initial Digit and power take effect at BeginPlay; the Blueprint does not promise interactive editor preview of these properties. During play call `SetDigit(0..9)` or `SetDigit(10)` for blank, and `SetCounterEnabled(bool)`. For another mesh, assign the material and use the component's `TargetMesh` / `MaterialSlots`, or `BindMesh`.

Each component owns its MID and a stable independent flicker phase. It does not tick or modify the shared material instance. The provided weapon evidence uses temporary bindings in the actual game pawn; it does not replace the game's existing shotgun or wire its ammunition count.

## Artist parameters

| Parameter | Default | Meaning |
|---|---:|---|
| `SP_DigitIndex` / `SP_CounterEnabled` | 4 / 1 | Numeral 0–9; 10 blank. Power 0 removes emission. |
| `SP_FaceWidthCm` / `SP_FaceHeightCm` | 2.4 / 1.8 | Physical face dimensions used for camera-correct parallax. |
| `SP_DepthCm` | 0.11 | Virtual cathode depth. |
| `SP_CoreIntensity` / `SP_DischargeIntensity` / `SP_HaloIntensity` | 2.8 / 1.5 / 0.24 | Hot core, narrow gas and restrained local halo. |
| `SP_GhostStrength` | 0.01 | Physically lit inactive wires. |
| `SP_InternalRadiance` / `SP_GlassEchoStrength` / `SP_EdgeRadiance` | 1.6 / 0.09 / 0.7 | Stroke-local chamber warmth, weak inner image and rim response. |
| `SP_ClearCoat` / `SP_ClearCoatRoughness` | 0.2 / 0.025 | Reflection strength and sharpness. |
| `SP_GlassCurvature` / `SP_CavityRoughness` | 0.10 / 0.65 | Optical normal curvature and dark substrate roughness. |
| `SP_MinCorePixels` | 0.85 | Minimum native coverage; close-up retains authored width. |
| `SP_SegmentFlickerStrength` / `Rate` / `Scale` | 0.05 / 5 / 18 | Smooth local discharge patches; the full names retain the `SP_SegmentFlicker` prefix. |
| `SP_FlickerPhase` | 0 | Per-instance phase; the component assigns it once. |
| `SP_FlickerStrength` | 0.006 | Very weak global instrument fluctuation. |

`VP_CoreColor=(1,.27,.045)`, `VP_DischargeColor=(1,.065,.002)`, `VP_HaloColor=(1,.025,.001)` are linear HDR multipliers. Small local flicker uses smooth spatial value noise with material Time; no frame hash or game Tick. Its time formulation does not depend on frame rate, but no frame-rate sweep was performed.

The material is opaque with virtual internal depth, not a transparent volume. `r.ClearCoatNormal=0` is preserved: one optical normal, suppressed substrate specular, outer clearcoat reflection. Global bloom, lens, exposure and camera defaults remain unchanged.

## Rebuild

1. Run `python ArtSource/Instruments/Nixie/build_sdf.py` with NumPy and Pillow. No SciPy is required. This reads the authoritative `ArtSource/Weapons/RMB93/v02/counter/build_counter.py` paths without executing its texture writes.
2. In this project's clean UE Editor Python, execute `runpy.run_path(str(Path(unreal.Paths.project_dir())/'ArtSource/Instruments/Nixie/build_ue_nixie.py'))['build']()` after importing `runpy`, `unreal`, and `Path` from `pathlib`. The builder writes only the feature asset root and saves all outputs; it refuses PIE or unrelated dirty packages.
3. After the native `UJamNixieCounterComponent` is built, run the equivalent `build_showcase.py` function `build()` to create the saved reusable Blueprint.

The HLSL is inlined into a Material Custom node; there is no machine-specific shader include or global engine shader registration. The builder imports its support maps from canonical ArtSource paths, not transient preview assets. The atlas is linear RGBA8, no mips, clamp/bilinear, 16 MiB resident. R encodes signed edge distance, G the float-convolved stroke-local irradiance, B authored wire radius. Cells 10–15 are explicitly blank and checked after PNG save. Details and source hashes are in `sdf_manifest.json`.

## Verified evidence and limits

`validation.json` records hashes, parameters, saved assets, runtime metadata and limitations. `renders/` contains the frozen v4 native HIP/ADS, diagnostic detail, angled glass, three temporal frames, and two actual Blueprint instances (2/8). All screenshots are 1672 × 941. The independent static review scored 8.1/Tier 3; movement and flicker were outside that static review.

UHT/Development Editor build and the native probe passed (24 checks). A fresh Editor process reopened the saved material, preset and compiled Blueprint, read back all parameters and checked dependencies. No preview dependency or dirty package remained.

Performance was sampled in Development Editor PIE on RTX 5080 / D3D12 at the actual 1996 × 1230 viewport, 600 frames per phase minus 30 warm-up frames. Native lit GPU median was 8.33 ms (repeat 8.17 ms); sixteen additional lit counters 8.15 ms, off 8.19 ms. Scene variation exceeds the on/off delta: these numbers do not establish an isolated material cost. Raw CSV and summaries are in `validation/`. UE reports 658 pixel / 153 vertex instructions and six samplers; its texture statistics are not a manual count of Custom-node fetches. OFF retains cavity/glass work.

Three temporal screenshots show small nonuniform changes along the wire (patch green-channel range median 0.36%, maximum 1.19% after tone mapping). This confirms a visible time-varying result, not isolated noise amplitude or motion acceptance. The actual pawn has no authored visible hands. Packaged/cooked validation, target-device performance, hand contact, reload animation and gameplay ammo binding remain separate gates.
