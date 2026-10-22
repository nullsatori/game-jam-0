# RMB-93 local model viewer

Run `serve.py` with Python 3 from any directory, then open <http://127.0.0.1:8733/viewer/>. The server binds only to localhost and serves only this asset's `v02` directory. Install the pinned Three.js dependency with `pnpm install` if `node_modules` is missing. No external art or environment maps are downloaded.

The default view looks along glTF +Z from negative Z, so Blender +Y is the visible left side. glTF +X remains forward and points left on screen. The environment lighting is generated from Three.js RoomEnvironment with three studio reflection cards: a broad overhead strip for the tubes, a low frontal fill for the receiver planes, and a subdued rear rim. These cards exist only inside the environment capture, never in the asset or export. A weak frontal and hemispheric fill preserve the graphite grip and fore-end in shadow. This is presentation lighting, not an Unreal environment.

- Buttons: left/right/front/rear/top/bottom/three-quarter views, perspective ADS, and counter close-up.
- Mouse: orbit, pan, zoom. H hides or restores all interface elements.
- Counter: live contract atlas digits 0–9 and OFF; optional bloom and wireframe.
- `?capture` hides UI; `?view=hero&digit=2` selects a preset and numeral.

**Цевьё** (`viewForeend`, view `foreend`) and **Рукоятка** (`viewGrip`, view `grip`) show tight orthographic three-quarter detail views. Each fits the actual part vertices with an 18% framing allowance, recalculated for the viewport and saved capture aspect ratios. Separate named meshes are supported; joined production exports use the authored fore-end/grip vertex regions within `Action`/`Body`. They retain the same geometry, materials, lighting, and current art pose.

**Приклад** (`viewStock`, view `stock`) adds a top-oblique orthographic detail view of every mesh tagged `assembly: Stock`, plus a small allowance around the stationary hinge. It fits the current world-space bounds, so selecting the view after opening loading access keeps the raised stock visible without resetting that pose. The same framing allowance and native capture sizing apply.

**Затыльник** (`viewButtpad`, view `buttpad`) is a front-oblique close-up of `Folded_stock_front_bridge` and `Folded_stock_heel_pad`, with roughly the final 4cm of the arms for steel-depth context. The current GLB joins these names into `Stock`, so the fallback selects its closed-pose vertices at X > 0.395m and extends the context boundary back to X = 0.365m. Bounds and the front-facing camera direction then follow the current folded, loading-access, or deployed stock transform. Selecting this view never changes the pose, materials, or lighting.

**Коробка** (`viewReceiver`, view `receiver`) frames the receiver side, left lever and upper lid/counter joint. Joined `Body` exports use the Blender region X [−0.060,0.125], Y [−0.025,0.025], Z [0,0.070] metres; separate `Receiver_shell` and `Left_control_lever` meshes are recognized too. The orthographic left-oblique direction is glTF (0.23,0.18,−1), fitted with the existing 18% allowance. Lighting, exposure, materials and poses are retained.

**Спусковой узел** (`viewControls`, view `controls`) is a close left-oblique view of the guard, trigger, and left lever with receiver/grip context. It recognizes `Trigger_guard`, `Trigger`, and `Left_control_lever`; joined `Body` exports use the authored Blender region X [−0.045,0.035], Y [−0.012,0.027], Z [−0.038,0.031] metres, converted to glTF axes. Camera direction is glTF (0.18,0.12,−1). The view preserves all current poses and presentation settings.

**Приклад разложен** (`stockDeployed`) toggles a separate deployed art pose: Stock rotates +195° around glTF Z at its existing hinge, while the loading lid closes and the action returns to zero travel. Entering this pose switches to a mild three-quarter whole-weapon view (`deployed`) that fits the **current posed bounds**, including the rearward stock. The focused `stock` view also uses a direction appropriate to the deployed frame. Opening loading access instead selects the existing +110° stock / −80° lid access pose; access and reset both clear deployed state. `__metrics.stockDeployed` and the button's `aria-pressed` expose that state. These are distinct art-review poses, not a tested shoulder attachment, player animation, or measured mechanism.

