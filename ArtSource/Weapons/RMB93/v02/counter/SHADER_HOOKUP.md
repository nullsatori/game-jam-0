# RMB93 v02 — recessed instrument counter

These five textures were authored from continuous vector wire numerals and native optical material fields. `build_counter.py` regenerates the exact files with a deterministic seed. There are no fonts or external texture dependencies. `Counter_Digit_States.png` is a CPU contact sheet, **not an Unreal screenshot**. `Glyphs_Authored_Paths.svg` contains the original editable glyph centerlines.

Revision 5 follows the user's direction to extend the approved **4** into a more Matrix-like numeric alphabet: long diagonals, cropped terminals, faceted loops and unequal stroke weights. **Revision 5 removes only the upturned right crossbar tip of 4 at the user's request.** The artistic 1 keeps a centered stem with short terminals; the compact 3 retains its accepted 108×149px centerline bounds. The slashed 0 and crossed-waist 8 stay distinct, as do 1/7 and open 3/closed-loop 9. The active core remains **1.61–2.99px at 256px per cell**, superseding the old 4–6px art target. The gas sheath is about 4–7px with a restrained local halo, and inactive cathodes are visible but remain nonemissive. Atlas channel meanings, shader colors, normalization, and parameters are unchanged.

## Texture imports

| File prefix `T_Jam_RMB93_` | Size | RGB contents | sRGB | UE compression |
|---|---|---|---|---|
| `Counter_BC` | 512 | Dark glass, fine anode mesh, subdued cathodes | On | Default |
| `Counter_N` | 512 | DirectX tangent normal, shallow convex lens | Off | Normalmap; Flip Green Off |
| `Counter_ORM` | 512 | AO / Roughness / Metallic | Off | Masks |
| `Counter_Masks` | 512 | Aperture / Inactive wire / Glass dirt | Off | Masks |
| `DigitAtlas_M` | 1024 | Hot wire / Gas envelope / Soft local halo | Off | Masks |

All are 8-bit RGB PNG, no alpha. Clamp U/V. Atlas: bilinear, SimpleAverage mip generation, LOD bias 0. Its active cells remain unique and nonzero through BOX mip 3; OFF and reserved cells stay completely black. This is a texture check, not an AA/readability guarantee in the game.

## Counter mesh and UV

Opaque Default Lit, one recessed front face with unique full 0..1 UV. The latest approved position is the **upper rear receiver chamfer facing the player**, replacing the earlier left-side-only placement. Blender center is `(-0.030, 0, 0.056)` metres; outward normal is `(-0.8, 0, 0.6)`. Face width is 0.024m and height 0.018m. Counter shape/border come from the weapon mesh; this texture does not introduce bolts or a separate instrument box. The texture aperture is rounded and inset by about 4%.

UE convention here is UV origin upper left as seen looking at the counter face from the player side: U screen right, V screen down. Verify with the numeral **2**, not only **0** or **8**.

Cells are 256 square in a 4×4, row-major, upper-left-origin PNG. Indices 0–9 show matching numerals; 10 is OFF; 11–15 are reserved black. Each cell has 16px black gutters. For a sanitized integer digit `D`, compute:

```text
col = D % 4
row = floor(D / 4)
AtlasUV = (float2(col,row) * 256 + 16.5 + saturate(LocalUV) * 223) / 1024
```

For a **Blender** Image Texture node, where image coordinates have bottom-left origin and a conventionally oriented surface UV is also bottom-left, use:

```text
U_atlas = (col*256 + 16.5 + U_local*223) / 1024
V_atlas = 1 - (row*256 + 16.5 + (1-V_local)*223) / 1024
```

Blender uses OpenGL tangent normals: invert the Counter_N **green channel in the Blender material only**. Keep the saved PNG DirectX for UE. glTF export likewise needs the normal adapted to its convention.

## Material calculation

Counter_BC already contains very faint inactive wire contours. They are nonemissive. Optional ghost adjustment may add `Counter_Masks.G * SP_GhostStrength` to Base Color; default `0.06` is the contract value, but avoid double-strength ghosting if the baked contours are sufficient. Start with the unmodified BC for the authored preview.