ADS uses a 55° vertical perspective camera at glTF `(-.40,.074,0)` looking parallel to +X toward `(.48,.074,0)`. This is a geometric review of the rear-facing counter and sights, not a verified player camera. `?view=ads` opens it directly. The counter close-up follows the exported Counter face's actual world-space normal, so it supports the revised rear chamfer as well as earlier source versions. No screen crosshair is added.

The fore-end slider previews all meshes marked `assembly: Action` with 0–55mm travel along glTF +X. The **Доступ к загрузке** button shows a static art access pose: `Stock` rotates 110° around glTF Z at `(-.012,.063,0)`; `LoadingLid` rotates −80° around glTF Z at `(.105,.055,0)`. Access mode resets the fore-end to closed and locks the slider until access is closed. Controls are disabled when their exported parts are absent. Every transform derives from the saved closed pose, so repeated changes do not accumulate drift. **Исходная поза** restores both controls. These are art-review transforms, not measured mechanical travel or gameplay reload animation. They introduce no ammunition or gameplay behavior. The values are centralized in `poses.js` for adjustment to the finished model.

For screenshot automation, `window.__review.size(1672,941)` sets the actual canvas resolution. `window.__metrics` reports load readiness, model triangles, mesh count, dimensions, canvas size, rolling visible-tab frame time/FPS, and errors. Wait until at least 120 visible frame samples before measuring. FPS describes this browser viewer and machine only; it does not establish UE or target device performance.

The postprocessing composer uses up to **4× MSAA on its own half-float render targets**, chosen from the sample counts supported for both RGBA16F and the 24-bit depth attachment. This covers geometry edges before bloom; default-canvas antialiasing alone does not cover those targets. `__metrics.msaaSamples` reports the selected count (0 on unsupported hardware). Runtime FPS must be measured again after this change.

The **Сохранить кадр** button, or `await window.__review.saveCapture()`, saves an exact 1672×941 canvas PNG to `renders/live_latest.png` and a current measurement snapshot to `renders/viewer_metrics.json`. Interface elements are excluded. The original canvas size is restored after saving, including on request failure. These two fixed files are overwritten on each capture. The endpoint rejects other paths, dimensions, payload types, and cross-origin requests. Resizing resets FPS samples. Captured FPS is therefore zero when the capture needed a size change; for a performance measurement, first set the viewer to the capture resolution, let visible frames accumulate, then save.

The **Neutral** (`lightNeutral`) and **Hard** (`lightHard`) buttons compare the same camera, pose, model, and materials under two light rigs. Neutral is the unchanged startup rig: environment 0.8, key 1.9 at (0.2,0.6,-0.55), fill 0.85, hemisphere 0.28, rim 1. Hard uses a grazing key at (0.6,0.16,-0.35), key 2.8, fill 0.12, hemisphere 0.08, rim 0.65, environment 0.25 to expose plane, normal, and roughness differences. Reflection environments and their geometry are shared. The counter retains its independent one-softbox environment, scaled by the selected environment intensity; its optical shader and glyphs are unchanged. Neither preset touches exposure, background, bloom, or framing. Leave exposure at the default 1 for matched comparisons, capture both without moving the camera, and archive each fixed-name capture before the next. This is a diagnostic studio light change, not a gameplay lighting simulation.

`window.__review.setLightPreset('Neutral'|'Hard')` selects the rig; `__metrics.lightingPreset` and capture metrics identify it, alongside actual intensities and key position. Switching resets FPS sampling. The existing `setLighting({...})` manual override remains available and marks the rig `Custom`. `setView()`, `setDigit()`, and `captureMode()` are exposed as well.

The dynamic digit shader keeps the source contract's RGB atlas channel meanings and aperture mask, then adds the layered instrument treatment below. There is no per-frame random digit change. Imported glTF normals must already use the OpenGL convention. The supplied source PNG normal maps remain DirectX for Unreal.

## Layered gas-discharge instrument shader