Connect Counter_N to Normal, Counter_ORM R/G/B to AO/Roughness/Metallic. Counter_Masks.B is available for localized glass roughness adjustment; the authored roughness already incorporates that same dirt distribution.

Sample RGB from DigitAtlas_M at AtlasUV, then:

```text
Emissive = Aperture * saturate(SP_CounterEnabled) * max(SP_EmissiveIntensity,0)
  * (R * VP_EmissiveColor
   + G * VP_DischargeColor * SP_DischargeStrength
   + B * VP_HaloColor * SP_HaloStrength)

VP_EmissiveColor   = linear RGB (1, 0.32, 0.055)
VP_DischargeColor  = linear RGB (1, 0.10, 0.006)
VP_HaloColor       = linear RGB (1, 0.045, 0.003)
SP_EmissiveIntensity = 5       // first tune within 2..8
SP_DischargeStrength = 0.55
SP_HaloStrength = 0.12
SP_CounterEnabled = 1
SP_DigitIndex = 4             // art preview only
```

The revision 4 core varies from 1.61px on horizontal segments to 2.99px on vertical segments; diagonals and curves vary continuously between them. Its narrow orange sheath and soft amber halo remain in the material without postprocess bloom. Bloom should add optical spread, not create the identity. Avoid a white hot RGB core, hard outlined glow, luminous mesh, or extra floating emissive particles.

The thinner art changes the presentation tradeoff: at a 30px-tall displayed glyph, the geometric core is roughly 0.29–0.54px before filtering. The envelope makes it appear wider, but this does **not** establish the old 2px-core acceptance criterion. Check the actual first-person framing and temporal AA; enlarge the displayed instrument if a clear core is required. Pixel-width acceptance remains open.

## Static Blender / glTF preview derivatives

`Counter_Emission4.png` is 512×512 **linear RGB**, generated by exact bilinear sampling of production atlas digit 4 with the contract UV formula and aperture. The weighted RGB sum is divided by **1.67**; use white emission color and **8.35** strength to reproduce contract intensity 5 (`5 × 1.67`). In Blender, load this version as Non-Color.

`Counter_Emission4_sRGB.png` contains the same values encoded with the sRGB transfer function. Use this version for a standard glTF emissive texture, which is decoded as sRGB, or in Blender with image color space sRGB before export. It needs the same 8.35 strength. Do not put the linear PNG in a glTF emissiveTexture without arranging equivalent decoding.

`Counter_N_OpenGL.png` is the source DirectX normal with only green inverted. These are preview derivatives in `counter/`, not additions to the UE texture contract. The live counter must still sample the atlas and update its digit.

## Gameplay handoff

The reusable `UJamNixieCounterComponent` owns one MID per bound display slot, with no Tick or global MPC. Its source is `Source/MindKore/JamNixieCounter.h/.cpp`. The current production weapon defaults are not replaced by this art package. Drive `SP_DigitIndex` from `FJamWeapon::Ammo` when it changes; capacity is currently 6, without an added chamber count. Empty = index 0, enabled 1. OFF/invalid = index 10, enabled 0. No global MPC, random intermediate digits, or per-frame number updates.

Still requires engine validation: direct UV/readback, 0 versus OFF, 24px minimum digit height and 2px minimum stroke in first person, temporal AA, mip behavior, fixed exposure, corridor bloom off/on, two independent weapons, saved asset reopen, and cooked Windows behavior.

All ten glyphs share centerline Y=45..211 in each 256px cell. Uniform XY scaling preserves each original aspect ratio; directional stroke width remains unchanged.


## Shared UE instrument feature

The RGB atlas above remains the Blender/WebGL and interchange contract. The reusable UE material is owned by `ArtSource/Instruments/Nixie/` and saved at `/Game/JamArt/Materials/Instruments/Nixie/M_Jam_Nixie`, with `MI_Jam_Nixie_RMB93` as the first application. It uses an additional shared 2048² distance/illumination atlas to keep the wire sharp across viewing scales, plus virtual cathode depth, glass and chamber lighting. This shared feature texture does not replace the nine weapon PBR maps. Runtime parameters `SP_DigitIndex` and `SP_CounterEnabled` are unchanged. See `Docs/Nixie.md` for the feature API, setup and current acceptance evidence.