Implemented **in this WebGL viewer**, in `nixie.js`: opaque black cavity with a dielectric glass coat, thin hot cathode, localized orange gas sheath, restrained red halo, and four fixed inactive cathode contours at different virtual depths. The existing fine anode mesh is sampled on the deepest layer. Camera-correct parallax uses view-space position/UV derivatives to recover the physical tangent basis, so the illusion follows the rear chamfer when orbiting. The active wire sits 1.1mm behind the glass; inactive layers are 1.35–2.25 times deeper. Grazing displacement is capped at 10% of the local UV width/height to preserve the aperture. Inactive electrodes affect BaseColor only, including when the display is OFF. Their fixed glyphs do not shuffle when the digit changes.

Glass reflections come from a real clearcoat BRDF and a restrained optical normal curve, not a painted luminous rectangle. Clearcoat is 0.9, clearcoat roughness 0.12, texture normal scale 0.45, and IOR 1.46. A smooth lens slope of `(UV−0.5) × 0.30` affects clearcoat only. A separate **counter-only** environment contains a dark neutral background (linear RGB 0.12) and exactly one neutral softbox (linear radiance 0.4). It does not inherit the studio room's windows or other reflection cards. This is intended to produce a restrained broad partial reflection without changing the body's studio lighting or hiding the cathodes. Its apparent coverage is view-dependent and requires live review. Whole-scene bloom is deliberately restrained (strength 0.12); turning it off preserves the gas sheath and local halo. Optional stable microflicker changes intensity by at most 0.6%; it never changes the number or makes it blink off.

**Viewer parameters and historical pre-port mapping**. These names describe the WebGL shader; the current shared UE API and defaults are in `ArtSource/Instruments/Nixie/build_ue_nixie.py` and `Docs/Nixie.md`:

| Parameter | Viewer value | UE5 meaning |
|---|---:|---|
| `SP_DigitIndex` / `SP_CounterEnabled` | 4 / 1 | Existing atlas cell and power state; OFF is index 10 / enabled 0 |
| `SP_GlassDepthCm` | 0.11 | Active cathode depth; UE world units are centimetres |
| `SP_CathodeGhostStrength` | 0.06 | Nonemissive wire contribution to BaseColor |
| `SP_EmissiveIntensity` | 4.4 | Active cathode emission multiplier |
| `SP_DischargeStrength` | 0.48 | Atlas G gas envelope; subtract 0.12×R before coloring |
| `SP_HaloStrength` | 0.055 | Atlas B local red halo |
| `SP_MicroFlickerStrength` | 0.006 | Bounded intensity variation; set 0 for static validation |
| `SP_GlassCurvature` | 0.30 | Dimensionless optical normal slope; clearcoat only, no silhouette or digit distortion |
| `VP_EmissiveColor` | (1, 0.34, 0.065) linear | Hot wire; atlas R is raised to 1.10 |
| `VP_DischargeColor` | (1, 0.095, 0.003) linear | Localized orange discharge |
| `VP_HaloColor` | (1, 0.035, 0.001) linear | Restrained red falloff |

The viewer retains the RGB-atlas WebGL implementation. The current UE feature uses the same authored glyph paths with a shared distance/illumination atlas, analytic edge coverage and independently controlled optical layers. Neither dynamic shader is embedded in GLB; the GLB has a static digit preview.

## UE review

The WebGL ADS view is not the game camera. Actual UE review uses `AJamZeroGPawn` in `/Game/Jam/Maps/Lvl_Jam_Ship`, its bodycam postprocess and independent FirstPerson projection. Five dedicated preview assemblies were imported and checked for triangle count, centimetre scale, bounds, normals and two material slots. No existing game weapon assets or camera defaults were replaced. The transient candidate uses its own sight coordinates; old-model coordinates are not reused blindly.

The current shared UE material is `/Game/JamArt/Materials/Instruments/Nixie/M_Jam_Nixie`, with `MI_Jam_Nixie_RMB93` as the first application. See `Docs/Nixie.md` for runtime component setup, appearance parameters and the current verification status. Source compilation, actual rendered frames, performance and cooked gameplay are separate checks. The present project does not create visible first-person hands, so these views do not establish hand or shoulder contact.
