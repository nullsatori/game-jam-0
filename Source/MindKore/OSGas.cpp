#include "OSGas.h"
#include <atomic>

#include "Async/ParallelFor.h"
#include "Camera/PlayerCameraManager.h"
#include "Components/DirectionalLightComponent.h"
#include "Engine/PostProcessVolume.h"
#include "Engine/GameViewportClient.h"
#include "Engine/Engine.h"
#include "Components/PointLightComponent.h"
#include "Components/RectLightComponent.h"
#include "Components/SpotLightComponent.h"
#include "Algo/AnyOf.h"
#include "Algo/Find.h"
#include "Components/LocalLightComponent.h"
#include "Engine/OverlapResult.h"
#include "DrawDebugHelpers.h"
#include "Misc/ScopeExit.h"
#include "OSGasRender.h"
#include "RendererInterface.h"
#include "RenderingThread.h"
#include "PostProcess/PostProcessMaterialInputs.h"
#include "SceneViewExtension.h"
#include "ScreenPass.h"
#include "Engine/Texture2D.h"
#include "Engine/World.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "HAL/IConsoleManager.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"
#include "MindKore.h"
#include "OSGravity.h"
#include "OSShip.h"
#include "OSLaserSight.h"
#include "EngineUtils.h"
#include "Engine/SpotLight.h"
#include "UObject/UObjectIterator.h"

namespace
{
	/** The step's parallel loops at background priority (they took the render's workers); the shot's stamp on the game
	 * thread at normal priority (the frame waits for it: os.Gas.StampPriority 0 - background as the steps). */
	TAutoConsoleVariable<int32> CVarGasStampPriority(
		TEXT("os.Gas.StampPriority"), 1,
		TEXT("1: a shot's first splat on the game thread runs its parallel loops at normal priority (the frame waits for it); 0: at background priority, as the steps."));
	template <typename FBody>
	void GasFor(int32 Num, const FBody& Body)
	{
		ParallelFor(Num, Body, IsInGameThread() && CVarGasStampPriority.GetValueOnAnyThread() > 0 ? EParallelForFlags::None : EParallelForFlags::BackgroundPriority);
	}

	/** The grids step at fixed rates (the same gas at any frame rate): 60 Hz to 0.5 s, 30 Hz after. */
	constexpr float FastStep = 1.0f / 60.0f;
	constexpr float SlowStep = 1.0f / 30.0f;
	constexpr float FastSeconds = 0.5f;
	/** A source joins a cloud of its look whose gas centre is this close, cm. */
	constexpr float JoinCm = 60.0f;
	/** Vacuum: a parcel's radius grows by this share of the way it flies (300 cm/s doubles a 4 cm parcel in 2 s). */
	constexpr float VacuumGrowPerCm = 0.0067f;
	/** Air: a parcel spreads by diffusion, cm/s. */
	constexpr float AirGrowCmPerSecond = 3.0f;
	/** FLIP share of the grid-to-parcel transfer (the rest PIC). */
	constexpr float Flip = 0.9f;
	/** A parcel's extra drag toward the flow, 1/s per cmВІ of radius, at most MaxDrag (the ring lives >= 0.5 s). */
	constexpr float DragPerCm2 = 0.02f;
	constexpr float MaxDrag = 1.8f;
	constexpr float HeatSeconds = 0.5f;
	/** The splat: the noise's scale, cm; the threshold that tears it; the density gain. */
	constexpr float NoiseCm = 8.0f;
	constexpr float NoiseCut = 0.2f;
	constexpr float NoiseGain = 1.8f;
	constexpr float DensityGain = 2.5f;
	/** A parcel kernel's share of its cell (1: opaque at its core), air and vacuum: tuned to T 0.5 / 0.75 / 0.95 at 0.2 / 1 / 4 s. */
	constexpr float AirMassScale = 1.2f;
	constexpr float VacuumMassScale = 0.9f;
	constexpr float KeyShadow = 0.1f;
	/** The key light's shadow through the render grid (relief), per unit of drawn density along the way. */
	constexpr float GridKeyShadow = 0.6f;
	constexpr float AmbientOcclusion = 0.225f;
	constexpr float KeyStrength = 0.014f;
	constexpr float KeyMax = 3.0f;
	constexpr float EyeClearCm = 30.0f;
	constexpr float EvictSeconds = 1.0f;
	/** In air: the gun's inherited velocity's time constant; old gas's relaxing to the still room; a body's push share. */
	constexpr float CarrierSeconds = 0.08f;
	constexpr float AirRestSeconds = 0.7f;
	constexpr float BodyShare = 0.5f;
	/** The share of a moving body's speed its wake gives the gas it parts. */
	constexpr float WakeShare = 0.2f;
	/** A body's push on the gas in it: by velocity, at most this, cm/s. */
	constexpr float BodyPushCap = 300.0f;

	TAutoConsoleVariable<int32> CVarGasCards(
		TEXT("os.Gas.Cards"), 0,
		TEXT("1: the old card wisps (shroud, muzzle stream) are drawn too; 0 (v11): the gas is parcels only."));

	TAutoConsoleVariable<int32> CVarGasLowRes(
		TEXT("os.Gas.LowRes"), 1,
		TEXT("1: the gas drawn once per screen block by its own pass after the tonemapper (v11 stage 5); 0: by M_OS_GasPost in every pixel (the old way, kept until the merge)."));

	TAutoConsoleVariable<int32> CVarGasDebugAlpha(
		TEXT("os.Gas.DebugAlpha"), 0,
		TEXT("1: the gas post-process draws only its opacity, grey (os.Gas.WalkProbe measures its jumps a frame); 3: the block less the pixel's own march; 4: the room's light the gas takes (os.Gas.Light 1)."));

	TAutoConsoleVariable<int32> CVarGasLight(
		TEXT("os.Gas.Light"), 1,
		TEXT("1: the gas lit by the room (the frame before it, 1/32 and blurred) and the key lamp's full colour, a darker albedo (v11 stage 6); 0: the light before stage 6 (A/B)."));
	TAutoConsoleVariable<float> CVarGasAmbientScale(
		TEXT("os.Gas.AmbientScale"), 0.75f,
		TEXT("os.Gas.Light 1: the room's light on the gas, x (unlit gas about 0.5-0.6 of the walls round it)."));
	TAutoConsoleVariable<float> CVarGasLightScale(
		TEXT("os.Gas.LightScale"), 2.0f,
		TEXT("os.Gas.Light 1: the lamps' light on the gas, x."));
	TAutoConsoleVariable<float> CVarGasForward(
		TEXT("os.Gas.Forward"), 0.7f,
		TEXT("os.Gas.Light 1: the forward-scattering share of the lamps' light (Henyey-Greenstein g 0.55; the rest isotropic) - smoke against a lamp glows at its rim."));
	TAutoConsoleVariable<float> CVarGasAlbedo(
		TEXT("os.Gas.Albedo"), 1.0f,
		TEXT("os.Gas.Light 1: the gas's albedo, x its look's colour."));

	TAutoConsoleVariable<float> CVarGasDriftCm(
		TEXT("os.Gas.DriftCm"), 3.0f,
		TEXT("The zone's air drift (v12 7a.3): a slow divergence-free field, 1.5 m across, changing over ~20 s - its top speed, cm/s."));
	TAutoConsoleVariable<float> CVarGasZoneAmbient(
		TEXT("os.Gas.ZoneAmbient"), 1.0f,
		TEXT("The zone's haze: the room's light on it, x (it is seen by the lamps, not by the room)."));
	TAutoConsoleVariable<float> CVarGasZoneDensity(
		TEXT("os.Gas.ZoneDensity"), 0.6f,
		TEXT("The zone's haze: the gas handed to it, x its mass."));
	TAutoConsoleVariable<float> CVarGasShimmer(
		TEXT("os.Gas.Shimmer"), 2.0f,
		TEXT("The air's shimmer behind heat (v12 7b.3), x; 0 off."));
	TAutoConsoleVariable<float> CVarGasBlockDiv(
		TEXT("os.Gas.BlockDiv"), 360.0f,
		TEXT("The gas's screen block: round(height / BlockDiv) px (360: 4 px at 1440p, 3 at 1080p; 270 before v12 7d)."));
	TAutoConsoleVariable<float> CVarGasDitherPx(
		TEXT("os.Gas.DitherPx"), 0.0f,
		TEXT("The Bayer threshold's cell, px (0: half a block, 1 px at least) - a finer lattice inside a block; the opacity is still the block's."));
	TAutoConsoleVariable<int32> CVarGasSmoothAlpha(
		TEXT("os.Gas.SmoothAlpha"), 0,
		TEXT("1: the block's result (light and opacity) read bilinearly between the blocks' centres (edge blocks as before) - finer still."));
	TAutoConsoleVariable<float> CVarGasZoneAlbedo(
		TEXT("os.Gas.ZoneAlbedo"), 0.8f,
		TEXT("The haze's albedo it ages to over 20 s (old smoke coagulates and whitens: a light veil in the light, dark in the shade)."));
	TAutoConsoleVariable<int32> CVarGasTorchShadow(
		TEXT("os.Gas.TorchShadow"), 1,
		TEXT("1: the torch's cone cuts the smoke with the shadows of what stands in it (a map of traces, v13 13a); 0: off (80e4fec)."));
	TAutoConsoleVariable<int32> CVarGasTorchShadowRes(
		TEXT("os.Gas.TorchShadowRes"), 16,
		TEXT("The torch's shadow map for the gas, texels a side (16, up to 32)."));
	TAutoConsoleVariable<int32> CVarGasTorchTracesPerFrame(
		TEXT("os.Gas.TorchTracesPerFrame"), 64,
		TEXT("The torch's shadow map: traces a frame (texels in turn; 64: a 16x16 map in 4 frames)."));
	TAutoConsoleVariable<float> CVarGasTorchShadowEase(
		TEXT("os.Gas.TorchShadowEase"), 0.08f,
		TEXT("The torch's shadow map: each texel's distance eases to its trace over this, s."));
	TAutoConsoleVariable<float> CVarGasFlashReach(
		TEXT("os.Gas.FlashReach"), 2.5f,
		TEXT("The muzzle's flash for the gas reaches this x its radius (v13 13b: the whole room's haze shows on the shot's frames); 1: as the lamp (80e4fec)."));
	TAutoConsoleVariable<float> CVarGasNixieLamp(
		TEXT("os.Gas.NixieLamp"), 1.0f,
		TEXT("The Nixie counter's orange glow on the smoke round the gun (a lamp for the gas only), x; 0 off."));
	TAutoConsoleVariable<int32> CVarGasTileLamps(
		TEXT("os.Gas.TileLamps"), 1,
		TEXT("1: a haze takes up to os.Gas.ZoneLamps of the zone's lamps and each screen block its own strongest at its gas (v13 13d), the player's lamps apart; 0: 4 lamps for the whole haze (80e4fec)."));
	TAutoConsoleVariable<int32> CVarGasZoneLamps(
		TEXT("os.Gas.ZoneLamps"), 16,
		TEXT("The haze's lamps a block chooses from (4-16)."));
	TAutoConsoleVariable<int32> CVarGasTileSlots(
		TEXT("os.Gas.TileSlots"), 4,
		TEXT("The zone's lamps a screen block takes (4-6; the last one soft - no seam where the 4th and 5th swap)."));
	TAutoConsoleVariable<int32> CVarGasShafts(
		TEXT("os.Gas.Shafts"), 1,
		TEXT("1: a sun (directional) or a spot/rect lamp tagged GasShaft lights the haze in layers with the shadows of frames and jambs - its light traced to the haze's parcels, an eighth a step (v13 13c); 0: off (80e4fec)."));
	TAutoConsoleVariable<float> CVarGasWarmLift(
		TEXT("os.Gas.WarmLift"), 4.0f,
		TEXT("Residual warmth's lift at full gravity, cm/s (4; try 10-15: the haze gathers under the ceiling sooner)."));
	TAutoConsoleVariable<float> CVarGasHazeDiffuse(
		TEXT("os.Gas.HazeDiffuse"), 500.0f,
		TEXT("The haze spreads through the room by a slow turbulent wander (v14), cm^2/s: 200 denser and slower, 400 faster and paler; 0: stays where it was (cb97b4e)."));
	TAutoConsoleVariable<float> CVarGasHazeRadiusMax(
		TEXT("os.Gas.HazeRadiusMax"), 250.0f,
		TEXT("A spreading haze parcel's radius grows with the haze's spread up to this, cm (overlapping into a veil, not round puffs); its matter kept."));
	TAutoConsoleVariable<float> CVarGasHazeEddyCm(
		TEXT("os.Gas.HazeEddyCm"), 80.0f,
		TEXT("The haze's wander comes mostly from a world-anchored eddy field this size, cm (neighbours move together: strands, not puffs)."));
	TAutoConsoleVariable<float> CVarGasHazeWanderOwn(
		TEXT("os.Gas.HazeWanderOwn"), 0.25f,
		TEXT("The share of the haze's wander that is each parcel's own (1: all its own, the round puffs of v14)."));
	TAutoConsoleVariable<float> CVarGasHazeDriftCm(
		TEXT("os.Gas.HazeDriftCm"), 6.0f,
		TEXT("The haze rides the room's slow currents: their top speed, cm/s (shots' gas keeps os.Gas.DriftCm)."));
	TAutoConsoleVariable<float> CVarGasHazeDriftScaleCm(
		TEXT("os.Gas.HazeDriftScaleCm"), 300.0f,
		TEXT("The room's currents for the haze: their size, cm."));
	TAutoConsoleVariable<float> CVarGasHazeNoiseCut(
		TEXT("os.Gas.HazeNoiseCut"), 0.15f,
		TEXT("The haze's noise threshold (0.3: as a shot's gas, holes at the kernels' rims)."));
	TAutoConsoleVariable<float> CVarGasHazeDiffuseZ(
		TEXT("os.Gas.HazeDiffuseZ"), 0.2f,
		TEXT("The haze's wander: its vertical share (warm air over cool is stable)."));
	TAutoConsoleVariable<float> CVarGasHazeEddySeconds(
		TEXT("os.Gas.HazeEddySeconds"), 10.0f,
		TEXT("The haze's wander: how long a parcel keeps its eddy's velocity, s."));
	TAutoConsoleVariable<float> CVarGasHazeDiffuseZeroG(
		TEXT("os.Gas.HazeDiffuseZeroG"), 0.5f,
		TEXT("The haze's wander in zero-G, x (isotropic: no convection mixes it)."));
	TAutoConsoleVariable<int32> CVarGasSpreadLog(
		TEXT("os.Gas.SpreadLog"), 0,
		TEXT("1: every 5 s each haze logs its parcels, mass, spread across and up (mass-weighted sigma, cm), CentreT - the spreading's probe (v14)."));
	TAutoConsoleVariable<int32> CVarGasHazeRoom(
		TEXT("os.Gas.HazeRoom"), 1,
		TEXT("1: the haze knows its room - a 25 cm grid flooded from the first shot by overlap tests: its walls hold it, open doors let it through, vacuum stops it (v14); 0: only the zone box's faces and the shots' planes."));
	TAutoConsoleVariable<float> CVarGasHazeFillMs(
		TEXT("os.Gas.HazeFillMs"), 0.15f,
		TEXT("The haze rooms' flood: game thread time a frame, ms."));
	TAutoConsoleVariable<float> CVarGasHazeWake(
		TEXT("os.Gas.HazeWake"), 0.15f,
		TEXT("A body moving through the haze leaves a turbulent wake: its wander kicked by this x the body's speed (30 cm/s at most); 0 off (v14)."));
	TAutoConsoleVariable<int32> CVarGasHazeVents(
		TEXT("os.Gas.HazeVents"), 1,
		TEXT("1: actors tagged GasVent draw the haze in (15 cm/s within 1 m) and take it 4x faster within 50 cm - its last gas goes there (v14); 0 off."));
	TAutoConsoleVariable<float> CVarGasHazeVentScale(
		TEXT("os.Gas.HazeVentScale"), 1.0f,
		TEXT("The zones' haze half-life x (1: as set on the zone, 45 s; 2: 90 s)."));
	TAutoConsoleVariable<int32> CVarGasFogSource(
		TEXT("os.Gas.FogSource"), 1,
		TEXT("1: a source with bIntoHaze (the fog and steam presets) lets its gas straight into the zone's haze (v15); 0: through a cloud slot of its own, as a weld."));
	TAutoConsoleVariable<int32> CVarGasFog(
		TEXT("os.Gas.Fog"), 1,
		TEXT("1: zones with a fog keep it in their air (v15: seeded over the room's grid, held against ventilation, asleep out of reach); 0: as without one."));
	TAutoConsoleVariable<float> CVarGasFogMass(
		TEXT("os.Gas.FogMass"), 9.0f,
		TEXT("A fog's background parcel mass at density 1 (calibrated: TestRoom, a floor fog 0.5 - CentreT ~0.35)."));
	TAutoConsoleVariable<float> CVarGasFogRefill(
		TEXT("os.Gas.FogRefill"), 4.0f,
		TEXT("A fog refills a furrow cut in it over this, s."));
	TAutoConsoleVariable<float> CVarGasFogWakeCm(
		TEXT("os.Gas.FogWakeCm"), 1200.0f,
		TEXT("A zone's fog wakes within this of the camera, cm (sleeps past 1.25 x)."));
	TAutoConsoleVariable<float> CVarGasFogStir(
		TEXT("os.Gas.FogStir"), 0.25f,
		TEXT("A floor fog's vertical drift and wander x (its layer stays down)."));
	TAutoConsoleVariable<float> CVarGasFogFallScale(
		TEXT("os.Gas.FogFallScale"), 1.0f,
		TEXT("A floor fog's settling x (its layer's height)."));
	TAutoConsoleVariable<int32> CVarGasFogLook(
		TEXT("os.Gas.FogLook"), 1,
		TEXT("1: the haze's look mixes toward its fog's by the fog's share of its mass (v15: its colour, a dimmer room light, a forward scatter); 0: as in v14."));
	TAutoConsoleVariable<float> CVarGasFogAmbient(
		TEXT("os.Gas.FogAmbient"), 0.35f,
		TEXT("A fog's share of the room's light (the haze's: os.Gas.ZoneAmbient)."));
	TAutoConsoleVariable<float> CVarGasFogG(
		TEXT("os.Gas.FogG"), 0.65f,
		TEXT("A fog's Henyey-Greenstein g toward the lamps (a glow round them)."));
	TAutoConsoleVariable<int32> CVarGasHazeSamples(
		TEXT("os.Gas.HazeSamples"), 32,
		TEXT("The haze's march samples at most."));
	TAutoConsoleVariable<int32> CVarGasFogSplit(
		TEXT("os.Gas.FogSplit"), 1,
		TEXT("1: a zone's haze with a fog in it is marched in two goes round the other clouds - in front of a puff over it, behind it under it (v15d; smoke alone as before); 0: in one go."));
	TAutoConsoleVariable<float> CVarGasHazeSlump(
		TEXT("os.Gas.HazeSlump"), 10.0f,
		TEXT("The haze (and a fog) spreads as a layer in gravity (v14c): the thick flows to the thin at up to this, cm/s, and stops as it thins; 0 off."));
	TAutoConsoleVariable<float> CVarGasHazeFlat(
		TEXT("os.Gas.HazeFlat"), 0.6f,
		TEXT("The haze's and a fog's kernels in gravity are squashed along it (v14c): vertical x this, horizontal / its root, volume kept; 1 balls."));
	TAutoConsoleVariable<float> CVarGasHazeDriftZ(
		TEXT("os.Gas.HazeDriftZ"), 0.4f,
		TEXT("The vertical of the room's currents for the haze in gravity x this (v14c; a floor fog: the least of this and os.Gas.FogStir); 1 off."));
	TAutoConsoleVariable<int32> CVarGasStampAsync(
		TEXT("os.Gas.StampAsync"), 1,
		TEXT("1: a shot's stamp (its first parcels into the atlas) starts on a worker the moment the gun has let its gas out; the gas tick only joins it - the same atlas on the same frame; 0: splatted in the gas tick."));
	TAutoConsoleVariable<float> CVarGasRise(
		TEXT("os.Gas.Rise"), 25.0f,
		TEXT("A shot's warm gas rises as a thermal: this x sqrt(1 s / its age), cm/s (keeps rising till the ceiling, slowing as it mixes; at least os.Gas.WarmLift); 0: os.Gas.WarmLift alone, from 1 s."));
	TAutoConsoleVariable<float> CVarGasZoneAlbedoSeconds(
		TEXT("os.Gas.ZoneAlbedoSeconds"), 8.0f,
		TEXT("The haze whitens to os.Gas.ZoneAlbedo over its smoke's this, s (a thin haze in a lit room is light grey, not a black veil; 20 as before)."));
	TAutoConsoleVariable<int32> CVarGasAmbientClamp(
		TEXT("os.Gas.AmbientClamp"), 1,
		TEXT("1: the room's light history the haze takes is clamped into the current neighbours' range - no trail of a torch's spot when turning (v16d); 0 as before."));
	TAutoConsoleVariable<int32> CVarGasAmbientHistory(
		TEXT("os.Gas.AmbientHistory"), 1,
		TEXT("0: the room's light the haze takes has no history (a debug switch)."));
	TAutoConsoleVariable<float> CVarGasAmbientTurn(
		TEXT("os.Gas.AmbientTurn"), 20.0f,
		TEXT("The room's light the haze takes follows the frame over 0.1 s / (1 + the view's turn, deg/s / this): turning, the last frames' light (a torch's spot) leaves no trail in the haze; 0: 0.1 s always."));
	TAutoConsoleVariable<int32> CVarGasDither(
		TEXT("os.Gas.Dither"), 2,
		TEXT("2 (v13): the Bayer dither on the view sphere (a turn leaves the dots on the smoke), the block's opacity held against its last frame's, a world grain; 1: an ordered 4x4 Bayer on the screen (v12); 0: a hash per block, the tail from 0.12 (before v12)."));
	TAutoConsoleVariable<float> CVarGasDitherHold(
		TEXT("os.Gas.DitherHold"), 0.15f,
		TEXT("os.Gas.Dither 2: a pixel keeps its last frame's level while the new opacity has crossed into the next by less than this share of a step (the opacity itself is always this frame's)."));
	TAutoConsoleVariable<float> CVarGasHazeRadius(
		TEXT("os.Gas.HazeRadius"), 60.0f,
		TEXT("The haze's parcels diffuse up to this radius, cm (60: a veil; 20: as before 24.10)."));
	TAutoConsoleVariable<float> CVarGasDitherGrain(
		TEXT("os.Gas.DitherGrain"), 0.5f,
		TEXT("os.Gas.Dither 2: the world grain's nudge of the threshold, x half a level."));
	TAutoConsoleVariable<int32> CVarGasBands(
		TEXT("os.Gas.Bands"), 4,
		TEXT("The gas's opacity levels (4; 8 a finer variant)."));
	TAutoConsoleVariable<float> CVarGasTickHz(
		TEXT("os.Gas.TickHz"), 0.0f,
		TEXT("> 0: the gas's own evolution shown in steps of 1/TickHz s (stop motion); its place and the camera stay smooth. 0: off."));

	TAutoConsoleVariable<int32> CVarGasDebug(
		TEXT("os.Gas.Debug"), 0,
		TEXT("1: draw every cloud's box (white), parcels (points, red heavy to blue light), bodies (yellow, velocity arrow), tunnels (cyan), wall/floor planes (green) and sources (magenta); 2: also each cloud's key lamp in the log once a second."));

	/** os.Fx.SmokeCost: the splat's share of the steps. */
	std::atomic<int64> GSplatUs{0}, GSplats{0};
	/** The splat's parts: light, kernels, noise, sharpen, atlas, the rest. */
	std::atomic<int64> GSplatPartUs[6] = {};

	uint32 Hash3(int32 X, int32 Y, int32 Z, uint32 Seed)
	{
		uint32 H = uint32(X) * 0x8DA6B343u ^ uint32(Y) * 0xD8163841u ^ uint32(Z) * 0xCB1AB31Fu ^ Seed * 0x9E3779B9u;
		H ^= H >> 13;
		H *= 0x5BD1E995u;
		H ^= H >> 15;
		return H;
	}

	/** Value noise in [0, 1], trilinear on an integer lattice. */
	float Noise3(const FVector3f& P, uint32 Seed)
	{
		const int32 X = FMath::FloorToInt(P.X), Y = FMath::FloorToInt(P.Y), Z = FMath::FloorToInt(P.Z);
		const float FX = P.X - X, FY = P.Y - Y, FZ = P.Z - Z;
		const float SX = FX * FX * (3 - 2 * FX), SY = FY * FY * (3 - 2 * FY), SZ = FZ * FZ * (3 - 2 * FZ);
		const auto V = [Seed](int32 A, int32 B, int32 C) { return float(Hash3(A, B, C, Seed) & 0xFFFF) / 65535.0f; };
		const float X00 = FMath::Lerp(V(X, Y, Z), V(X + 1, Y, Z), SX), X10 = FMath::Lerp(V(X, Y + 1, Z), V(X + 1, Y + 1, Z), SX);
		const float X01 = FMath::Lerp(V(X, Y, Z + 1), V(X + 1, Y, Z + 1), SX), X11 = FMath::Lerp(V(X, Y + 1, Z + 1), V(X + 1, Y + 1, Z + 1), SX);
		return FMath::Lerp(FMath::Lerp(X00, X10, SY), FMath::Lerp(X01, X11, SY), SZ);
	}

	FVector3f SnapTo(const FVector3f& V, float Cell)
	{
		return FVector3f(FMath::RoundToFloat(V.X / Cell) * Cell, FMath::RoundToFloat(V.Y / Cell) * Cell, FMath::RoundToFloat(V.Z / Cell) * Cell);
	}

	FOSSmokeParams GridParams()
	{
		FOSSmokeParams K;
		K.Buoyancy = 160.0f;
		K.DragSeconds = 0.6f;
		K.Vorticity = 0.25f;
		K.HeatSeconds = HeatSeconds;
		K.Turbulence = 900.0f;
		K.AmbientChurn = 30.0f;
		K.ResidualLift = 0.15f;
		K.Jacobi = 6;
		return K;
	}
}

/** The gas's screen-block pass hooked after the tonemapper (where M_OS_GasPost draws): the subsystem hands it a frame. */
class FOSGasViewExtension : public FWorldSceneViewExtension
{
public:
	FOSGasViewExtension(const FAutoRegister& AutoRegister, UWorld* InWorld) : FWorldSceneViewExtension(AutoRegister, InWorld) {}

	void SetData(const FOSGasRenderData& InData)
	{
		FScopeLock Lock(&DataLock);
		Data = InData;
	}

	virtual void SubscribeToPostProcessingPass(EPostProcessingPass Pass, const FSceneView& InView, FPostProcessingPassDelegateArray& InOutPassCallbacks, bool bIsPassEnabled) override
	{
		if (Pass == EPostProcessingPass::Tonemap && CVarGasLowRes.GetValueOnAnyThread() > 0)
		{
			InOutPassCallbacks.Add(FAfterPassCallbackDelegate::CreateRaw(this, &FOSGasViewExtension::Draw));
		}
	}

private:
	FScreenPassTexture Draw(FRDGBuilder& GraphBuilder, const FSceneView& View, const FPostProcessMaterialInputs& Inputs)
	{
		FOSGasRenderData Now;
		{
			FScopeLock Lock(&DataLock);
			Now = Data;
		}
		return OSAddGasPasses(GraphBuilder, View, Inputs, Now, AmbientHistory, HoldHistory);
	}

	/** The room's light last frame (render thread only). */
	TRefCountPtr<IPooledRenderTarget> AmbientHistory;
	/** The blocks' held opacity last frame (v13 13e). */
	TRefCountPtr<IPooledRenderTarget> HoldHistory;
	FCriticalSection DataLock;
	FOSGasRenderData Data;
};

namespace
{
	/**
	 * The zone's drift (v12 7a.3): the curl of a few travelling sine waves' potential - divergence-free by construction,
	 * 1.5 m across, each wave's phase turning over ~20 s (no jerks), world-anchored; the velocity and its gradient
	 * (dV_i/dx_j in OutGrad[i]) at a world point. Speed at most SpeedCm.
	 */
	void DriftAt(const FVector3f& World, float Time, uint32 Seed, float SpeedCm, FVector3f& OutV, FVector3f OutGrad[3], float ScaleCm = 150.0f, float PeriodS = 20.0f)
	{
		constexpr int32 Waves = 5;
		OutV = FVector3f::ZeroVector;
		OutGrad[0] = OutGrad[1] = OutGrad[2] = FVector3f::ZeroVector;
		if (SpeedCm <= 0.0f)
		{
			return;
		}
		const float Amp = SpeedCm / 3.0f;
		for (int32 W = 0; W < Waves; ++W)
		{
			const auto R = [Seed, W](int32 K) { return float(Hash3(W, K, 7, Seed) & 0xFFFF) / 65535.0f; };
			// A direction on the sphere, a wavelength round 1.5 m, a rate round 20 s, a phase; the potential across it.
			const float Z = R(0) * 2.0f - 1.0f, A = R(1) * UE_TWO_PI;
			const FVector3f Dir(FMath::Sqrt(1.0f - Z * Z) * FMath::Cos(A), FMath::Sqrt(1.0f - Z * Z) * FMath::Sin(A), Z);
			const float K = UE_TWO_PI / (ScaleCm * (0.75f + 0.5f * R(2)));
			const float Omega = UE_TWO_PI / (PeriodS * (0.7f + 0.6f * R(3)));
			FVector3f U, V;
			Dir.FindBestAxisVectors(U, V);
			const float B = R(4) * UE_TWO_PI;
			const FVector3f Pot = U * FMath::Cos(B) + V * FMath::Sin(B);
			const FVector3f Curl = Dir.Cross(Pot) * Amp;
			const float Theta = K * (Dir | World) + Omega * Time + R(5) * UE_TWO_PI;
			OutV += Curl * FMath::Cos(Theta);
			const float S = -FMath::Sin(Theta) * K;
			for (int32 I = 0; I < 3; ++I)
			{
				OutGrad[I] += Dir * (Curl[I] * S);
			}
		}
		const float Speed = OutV.Size();
		if (Speed > SpeedCm)
		{
			OutV *= SpeedCm / Speed;
		}
	}

	/** A haze parcel's spreading radius grows past this share of the diffusivity (r^2 = r0^2 + 2 x share x D x t). */
	constexpr float HazeSpreadShare = 0.5f;
	/** A fog refills a furrow over this, s (os.Gas.FogRefill, read each frame on the game thread). */
	std::atomic<float> GFogRefill{ 4.0f };
	/** How long residual warmth lasts, s (its lift: os.Gas.WarmLift). */
	constexpr float WarmSeconds = 20.0f;
	/** The haze's strands: the longest stretch, its relaxing, s. */
	constexpr float MaxStretch = 5.0f;
	constexpr float StretchSeconds = 15.0f;
	/** A shot's cloud starts its hand-over at this fade age, spreads it over HandSpread s, each parcel over HandRamp. */
	constexpr float HandFrom = 3.0f;
	constexpr float HandSpread = 1.0f;
	constexpr float HandRamp = 0.5f;
}

/**
 * The same matter in more room: a parcel's kernel's density by its radius. Air (3 cm / r)^2, vacuum 5 cm / r; a haze
 * past its HazeRadius (it spreads to metres, v14) by the cube - its matter kept as it grows (the square grew it).
 */
float UOSGasSubsystem::ParcelThin(const FCloud& Cloud, float RadiusCm)
{
	if (!Cloud.bAir)
	{
		return FMath::Clamp(5.0f / RadiusCm, 0.05f, 1.0f);
	}
	if (!Cloud.bZone)
	{
		return FMath::Clamp(FMath::Square(3.0f / RadiusCm), 0.02f, 1.0f);
	}
	const float Reach = FMath::Max(Cloud.HazeRadius, 1.0f);
	const float Near = FMath::Min(RadiusCm, Reach);
	const float Thin = FMath::Clamp(FMath::Square(3.0f / Near), 0.005f, 1.0f);
	return RadiusCm > Reach ? Thin * FMath::Cube(Reach / RadiusCm) : Thin;
}

UOSGasSubsystem* UOSGasSubsystem::Get(const UObject* WorldContext)
{
	const UWorld* World = WorldContext ? WorldContext->GetWorld() : nullptr;
	return World ? World->GetSubsystem<UOSGasSubsystem>() : nullptr;
}

bool UOSGasSubsystem::DoesSupportWorldType(const EWorldType::Type WorldType) const
{
	return WorldType == EWorldType::Game || WorldType == EWorldType::PIE;
}

TStatId UOSGasSubsystem::GetStatId() const
{
	RETURN_QUICK_DECLARE_CYCLE_STAT(UOSGasSubsystem, STATGROUP_Tickables);
}

void UOSGasSubsystem::Deinitialize()
{
	WaitAll();
	// The pass lets go of the textures before they go (an empty frame: it draws nothing).
	if (GasView.IsValid())
	{
		GasView->SetData(FOSGasRenderData());
		FlushRenderingCommands();
		GasView.Reset();
	}
	Super::Deinitialize();
}

void UOSGasSubsystem::OnWorldBeginPlay(UWorld& InWorld)
{
	Super::OnWorldBeginPlay(InWorld);
	if (UMaterialInterface* Material = PostMaterial.LoadSynchronous())
	{
		const auto FloatTexture = [this](int32 W, int32 H)
		{
			UTexture2D* T = UTexture2D::CreateTransient(W, H, PF_A32B32G32R32F);
			T->SRGB = false;
			T->Filter = TF_Nearest;
			T->NeverStream = true;
			T->UpdateResource();
			return T;
		};
		DataTexture = FloatTexture(MaxDrawn, DataRows);
		TunnelTexture = FloatTexture(MaxTunnels, 3);
		HeatTexture = FloatTexture(MaxHeat, 2);
		TorchTexture = FloatTexture(MaxTorchRes, MaxTorchRes);
		Post = UMaterialInstanceDynamic::Create(Material, this);
		Post->SetTextureParameterValue(TEXT("GasData"), DataTexture);
		Post->SetTextureParameterValue(TEXT("Tunnels"), TunnelTexture);
		FActorSpawnParameters Params;
		Params.ObjectFlags |= RF_Transient;
		APostProcessVolume* Volume = InWorld.SpawnActor<APostProcessVolume>(APostProcessVolume::StaticClass(), FTransform::Identity, Params);
		Volume->bUnbound = true;
		Volume->Priority = 1000.0f;
		Volume->Settings.WeightedBlendables.Array.Add(FWeightedBlendable(1.0f, Post));
		PostVolume = Volume;
		GasView = FSceneViewExtensions::NewExtension<FOSGasViewExtension>(&InWorld);
	}
	// The pool: every cloud's volume, material, textures and grid made now, not on the first shot (a 30 ms frame).
	Clouds.SetNum(MaxClouds + MaxZones);
	for (int32 Index = 0; Index < Clouds.Num(); ++Index)
	{
		FCloud& Cloud = Clouds[Index];
		Cloud.bZone = Index >= MaxClouds;
		EnsureRender(Cloud);
		if (!Cloud.Grid)
		{
			Cloud.Grid = MakeUnique<FOSSmokeSim>();
			Cloud.Grid->Reset(VelocityCells, VelocityCellCm, true, FVector3f(0.0f, 0.0f, -1.0f));
		}
	}
}

#if !UE_BUILD_SHIPPING
namespace
{
	/** The gas tick's sections (os.Gas.WalkProbe's cost line: the worst frame's split), us this frame and in the worst one. */
	enum EGasSec { SecSources, SecLights, SecTorch, SecFlood, SecFog, SecHeat, SecMovers, SecStamp, SecUpload, SecRoom, SecHand, SecKey, SecLamps, SecCloudRest, SecPost, SecCount };
	const TCHAR* const GGasSecName[SecCount] = { TEXT("sources"), TEXT("lights"), TEXT("torch"), TEXT("flood"), TEXT("fog"), TEXT("heat"), TEXT("movers"), TEXT("stamp"),
		TEXT("upload"), TEXT("room"), TEXT("handover"), TEXT("key"), TEXT("lamps"), TEXT("cloud rest"), TEXT("post") };
	double GGasSec[SecCount] = {}, GGasPeakSec[SecCount] = {};
	/** How long the gas tick waited for async stamps (os.Gas.StampAsync): joins, of them still running, the total and the longest wait. */
	int32 GStampJoins = 0, GStampWaited = 0;
	double GStampWaitUs = 0.0, GStampMaxWaitUs = 0.0;
	struct FGasSecTimer
	{
		EGasSec Sec;
		double T0 = FPlatformTime::Seconds();
		explicit FGasSecTimer(EGasSec InSec) : Sec(InSec) {}
		~FGasSecTimer() { GGasSec[Sec] += (FPlatformTime::Seconds() - T0) * 1e6; }
	};
}
#define OS_GAS_SEC(Name) FGasSecTimer GasSecTimer##Name(Sec##Name)
#else
#define OS_GAS_SEC(Name)
#endif

void UOSGasSubsystem::WaitAll() const
{
	for (const FCloud& Cloud : Clouds)
	{
		if (Cloud.Pending.IsValid())
		{
#if !UE_BUILD_SHIPPING
			// A stamp still on its worker (the walk probe asks every frame): its wait counted as the tick's would be.
			if (Cloud.bStampRunning)
			{
				const bool bRunning = !Cloud.Pending.IsCompleted();
				const double W0 = FPlatformTime::Seconds();
				Cloud.Pending.Wait();
				const double Us = (FPlatformTime::Seconds() - W0) * 1e6;
				++GStampJoins;
				GStampWaited += bRunning ? 1 : 0;
				GStampWaitUs += Us;
				GStampMaxWaitUs = FMath::Max(GStampMaxWaitUs, Us);
			}
#endif
			Cloud.Pending.Wait();
			Cloud.Pending = UE::Tasks::FTask();
		}
	}
}

int32 UOSGasSubsystem::GetCloudCount() const
{
	int32 Count = 0;
	for (const FCloud& Cloud : Clouds)
	{
		Count += Cloud.IsAlive() ? 1 : 0;
	}
	return Count;
}

int32 UOSGasSubsystem::GetParcelCount() const
{
	int32 Count = 0;
	for (const FCloud& Cloud : Clouds)
	{
		Count += Cloud.IsAlive() ? Cloud.Parcels.Num() + Cloud.Incoming.Num() : 0;
	}
	return Count;
}

void UOSGasSubsystem::ClearClouds()
{
	WaitAll();
	for (FCloud& Cloud : Clouds)
	{
		Cloud.Age = -1.0f;
		Cloud.Parcels.Reset();
		Cloud.Incoming.Reset();
		Cloud.bDrawable = false;
	}
	Sources.Reset();
}

bool UOSGasSubsystem::GetYoungestCloud(float MinAge, FVector& OutCentre, float& OutHalf) const
{
	const FCloud* Best = nullptr;
	for (const FCloud& Cloud : Clouds)
	{
		if (!Cloud.bZone && Cloud.Age >= MinAge && (!Best || Cloud.Age < Best->Age))
		{
			Best = &Cloud;
		}
	}
	if (Best)
	{
		OutCentre = Best->Origin + FVector(Best->DrawnCentre);
		OutHalf = 0.5f * RenderCells * Best->PackedCellCm;
	}
	return Best != nullptr;
}

FString UOSGasSubsystem::DescribeClouds() const
{
	WaitAll();
	FString Line;
	for (const FCloud& Cloud : Clouds)
	{
		if (!Cloud.IsAlive())
		{
			continue;
		}
		float Mass = 0.0f, Speed = 0.0f, Heat = 0.0f;
		for (const FOSGasParcel& P : Cloud.Parcels)
		{
			Mass += P.Mass;
			Speed += P.V.Size() * P.Mass;
			Heat = FMath::Max(Heat, P.Heat);
		}
		Line += FString::Printf(TEXT(" [gas age %.2f %s, %d parcels, mass %.1f, mean |V| %.0f cm/s, heat %.2f, centre %s, cell %.1f cm, T %.2f, %d drawn tunnel(s)/body(s)]"),
			Cloud.Age, Cloud.bAir ? TEXT("air") : TEXT("vacuum"), Cloud.Parcels.Num(), Mass, Speed / FMath::Max(Mass, 0.01f), Heat,
			*(Cloud.Origin + FVector(Cloud.DrawnCentre)).ToCompactString(), Cloud.PackedCellCm, Cloud.CentreT, Cloud.TunnelCount);
	}
	return Line;
}

bool UOSGasSubsystem::ProbeYoungest(FVector& OutCentroid, int32& OutParcels, int32& OutBehind, FVector& OutBox, float& OutSpread, float& OutHalf, float& OutGridCell, float& OutOldSpeed) const
{
	WaitAll();
	const FCloud* Best = nullptr;
	for (const FCloud& Cloud : Clouds)
	{
		if (!Cloud.bZone && Cloud.IsAlive() && (!Best || Cloud.Parcels.Num() > Best->Parcels.Num()))
		{
			Best = &Cloud;
		}
	}
	if (!Best)
	{
		return false;
	}
	FVector Sum = FVector::ZeroVector;
	double Mass = 0.0;
	OutBehind = 0;
	for (const FOSGasParcel& P : Best->Parcels)
	{
		const FVector At = Best->Origin + FVector(P.P);
		Sum += At * P.Mass;
		Mass += P.Mass;
		for (int32 I = 0; I < Best->Planes.Num(); ++I)
		{
			if (Best->PlaneBoxes[I].IsInside(At) && Best->Planes[I].PlaneDot(At) < -1.0)
			{
				++OutBehind;
				break;
			}
		}
	}
	OutCentroid = Mass > 0.0 ? Sum / Mass : Best->Origin;
	double Far = 0.0;
	for (const FOSGasParcel& P : Best->Parcels)
	{
		Far += FVector::Dist(Best->Origin + FVector(P.P), OutCentroid) * P.Mass;
	}
	OutSpread = Mass > 0.0 ? float(Far / Mass) : 0.0f;
	OutHalf = 0.5f * RenderCells * Best->RenderCellCm;
	// Old gas (over 0.5 s) away from bodies (over 40 cm): its mean speed.
	double OldSum = 0.0;
	int32 OldCount = 0;
	for (const FOSGasParcel& P : Best->Parcels)
	{
		bool bNear = false;
		for (const FOSSmokeSim::FMover& M : Best->NextMovers)
		{
			bNear = bNear || FVector3f::Dist(P.P, M.Centre) < M.RadiusCm + 40.0f;
		}
		if (P.Age > 0.5f && !bNear)
		{
			OldSum += P.V.Size();
			++OldCount;
		}
	}
	OutOldSpeed = OldCount ? float(OldSum / OldCount) : 0.0f;
	OutGridCell = Best->Grid ? Best->Grid->CellCm : 0.0f;
	OutParcels = Best->Parcels.Num();
	OutBox = Best->Origin + FVector(Best->DrawnCentre);
	return true;
}

void UOSGasSubsystem::DrawDebug() const
{
	const UWorld* World = GetWorld();
	for (const FCloud& Cloud : Clouds)
	{
		if (!Cloud.IsAlive())
		{
			continue;
		}
		const FVector Centre = Cloud.Origin + FVector(Cloud.DrawnCentre);
		DrawDebugBox(World, Centre, FVector(0.5f * RenderCells * Cloud.RenderCellCm), FColor::White, false, -1.0f, 0, 0.5f);
		DrawDebugPoint(World, Cloud.Origin, 10.0f, FColor::Orange, false, -1.0f);
		for (const FPlane& Plane : Cloud.Planes)
		{
			const FVector N(Plane);
			const FVector OnIt = FVector::PointPlaneProject(Centre, Plane);
			DrawDebugSolidPlane(World, Plane, OnIt, FVector2D(60.0f), FColor(0, 200, 0, 60), false, -1.0f);
			DrawDebugDirectionalArrow(World, OnIt, OnIt + N * 30.0f, 8.0f, FColor::Green, false, -1.0f, 0, 1.0f);
		}
		// The parcels only while no step runs (they are the step's then).
		if (!Cloud.IsBusy())
		{
			for (const FOSGasParcel& P : Cloud.Parcels)
			{
				const float M = FMath::Clamp(P.Mass, 0.0f, 1.0f);
				DrawDebugPoint(World, Cloud.Origin + FVector(P.P), 4.0f, FColor(uint8(255 * M), 40, uint8(255 * (1.0f - M))), false, -1.0f);
			}
		}
		for (const FOSSmokeSim::FMover& M : Cloud.NextMovers)
		{
			const FVector C = Cloud.Origin + FVector(M.Centre);
			if (M.Extent.IsZero())
			{
				DrawDebugSphere(World, C, M.RadiusCm, 12, FColor::Yellow, false, -1.0f);
			}
			else
			{
				DrawDebugBox(World, C, FVector(M.Extent), FQuat(M.Rotation), FColor::Yellow, false, -1.0f);
			}
			DrawDebugDirectionalArrow(World, C, C + FVector(M.Velocity) * 0.2f, 10.0f, FColor::Yellow, false, -1.0f, 0, 1.5f);
		}
	}
	for (const FTunnel& T : Tunnels)
	{
		DrawDebugLine(World, T.Start, T.End, FColor::Cyan, false, -1.0f, 0, 1.0f);
	}
	for (const FSource& S : Sources)
	{
		FVector Where = S.Where;
		if (const USceneComponent* Attach = S.bAttached ? S.Attach.Get() : nullptr)
		{
			Where = Attach->GetComponentTransform().TransformPosition(S.Where);
		}
		DrawDebugPoint(World, Where, 12.0f, FColor::Magenta, false, -1.0f);
	}
}


FString UOSGasSubsystem::TakeCostLine()
{
	FScopeLock Lock(&CostLock);
	const int64 Splats = GSplats.exchange(0), SplatUs = GSplatUs.exchange(0);
	FString Parts;
	for (int32 I = 0; I < 6; ++I)
	{
		Parts += FString::Printf(TEXT(" %.0f"), Splats ? double(GSplatPartUs[I].exchange(0)) / Splats : 0.0);
	}
	FString Peak;
#if !UE_BUILD_SHIPPING
	for (int32 I = 0; I < SecCount; ++I)
	{
		Peak += GGasPeakSec[I] >= 10.0 ? FString::Printf(TEXT(" %s %.0f"), GGasSecName[I], GGasPeakSec[I]) : FString();
		GGasPeakSec[I] = 0.0;
	}
	Peak += FString::Printf(TEXT("; async stamps %d, still running at the tick %d, waited %.0f us in all, %.0f us at most"), GStampJoins, GStampWaited, GStampWaitUs, GStampMaxWaitUs);
	GStampJoins = GStampWaited = 0;
	GStampWaitUs = GStampMaxWaitUs = 0.0;
#endif
	const FString Line = FString::Printf(TEXT("Gas cost: %d step(s) off the game thread, %.0f us mean (the splat %.0f: light, kernels, noise, sharpen, atlas, rest%s), %.0f us max; game thread in %d frame(s), %.0f us mean, %.0f us max; %d cloud(s), %d parcel(s)"),
		CostSteps, CostSteps ? CostStepUs / CostSteps : 0.0, Splats ? double(SplatUs) / Splats : 0.0, *Parts, CostMaxStepUs, CostFrames, CostFrames ? CostFrameUs / CostFrames : 0.0, CostMaxFrameUs,
		GetCloudCount(), GetParcelCount()) + TEXT("; the worst tick (us):") + Peak;
	CostStepUs = CostMaxStepUs = CostFrameUs = CostMaxFrameUs = 0.0;
	CostSteps = CostFrames = 0;
	return Line;
}

// --- sources --------------------------------------------------------------------------

int32 UOSGasSubsystem::CloudFor(const FOSGasLook& Look, const FVector& Where, float JoinAge, bool bFreeSlotOnly)
{
	UWorld* World = GetWorld();
	// The youngest of this look with room, its gas close, young enough (a new shot never joins: JoinAge < 0). The
	// youngest: a shot's hole puffs went into the shot before it (the first match), stretching that cloud's box to
	// the new muzzle - it jumped (owner 23.10: В«СЂС‹РІРєРѕРј СЃРєР»РµРёРІР°СЋС‚СЃСЏВ»).
	int32 Join = INDEX_NONE;
	for (int32 Index = 0; Index < MaxClouds && JoinAge >= 0.0f; ++Index)
	{
		const FCloud& Cloud = Clouds[Index];
		if (Cloud.IsAlive() && Cloud.Age <= JoinAge && Cloud.Evict <= 0.0f && Cloud.Look == Look && Cloud.Parcels.Num() + Cloud.Incoming.Num() < MaxParcels
			&& FVector::Dist(Cloud.Origin + FVector(Cloud.DrawnCentre), Where) < JoinCm && Cloud.TakesGas() && (Join == INDEX_NONE || Cloud.Age < Clouds[Join].Age))
		{
			Join = Index;
		}
	}
	if (Join != INDEX_NONE)
	{
		return Join;
	}
	// A free slot (its last step done: no wait on the game thread) or a new one. None: the gas goes into the youngest
	// of its look within 1.5 m while a fading cloud frees a slot - a living cloud's slot was taken on the spot (it
	// vanished in one frame); none near, it is not let out.
	int32 Slot = Clouds.IndexOfByPredicate([](const FCloud& Each) { return !Each.bZone && !Each.IsAlive() && !Each.IsBusy(); });
	if (Slot == INDEX_NONE && bFreeSlotOnly)
	{
		return INDEX_NONE;
	}
	if (Slot == INDEX_NONE)
	{
		int32 Youngest = INDEX_NONE;
		for (int32 Index = 0; Index < MaxClouds; ++Index)
		{
			const FCloud& Each = Clouds[Index];
			if (Each.IsAlive() && Each.Look == Look && Each.Parcels.Num() + Each.Incoming.Num() < MaxParcels
				&& FVector::Dist(Each.Origin + FVector(Each.DrawnCentre), Where) < 150.0f && (Youngest == INDEX_NONE || Each.Age < Clouds[Youngest].Age))
			{
				Youngest = Index;
			}
		}
		return Youngest;
	}
	FCloud& Cloud = Clouds[Slot];
	Cloud.Pending = UE::Tasks::FTask();
	int32 Free = 0;
	for (const FCloud& Each : Clouds)
	{
		Free += !Each.bZone && !Each.IsAlive() && &Each != &Cloud ? 1 : 0;
	}
	// A level source never starts an eviction (it would take a shot's cloud); a shot does - the oldest unfed first.
	if (Free < 3 && !bFreeSlotOnly)
	{
		FCloud* Oldest = nullptr;
		for (FCloud& Each : Clouds)
		{
			const double WorldNow = World->GetTimeSeconds();
			if (!Each.bZone && &Each != &Cloud && Each.IsAlive() && Each.Evict <= 0.0f
				&& (!Oldest || (Oldest->IsFed(WorldNow) && !Each.IsFed(WorldNow)) || (Oldest->IsFed(WorldNow) == Each.IsFed(WorldNow) && Each.Age > Oldest->Age)))
			{
				Oldest = &Each;
			}
		}
		if (Oldest)
		{
			Oldest->Evict = EvictSeconds;
		}
	}
	InitCloud(Cloud, Look, Where, true);
	return Slot;
}

void UOSGasSubsystem::InitCloud(FCloud& Cloud, const FOSGasLook& Look, const FVector& Where, bool bTrace)
{
	UWorld* World = GetWorld();
	Cloud.Look = Look;
	Cloud.Origin = Where;
	Cloud.bHands = false;
	Cloud.HandStart = -1.0;
	Cloud.FaintFor = 0.0f;
	Cloud.MaxDensity = 1.0f;
	Cloud.FogIn = 1.0f;
	Cloud.bFogFloor = false;
	Cloud.FogShare = 0.0f;
	Cloud.SlumpRef[0] = Cloud.SlumpRef[1] = 0.0f;
	Cloud.HazeAge = 0.0f;
	Cloud.SimToWorld = World->GetTimeSeconds();
	Cloud.DrawnCentre = Cloud.PackedCentre = FVector3f::ZeroVector;
	Cloud.Parcels.Reset();
	Cloud.Incoming.Reset();
	Cloud.Commands.Reset();
	Cloud.Movers.Reset();
	Cloud.NextCommands.Reset();
	Cloud.NextMovers.Reset();
	Cloud.Age = 0.0f;
	Cloud.FadeAge = 0.0f;
	Cloud.FedWindow = 0.5f;
	Cloud.LastFed = World->GetTimeSeconds();
	Cloud.Evict = 0.0f;
	Cloud.StepCarry = 0.0f;
	Cloud.Seed = FMath::Rand();
	Cloud.bAir = OSAirZoneAt(World, Where) != nullptr;
	Cloud.Gravity = OSGravityAt(World, Where).Acceleration;
	Cloud.RenderCellCm = Cloud.PackedCellCm = Cloud.PrevCellCm = 2.0f;
	Cloud.PrevCentre = FVector3f::ZeroVector;
	Cloud.CurTime = Cloud.PrevTime = Cloud.ShowTime = Cloud.PackedTime = Cloud.SimTime = 0.0f;
	Cloud.StepDt = FastStep;
	Cloud.CurHalf = 0;
	Cloud.bStampAtlas = false;
	Cloud.bStampRunning = false;
	Cloud.bDrawable = false;
	Cloud.LooseSince = Cloud.LastRebox = -1.0f;
	Cloud.Serial = NextSerial++;
	Cloud.BirthWorld = float(World->GetTimeSeconds());
	Cloud.bHasGas = false;
	Cloud.bPendingAtlas = false;
	if (!Cloud.Grid)
	{
		Cloud.Grid = MakeUnique<FOSSmokeSim>();
	}
	Cloud.Grid->Reset(VelocityCells, VelocityCellCm, Cloud.bAir, FVector3f(Cloud.Gravity.GetSafeNormal()));
	// The floor and the walls round the birth: planes the gas slides on (Visibility; a few traces, once).
	Cloud.bGridAnchored = false;
	Cloud.Reboxes = Cloud.Carves = Cloud.Blows = 0;
	Cloud.MaxReboxCm = Cloud.MaxCellCm = 0.0f;
	Cloud.MinCellCm = 1e9f;
	Cloud.Planes.Reset();
	Cloud.PlaneBoxes.Reset();
	// 16 ways round, up and down, 4 m; static and dynamic geometry (a window's glass does not block Visibility), no
	// pawns or loose props (they are bodies). Was 6 axis traces of 1.5 m: the gas left a round room through its glass.
	FCollisionQueryParams Query(SCENE_QUERY_STAT(OSGasPlanes), false);
	if (const APlayerController* Controller = World->GetFirstPlayerController(); Controller && Controller->GetPawn())
	{
		Query.AddIgnoredActor(Controller->GetPawn());
		TArray<AActor*> Held;
		Controller->GetPawn()->GetAttachedActors(Held, true, true);
		Query.AddIgnoredActors(Held);
	}
	FCollisionObjectQueryParams Statics;
	Statics.AddObjectTypesToQuery(ECC_WorldStatic);
	Statics.AddObjectTypesToQuery(ECC_WorldDynamic);
	TArray<FVector, TInlineAllocator<18>> Ways = { FVector::UpVector, -FVector::UpVector };
	for (int32 Index = 0; Index < 16; ++Index)
	{
		const float A = Index * UE_TWO_PI / 16.0f;
		Ways.Add(FVector(FMath::Cos(A), FMath::Sin(A), 0.0f));
	}
	for (const FVector& Direction : Ways)
	{
		if (!bTrace)
		{
			break;
		}
		FHitResult Hit;
		if (World->LineTraceSingleByObjectType(Hit, Where, Where + Direction * 400.0f, Statics, Query)
			&& !Hit.bStartPenetrating && (Hit.ImpactNormal | Direction) < -0.2f
			&& Hit.Component.IsValid() && !Hit.Component->IsSimulatingPhysics()
			&& !Cloud.Planes.ContainsByPredicate([&Hit](const FPlane& Each) { return (FVector(Each) | Hit.ImpactNormal) > 0.99f && FMath::Abs(Each.PlaneDot(Hit.ImpactPoint)) < 5.0f; }))
		{
			Cloud.Planes.Emplace(Hit.ImpactPoint, Hit.ImpactNormal);
			Cloud.PlaneBoxes.Add(Hit.Component->Bounds.GetBox().ExpandBy(10.0f));
		}
	}
	EnsureRender(Cloud);
	PickKey(Where, Cloud.KeyDir, Cloud.KeyColour);
	Cloud.LampsSeen.Reset();
	for (FCloud::FLampSlot& Each : Cloud.Lamps)
	{
		Each = FCloud::FLampSlot();
	}
	Cloud.KeyLamp = INDEX_NONE;
	Cloud.bLampsFresh = true;
}

int32 UOSGasSubsystem::ZoneCloudFor(const AOSGravityZone* Zone)
{
	const FFogZone* Fogged = Zone ? FogZones.Find(Zone) : nullptr;
	if (!Zone || (Zone->HazeHalfLifeSeconds <= 0.0f && !(Fogged && Fogged->Fog != EOSZoneFog::None)))
	{
		return INDEX_NONE;
	}
	for (int32 Index = MaxClouds; Index < Clouds.Num(); ++Index)
	{
		if (Clouds[Index].IsAlive() && Clouds[Index].Zone.Get() == Zone)
		{
			return Index;
		}
	}
	const int32 Slot = Clouds.IndexOfByPredicate([](const FCloud& Each) { return Each.bZone && !Each.IsAlive() && !Each.IsBusy(); });
	if (Slot == INDEX_NONE)
	{
		return INDEX_NONE;
	}
	FVector Centre;
	const FCollisionShape Shape = Zone->GetQueryShape(Centre);
	const FVector Extent = Shape.IsSphere() ? FVector(Shape.GetSphereRadius()) : Shape.GetExtent();
	FOSGasLook Look;
	Look.LifeSeconds = 1e9f;
	Look.bEmbers = false;
	FCloud& Cloud = Clouds[Slot];
	Cloud.Pending = UE::Tasks::FTask();
	InitCloud(Cloud, Look, Centre, false);
	Cloud.Zone = Zone;
	Cloud.HazeHalfLife = Zone->HazeHalfLifeSeconds;
	Cloud.DriftSeed = GetTypeHash(Zone->GetFName());
	Cloud.bAir = true;
	Cloud.StepDt = 0.2f;
	// Its walls: the zone's box faces (the shots that hand over bring their own traced walls too).
	const FBox Box(Centre - Extent, Centre + Extent);
	for (int32 Axis = 0; Axis < 3; ++Axis)
	{
		for (const float Side : { -1.0f, 1.0f })
		{
			FVector Normal = FVector::ZeroVector;
			Normal[Axis] = -Side;
			FVector At = Centre;
			At[Axis] += Side * Extent[Axis];
			Cloud.Planes.Emplace(At, Normal);
			Cloud.PlaneBoxes.Add(Box.ExpandBy(10.0f));
		}
	}
	UE_LOG(LogMindKore, Log, TEXT("Gas: the haze of %s begins (half-life %.0f s)"), *Zone->GetName(), Cloud.HazeHalfLife);
	return Slot;
}

void UOSGasSubsystem::HandOver(FCloud& Cloud, const FVector& Centre)
{
	UWorld* World = GetWorld();
	const double Now = World->GetTimeSeconds();
	const AOSGravityZone* Zone = OSAirZoneAt(World, Centre);
	// Only gas that lives long (a thruster's second-long cone fades as it did) and only into a zone with haze.
	Cloud.bHands = Cloud.bAir && Cloud.Look.LifeSeconds > HandFrom + 0.5f && Zone && Zone->HazeHalfLifeSeconds > 0.0f;
	if (!Cloud.bHands)
	{
		return;
	}
	const bool bStart = Cloud.HandStart < 0.0 && Cloud.FadeAge >= HandFrom;
	// A fed cloud's parcels go one by one as they would fade (a weld's steady flow); the rest once it is HandFrom old.
	// One by one only while fed (a weld): an unfed cloud's short-lived wisps fade by their age as before.
	const bool bFed = Cloud.IsFed(Now);
	bool bAny = bStart || Cloud.HandStart >= 0.0;
	for (const FOSGasParcel& P : Cloud.Parcels)
	{
		bAny |= bFed && P.HandOut < 0.0f && P.Age >= 0.6f * P.Life;
	}
	if (!bAny)
	{
		return;
	}
	const int32 ZoneSlot = ZoneCloudFor(Zone);
	if (ZoneSlot == INDEX_NONE)
	{
		// No haze cloud free: it fades as before.
		Cloud.bHands = false;
		return;
	}
	FCloud& Haze = Clouds[ZoneSlot];
	if (bStart)
	{
		// The oldest first, all of them leaving within HandSpread s.
		Cloud.HandStart = Now;
		TArray<int32> Order;
		for (int32 I = 0; I < Cloud.Parcels.Num(); ++I)
		{
			if (Cloud.Parcels[I].HandOut < 0.0f)
			{
				Order.Add(I);
			}
		}
		Order.Sort([&Cloud](int32 A, int32 B) { return Cloud.Parcels[A].Age > Cloud.Parcels[B].Age; });
		for (int32 Rank = 0; Rank < Order.Num(); ++Rank)
		{
			Cloud.Parcels[Order[Rank]].HandOut = float(Now + HandSpread * Rank / FMath::Max(1, Order.Num() - 1));
		}
	}
	const float L = Cloud.Look.LifeSeconds;
	// The cloud's own fade held where it stands at HandFrom (UpdatePost): the haze takes the gas at that weight.
	const float Held = FMath::Cube(1.0f - FMath::SmoothStep(0.3f * L, L, HandFrom));
	const float Gain = Held * Cloud.Look.Extinction / FMath::Max(Haze.Look.Extinction, 1e-4f) * CVarGasZoneDensity.GetValueOnGameThread();
	const FVector3f Shift(Cloud.Origin - Haze.Origin);
	for (FOSGasParcel& P : Cloud.Parcels)
	{
		if (P.HandOut < 0.0f && (Cloud.HandStart >= 0.0 || (bFed && P.Age >= 0.6f * P.Life)))
		{
			// Late gas (a seep after the start), or one fading by its own age: next.
			P.HandOut = float(Now + HandRamp);
		}
		// The copy goes in HandRamp s ahead of its ramp: the haze's atlas (5 Hz, a step behind) has it by then.
		if (P.HandOut >= 0.0f && !P.bHanded && Now >= P.HandOut - HandRamp && Haze.Parcels.Num() + Haze.Incoming.Num() < 4 * MaxZoneParcels)
		{
			P.bHanded = true;
			FOSGasParcel& C = Haze.Incoming.Add_GetRef(P);
			C.P += Shift;
			C.Birth += Shift;
			C.HandIn = P.HandOut;
			C.HandOut = -1.0f;
			C.bHanded = false;
			C.Life = 1e9f;
			C.Cling = 0.0f;
			C.Carrier = FVector3f::ZeroVector;
			C.bInstant = false;
			const float AgeThen = P.Age + float(P.HandOut - Now);
			C.Mass = P.Mass * Gain * (1.0f - FMath::SmoothStep(0.6f * P.Life, P.Life, AgeThen));
			C.MaxRadiusCm = FMath::Max(C.MaxRadiusCm, 20.0f);
		}
	}
	// Its walls join the haze's (the zone's box knows no inner walls).
	for (int32 I = 0; I < Cloud.Planes.Num() && Haze.Planes.Num() < 48 && !Haze.IsBusy(); ++I)
	{
		const FPlane& Plane = Cloud.Planes[I];
		if (!Haze.Planes.ContainsByPredicate([&Plane](const FPlane& Each) { return (FVector(Each) | FVector(Plane)) > 0.99f && FMath::Abs(Each.W - Plane.W) < 5.0f; }))
		{
			Haze.Planes.Add(Plane);
			Haze.PlaneBoxes.Add(Cloud.PlaneBoxes[I]);
		}
	}
	Haze.LastFed = Now;
}

void UOSGasSubsystem::Emit(FCloud& Cloud, const FOSGasRecipe& R, const FVector& Where, const FVector& Direction, const FVector& Carrier, int32 Count, int32 Cap)
{
	Cloud.LastFed = GetWorld()->GetTimeSeconds();
	const FVector3f At(Where - Cloud.Origin);
	const FVector3f Axis = FVector3f(Direction.GetSafeNormal(UE_SMALL_NUMBER, FVector::ForwardVector));
	FVector3f U, W;
	Axis.FindBestAxisVectors(U, W);
	for (int32 Index = 0; Index < Count && Cloud.Parcels.Num() + Cloud.Incoming.Num() < Cap; ++Index)
	{
		FOSGasParcel& P = Cloud.Incoming.AddDefaulted_GetRef();
		const FVector3f Dir = FVector3f(FMath::VRandCone(FVector(Axis), FMath::DegreesToRadians(FMath::Max(R.ConeDeg, 0.5f))));
		const float Speed = R.SpeedCm * FMath::FRandRange(R.SpeedMin, 1.0f);
		P.P = At;
		P.Throw = Dir * Speed;
		P.Carrier = FVector3f(Carrier * R.CarrierShare);
		P.CarrierTau = FMath::Max(R.CarrierSeconds, 0.01f);
		P.V = P.Carrier + P.Throw;
		if (R.RingCm > 0.0f)
		{
			// A vortex ring round the axis: a point on the ring, a little off its core, turning round the core (the
			// side out of the axis rolls back, the inner side forward) and spreading out with it.
			const float Theta = FMath::FRandRange(0.0f, UE_TWO_PI);
			const FVector3f Out = U * FMath::Cos(Theta) + W * FMath::Sin(Theta);
			const float Ring = R.RingCm * FMath::FRandRange(0.6f, 1.0f);
			const float Phi = FMath::FRandRange(0.0f, UE_TWO_PI);
			const float Core = FMath::FRandRange(1.0f, 4.0f);
			const FVector3f Off = Out * FMath::Cos(Phi) + Axis * FMath::Sin(Phi);
			const FVector3f Turn = -Out * FMath::Sin(Phi) + Axis * FMath::Cos(Phi);
			P.P += Out * Ring * 0.5f + Off * Core;
			// The rear half 30% slower than the front: the speed gradient that rolls the ring over into a mushroom.
			if (FMath::Sin(Phi) < 0.0f)
			{
				P.Throw *= 0.7f;
				P.V = P.Carrier + P.Throw;
			}
			P.V += Turn * (UE_TWO_PI * Core * R.RingTurnsPerSecond * 4.0f) + Out * Speed * 0.12f;
		}
		P.Birth = P.P;
		P.RadiusCm = FMath::FRandRange(R.RadiusMinCm, R.RadiusMaxCm);
		P.Mass = R.Mass;
		P.Heat = R.Heat;
		P.Warm = R.Heat;
		P.Air = P.AirTarget = Cloud.bAir ? 1.0f : 0.0f;
		P.Life = R.LifeSeconds * FMath::FRandRange(0.8f, 1.2f);
		P.Cling = R.ClingSeconds;
		P.MaxRadiusCm = R.MaxRadiusCm;
		P.DragSeconds = R.DragSeconds;
		P.bInstant = R.bInstant;
		P.Fall = R.FallCmPerS2;
		P.Seed = FMath::Rand();
	}
}

int32 UOSGasSubsystem::AddSource(const FOSGasRecipe& Recipe, const FOSGasLook& Look, const FVector& Where, const FVector& Direction, const FVector& CarrierVelocity,
	const USceneComponent* Attach)
{
	FSource& Source = Sources.AddDefaulted_GetRef();
	Source.Id = NextSourceId++;
	Source.Recipe = Recipe;
	// In a vacuum the recipe's vacuum release: longer, even, bigger parcels.
	if (Recipe.VacuumBurstCount > 0 && OSAirZoneAt(GetWorld(), Attach ? Attach->GetComponentTransform().TransformPosition(Where) : Where) == nullptr)
	{
		Source.Recipe.BurstCount = Recipe.VacuumBurstCount;
		Source.Recipe.BurstSeconds = Recipe.VacuumBurstSeconds;
		Source.Recipe.SpeedMin = Recipe.VacuumSpeedMin;
		Source.Recipe.RadiusMinCm = Recipe.VacuumRadiusMinCm;
		Source.Recipe.RadiusMaxCm = Recipe.VacuumRadiusMaxCm;
		// A cone with a tail, not a cloud (design v11): no vortex ring and its sideways throw (nothing to roll against in a
		// vacuum); the speeds' spread lays the fast ahead and the slow by the muzzle.
		Source.Recipe.RingCm = 0.0f;
		Source.Recipe.ConeDeg = FMath::Min(Recipe.ConeDeg, 40.0f);
		// Half the speed: a plume of ~1.5 m that stays in view (at full speed the fast gas was 3-4 m off and thinned to
		// nothing against the dark in a second).
		Source.Recipe.SpeedCm *= 0.5f;
	}
	const FOSGasRecipe& Used = Source.Recipe;
	Source.Look = Look;
	Source.Where = Where;
	Source.Direction = Direction;
	Source.Carrier = CarrierVelocity;
	Source.Attach = Attach;
	Source.bAttached = Attach != nullptr;
	Source.BurstOwed = float(Used.BurstCount);
	Source.BurstLeft = Used.BurstCount > 0 ? FMath::Max(Used.BurstSeconds, 1e-3f) : 0.0f;
	// An attached source's Where and Direction are in its parent's space: its first parcels go out in the world.
	FVector WorldWhere = Where, WorldDirection = Direction;
	if (Attach)
	{
		WorldWhere = Attach->GetComponentTransform().TransformPosition(Where);
		WorldDirection = Attach->GetComponentTransform().TransformVectorNoScale(Direction);
	}
	if (Used.BurstCount > 0)
	{
		// The first frame's share now, stamped into the atlas on the game thread this frame (TickCloud).
		const float Frame = FMath::Min(GetWorld()->GetDeltaSeconds() / Source.BurstLeft, 1.0f);
		// At most 20 on the game thread (the rest from the next step on: no hitch).
		const int32 Now = FMath::Clamp(FMath::RoundToInt(Source.BurstOwed * Frame), 1, 20);
		Source.BurstOwed = FMath::Max(0.0f, Source.BurstOwed - Now);
		const bool bShot = Used.bShotCloud || (Used.BurstCount >= 20 && !Used.bJoinThisFrame);
		Source.Cloud = CloudFor(Look, WorldWhere, Used.bJoinThisFrame ? 0.0f : bShot ? -1.0f : 0.3f);
		if (Source.Cloud != INDEX_NONE)
		{
			FCloud& Cloud = Clouds[Source.Cloud];
			Source.CloudSerial = Cloud.Serial;
			Emit(Cloud, Used, WorldWhere, WorldDirection, CarrierVelocity, Now);
			Cloud.bStamp = true;
			if (bShot)
			{
				ShotCloud = Source.Cloud;
				ShotSerial = Cloud.Serial;
			}
		}
	}
	return Source.Id;
}

void UOSGasSubsystem::SetRate(int32 Id, float PerSecond)
{
	if (FSource* Source = Sources.FindByPredicate([Id](const FSource& Each) { return Each.Id == Id; }))
	{
		Source->Rate = FMath::Max(PerSecond, 0.0f);
	}
}

void UOSGasSubsystem::Stop(int32 Id)
{
	Sources.RemoveAll([Id](const FSource& Each) { return Each.Id == Id; });
}

void UOSGasSubsystem::EmitNow(const FOSGasRecipe& Recipe, const FOSGasLook& Look, const FVector& Where, const FVector& Direction, const FVector& CarrierVelocity, int32 Count,
	bool bMayStartCloud)
{
	if (Count <= 0)
	{
		return;
	}
	// A seep or a hot case's puff goes only into the last shot's cloud while it is well inside its box, or (a barrel's
	// seep) into the barrel's own small cloud, started while 4 slots are free - never into an older stranger: the old
	// clouds keep still (owner 23.10: В«СЂС‹РІРєРѕРј СЃРєР»РµРёРІР°СЋС‚СЃСЏВ»); as clouds of their own, one each, they evicted the shots'.
	const auto Takes = [this, &Look, &Where](int32 Slot, uint32 Serial, float Share)
	{
		if (!Clouds.IsValidIndex(Slot))
		{
			return false;
		}
		const FCloud& Cloud = Clouds[Slot];
		return Cloud.IsAlive() && Cloud.Serial == Serial && Cloud.Evict <= 0.0f && Cloud.Look == Look && Cloud.TakesGas() && Cloud.Parcels.Num() + Cloud.Incoming.Num() < MaxParcels
			&& (Where - (Cloud.Origin + FVector(Cloud.DrawnCentre))).GetAbs().GetMax() < Share * RenderCells * Cloud.RenderCellCm;
	};
	if (Takes(ShotCloud, ShotSerial, 0.45f))
	{
		Emit(Clouds[ShotCloud], Recipe, Where, Direction, CarrierVelocity, Count);
		return;
	}
	if (!bMayStartCloud)
	{
		return;
	}
	if (!Takes(SmallCloud, SmallSerial, 0.45f))
	{
		if (MaxClouds - GetCloudCount() < 4)
		{
			return;
		}
		SmallCloud = CloudFor(Look, Where, -1.0f, true);
		if (SmallCloud == INDEX_NONE)
		{
			return;
		}
		SmallSerial = Clouds[SmallCloud].Serial;
	}
	Emit(Clouds[SmallCloud], Recipe, Where, Direction, CarrierVelocity, Count);
}

void UOSGasSubsystem::TickSources(float DeltaTime)
{
	for (int32 Index = Sources.Num() - 1; Index >= 0; --Index)
	{
		FSource& S = Sources[Index];
		FVector Where = S.Where, Direction = S.Direction;
		if (S.bAttached)
		{
			const USceneComponent* Attach = S.Attach.Get();
			if (!Attach)
			{
				Sources.RemoveAt(Index);
				continue;
			}
			const FTransform Frame = Attach->GetComponentTransform();
			Where = Frame.TransformPosition(S.Where);
			Direction = Frame.TransformVectorNoScale(S.Direction);
			// The point's own velocity (v16c): the gun turned, swayed or stopped after the shot - its later wisps leave
			// with the hole as it moves now (the shot's frozen velocity threw them ahead or aside). Smoothed over ~0.03 s.
			if (S.Recipe.bLiveCarrier && DeltaTime > 1e-5f)
			{
				const FVector Now = S.bHasLast ? (Where - S.LastWhere) / DeltaTime : S.Carrier;
				S.LiveVelocity = S.bHasLast ? S.LiveVelocity + (Now - S.LiveVelocity) * (1.0f - FMath::Exp(-DeltaTime / 0.03f)) : Now;
				S.LastWhere = Where;
				S.bHasLast = true;
			}
		}
		float Owed = 0.0f;
		if (S.BurstLeft > 0.0f)
		{
			const float Share = FMath::Min(DeltaTime / S.BurstLeft, 1.0f);
			const float Now = S.BurstOwed * Share;
			S.BurstOwed -= Now;
			S.BurstLeft -= DeltaTime;
			Owed += Now;
		}
		Owed += S.Rate * DeltaTime;
		S.Carry += Owed;
		const int32 Count = FMath::FloorToInt(S.Carry);
		if (Count > 0)
		{
			S.Carry -= Count;
			// Its own cloud (the serial: a reused slot is another cloud), else one for it.
			if (S.Cloud == INDEX_NONE || !Clouds.IsValidIndex(S.Cloud) || !Clouds[S.Cloud].IsAlive() || Clouds[S.Cloud].Serial != S.CloudSerial)
			{
				S.Cloud = CloudFor(S.Look, Where, S.Recipe.bJoinThisFrame ? 0.0f : S.Recipe.BurstCount >= 20 ? -1.0f : 0.3f);
				S.CloudSerial = S.Cloud != INDEX_NONE ? Clouds[S.Cloud].Serial : 0;
			}
			if (S.Cloud != INDEX_NONE)
			{
				Emit(Clouds[S.Cloud], S.Recipe, Where, Direction, S.Recipe.bLiveCarrier && S.bHasLast ? S.LiveVelocity : S.Carrier, Count);
			}
		}
		if (S.BurstLeft <= 0.0f && S.Rate <= 0.0f && S.Carry < 1.0f)
		{
			Sources.RemoveAt(Index);
		}
	}
}

// --- global effects ---------------------------------------------------------------------

void UOSGasSubsystem::CarveRay(const FVector& Start, const FVector& End, float RadiusCm)
{
	const FVector Along = End - Start;
	const float Length = Along.Size();
	if (Length < 1.0f || RadiusCm <= 0.0f || GetCloudCount() == 0)
	{
		return;
	}
	bool bThrough = false;
	FVector Where = Start;
	for (FCloud& Cloud : Clouds)
	{
		if (!Cloud.IsAlive() || Cloud.Age < 0.15f)
		{
			continue;
		}
		const FVector Centre = Cloud.Origin + FVector(Cloud.DrawnCentre);
		const FVector Closest = FMath::ClosestPointOnSegment(Centre, Start, End);
		if (FVector::Dist(Closest, Centre) < 0.5f * RenderCells * Cloud.PackedCellCm * 1.74f)
		{
			bThrough = true;
			Where = Closest;
			Cloud.NextCommands.Add({ FCommand::EKind::Carve, Start, End, RadiusCm, 0.0f });
			++Cloud.Carves;
		}
	}
	if (!bThrough)
	{
		return;
	}
	// A round's flight comes a frame at a time: its next stretch lengthens its tunnel.
	for (FTunnel& Each : Tunnels)
	{
		if (Each.RadiusCm == RadiusCm && FVector::DistSquared(Each.End, Start) < 1.0f && ((Each.End - Each.Start).GetSafeNormal() | (Along / Length)) > 0.999f)
		{
			Each.End = End;
			return;
		}
	}
	FTunnel& Tunnel = Tunnels.AddDefaulted_GetRef();
	Tunnel.Birth = float(GetWorld()->GetTimeSeconds());
	Tunnel.Start = Start;
	Tunnel.End = End;
	Tunnel.RadiusCm = RadiusCm;
	Tunnel.bVacuum = OSAirZoneAt(GetWorld(), Where) == nullptr;
	if (Tunnels.Num() > MaxTunnels)
	{
		Tunnels.RemoveAt(0, Tunnels.Num() - MaxTunnels);
	}
}

void UOSGasSubsystem::PushHaze(const FVector& Location, const FVector& Direction, float RadiusCm, float SpeedCm)
{
	for (FCloud& Cloud : Clouds)
	{
		if (Cloud.bZone && Cloud.IsAlive())
		{
			Cloud.NextCommands.Add({ FCommand::EKind::Burst, Location, Direction, RadiusCm, SpeedCm });
		}
	}
}

void UOSGasSubsystem::Burst(const FVector& Location, const FVector& Normal, float RadiusCm, float SpeedCm)
{
	const float Speed = SpeedCm * (OSAirZoneAt(GetWorld(), Location) ? 1.0f : 1.5f);
	for (FCloud& Cloud : Clouds)
	{
		if (Cloud.IsAlive() && FVector::Dist(Cloud.Origin + FVector(Cloud.DrawnCentre), Location) < RadiusCm + 0.5f * RenderCells * Cloud.PackedCellCm * 1.74f)
		{
			Cloud.NextCommands.Add({ FCommand::EKind::Burst, Location, Normal, RadiusCm, Speed });
			++Cloud.Blows;
		}
	}
}

int32 UOSGasSubsystem::EmitForSource(const FOSGasRecipe& Recipe, const FOSGasLook& Look, const FVector& Where, const FVector& Direction, const FVector& Carrier, int32 Count,
	bool bContinuous, int32& InOutCloud, uint32& InOutSerial, float FeedWindow)
{
	if (Count <= 0)
	{
		return 0;
	}
	const double Now = GetWorld()->GetTimeSeconds();
	if (bContinuous)
	{
		SourceSpent.RemoveAll([Now](const TPair<double, int32>& Each) { return Now - Each.Key > 1.0; });
		int32 Spent = 0;
		for (const TPair<double, int32>& Each : SourceSpent)
		{
			Spent += Each.Value;
		}
		const int32 Allowed = FMath::Clamp(SourceRatePerSecond - Spent, 0, Count);
		if (Allowed < Count)
		{
			UE_LOG(LogMindKore, Verbose, TEXT("Gas: a source over the budget (%d parcels a second for all), %d of %d refused"), SourceRatePerSecond, Count - Allowed, Count);
		}
		Count = Allowed;
		if (Count > 0)
		{
			SourceSpent.Emplace(Now, Count);
		}
	}
	else if (Count > SourceBurstMax)
	{
		UE_LOG(LogMindKore, Log, TEXT("Gas: a source's burst of %d over the budget (%d), cut"), Count, SourceBurstMax);
		Count = SourceBurstMax;
	}
	if (Count <= 0)
	{
		return 0;
	}
	// Straight into the zone's haze (v15: a fog, steam): its pool's slot, the haze's own cap; it lives as haze.
	if (Recipe.bIntoHaze && CVarGasFogSource.GetValueOnGameThread() > 0)
	{
		const AOSGravityZone* Zone = OSAirZoneAt(GetWorld(), Where);
		if (Zone && Zone->HazeHalfLifeSeconds > 0.0f && CVarGasHazeRoom.GetValueOnGameThread() > 0)
		{
			RoomFor(Zone, Where);
		}
		const int32 Slot = ZoneCloudFor(Zone);
		if (Slot == INDEX_NONE)
		{
			UE_LOG(LogMindKore, Log, TEXT("Gas: a haze source refused - no air zone with haze here or no free haze slot"));
			return 0;
		}
		FCloud& Haze = Clouds[Slot];
		InOutCloud = Slot;
		InOutSerial = Haze.Serial;
		const int32 Before = Haze.Incoming.Num();
		Emit(Haze, Recipe, Where, Direction, Carrier, Count, 4 * MaxZoneParcels);
		const float Gain = Look.Extinction / FMath::Max(Haze.Look.Extinction, 1e-4f);
		for (int32 I = Before; I < Haze.Incoming.Num(); ++I)
		{
			FOSGasParcel& P = Haze.Incoming[I];
			P.Life = 1e9f;
			P.Cling = 0.0f;
			P.bInstant = false;
			P.bFog = true;
			P.Mass *= Gain;
			P.Vent = Recipe.HazeHalfLifeSeconds > 0.0f ? UE_LN2 / Recipe.HazeHalfLifeSeconds : 0.0f;
		}
		Haze.LastFed = Now;
		return Haze.Incoming.Num() - Before;
	}
	if (!Clouds.IsValidIndex(InOutCloud) || !Clouds[InOutCloud].IsAlive() || Clouds[InOutCloud].Serial != InOutSerial || Clouds[InOutCloud].Evict > 0.0f
		|| !Clouds[InOutCloud].TakesGas() || Clouds[InOutCloud].Parcels.Num() + Clouds[InOutCloud].Incoming.Num() + Count > MaxParcels)
	{
		// A free slot only: no eviction, no stranger's cloud (a shot's) - none free, refused.
		InOutCloud = CloudFor(Look, Where, -1.0f, true);
		if (InOutCloud == INDEX_NONE)
		{
			UE_LOG(LogMindKore, Log, TEXT("Gas: a source refused - no free cloud slot (%d alive)"), GetCloudCount());
			return 0;
		}
		InOutSerial = Clouds[InOutCloud].Serial;
	}
	Emit(Clouds[InOutCloud], Recipe, Where, Direction, Carrier, Count);
	Clouds[InOutCloud].FedWindow = FMath::Max(0.5f, FeedWindow);
	return Count;
}

void UOSGasSubsystem::AddHeat(const FVector& Where, const FVector& Direction, int32 Count, float Heat, float Seconds, float LifeMax)
{
	// No air, no shimmer.
	if (!OSAirZoneAt(GetWorld(), Where))
	{
		return;
	}
	const FVector Axis = Direction.GetSafeNormal(UE_SMALL_NUMBER, FVector::ForwardVector);
	for (int32 Index = 0; Index < Count; ++Index)
	{
		if (HeatPuffs.Num() >= MaxHeat)
		{
			HeatPuffs.RemoveAt(0);
		}
		FHeatPuff& H = HeatPuffs.AddDefaulted_GetRef();
		H.P = Where;
		H.V = FMath::VRandCone(Axis, FMath::DegreesToRadians(15.0f)) * FMath::FRandRange(20.0f, 60.0f);
		H.RadiusCm = FMath::FRandRange(2.0f, 3.5f);
		H.Heat = Heat;
		// Over Seconds (a shot's 80 ms): a negative age waits.
		H.Age = -Seconds * Index / FMath::Max(1, Count - 1);
		H.Life = FMath::FRandRange(1.0f, FMath::Max(LifeMax, 1.0f));
	}
}

float UOSGasSubsystem::Transmittance(const FVector& A, const FVector& B, const UObject* Caller)
{
	const double Now = GetWorld()->GetTimeSeconds();
	FSeenThrough* Cached = Caller ? SeenThrough.Find(Caller) : nullptr;
	if (Cached && Now - Cached->At < 0.25)
	{
		return Cached->T;
	}
	const FVector Seg = B - A;
	const double Length = Seg.Size();
	float Tau = 0.0f;
	if (Length > 1.0)
	{
		const FVector Dir = Seg / Length;
		for (const FCloud& Cloud : Clouds)
		{
			if (!Cloud.IsAlive() || !Cloud.SnapBox.IsValid || !FMath::LineBoxIntersection(Cloud.SnapBox, A, B, Seg))
			{
				continue;
			}
			float CloudTau = 0.0f;
			for (int32 I = 0; I < Cloud.Snap.Num(); ++I)
			{
				const FVector4f& S = Cloud.Snap[I];
				const FVector M = FVector(S.X, S.Y, S.Z) - A;
				const double Tc = M | Dir;
				const double D2 = M.SizeSquared() - Tc * Tc;
				const double R2 = double(S.W) * S.W;
				if (D2 >= R2)
				{
					continue;
				}
				const double H = FMath::Sqrt(R2 - D2);
				const double T0 = FMath::Max(Tc - H, 0.0), T1 = FMath::Min(Tc + H, Length);
				if (T1 > T0)
				{
					CloudTau += Cloud.SnapDensity[I] * float(T1 - T0) * float(1.0 - D2 / R2);
				}
			}
			Tau += CloudTau * Cloud.Look.Extinction * Cloud.DrawGain;
		}
	}
	const float T = FMath::Exp(-Tau);
	if (Caller)
	{
		FSeenThrough& Entry = SeenThrough.FindOrAdd(Caller);
		Entry.At = Now;
		Entry.T = T;
	}
	return T;
}

void UOSGasSubsystem::AddAirFlow(const FVector& Location, const FVector& Direction, float SpeedCm, float ReachCm, float Seconds)
{
	AirFlows.Add({ Location, Direction.GetSafeNormal(), SpeedCm, ReachCm, Seconds });
}

// --- the frame -------------------------------------------------------------------------

void UOSGasSubsystem::Tick(float DeltaTime)
{
	const double FrameStart = FPlatformTime::Seconds();
	const bool bAny = GetCloudCount() > 0 || !Sources.IsEmpty() || !HeatPuffs.IsEmpty();
#if !UE_BUILD_SHIPPING
	FMemory::Memzero(GGasSec, sizeof(GGasSec));
#endif
	{
		OS_GAS_SEC(Sources);
		TickSources(DeltaTime);
	}
#if !UE_BUILD_SHIPPING
	const double LightsT0 = FPlatformTime::Seconds();
#endif
	for (int32 Index = Tunnels.Num() - 1; Index >= 0; --Index)
	{
		FTunnel& T = Tunnels[Index];
		T.Age += DeltaTime;
		if (T.Age >= (0.8f + 2.5f) * (T.bVacuum ? 2.0f : 1.0f))
		{
			Tunnels.RemoveAt(Index);
		}
	}
	// Air flows: the parcels in reach drawn to the hole (a command for the next step).
	for (int32 Index = AirFlows.Num() - 1; Index >= 0; --Index)
	{
		FAirFlow& Flow = AirFlows[Index];
		Flow.Seconds -= DeltaTime;
		if (Flow.Seconds <= 0.0f)
		{
			AirFlows.RemoveAt(Index);
			continue;
		}
	}
	// The world's lamps, once a second for every cloud (PickKey ran it a cloud each and twice with the lamps' set).
	KeyLightsAge += DeltaTime;
	if (KeyLightsAge > 1.0f)
	{
		KeyLightsAge = 0.0f;
		KeyLights.Reset();
		for (TObjectIterator<ULightComponent> It; It; ++It)
		{
			if (It->GetWorld() == GetWorld())
			{
				KeyLights.Add(*It);
			}
		}
	}
	// Ventilation grilles (v14): actors tagged GasVent, once a second; each draws the air (the haze) in within 1 m.
	VentsAge += DeltaTime;
	if (VentsAge > 1.0f)
	{
		VentsAge = 0.0f;
		Vents.Reset();
		if (CVarGasHazeVents.GetValueOnGameThread() > 0)
		{
			for (TActorIterator<AActor> It(GetWorld()); It; ++It)
			{
				if (It->ActorHasTag(TEXT("GasVent")))
				{
					Vents.Add(*It);
					AddAirFlow(It->GetActorLocation(), It->GetActorForwardVector(), 15.0f, 100.0f, 1.05f);
				}
			}
		}
	}
	// The player's own lamps every frame: a lamp put on the gun counts the frame it is lit, not a second later.
	PlayerLights.Reset();
	if (const APlayerController* Controller = GetWorld()->GetFirstPlayerController(); Controller && Controller->GetPawn())
	{
		TArray<ULightComponent*> Own;
		Controller->GetPawn()->GetComponents<ULightComponent>(Own);
		TArray<AActor*> Carried;
		Controller->GetPawn()->GetAttachedActors(Carried, true, true);
		for (AActor* Each : Carried)
		{
			TArray<ULightComponent*> More;
			Each->GetComponents<ULightComponent>(More);
			Own.Append(More);
		}
		for (ULightComponent* L : Own)
		{
			PlayerLights.Add(L);
		}
	}
#if !UE_BUILD_SHIPPING
	GGasSec[SecLights] += (FPlatformTime::Seconds() - LightsT0) * 1e6;
#endif
	{
		OS_GAS_SEC(Torch);
		UpdateTorchShadow(DeltaTime);
	}
	{
		OS_GAS_SEC(Flood);
		FloodRooms();
	}
	{
		OS_GAS_SEC(Fog);
		TickFog(DeltaTime);
	}
#if !UE_BUILD_SHIPPING
	const double HeatT0 = FPlatformTime::Seconds();
#endif
	// Heat puffs: up 20-30 cm/s against gravity (none in zero-G), widening, cooling.
	for (int32 Index = HeatPuffs.Num() - 1; Index >= 0; --Index)
	{
		FHeatPuff& H = HeatPuffs[Index];
		H.Age += DeltaTime;
		if (H.Age >= H.Life)
		{
			HeatPuffs.RemoveAt(Index);
			continue;
		}
		if (H.Age < 0.0f)
		{
			continue;
		}
		const FVector G = OSGravityAt(GetWorld(), H.P).Acceleration;
		const FVector Rise = -G.GetSafeNormal() * (25.0f * FMath::Min(G.Size() / 980.0f, 1.0f));
		H.V = Rise + (H.V - Rise) * FMath::Exp(-DeltaTime / 0.3f);
		H.P += H.V * DeltaTime;
		H.RadiusCm = FMath::Min(H.RadiusCm + 4.0f * DeltaTime, 9.0f);
	}
#if !UE_BUILD_SHIPPING
	GGasSec[SecHeat] += (FPlatformTime::Seconds() - HeatT0) * 1e6;
#endif
	TSet<TObjectKey<UPrimitiveComponent>> Seen;
	for (FCloud& Cloud : Clouds)
	{
		if (Cloud.IsAlive())
		{
			{
				OS_GAS_SEC(Movers);
				FindMovers(Cloud, DeltaTime, Seen);
			}
#if !UE_BUILD_SHIPPING
			const double Inner0 = GGasSec[SecStamp] + GGasSec[SecUpload] + GGasSec[SecRoom] + GGasSec[SecHand] + GGasSec[SecKey] + GGasSec[SecLamps];
			const double CloudT0 = FPlatformTime::Seconds();
#endif
			TickCloud(Cloud, DeltaTime);
#if !UE_BUILD_SHIPPING
			const double Inner1 = GGasSec[SecStamp] + GGasSec[SecUpload] + GGasSec[SecRoom] + GGasSec[SecHand] + GGasSec[SecKey] + GGasSec[SecLamps];
			GGasSec[SecCloudRest] += (FPlatformTime::Seconds() - CloudT0) * 1e6 - (Inner1 - Inner0);
#endif
		}
	}
	for (auto It = LastMoverAt.CreateIterator(); It; ++It)
	{
		if (!Seen.Contains(It.Key()))
		{
			It.RemoveCurrent();
		}
	}
	{
		OS_GAS_SEC(Post);
		UpdatePost();
	}
#if !UE_BUILD_SHIPPING
	if (CVarGasDebug.GetValueOnGameThread() > 0)
	{
		DrawDebug();
	}
#endif
	if (bAny)
	{
		const double Us = (FPlatformTime::Seconds() - FrameStart) * 1e6;
		FScopeLock Lock(&CostLock);
		CostFrameUs += Us;
#if !UE_BUILD_SHIPPING
		if (Us > CostMaxFrameUs)
		{
			FMemory::Memcpy(GGasPeakSec, GGasSec, sizeof(GGasSec));
		}
#endif
		CostMaxFrameUs = FMath::Max(CostMaxFrameUs, Us);
		++CostFrames;
	}
}

void UOSGasSubsystem::KickStamps()
{
	if (CVarGasStampAsync.GetValueOnGameThread() <= 0)
	{
		return;
	}
	for (FCloud& Cloud : Clouds)
	{
		if (!Cloud.IsAlive() || !Cloud.bStamp || Cloud.IsBusy() || Cloud.bStampRunning)
		{
			continue;
		}
		// The same work as the tick's stamp, moved: the parcels let out so far go in; later ones wait in Incoming for
		// the next step. The cloud is busy (Pending) until the tick joins it: no step, no atlas, no hand-over meanwhile.
		Cloud.bStamp = false;
		Cloud.Parcels.Append(MoveTemp(Cloud.Incoming));
		Cloud.Incoming.Reset();
		Cloud.bStampRunning = true;
		FCloud* Ptr = &Cloud;
		Cloud.Pending = UE::Tasks::Launch(UE_SOURCE_LOCATION, [Ptr]() { Splat(*Ptr); });
	}
}

void UOSGasSubsystem::TickCloud(FCloud& Cloud, float DeltaTime)
{
	UWorld* World = GetWorld();
	// The shot's stamp on a worker (os.Gas.StampAsync): joined here, waited for if still running - the shot is seen on
	// its own frame (the owner's v11 rule); then on as a stamp splatted here.
	if (Cloud.bStampRunning)
	{
		Cloud.bStampRunning = false;
		if (Cloud.Pending.IsValid())
		{
#if !UE_BUILD_SHIPPING
			const bool bRunning = !Cloud.Pending.IsCompleted();
			const double W0 = FPlatformTime::Seconds();
#endif
			Cloud.Pending.Wait();
#if !UE_BUILD_SHIPPING
			const double Us = (FPlatformTime::Seconds() - W0) * 1e6;
			++GStampJoins;
			GStampWaited += bRunning ? 1 : 0;
			GStampWaitUs += Us;
			GStampMaxWaitUs = FMath::Max(GStampMaxWaitUs, Us);
			GGasSec[SecStamp] += Us;
#endif
			Cloud.Pending = UE::Tasks::FTask();
		}
		Cloud.bStampAtlas = true;
		Cloud.PackedTime = Cloud.SimTime;
	}
	const bool bBusy = Cloud.IsBusy();
	if (!bBusy && Cloud.Pending.IsValid())
	{
		Cloud.Pending = UE::Tasks::FTask();
	}
	Cloud.Age += DeltaTime;
	const bool bFed = Cloud.IsFed(World->GetTimeSeconds());
	// Fed: up to 0.3 of its life; one that had faded past it comes back no faster than a second a second (a jump
	// brought its old gas back up in a frame).
	const float Hold = 0.3f * Cloud.Look.LifeSeconds;
	Cloud.FadeAge = !bFed ? Cloud.FadeAge + DeltaTime : Cloud.FadeAge > Hold ? FMath::Max(Hold, Cloud.FadeAge - DeltaTime) : FMath::Min(Cloud.FadeAge + DeltaTime, Hold);
	// The haze ends once it is no longer seen - its drawn opacity under 0.03 for 5 s, faded out as an eviction - not by
	// its parcels' mass (it held its two slots ~5 minutes: a third zone got none).
	const AOSGravityZone* FogZoneOf = Cloud.bZone ? Cloud.Zone.Get() : nullptr;
	const FFogZone* FogState = FogZoneOf ? FogZones.Find(FogZoneOf) : nullptr;
	if (Cloud.bZone && !bBusy && Cloud.bDrawable && Cloud.Evict <= 0.0f && !(FogState && FogState->bAwake))
	{
		const float Seen = FMath::Max(1.0f - Cloud.CentreT, 1.0f - FMath::Exp(-Cloud.MaxDensity * Cloud.Look.Extinction * Cloud.PackedCellCm * 3.0f));
		Cloud.FaintFor = Seen < 0.03f ? Cloud.FaintFor + DeltaTime : 0.0f;
		if (Cloud.FaintFor >= 5.0f)
		{
			Cloud.Evict = EvictSeconds;
		}
	}
#if !UE_BUILD_SHIPPING
	// os.Gas.SpreadLog 1: the haze's spread every 5 s (v14's probe).
	if (Cloud.bZone && !bBusy && CVarGasSpreadLog.GetValueOnGameThread() > 0 && !Cloud.Parcels.IsEmpty()
		&& FMath::FloorToInt(Cloud.Age / 5.0f) != FMath::FloorToInt((Cloud.Age - DeltaTime) / 5.0f))
	{
		FVector3f C = FVector3f::ZeroVector;
		float M = 0.0f;
		for (const FOSGasParcel& P : Cloud.Parcels)
		{
			C += P.P * P.Mass;
			M += P.Mass;
		}
		C /= FMath::Max(M, 1e-6f);
		const FVector3f Down = FVector3f(Cloud.Gravity.GetSafeNormal(UE_SMALL_NUMBER, FVector(0, 0, -1)));
		double H2 = 0.0, Z2 = 0.0;
		for (const FOSGasParcel& P : Cloud.Parcels)
		{
			const FVector3f D = P.P - C;
			const float Z = D | Down;
			Z2 += P.Mass * Z * Z;
			H2 += P.Mass * ((D - Down * Z).SizeSquared() * 0.5f);
		}
		UE_LOG(LogMindKore, Log, TEXT("Gas: haze spread at %.0f s - %d parcels, mass %.2f, sigma across %.0f cm, up %.0f cm, CentreT %.2f, its centre at z %.0f cm"), Cloud.Age, Cloud.Parcels.Num(), M,
			FMath::Sqrt(H2 / FMath::Max(M, 1e-6f)), FMath::Sqrt(Z2 / FMath::Max(M, 1e-6f)), Cloud.CentreT, Cloud.Origin.Z + C.Z);
	}
#endif
	const bool bEvicted = Cloud.Evict > 0.0f && (Cloud.Evict -= DeltaTime) <= 0.0f;
	// The haze waits 2 s for its first gas (the copies come ahead of their ramps).
	const bool bEmpty = !bBusy && Cloud.Parcels.IsEmpty() && Cloud.Incoming.IsEmpty() && Cloud.Age > (Cloud.bZone ? 2.0f : 0.3f);
	if ((Cloud.FadeAge >= Cloud.Look.LifeSeconds || bEvicted) && bBusy)
	{
		// Faded out already; tried again next frame (an eviction kept pending).
		if (bEvicted)
		{
			Cloud.Evict = 1e-4f;
		}
		return;
	}
	if (Cloud.FadeAge >= Cloud.Look.LifeSeconds || bEvicted || bEmpty)
	{
		UE_LOG(LogMindKore, Log, TEXT("Gas: a cloud ends at %.2f s (%s), %d parcels; %d re-box(es), the largest %.1f cm, cells %.1f-%.1f cm; %d carve(s), %d blow(s)"), Cloud.Age,
			bEvicted ? TEXT("evicted") : bEmpty ? TEXT("empty") : TEXT("its life"), Cloud.Parcels.Num(), Cloud.Reboxes, Cloud.MaxReboxCm,
			Cloud.MinCellCm > 1e8f ? 0.0f : Cloud.MinCellCm, Cloud.MaxCellCm, Cloud.Carves, Cloud.Blows);
		Cloud.Age = -1.0f;
		Cloud.Parcels.Reset();
		Cloud.Incoming.Reset();
		Cloud.bDrawable = false;
		return;
	}
	// A burst's first parcels: splatted here, on the game thread, so the shot is seen on its own frame.
	if (!bBusy && Cloud.bStamp)
	{
		Cloud.bStamp = false;
		Cloud.Parcels.Append(MoveTemp(Cloud.Incoming));
		Cloud.Incoming.Reset();
		OS_GAS_SEC(Stamp);
		Splat(Cloud);
		Cloud.bStampAtlas = true;
		Cloud.PackedTime = Cloud.SimTime;
	}
	// The last step's atlas: the box moves with it.
	if (!bBusy && Cloud.bPendingAtlas)
	{
		Cloud.bPendingAtlas = false;
		if (Cloud.bDrawable && (Cloud.DrawnCentre != Cloud.PackedCentre || Cloud.RenderCellCm != Cloud.PackedCellCm))
		{
			++Cloud.Reboxes;
			Cloud.MaxReboxCm = FMath::Max(Cloud.MaxReboxCm, FVector3f::Dist(Cloud.DrawnCentre, Cloud.PackedCentre));
		}
		Cloud.MinCellCm = FMath::Min(Cloud.MinCellCm, Cloud.PackedCellCm);
		Cloud.MaxCellCm = FMath::Max(Cloud.MaxCellCm, Cloud.PackedCellCm);
		const bool bBoth = Cloud.bStampAtlas || !Cloud.bDrawable;
		Cloud.bStampAtlas = false;
		Cloud.PrevCentre = bBoth ? Cloud.PackedCentre : Cloud.DrawnCentre;
		Cloud.PrevCellCm = bBoth ? Cloud.PackedCellCm : Cloud.RenderCellCm;
		Cloud.PrevTime = bBoth ? Cloud.PackedTime : Cloud.CurTime;
		{
			const FVector3f OccLo = Cloud.PackedCentre + (FVector3f(Cloud.AtlasLo) - FVector3f(0.5f * RenderCells)) * Cloud.PackedCellCm;
			const FVector3f OccHi = Cloud.AtlasHi.X >= Cloud.AtlasLo.X ? Cloud.PackedCentre + (FVector3f(Cloud.AtlasHi) + FVector3f(1.0f - 0.5f * RenderCells)) * Cloud.PackedCellCm : OccLo;
			Cloud.PrevOccLo = bBoth ? OccLo : Cloud.CurOccLo;
			Cloud.PrevOccHi = bBoth ? OccHi : Cloud.CurOccHi;
			Cloud.CurOccLo = OccLo;
			Cloud.CurOccHi = OccHi;
		}
		Cloud.DrawnCentre = Cloud.PackedCentre;
		Cloud.RenderCellCm = Cloud.PackedCellCm;
		Cloud.CurTime = Cloud.PackedTime;
		if (bBoth)
		{
			Cloud.ShowTime = Cloud.CurTime;
		}
		Cloud.CurHalf = bBoth ? 0 : 1 - Cloud.CurHalf;
		const FIntPoint Size = FOSSmokeSim::AtlasSize(RenderCells);
		OS_GAS_SEC(Upload);
		for (int32 Half = 0; Half < 2; ++Half)
		{
			if (!bBoth && Half != Cloud.CurHalf)
			{
				continue;
			}
			uint8* Data = static_cast<uint8*>(FMemory::Malloc(Cloud.Atlas.Num() * sizeof(FColor)));
			FMemory::Memcpy(Data, Cloud.Atlas.GetData(), Cloud.Atlas.Num() * sizeof(FColor));
			FUpdateTextureRegion2D* Region = new FUpdateTextureRegion2D(0, Half * Size.Y, 0, 0, Size.X, Size.Y);
			Cloud.Texture->UpdateTextureRegions(0, 1, Region, Size.X * sizeof(FColor), sizeof(FColor), Data,
				[](uint8* Src, const FUpdateTextureRegion2D* Regions) { FMemory::Free(Src); delete Regions; });
		}
		// Drawn from its first atlas with gas on; off only when the cloud ends (no flicker).
		Cloud.bDrawable |= Cloud.bHasGas;
	}
	// Air or vacuum and gravity where the gas is now (the haze: its zone's).
	const FVector Centre = Cloud.Origin + FVector(Cloud.DrawnCentre);
	if (!bBusy && !Cloud.bZone)
	{
		// The room's flood starts with the first shot in an air zone (3 s ahead of the hand-over).
		if (Cloud.bAir && CVarGasHazeRoom.GetValueOnGameThread() > 0)
		{
			if (const AOSGravityZone* Zone = OSAirZoneAt(World, Cloud.Origin); Zone && Zone->HazeHalfLifeSeconds > 0.0f)
			{
				OS_GAS_SEC(Room);
				RoomFor(Zone, Cloud.Origin);
			}
		}
		OS_GAS_SEC(Hand);
		HandOver(Cloud, Centre);
	}
	if (!bBusy && Cloud.bZone)
	{
		const AOSGravityZone* ZoneOf = Cloud.Zone.Get();
		const TSharedPtr<FOSGasRoom>* Found = ZoneOf ? Rooms.Find(ZoneOf) : nullptr;
		Cloud.Room = Found ? *Found : nullptr;
		Cloud.bUseRoom = Cloud.Room.IsValid() && CVarGasHazeRoom.GetValueOnGameThread() > 0;
	}
	if (!bBusy && Cloud.bZone)
	{
		const AOSGravityZone* Zone = Cloud.Zone.Get();
		Cloud.HazeHalfLife = Zone ? Zone->HazeHalfLifeSeconds * FMath::Max(CVarGasHazeVentScale.GetValueOnGameThread(), 0.01f) : 1.0f;
	}
	else if (!bBusy)
	{
		Cloud.bAir = OSAirZoneAt(World, Centre) != nullptr;
		Cloud.Gravity = OSGravityAt(World, Centre).Acceleration;
		Cloud.Grid->bAir = Cloud.bAir;
		Cloud.Grid->Down = FVector3f(Cloud.Gravity.GetSafeNormal());
	}
	// Fixed steps on the gas's own clock (SimTime), never waited for and no time thrown away (four at most at once,
	// the rest next frame; a quarter second behind at most). 60 Hz to 0.5 s, 30 Hz to 1.5 s, 15 Hz to 3 s, 10 Hz after:
	// the drawing blends the last two atlases, so a slow step shows no stairs.
	Cloud.StepCarry = FMath::Min(Cloud.StepCarry + DeltaTime, 0.25f);
	if (!bBusy)
	{
		// A fed cloud keeps stepping at 30 Hz at least (fresh gas in it).
		const float Since = bFed ? FMath::Min(Cloud.SimTime, 1.0f) : Cloud.SimTime;
		// The haze: 5 Hz, 15 with a body in it (a hole in it followed a walker 0.2 s late).
		const float Dt = Cloud.bZone ? (Cloud.NextMovers.IsEmpty() ? 0.2f : 1.0f / 15.0f)
			: Since < FastSeconds ? FastStep : Since < 1.5f ? SlowStep : Since < 3.0f ? 1.0f / 15.0f : 0.1f;
		const int32 Steps = FMath::Min(FMath::FloorToInt(Cloud.StepCarry / Dt + 1e-3f), 4);
		if (Steps > 0)
		{
			Cloud.StepCarry = FMath::Max(0.0f, Cloud.StepCarry - Steps * Dt);
			Cloud.SimTime += Steps * Dt;
			Cloud.StepDt = Dt;
			// The gas's clock against the world's (it loses time on a hitch: the hand-over's ramps go by the world's).
			Cloud.SimToWorld = World->GetTimeSeconds() - Cloud.StepCarry - Cloud.SimTime;
			Cloud.DriftCm = CVarGasDriftCm.GetValueOnGameThread();
			Cloud.WarmLift = CVarGasWarmLift.GetValueOnGameThread();
			Cloud.Rise = FMath::Max(CVarGasRise.GetValueOnGameThread(), 0.0f);
			Cloud.HazeRadius = CVarGasHazeRadius.GetValueOnGameThread();
			Cloud.HazeDiffuse = CVarGasHazeDiffuse.GetValueOnGameThread();
			Cloud.HazeRadiusMax = CVarGasHazeRadiusMax.GetValueOnGameThread();
			Cloud.HazeEddyCm = FMath::Max(CVarGasHazeEddyCm.GetValueOnGameThread(), 10.0f);
			Cloud.HazeOwn = FMath::Clamp(CVarGasHazeWanderOwn.GetValueOnGameThread(), 0.0f, 1.0f);
			Cloud.HazeDriftCm = CVarGasHazeDriftCm.GetValueOnGameThread();
			Cloud.HazeDriftScale = FMath::Max(CVarGasHazeDriftScaleCm.GetValueOnGameThread(), 30.0f);
			Cloud.HazeNoiseCut = CVarGasHazeNoiseCut.GetValueOnGameThread();
			{
				const FFogZone* Fog = Cloud.bZone && Cloud.Zone.IsValid() ? FogZones.Find(Cloud.Zone.Get()) : nullptr;
				Cloud.bFogFloor = Fog && Fog->Fog == EOSZoneFog::Floor && Cloud.Gravity.SizeSquared() > 1.0;
				Cloud.FogStir = CVarGasFogStir.GetValueOnGameThread();
				Cloud.HazeSlump = FMath::Max(CVarGasHazeSlump.GetValueOnGameThread(), 0.0f);
				Cloud.HazeFlat = FMath::Clamp(CVarGasHazeFlat.GetValueOnGameThread(), 0.2f, 1.0f);
				Cloud.HazeDriftZ = FMath::Clamp(CVarGasHazeDriftZ.GetValueOnGameThread(), 0.0f, 1.0f);
			}
			Cloud.HazeDiffuseZ = CVarGasHazeDiffuseZ.GetValueOnGameThread();
			Cloud.HazeEddy = FMath::Max(CVarGasHazeEddySeconds.GetValueOnGameThread(), 0.1f);
			Cloud.HazeZeroG = CVarGasHazeDiffuseZeroG.GetValueOnGameThread();
			Cloud.HazeWake = CVarGasHazeWake.GetValueOnGameThread();
			Cloud.Vents.Reset();
			if (Cloud.bZone && CVarGasHazeVents.GetValueOnGameThread() > 0)
			{
				for (const TWeakObjectPtr<AActor>& Each : Vents)
				{
					if (const AActor* Vent = Each.Get())
					{
						Cloud.Vents.Add(FVector3f(Vent->GetActorLocation() - Cloud.Origin));
					}
				}
			}
			if (!Cloud.bZone)
			{
				const AOSGravityZone* Zone = OSAirZoneAt(World, Centre);
				Cloud.DriftSeed = Zone ? GetTypeHash(Zone->GetFName()) : 0u;
			}
			LaunchStep(Cloud, Steps, Dt);
		}
	}
	// The moment shown: a step behind the gas's clock, held at the current atlas while a step is late, never back.
	Cloud.ShowTime = FMath::Clamp(Cloud.SimTime + Cloud.StepCarry - Cloud.StepDt, Cloud.ShowTime, Cloud.CurTime);
	// The material: the drawn box (world axes), the life's fade, the light, the eye and the body.
	{
		FVector3f Dir;
		FLinearColor Colour;
		{
			OS_GAS_SEC(Key);
			PickKey(Centre, Dir, Colour);
		}
		const float Ease = 1.0f - FMath::Exp(-DeltaTime / 0.25f);
		// os.Gas.Light 1: four lamps that see the cloud; the self-shadow baked toward the strongest (eased below: 0.25 s).
		if (CVarGasLight.GetValueOnGameThread() > 0)
		{
			OS_GAS_SEC(Lamps);
			UpdateLamps(Cloud, Centre, DeltaTime, Dir);
		}
		// os.Gas.Light 0: 80% white: a saturated lamp (the planetoid's red rock light) barely tints the gas (it turned it brown).
		Colour = FMath::Lerp(Colour, FLinearColor(Colour.GetLuminance(), Colour.GetLuminance(), Colour.GetLuminance()), 0.8f);
		Cloud.KeyDir = (Cloud.KeyDir + (Dir - Cloud.KeyDir) * Ease).GetSafeNormal(UE_SMALL_NUMBER, Dir);
		Cloud.KeyColour = Cloud.KeyColour + (Colour - Cloud.KeyColour) * Ease;
	}
	// The rest is the post-process's (UpdatePost, once a frame for all clouds).
}

void UOSGasSubsystem::AmbientFor(bool bAir, FLinearColor& OutTop, FLinearColor& OutBottom) const
{
	// os.Gas.Light 0 only (the A/B): by the map's name. os.Gas.Light 1 takes the room's light from the frame.
	if (!bAir)
	{
		OutTop = FLinearColor(0.30f, 0.32f, 0.38f);
		OutBottom = FLinearColor(0.15f, 0.16f, 0.20f);
		return;
	}
	const FString Map = GetWorld()->GetMapName();
	if (Map.Contains(TEXT("Ship")))
	{
		// The ship's room: pink from the ceiling, blue from the snow below the windows.
		OutTop = FLinearColor(0.92f, 0.62f, 0.72f);
		OutBottom = FLinearColor(0.42f, 0.62f, 0.78f);
	}
	else if (Map.Contains(TEXT("TestRoom")))
	{
		OutTop = FLinearColor(0.78f, 0.72f, 0.64f);
		OutBottom = FLinearColor(0.38f, 0.35f, 0.33f);
	}
	else
	{
		OutTop = FLinearColor(0.65f, 0.66f, 0.70f);
		OutBottom = FLinearColor(0.32f, 0.33f, 0.36f);
	}
}

void UOSGasSubsystem::UpdatePost()
{
	if (!Post)
	{
		return;
	}
	UWorld* World = GetWorld();
	FVector Eye = FVector::ZeroVector;
	if (const APlayerController* Controller = World->GetFirstPlayerController(); Controller && Controller->PlayerCameraManager)
	{
		Eye = Controller->PlayerCameraManager->GetCameraLocation();
	}
	// Every drawable cloud (MaxDrawn = MaxClouds: a set picked by distance swapped clouds in and out whole).
	TArray<FCloud*, TInlineAllocator<MaxDrawn>> Drawn;
	for (FCloud& Cloud : Clouds)
	{
		if (Cloud.IsAlive() && Cloud.bDrawable)
		{
			Drawn.Add(&Cloud);
		}
	}
	Drawn.Sort([&Eye](const FCloud& A, const FCloud& B)
	{
		return FVector::DistSquared(A.Origin + FVector(A.DrawnCentre), Eye) < FVector::DistSquared(B.Origin + FVector(B.DrawnCentre), Eye);
	});
	Drawn.SetNum(FMath::Min(Drawn.Num(), MaxDrawn));
	{
		TArray<int32> Set;
		for (const FCloud* Each : Drawn)
		{
			Set.Add(int32(Each - Clouds.GetData()));
		}
		Set.Sort();
		DrawnSetChanges += Set != LastDrawnSet ? 1 : 0;
		LastDrawnSet = MoveTemp(Set);
	}
	// Composited oldest first, a stable order: by distance, two overlapping clouds swapped places as their centres
	// moved and the overlap's colour jumped (owner 23.10: В«СЂС‹РІРєРѕРј СЃРєР»РµРёРІР°СЋС‚СЃСЏ Рё РїРѕРґРµСЂРіРёРІР°СЋС‚СЃСЏВ»).
	Drawn.Sort([](const FCloud& A, const FCloud& B) { return A.Age > B.Age; });
	// GasData: a column a cloud, DataRows rows (Scripts/os_muzzle_fx.py, GAS_POST_HLSL): 0 the current atlas's centre
	// and half, 1 cells + its cell + gain + extinction, 2 key light + the phase, 3 colour + birth (world s), 4 warm +
	// the current's half of the texture, 5 ambient top, 6 ambient bottom, 7 the previous atlas's centre and half, 8 its cell.
	constexpr int32 Rows = DataRows;
	TArray<FLinearColor> Data;
	Data.SetNumZeroed(MaxDrawn * Rows);
	BoundGrids.SetNum(MaxDrawn);
	for (int32 Index = 0; Index < Drawn.Num(); ++Index)
	{
		FCloud& Cloud = *Drawn[Index];
		const FVector Centre = Cloud.Origin + FVector(Cloud.DrawnCentre);
		const float Half = 0.5f * RenderCells * Cloud.RenderCellCm;
		// The cloud melts from 0.3 of its life: the density's gain as the cube of the fade, so a thick cloud's opacity
		// (1 - exp(-density)) falls about evenly instead of holding to the last half second and going at once (owner
		// 23.10: the veil went in 0.2 s).
		// A cloud handing its gas to the haze holds its fade where it stands at HandFrom (the haze takes the rest); the
		// haze itself never fades by age.
		const float FadeAt = Cloud.bHands ? FMath::Min(Cloud.FadeAge, HandFrom) : Cloud.FadeAge;
		const float LifeFade = Cloud.bZone ? 1.0f : FMath::Cube(1.0f - FMath::SmoothStep(0.3f * Cloud.Look.LifeSeconds, Cloud.Look.LifeSeconds, FadeAt));
		const float EvictFade = Cloud.Evict > 0.0f ? FMath::Cube(Cloud.Evict / EvictSeconds) : 1.0f;
		Cloud.DrawGain = LifeFade * EvictFade * Cloud.FogIn;
		// The key light in display terms (the post-process is after the tonemapper): a soft knee, never blown white.
		const bool bNewLight = CVarGasLight.GetValueOnGameThread() > 0;
		const FLinearColor K = Cloud.KeyColour;
		const FLinearColor Key(1.0f - FMath::Exp(-K.R * 0.8f), 1.0f - FMath::Exp(-K.G * 0.8f), 1.0f - FMath::Exp(-K.B * 0.8f));
		FLinearColor Top, Bottom;
		AmbientFor(Cloud.bAir, Top, Bottom);
		// The blend of the two atlases: where the moment shown is between their times.
		// os.Gas.TickHz: the moment shown in steps on the world's clock (stop motion; the box and the camera stay smooth).
		float Show = Cloud.ShowTime;
		if (const float Hz = CVarGasTickHz.GetValueOnGameThread(); Hz > 0.0f)
		{
			Show = float(FMath::FloorToDouble((Cloud.SimToWorld + Show) * Hz) / Hz - Cloud.SimToWorld);
		}
		const float Phase = Cloud.CurTime > Cloud.PrevTime + 1e-5f ? FMath::Clamp((Show - Cloud.PrevTime) / (Cloud.CurTime - Cloud.PrevTime), 0.0f, 1.0f) : 1.0f;
		const FVector PrevCentre = Cloud.Origin + FVector(Cloud.PrevCentre);
		Data[0 * MaxDrawn + Index] = FLinearColor(Centre.X, Centre.Y, Centre.Z, Half);
		Data[1 * MaxDrawn + Index] = FLinearColor(float(RenderCells), Cloud.RenderCellCm, LifeFade * EvictFade * Cloud.FogIn, Cloud.Look.Extinction);
		Data[2 * MaxDrawn + Index] = FLinearColor(Key.R, Key.G, Key.B, Phase);
		// ponytail: os.Gas.Light 0 shows the default look's old blue-grey (the A/B); goes with the old light after the merge.
		const FLinearColor Albedo = !bNewLight && Cloud.Look.Colour == FOSGasLook().Colour ? FLinearColor(0.50f, 0.52f, 0.56f) : Cloud.Look.Colour;
		// The haze whitens as it ages (v12 7d): to os.Gas.ZoneAlbedo over its gas's 20 s.
		// v15c: and toward its fog's colour by the fog's share.
		const FLinearColor Shown = Cloud.bZone && bNewLight ? FMath::Lerp(FMath::Lerp(Albedo, FLinearColor(FVector3f(CVarGasZoneAlbedo.GetValueOnGameThread())), FMath::Clamp(Cloud.HazeAge / FMath::Max(CVarGasZoneAlbedoSeconds.GetValueOnGameThread(), 0.1f), 0.0f, 1.0f)), Cloud.FogColour, Cloud.FogShare) : Albedo;
		Data[3 * MaxDrawn + Index] = FLinearColor(Shown.R, Shown.G, Shown.B, Cloud.BirthWorld);
		// The haze's G is the shafts' light (v13 13c): no warm core read from it.
		const FLinearColor Warm = Cloud.Look.bEmbers && !(Cloud.bZone && CVarGasShafts.GetValueOnGameThread() > 0) ? Cloud.Look.WarmColour : FLinearColor::Black;
		Data[4 * MaxDrawn + Index] = FLinearColor(Warm.R, Warm.G, Warm.B, float(Cloud.CurHalf));
		Data[5 * MaxDrawn + Index] = Top;
		Data[6 * MaxDrawn + Index] = Bottom;
		Data[7 * MaxDrawn + Index] = FLinearColor(PrevCentre.X, PrevCentre.Y, PrevCentre.Z, 0.5f * RenderCells * Cloud.PrevCellCm);
		// 8: the previous atlas's cell; os.Gas.Light 1: the room's light x (the haze 0.5), the lamps' Henyey-Greenstein g
		// (the haze from 0.55 to 0.3 over its gas's 20 s), the march's samples at most (the haze 32).
		// v15c: the age is the smoke's only (a fog's parcels live for ever); the fog's share of the mass eased over 0.5 s.
		float HazeAge = 0.0f;
		if (Cloud.bZone && !Cloud.IsBusy() && !Cloud.Parcels.IsEmpty())
		{
			float Mass = 0.0f, FogMass = 0.0f;
			for (const FOSGasParcel& P : Cloud.Parcels)
			{
				if (P.bFog)
				{
					FogMass += P.Mass;
					continue;
				}
				HazeAge += P.Age * P.Mass;
				Mass += P.Mass;
			}
			Cloud.HazeAge = HazeAge / FMath::Max(Mass, 1e-4f);
			const float Target = CVarGasFogLook.GetValueOnGameThread() > 0 ? FogMass / FMath::Max(Mass + FogMass, 1e-4f) : 0.0f;
			const float Ease = 1.0f - FMath::Exp(-GetWorld()->GetDeltaSeconds() / 0.5f);
			Cloud.FogShare += (Target - Cloud.FogShare) * Ease;
			const FFogZone* Fog = Cloud.Zone.IsValid() ? FogZones.Find(Cloud.Zone.Get()) : nullptr;
			Cloud.FogColour += ((Fog && Fog->Fog != EOSZoneFog::None ? Fog->Colour : FLinearColor(0.88f, 0.88f, 0.88f)) - Cloud.FogColour) * Ease;
		}
		const float AgeShare = FMath::Clamp(Cloud.HazeAge / 20.0f, 0.0f, 1.0f);
		Data[8 * MaxDrawn + Index] = Cloud.bZone
			? FLinearColor(Cloud.PrevCellCm, FMath::Lerp(CVarGasZoneAmbient.GetValueOnGameThread(), CVarGasFogAmbient.GetValueOnGameThread(), Cloud.FogShare),
				FMath::Lerp(FMath::Lerp(0.55f, 0.3f, AgeShare), CVarGasFogG.GetValueOnGameThread(), Cloud.FogShare),
				// Negative: the shader marches the haze in two goes round the other clouds (os.Gas.FogSplit, v15d).
				float(FMath::Clamp(CVarGasHazeSamples.GetValueOnGameThread(), 8, 96)) * (CVarGasFogSplit.GetValueOnGameThread() > 0 && Cloud.FogShare > 0.01f ? -1.0f : 1.0f))
			: FLinearColor(Cloud.PrevCellCm, 1.0f, 0.55f, 96.0f);
		// 9-12: where each atlas holds gas (world): the march goes through these, not the whole boxes.
		const FVector O(Cloud.Origin);
		const FVector A0 = O + FVector(Cloud.CurOccLo), A1 = O + FVector(Cloud.CurOccHi), B0 = O + FVector(Cloud.PrevOccLo), B1 = O + FVector(Cloud.PrevOccHi);
		Data[9 * MaxDrawn + Index] = FLinearColor(A0.X, A0.Y, A0.Z, 0.0f);
		Data[10 * MaxDrawn + Index] = FLinearColor(A1.X, A1.Y, A1.Z, 0.0f);
		Data[11 * MaxDrawn + Index] = FLinearColor(B0.X, B0.Y, B0.Z, 0.0f);
		Data[12 * MaxDrawn + Index] = FLinearColor(B1.X, B1.Y, B1.Z, 0.0f);
		// 13-28 (os.Gas.Light 1), a lamp in 4 rows: its position (or the direction to a sun) + radius (0 a sun); its light
		// (colour x brightness, the renderer's units, x the crossfade x whether it sees the cloud - read every frame, not
		// eased: a flicker and the muzzle's flash reach the smoke) + falloff exponent (0 inverse square); the cone's axis +
		// cos outer (-2 none); cos inner + 1 for the key (its baked self-shadow; the others the ambient occlusion).
		const bool bTiledCloud = Cloud.bZone && CVarGasTileLamps.GetValueOnGameThread() > 0;
		const int32 TileWorld = bTiledCloud ? FMath::Clamp(CVarGasZoneLamps.GetValueOnGameThread(), 4, MaxZoneLamps) : 0;
		// 9.w: a haze's tile lamps - how many of the zone's follow its 2 player slots (0: the 4 slots as before).
		Data[9 * MaxDrawn + Index].A = float(TileWorld);
		for (int32 Lamp = 0; bNewLight && Lamp < (bTiledCloud ? MaxPlayerSlots + TileWorld : MaxLamps); ++Lamp)
		{
			const FCloud::FLampSlot& Slot = Cloud.Lamps[Lamp];
			const ULightComponent* L = Slot.Lamp.Get();
			if (!L || !L->IsVisible() || Slot.Weight <= 0.0f)
			{
				continue;
			}
			const FCloud::FLampSeen* Seen = Cloud.LampsSeen.Find(L);
			const FLinearColor Light = L->GetLightColor() * (L->ComputeLightBrightness() * Slot.Weight * (Seen ? Seen->Seen : 0.0f));
			const bool bSun = L->IsA<UDirectionalLightComponent>();
			const FVector At = bSun ? -L->GetDirection() : L->GetComponentLocation();
			const ULocalLightComponent* Local = Cast<ULocalLightComponent>(L);
			const UPointLightComponent* Point = Cast<UPointLightComponent>(L);
			FVector Axis = FVector::ZeroVector;
			float CosOuter = -2.0f, CosInner = -1.0f;
			OSGasLampCone(L, Axis, CosOuter, CosInner) ? void() : void(Axis = FVector::ZeroVector);
			const int32 Row = 13 + 4 * Lamp;
			const APawn* Player = GetWorld()->GetFirstPlayerController() ? GetWorld()->GetFirstPlayerController()->GetPawn() : nullptr;
			const AActor* Owner = L->GetOwner();
			const bool bPlayers = Player && Owner && (Owner == Player || Owner->GetAttachParentActor() == Player || Owner->GetOwner() == Player);
			// The muzzle's flash for the gas reaches the whole room's haze (v13 13b): its radius x os.Gas.FlashReach, the
			// same falloff's exponent - at the muzzle about as before, the far haze lit on the shot's frames.
			const bool bFlash = bPlayers && Point && !L->IsA<USpotLightComponent>() && L->bAffectsWorld;
			const float Radius = bSun || !Local ? 0.0f : Local->AttenuationRadius * (bFlash ? CVarGasFlashReach.GetValueOnGameThread() : 1.0f);
			Data[(Row + 0) * MaxDrawn + Index] = FLinearColor(At.X, At.Y, At.Z, Radius);
			Data[(Row + 1) * MaxDrawn + Index] = FLinearColor(Light.R, Light.G, Light.B, Point && !Point->bUseInverseSquaredFalloff ? Point->LightFalloffExponent : 0.0f);
			Data[(Row + 2) * MaxDrawn + Index] = FLinearColor(Axis.X, Axis.Y, Axis.Z, CosOuter);
			// z: the player's own lamp (by the camera: its shadow in the gas is the march's own transmittance).
			// w: the haze's shaft source (its shadow is the parcels' traced light in G).
			const bool bShaftLamp = Cloud.bZone && Cloud.bShafts && Cloud.ShaftLamp.Get() == L;
			Data[(Row + 3) * MaxDrawn + Index] = FLinearColor(CosInner, Slot.KeyShare, bPlayers ? 1.0f : 0.0f, bShaftLamp ? 1.0f : 0.0f);
		}
		if (BoundGrids[Index] != Cloud.Texture)
		{
			BoundGrids[Index] = Cloud.Texture;
			Post->SetTextureParameterValue(FName(*FString::Printf(TEXT("Grid%d"), Index)), Cloud.Texture);
		}
	}
	{
		uint8* Bytes = static_cast<uint8*>(FMemory::Malloc(Data.Num() * sizeof(FLinearColor)));
		FMemory::Memcpy(Bytes, Data.GetData(), Data.Num() * sizeof(FLinearColor));
		FUpdateTextureRegion2D* Region = new FUpdateTextureRegion2D(0, 0, 0, 0, MaxDrawn, Rows);
		DataTexture->UpdateTextureRegions(0, 1, Region, MaxDrawn * sizeof(FLinearColor), sizeof(FLinearColor), Bytes,
			[](uint8* Src, const FUpdateTextureRegion2D* Regions) { FMemory::Free(Src); delete Regions; });
	}
	Post->SetScalarParameterValue(TEXT("CloudCount"), float(Drawn.Num()));
	// Tunnels (world): three rows - start + radius, end + ragged, birth (world s). Each opens over 0.1 s (as the gas's
	// bite) and heals. No bodies: the gas pushes itself out of them (a sphere popping in and out as a pawn entered a
	// box was a jump).
	TArray<FLinearColor> TRows;
	TRows.SetNumZeroed(MaxTunnels * 3);
	int32 Count = 0;
	for (const FTunnel& T : Tunnels)
	{
		if (Count >= MaxTunnels)
		{
			break;
		}
		const float Slow = T.bVacuum ? 2.0f : 1.0f;
		const float Heal = FMath::SmoothStep(0.8f * Slow, 3.3f * Slow, T.Age);
		const float Ragged = FMath::SmoothStep(0.8f * Slow, (0.8f + 1.25f) * Slow, T.Age) * 0.75f;
		if (T.RadiusCm * (1.0f - Heal) < 0.3f)
		{
			continue;
		}
		const float Open = FMath::SmoothStep(0.0f, 0.1f, T.Age);
		TRows[Count] = FLinearColor(T.Start.X, T.Start.Y, T.Start.Z, T.RadiusCm * (1.0f - Heal) * Open);
		TRows[MaxTunnels + Count] = FLinearColor(T.End.X, T.End.Y, T.End.Z, Ragged);
		TRows[2 * MaxTunnels + Count] = FLinearColor(T.Birth, 0.0f, 0.0f, 0.0f);
		++Count;
	}
	{
		uint8* Bytes = static_cast<uint8*>(FMemory::Malloc(TRows.Num() * sizeof(FLinearColor)));
		FMemory::Memcpy(Bytes, TRows.GetData(), TRows.Num() * sizeof(FLinearColor));
		FUpdateTextureRegion2D* Region = new FUpdateTextureRegion2D(0, 0, 0, 0, MaxTunnels, 3);
		TunnelTexture->UpdateTextureRegions(0, 1, Region, MaxTunnels * sizeof(FLinearColor), sizeof(FLinearColor), Bytes,
			[](uint8* Src, const FUpdateTextureRegion2D* Regions) { FMemory::Free(Src); delete Regions; });
	}
	Post->SetScalarParameterValue(TEXT("TunnelCount"), float(Count));
	for (FCloud* Cloud : Drawn)
	{
		Cloud->TunnelCount = Count;
	}
	// The screen block: round(H / 270) px (4 at 1080p) - the same block at the muzzle and at 5 m.
	FVector2D View(1920.0, 1080.0);
	if (GEngine && GEngine->GameViewport)
	{
		GEngine->GameViewport->GetViewportSize(View);
	}
	const float BlockPx = FMath::Max(1.0f, FMath::RoundToFloat(float(View.Y) / FMath::Max(CVarGasBlockDiv.GetValueOnGameThread(), 1.0f)));
	Post->SetScalarParameterValue(TEXT("BlockPx"), BlockPx);
	Post->SetScalarParameterValue(TEXT("EyeClear"), EyeClearCm);
	// The screen-block pass (os.Gas.LowRes 1) gets the same; the material then draws nothing (no clouds, no tunnels).
	if (GasView.IsValid())
	{
		FOSGasRenderData Frame;
		const bool bLowRes = CVarGasLowRes.GetValueOnGameThread() > 0;
		for (int32 Index = 0; Index < FMath::Min(Drawn.Num(), FOSGasRenderData::MaxGrids); ++Index)
		{
			Frame.Grids[Index] = BoundGrids[Index] ? BoundGrids[Index]->GetResource() : nullptr;
		}
		Frame.Data = DataTexture->GetResource();
		Frame.Tunnels = TunnelTexture->GetResource();
		Frame.CloudCount = bLowRes ? Drawn.Num() : 0;
		Frame.TunnelCount = Count;
		// The heat puffs (shimmer) and the laser's beam (its glow in the gas).
		{
			TArray<FLinearColor> HRows;
			HRows.SetNumZeroed(MaxHeat * 2);
			int32 Heats = 0;
			for (const FHeatPuff& H : HeatPuffs)
			{
				if (H.Age < 0.0f)
				{
					continue;
				}
				const float Fade = FMath::Square(1.0f - H.Age / H.Life) * FMath::SmoothStep(0.0f, 0.05f, H.Age);
				HRows[Heats] = FLinearColor(H.P.X, H.P.Y, H.P.Z, H.RadiusCm);
				HRows[MaxHeat + Heats] = FLinearColor(H.Heat * Fade, 0.0f, 0.0f, 0.0f);
				++Heats;
			}
			uint8* Bytes = static_cast<uint8*>(FMemory::Malloc(HRows.Num() * sizeof(FLinearColor)));
			FMemory::Memcpy(Bytes, HRows.GetData(), HRows.Num() * sizeof(FLinearColor));
			FUpdateTextureRegion2D* Region = new FUpdateTextureRegion2D(0, 0, 0, 0, MaxHeat, 2);
			HeatTexture->UpdateTextureRegions(0, 1, Region, MaxHeat * sizeof(FLinearColor), sizeof(FLinearColor), Bytes,
				[](uint8* Src, const FUpdateTextureRegion2D* Regions) { FMemory::Free(Src); delete Regions; });
			Frame.Heat = HeatTexture->GetResource();
			Frame.HeatCount = CVarGasShimmer.GetValueOnGameThread() > 0.0f ? Heats : 0;
			Frame.Shimmer = CVarGasShimmer.GetValueOnGameThread();
		}
		if (const APlayerController* Controller = World->GetFirstPlayerController(); Controller && Controller->GetPawn())
		{
			FVector From, To;
			FLinearColor Light;
			if (const UOSLaserSightComponent* Laser = Controller->GetPawn()->FindComponentByClass<UOSLaserSightComponent>(); Laser && Laser->GetGasBeam(From, To, Light))
			{
				Frame.LaserFrom = FVector3f(From);
				Frame.LaserTo = FVector3f(To);
				Frame.LaserLight = Light;
			}
		}
		// The torch's shadow map (v13 13a).
		Frame.TorchShadow = TorchTexture ? TorchTexture->GetResource() : nullptr;
		Frame.TorchOn = Torch.bOn ? Torch.Strength : 0.0f;
		Frame.TorchPos = FVector3f(Torch.Pos);
		Frame.TorchAxis = FVector3f(Torch.Axis);
		Frame.TorchU = FVector3f(Torch.U);
		Frame.TorchV = FVector3f(Torch.V);
		Frame.TorchTan = Torch.TanOuter;
		Frame.TorchRes = float(Torch.Res);
		Frame.BlockPx = BlockPx;
		Frame.DitherPx = CVarGasDitherPx.GetValueOnGameThread() > 0.0f ? CVarGasDitherPx.GetValueOnGameThread() : FMath::Max(1.0f, FMath::RoundToFloat(BlockPx * 0.5f));
		Frame.SmoothAlpha = float(CVarGasSmoothAlpha.GetValueOnGameThread());
		Frame.DitherHold = CVarGasDitherHold.GetValueOnGameThread();
		Frame.TileSlots = float(FMath::Clamp(CVarGasTileSlots.GetValueOnGameThread(), 4, 6));
		Frame.DitherGrain = CVarGasDitherGrain.GetValueOnGameThread();
		Frame.EyeClear = EyeClearCm;
#if !UE_BUILD_SHIPPING
		Frame.DebugMode = float(CVarGasDebugAlpha.GetValueOnGameThread());
#endif
		Frame.Light = float(CVarGasLight.GetValueOnGameThread());
		Frame.AmbientScale = CVarGasAmbientScale.GetValueOnGameThread();
		Frame.LightScale = CVarGasLightScale.GetValueOnGameThread();
		Frame.Albedo = CVarGasAlbedo.GetValueOnGameThread();
		Frame.Forward = CVarGasForward.GetValueOnGameThread();
		Frame.Dither = float(CVarGasDither.GetValueOnGameThread());
		Frame.Bands = float(FMath::Max(1, CVarGasBands.GetValueOnGameThread()));
		Frame.DeltaSeconds = World->GetDeltaSeconds();
		// The view's turn this frame (the room's light history is screen space: shortened while turning).
		if (const APlayerController* Controller = World->GetFirstPlayerController(); Controller && Controller->PlayerCameraManager)
		{
			const FVector Forward = Controller->PlayerCameraManager->GetCameraRotation().Vector();
			const float TurnDeg = FMath::RadiansToDegrees(FMath::Acos(FMath::Clamp(float(Forward | LastViewForward), -1.0f, 1.0f)));
			LastViewForward = Forward;
			const float Per = CVarGasAmbientTurn.GetValueOnGameThread();
			Frame.AmbientSeconds = Per > 0.0f ? 0.1f / (1.0f + TurnDeg / FMath::Max(Frame.DeltaSeconds, 1e-4f) / Per) : 0.1f;
		}
		Frame.AmbientSeconds = CVarGasAmbientHistory.GetValueOnGameThread() > 0 ? Frame.AmbientSeconds : 0.0f;
		Frame.AmbientClamp = CVarGasAmbientClamp.GetValueOnGameThread() > 0 ? 1.0f : 0.0f;
		GasView->SetData(Frame);
		if (bLowRes)
		{
			Post->SetScalarParameterValue(TEXT("CloudCount"), 0.0f);
			Post->SetScalarParameterValue(TEXT("TunnelCount"), 0.0f);
		}
		// The material out of the frame altogether while the pass draws (it cost 0.1 ms even with nothing to march).
		if (APostProcessVolume* Volume = Cast<APostProcessVolume>(PostVolume); Volume && Volume->Settings.WeightedBlendables.Array.Num() > 0)
		{
			Volume->Settings.WeightedBlendables.Array[0].Weight = bLowRes ? 0.0f : 1.0f;
		}
	}
#if !UE_BUILD_SHIPPING
	Post->SetScalarParameterValue(TEXT("DebugAlpha"), CVarGasDebugAlpha.GetValueOnGameThread() > 0 ? 1.0f : 0.0f);
#endif
}


void UOSGasSubsystem::LaunchStep(FCloud& Cloud, int32 Steps, float Dt)
{
	// What came while the last step ran joins now (the game thread holds the cloud: no step in flight), and so do the
	// bodies and the commands (written to while the step ran they were a data race: pushes from half-written bodies).
	Cloud.Parcels.Append(MoveTemp(Cloud.Incoming));
	Cloud.Incoming.Reset();
	Cloud.Movers = Cloud.NextMovers;
	Cloud.Commands.Append(MoveTemp(Cloud.NextCommands));
	Cloud.NextCommands.Reset();
	if (Cloud.Parcels.IsEmpty())
	{
		return;
	}
	// Air or vacuum per parcel (v12 7c.4): an eighth of them each launch, round (the zones are the game thread's).
	{
		const int32 Num = Cloud.Parcels.Num();
		const int32 Share = FMath::Max(1, (Num + 7) / 8);
		for (int32 K = 0; K < Share && Num > 0; ++K)
		{
			Cloud.AirCursor = (Cloud.AirCursor + 1) % Num;
			FOSGasParcel& P = Cloud.Parcels[Cloud.AirCursor];
			P.AirTarget = OSAirZoneAt(GetWorld(), Cloud.Origin + FVector(P.P)) ? 1.0f : 0.0f;
		}
	}
	// A haze's shaft source (v13 13c): an eighth of its parcels traced to it each launch (a sun 200 m away, a lamp to 30 cm
	// short of it), the player and what they carry left out (walking through a shaft casts no shadow in the smoke).
	Cloud.bShafts = Cloud.bZone && CVarGasShafts.GetValueOnGameThread() > 0;
	if (const ULightComponent* Source = Cloud.ShaftLamp.Get(); Cloud.bShafts && Source && !Cloud.Parcels.IsEmpty())
	{
		FCollisionQueryParams Params(SCENE_QUERY_STAT(OSGasShafts), false);
		if (const APlayerController* Controller = GetWorld()->GetFirstPlayerController(); Controller && Controller->GetPawn())
		{
			Params.AddIgnoredActor(Controller->GetPawn());
			TArray<AActor*> Carried;
			Controller->GetPawn()->GetAttachedActors(Carried, true, true);
			Params.AddIgnoredActors(Carried);
		}
		Params.AddIgnoredActor(Source->GetOwner());
		const bool bSun = Source->IsA<UDirectionalLightComponent>();
		const int32 Num = Cloud.Parcels.Num();
		for (int32 K = 0; K < FMath::Max(1, (Num + 7) / 8); ++K)
		{
			Cloud.ShaftCursor = (Cloud.ShaftCursor + 1) % Num;
			FOSGasParcel& P = Cloud.Parcels[Cloud.ShaftCursor];
			const FVector From = Cloud.Origin + FVector(P.P);
			const FVector Way = bSun ? -Source->GetDirection() : (Source->GetComponentLocation() - From).GetSafeNormal();
			const FVector End = bSun ? From + Way * 20000.0 : Source->GetComponentLocation() - Way * 30.0;
			P.ShaftTarget = GetWorld()->LineTraceTestByChannel(From, End, ECC_Visibility, Params) ? 0.0f : 1.0f;
		}
	}
	// The snapshot Transmittance reads (the parcels are the step's until it is done).
	{
		Cloud.Snap.Reset();
		Cloud.SnapDensity.Reset();
		Cloud.SnapBox = FBox(ForceInit);
		const float MassScale = Cloud.bAir ? AirMassScale : VacuumMassScale;
		for (const FOSGasParcel& P : Cloud.Parcels)
		{
			const float Thin = ParcelThin(Cloud, P.RadiusCm);
			// The kernel's core density as the splat lays it, about half torn by the noise.
			const float Density = FMath::Min(P.Mass * Thin * MassScale, 0.95f) * DensityGain * 0.5f;
			if (Density < 1e-3f)
			{
				continue;
			}
			const FVector W = Cloud.Origin + FVector(P.P);
			Cloud.Snap.Add(FVector4f(FVector3f(W), P.RadiusCm));
			Cloud.SnapDensity.Add(Density);
			Cloud.SnapBox += FBox(W - FVector(P.RadiusCm), W + FVector(P.RadiusCm));
		}
	}
	FCloud* Ptr = &Cloud;
	Cloud.PackedTime = Cloud.SimTime;
	TArray<FAirFlow> FlowsNow;
	for (const FAirFlow& Flow : AirFlows)
	{
		if (FVector::Dist(Cloud.Origin + FVector(Cloud.DrawnCentre), Flow.Location) < Flow.ReachCm + 0.5f * RenderCells * Cloud.RenderCellCm)
		{
			FlowsNow.Add(Flow);
		}
	}
	Cloud.KeyDir.Normalize();
	UOSGasSubsystem* Self = this;
	Cloud.Pending = UE::Tasks::Launch(UE_SOURCE_LOCATION, [Ptr, Steps, Dt, FlowsNow, Self]()
	{
		const double T0 = FPlatformTime::Seconds();
		StepCloud(*Ptr, Steps, Dt, FlowsNow);
		const double Us = (FPlatformTime::Seconds() - T0) * 1e6;
		FScopeLock Lock(&Self->CostLock);
		Self->CostStepUs += Us;
		Self->CostMaxStepUs = FMath::Max(Self->CostMaxStepUs, Us);
		++Self->CostSteps;
	});
}

// --- the step (off the game thread) ----------------------------------------------------------

void UOSGasSubsystem::StepCloud(FCloud& Cloud, int32 Steps, float Dt, const TArray<FAirFlow>& FlowsNow)
{
	TArray<FOSGasParcel>& Parcels = Cloud.Parcels;
	const FVector3f Origin(Cloud.Origin);
	// The commands, a step at a time over 0.1 s: a tunnel eats the parcels in it in a ramp (the whole bite in one step
	// showed as a jump) and pushes them out over its first 3 steps; a blow throws them out the same way. Air flows
	// (a hole, a lock) pull at the step's own Dt (they were a push x DeltaTime x 4 a frame: another strength at
	// another frame rate).
	const auto ApplyCommands = [&Cloud, &Parcels, &FlowsNow, Dt]()
	{
		for (FCommand& C : Cloud.Commands)
		{
			if (C.StepsTotal == 0)
			{
				C.StepsTotal = C.StepsLeft = FMath::Max(1, FMath::RoundToInt(0.1f / Dt));
			}
			const int32 Done = C.StepsTotal - C.StepsLeft;
			const int32 PushSteps = FMath::Min(3, C.StepsTotal);
			const float PushShare = Done < PushSteps ? 1.0f / PushSteps : 0.0f;
			// The share Bite of the mass goes evenly over the steps: this step's factor is what is left after it over
			// what was left before it.
			const auto Ramp = [Done, &C](float Bite)
			{
				const float Before = 1.0f - Bite * Done / C.StepsTotal;
				const float After = 1.0f - Bite * (Done + 1) / C.StepsTotal;
				return Before > 1e-4f ? FMath::Clamp(After / Before, 0.0f, 1.0f) : 0.0f;
			};
			const FVector3f A(C.A - Cloud.Origin), B(C.B);
			for (FOSGasParcel& P : Parcels)
			{
				if (C.Kind == FCommand::EKind::Carve)
				{
					const FVector3f E(C.B - Cloud.Origin);
					const FVector3f Seg = E - A;
					const float T = FMath::Clamp(((P.P - A) | Seg) / FMath::Max(Seg.SizeSquared(), 1e-3f), 0.0f, 1.0f);
					const FVector3f Off = P.P - (A + Seg * T);
					const float Reach = C.RadiusCm + P.RadiusCm;
					const float D = Off.Size();
					if (D < Reach)
					{
						const float Overlap = 1.0f - D / Reach;
						P.Mass *= Ramp(Overlap);
						P.V += Off.GetSafeNormal(UE_SMALL_NUMBER, FVector3f::UpVector) * (120.0f * Overlap * PushShare);
					}
				}
				else
				{
					const FVector3f Off = P.P - A;
					const float D = Off.Size();
					const float Reach = FMath::Min(C.RadiusCm, 60.0f);
					const FVector3f Normal = B.GetSafeNormal();
					// Only the hemisphere in front of the surface (the side the blow comes out of), falling off with distance.
					if (D < Reach && (C.SpeedCm < 0.0f || (Off | Normal) >= -0.2f * D))
					{
						const float Fall = 1.0f - D / Reach;
						const FVector3f Out = Off.GetSafeNormal(UE_SMALL_NUMBER, Normal);
						P.V += Out * (C.SpeedCm * Fall * PushShare);
						// A blow thins a shot's gas; the haze is only pushed (v14: a new shot shoves the old haze aside).
						if (C.SpeedCm > 0.0f && !Cloud.bZone)
						{
							P.Mass *= Ramp(0.4f * Fall);
						}
					}
				}
			}
			--C.StepsLeft;
		}
		Cloud.Commands.RemoveAll([](const FCommand& C) { return C.StepsLeft <= 0; });
		for (const FAirFlow& Flow : FlowsNow)
		{
			const FVector3f At(Flow.Location - Cloud.Origin);
			for (FOSGasParcel& P : Parcels)
			{
				const FVector3f Off = P.P - At;
				const float D = Off.Size();
				if (D < Flow.ReachCm)
				{
					P.V -= Off.GetSafeNormal(UE_SMALL_NUMBER, -FVector3f(Flow.Direction)) * (Flow.SpeedCm * 4.0f * Dt * (1.0f - D / Flow.ReachCm));
				}
			}
		}
	};
	if (Cloud.bZone && Parcels.Num() > 1)
	{
		// The haze: parcels within 8 cm of each other become one (mass summed, radius by volume) - only those fully in.
		const double Now = Cloud.SimToWorld + Cloud.SimTime;
		const auto Settled = [Now](const FOSGasParcel& P) { return P.HandIn < 0.0f || Now >= P.HandIn + HandRamp + 0.5; };
		const float HazeRadius = Cloud.HazeRadius;
		const auto Merge = [HazeRadius](FOSGasParcel& A, const FOSGasParcel& B)
		{
			const float M = FMath::Max(A.Mass + B.Mass, 1e-6f);
			A.P = (A.P * A.Mass + B.P * B.Mass) / M;
			A.V = (A.V * A.Mass + B.V * B.Mass) / M;
			A.Birth = (A.Birth * A.Mass + B.Birth * B.Mass) / M;
			A.Warm = (A.Warm * A.Mass + B.Warm * B.Mass) / M;
			A.Flat = (A.Flat * A.Mass + B.Flat * B.Mass) / M;
			// By volume, never below the larger of the two, up to the haze's own reach (v14: two 60 cm parcels became one of
			// 20 cm with twice the mass - the core's density x9 in a step).
			A.MaxRadiusCm = FMath::Max3(A.MaxRadiusCm, B.MaxRadiusCm, HazeRadius);
			A.RadiusCm = FMath::Max(FMath::Max(A.RadiusCm, B.RadiusCm), FMath::Min(FMath::Pow(FMath::Cube(A.RadiusCm) + FMath::Cube(B.RadiusCm), 1.0f / 3.0f), A.MaxRadiusCm));
			if (B.Mass > A.Mass)
			{
				A.StretchAxis = B.StretchAxis;
				A.Stretch = B.Stretch;
			}
			A.Mass = M;
			A.Base += B.Base;
			A.bFog = A.bFog || B.bFog;
			A.HandIn = -1.0f;
		};
		TMap<FIntVector, int32> Cells;
		for (int32 I = Parcels.Num() - 1; I >= 0; --I)
		{
			if (!Settled(Parcels[I]) || Parcels[I].Dissolve > 0.0f)
			{
				continue;
			}
			const FIntVector Key(FMath::FloorToInt(Parcels[I].P.X / 8.0f), FMath::FloorToInt(Parcels[I].P.Y / 8.0f), FMath::FloorToInt(Parcels[I].P.Z / 8.0f));
			if (int32* Other = Cells.Find(Key); Other && FVector3f::DistSquared(Parcels[*Other].P, Parcels[I].P) < 64.0f)
			{
				Merge(Parcels[*Other], Parcels[I]);
				Parcels.RemoveAtSwap(I, EAllowShrinking::No);
				// The swapped-in last parcel's cell entry pointed past the end: re-point it.
				for (auto& Pair : Cells)
				{
					Pair.Value = Pair.Value == Parcels.Num() ? I : Pair.Value;
				}
			}
			else
			{
				Cells.Add(Key, I);
			}
		}
		// Still too many: the lightest settled one into its nearest - only one within twice its radius (a merge across the
		// room teleported its mass), else it waits a step.
		const auto Nearest = [&Parcels](int32 Of)
		{
			int32 Near = INDEX_NONE;
			float Best = 1e30f;
			for (int32 I = 0; I < Parcels.Num(); ++I)
			{
				const float D2 = I == Of || Parcels[I].Dissolve > 0.0f ? 1e30f : FVector3f::DistSquared(Parcels[I].P, Parcels[Of].P);
				if (D2 < Best)
				{
					Best = D2;
					Near = I;
				}
			}
			return TPair<int32, float>(Near, Best);
		};
		TSet<int32> Waiting;
		while (Parcels.Num() > MaxZoneParcels)
		{
			int32 Light = INDEX_NONE;
			for (int32 I = 0; I < Parcels.Num(); ++I)
			{
				Light = Settled(Parcels[I]) && Parcels[I].Dissolve <= 0.0f && !Waiting.Contains(I) && (Light == INDEX_NONE || Parcels[I].Mass < Parcels[Light].Mass) ? I : Light;
			}
			if (Light == INDEX_NONE)
			{
				break;
			}
			const TPair<int32, float> Near = Nearest(Light);
			if (Near.Key == INDEX_NONE || Near.Value > FMath::Square(2.0f * Parcels[Light].RadiusCm))
			{
				Waiting.Add(Light);
				continue;
			}
			Merge(Parcels[Near.Key], Parcels[Light]);
			Parcels.RemoveAtSwap(Light, EAllowShrinking::No);
			Waiting.Reset();
		}
		// A hard cap: past it the lightest dissolve - their mass goes into their nearest neighbour over 1 s, no jump.
		int32 Going = 0;
		for (const FOSGasParcel& P : Parcels)
		{
			Going += P.Dissolve > 0.0f ? 1 : 0;
		}
		while (Parcels.Num() - Going > HardZoneParcels)
		{
			int32 Light = INDEX_NONE;
			for (int32 I = 0; I < Parcels.Num(); ++I)
			{
				Light = Settled(Parcels[I]) && Parcels[I].Dissolve <= 0.0f && (Light == INDEX_NONE || Parcels[I].Mass < Parcels[Light].Mass) ? I : Light;
			}
			if (Light == INDEX_NONE)
			{
				break;
			}
			Parcels[Light].Dissolve = FMath::Max(Parcels[Light].Mass, 1e-4f);
			++Going;
		}
		const float StepsDt = Steps * Dt;
		for (int32 I = 0; I < Parcels.Num() && Going > 0; ++I)
		{
			FOSGasParcel& P = Parcels[I];
			if (P.Dissolve <= 0.0f)
			{
				continue;
			}
			const int32 Near = Nearest(I).Key;
			const float Share = FMath::Min(P.Dissolve * StepsDt, P.Mass);
			const float BaseShare = P.Mass > 1e-6f ? P.Base * Share / P.Mass : P.Base;
			if (Near != INDEX_NONE)
			{
				Parcels[Near].Mass += Share;
				Parcels[Near].Base += BaseShare;
			}
			P.Mass -= Share;
			P.Base = FMath::Max(0.0f, P.Base - BaseShare);
		}
	}
	const float GScale = Cloud.Gravity.Size() / 980.0f;
	const FVector3f Down = FVector3f(Cloud.Gravity.GetSafeNormal());
	const FOSSmokeParams K = GridParams();
	FOSSmokeSim& G = *Cloud.Grid;
	const int32 N = G.N;
	// The haze spreads as a layer (v14c): cooled smoke in a layered room flows out thin - the thick runs to the thin, its
	// front slows as it thins. A flat grid across gravity over the zone's box, a layer each for the haze and the fog (the
	// ceiling's and the floor's mass do not push each other): the parcels' mass in air, blurred twice, over its maximum
	// eased over 2 s (a new shot does not jump it); a parcel goes down that thickness's slope, across gravity, at most S.
	const float SlumpS = Cloud.bZone && GScale >= 0.05f ? Cloud.HazeSlump * FMath::Min(1.0f, GScale) : 0.0f;
	FVector3f SlumpT1 = FVector3f::ZeroVector, SlumpT2 = FVector3f::ZeroVector;
	float SlumpU0 = 0.0f, SlumpV0 = 0.0f, SlumpCs = 50.0f;
	int32 SlumpNx = 0, SlumpNy = 0;
	TArray<float> SlumpGx[2], SlumpGy[2];
	if (SlumpS > 0.0f && !Parcels.IsEmpty())
	{
		SlumpT1 = FVector3f::CrossProduct(Down, FMath::Abs(Down.Z) < 0.9f ? FVector3f::UnitZ() : FVector3f::UnitX()).GetSafeNormal();
		SlumpT2 = FVector3f::CrossProduct(Down, SlumpT1);
		FVector3f Lo3(1e9f), Hi3(-1e9f);
		if (Cloud.bUseRoom && Cloud.Room.IsValid())
		{
			Lo3 = FVector3f(Cloud.Room->Lo) - Origin;
			Hi3 = Lo3 + FVector3f(Cloud.Room->Dim) * Cloud.Room->Cell;
		}
		else
		{
			for (const FOSGasParcel& P : Parcels)
			{
				Lo3 = Lo3.ComponentMin(P.P);
				Hi3 = Hi3.ComponentMax(P.P);
			}
			Lo3 -= FVector3f(200.0f);
			Hi3 += FVector3f(200.0f);
		}
		float U1 = -1e9f, V1 = -1e9f;
		SlumpU0 = SlumpV0 = 1e9f;
		for (int32 Corner = 0; Corner < 8; ++Corner)
		{
			const FVector3f C(Corner & 1 ? Hi3.X : Lo3.X, Corner & 2 ? Hi3.Y : Lo3.Y, Corner & 4 ? Hi3.Z : Lo3.Z);
			SlumpU0 = FMath::Min(SlumpU0, C | SlumpT1);
			SlumpV0 = FMath::Min(SlumpV0, C | SlumpT2);
			U1 = FMath::Max(U1, C | SlumpT1);
			V1 = FMath::Max(V1, C | SlumpT2);
		}
		SlumpCs = FMath::Max(50.0f, FMath::Max(U1 - SlumpU0, V1 - SlumpV0) / 64.0f);
		SlumpNx = FMath::Clamp(FMath::CeilToInt((U1 - SlumpU0) / SlumpCs) + 1, 2, 64);
		SlumpNy = FMath::Clamp(FMath::CeilToInt((V1 - SlumpV0) / SlumpCs) + 1, 2, 64);
		const int32 Cells = SlumpNx * SlumpNy;
		TArray<float> Hh[2];
		for (int32 Layer = 0; Layer < 2; ++Layer)
		{
			Hh[Layer].SetNumZeroed(Cells);
		}
		for (const FOSGasParcel& P : Parcels)
		{
			if (P.Air <= 0.5f)
			{
				continue;
			}
			const float Fx = FMath::Clamp(((P.P | SlumpT1) - SlumpU0) / SlumpCs - 0.5f, 0.0f, SlumpNx - 1.001f);
			const float Fy = FMath::Clamp(((P.P | SlumpT2) - SlumpV0) / SlumpCs - 0.5f, 0.0f, SlumpNy - 1.001f);
			const int32 X = int32(Fx), Y = int32(Fy);
			const float Ax = Fx - X, Ay = Fy - Y;
			TArray<float>& Hl = Hh[P.bFog ? 1 : 0];
			Hl[Y * SlumpNx + X] += P.Mass * (1.0f - Ax) * (1.0f - Ay);
			Hl[Y * SlumpNx + X + 1] += P.Mass * Ax * (1.0f - Ay);
			Hl[(Y + 1) * SlumpNx + X] += P.Mass * (1.0f - Ax) * Ay;
			Hl[(Y + 1) * SlumpNx + X + 1] += P.Mass * Ax * Ay;
		}
		TArray<float> Tmp;
		Tmp.SetNumUninitialized(Cells);
		for (int32 Layer = 0; Layer < 2; ++Layer)
		{
			TArray<float>& Hl = Hh[Layer];
			// Two 3x3 blurs (1 2 1 each way), the edges clamped.
			for (int32 Pass = 0; Pass < 2; ++Pass)
			{
				for (int32 Y = 0; Y < SlumpNy; ++Y)
				{
					for (int32 X = 0; X < SlumpNx; ++X)
					{
						const int32 Xm = FMath::Max(X - 1, 0), Xp = FMath::Min(X + 1, SlumpNx - 1);
						Tmp[Y * SlumpNx + X] = 0.25f * (Hl[Y * SlumpNx + Xm] + 2.0f * Hl[Y * SlumpNx + X] + Hl[Y * SlumpNx + Xp]);
					}
				}
				for (int32 Y = 0; Y < SlumpNy; ++Y)
				{
					const int32 Ym = FMath::Max(Y - 1, 0), Yp = FMath::Min(Y + 1, SlumpNy - 1);
					for (int32 X = 0; X < SlumpNx; ++X)
					{
						Hl[Y * SlumpNx + X] = 0.25f * (Tmp[Ym * SlumpNx + X] + 2.0f * Tmp[Y * SlumpNx + X] + Tmp[Yp * SlumpNx + X]);
					}
				}
			}
			float Max = 0.0f;
			for (const float V : Hl)
			{
				Max = FMath::Max(Max, V);
			}
			float& Ref = Cloud.SlumpRef[Layer];
			Ref = Ref <= 0.0f ? Max : Ref + (Max - Ref) * (1.0f - FMath::Exp(-Steps * Dt / 2.0f));
			// The slope of h / HRef per cm, a central difference (one-sided at the edges).
			SlumpGx[Layer].SetNumZeroed(Cells);
			SlumpGy[Layer].SetNumZeroed(Cells);
			if (Ref > 1e-6f)
			{
				const float Scale = 1.0f / (Ref * SlumpCs);
				for (int32 Y = 0; Y < SlumpNy; ++Y)
				{
					const int32 Ym = FMath::Max(Y - 1, 0), Yp = FMath::Min(Y + 1, SlumpNy - 1);
					for (int32 X = 0; X < SlumpNx; ++X)
					{
						const int32 Xm = FMath::Max(X - 1, 0), Xp = FMath::Min(X + 1, SlumpNx - 1);
						SlumpGx[Layer][Y * SlumpNx + X] = (Hl[Y * SlumpNx + Xp] - Hl[Y * SlumpNx + Xm]) * Scale / FMath::Max(Xp - Xm, 1);
						SlumpGy[Layer][Y * SlumpNx + X] = (Hl[Yp * SlumpNx + X] - Hl[Ym * SlumpNx + X]) * Scale / FMath::Max(Yp - Ym, 1);
					}
				}
			}
		}
	}
	const auto SlumpAt = [&](const FOSGasParcel& P) -> FVector3f
	{
		if (SlumpNx == 0)
		{
			return FVector3f::ZeroVector;
		}
		const float Fx = FMath::Clamp(((P.P | SlumpT1) - SlumpU0) / SlumpCs - 0.5f, 0.0f, SlumpNx - 1.001f);
		const float Fy = FMath::Clamp(((P.P | SlumpT2) - SlumpV0) / SlumpCs - 0.5f, 0.0f, SlumpNy - 1.001f);
		const int32 X = int32(Fx), Y = int32(Fy);
		const float Ax = Fx - X, Ay = Fy - Y;
		const int32 Layer = P.bFog ? 1 : 0;
		const auto Bi = [&](const TArray<float>& A)
		{
			return FMath::Lerp(FMath::Lerp(A[Y * SlumpNx + X], A[Y * SlumpNx + X + 1], Ax), FMath::Lerp(A[(Y + 1) * SlumpNx + X], A[(Y + 1) * SlumpNx + X + 1], Ax), Ay);
		};
		FVector2f V = -SlumpS * 100.0f * FVector2f(Bi(SlumpGx[Layer]), Bi(SlumpGy[Layer]));
		const float Len = V.Size();
		V *= Len > SlumpS ? SlumpS / Len : 1.0f;
		return SlumpT1 * V.X + SlumpT2 * V.Y;
	};
	// The kernels' squash eases to its target over 6 s (in zero-G back to a ball).
	const float FlatTarget = GScale >= 0.05f ? Cloud.HazeFlat : 1.0f;
	TArray<float> OldX, OldY, OldZ;
	for (int32 Step = 0; Step < Steps; ++Step)
	{
		++Cloud.StepIndex;
		ApplyCommands();
		FVector3f GridCentre = FVector3f::ZeroVector;
		{
			// The grid's cell follows the cloud's size (20 cells over it, 6 cm at least): a vacuum cone metres long keeps
			// its vortices; parcels beyond it keep their own velocity.
			FVector3f Lo3(1e9f), Hi3(-1e9f);
			for (const FOSGasParcel& P : Parcels)
			{
				Lo3 = Lo3.ComponentMin(P.P);
				Hi3 = Hi3.ComponentMax(P.P);
			}
			// In air a fixed 12 cm cell (a cell changing with the spread resampled the flow every step); in a vacuum it
			// follows the cone's length.
			G.CellCm = Cloud.bAir ? 12.0f : FMath::Clamp((Hi3 - Lo3).GetMax() / (VelocityCells - 2), VelocityCellCm, 40.0f);
		}
		const float GridCell = G.CellCm;
		if (!Cloud.bZone)
		{
			// The velocity grid round the gas (whole cells): the parcels' momentum, mass and heat onto it.
			float Mass = 0.0f;
			for (const FOSGasParcel& P : Parcels)
			{
				GridCentre += P.P * P.Mass;
				Mass += P.Mass;
			}
			GridCentre = SnapTo(GridCentre / FMath::Max(Mass, 1e-3f), GridCell);
			// Anchored: the grid moves only when the gas's mass centre leaves 3 cells round it, by whole cells.
			if (Cloud.bAir && Cloud.bGridAnchored && (GridCentre - Cloud.GridAnchor).GetAbsMax() <= 3.0f * GridCell)
			{
				GridCentre = Cloud.GridAnchor;
			}
			Cloud.GridAnchor = GridCentre;
			Cloud.bGridAnchored = true;
			const FVector3f GridWorld = Origin + GridCentre;
			G.Planes.Reset();
			const FBox GridBox = FBox::BuildAABB(FVector(GridWorld), FVector(0.5f * N * GridCell));
			for (int32 I = 0; I < Cloud.Planes.Num(); ++I)
			{
				const FPlane& Plane = Cloud.Planes[I];
				if (!Cloud.PlaneBoxes[I].Intersect(GridBox))
				{
					continue;
				}
				const FVector Normal(Plane);
				G.Planes.Emplace(FVector3f(Normal * Plane.W - FVector(GridWorld)), FVector3f(Normal));
			}
			G.RebuildSolids();
			G.Movers = Cloud.Movers;
			for (FOSSmokeSim::FMover& M : G.Movers)
			{
				M.Centre -= GridCentre;
			}
			const int32 Cells = N * N * N;
			for (TArray<float>* F : { &G.Rho, &G.Heat, &G.Ux, &G.Uy, &G.Uz })
			{
				FMemory::Memzero(F->GetData(), Cells * sizeof(float));
			}
			for (const FOSGasParcel& P : Parcels)
			{
				const FVector3f C = (P.P - GridCentre) / GridCell + FVector3f(0.5f * N - 0.5f);
				const int32 X = FMath::FloorToInt(C.X), Y = FMath::FloorToInt(C.Y), Z = FMath::FloorToInt(C.Z);
				const FVector3f F = C - FVector3f(X, Y, Z);
				for (int32 K3 = 0; K3 < 8; ++K3)
				{
					const int32 DX = K3 & 1, DY = (K3 >> 1) & 1, DZ = K3 >> 2;
					const int32 CX = X + DX, CY = Y + DY, CZ = Z + DZ;
					if (CX < 0 || CY < 0 || CZ < 0 || CX >= N || CY >= N || CZ >= N)
					{
						continue;
					}
					const float Wt = (DX ? F.X : 1 - F.X) * (DY ? F.Y : 1 - F.Y) * (DZ ? F.Z : 1 - F.Z) * P.Mass;
					const int32 I = G.Index(CX, CY, CZ);
					G.Rho[I] += Wt;
					G.Heat[I] += Wt * P.Heat;
					G.Ux[I] += Wt * P.V.X;
					G.Uy[I] += Wt * P.V.Y;
					G.Uz[I] += Wt * P.V.Z;
				}
			}
			for (int32 I = 0; I < Cells; ++I)
			{
				if (G.Rho[I] > 1e-4f)
				{
					const float Inv = 1.0f / G.Rho[I];
					G.Ux[I] *= Inv;
					G.Uy[I] *= Inv;
					G.Uz[I] *= Inv;
					G.Heat[I] *= Inv;
					G.Rho[I] = FMath::Min(G.Rho[I], 1.0f);
				}
			}
			OldX = G.Ux;
			OldY = G.Uy;
			OldZ = G.Uz;
			G.bAir = Cloud.bAir;
			G.StepGrid(Dt, K, GScale);
			// The grid back onto the parcels: FLIP (the change) mostly, PIC (the grid's own) the rest; and a drag toward
			// the flow by the parcel's size.
			for (FOSGasParcel& P : Parcels)
			{
				const FVector3f C = (P.P - GridCentre) / GridCell + FVector3f(0.5f * N - 0.5f);
				if (C.GetMin() < 0.0f || C.GetMax() > N - 1.0f || P.Air < 0.5f)
				{
					continue;
				}
				const FVector3f New(G.Sample(G.Ux, C), G.Sample(G.Uy, C), G.Sample(G.Uz, C));
				const FVector3f Old(G.Sample(OldX, C), G.Sample(OldY, C), G.Sample(OldZ, C));
				// The ring keeps its spin its first 0.3 s: all FLIP, no drag.
				const bool bYoung = P.Age < 0.3f;
				const float Share = bYoung ? 1.0f : Flip;
				if (Cloud.bAir && P.Age > 0.5f)
				{
					// Old gas in air takes a little of the flow (PIC, 15% a step), not the young jet's changes (FLIP
					// handed the jet's push to all of it: the whole cloud went in one piece, В«РјР°РіРЅРёС‚РёС‚В»).
					P.V += (New - P.V) * 0.15f;
				}
				else
				{
					P.V = (P.V + New - Old) * Share + New * (1.0f - Share);
				}
				// One drag in air: the grid's (tau 0.6 s); none of the parcel's own (it stopped the ring dead, В«С‚РѕСЂРјРѕР·РёС‚В»).
				const float Hold = 0.0f;
				P.V += (New - P.V) * Hold;
			}
		}
		// Motion; a clinging parcel keeps its carrier's velocity plus its own throw.
		for (int32 Index = Parcels.Num() - 1; Index >= 0; --Index)
		{
			FOSGasParcel& P = Parcels[Index];
			P.Age += Dt;
			if (P.Cling > 0.0f)
			{
				// Its own drag brakes the throw, not the carrier (a hole's pop stops by the shroud and rides on with the
				// gun; drag on the whole velocity below would stop it dead in the world).
				P.Cling -= Dt;
				if (P.DragSeconds > 0.0f)
				{
					P.Throw *= FMath::Exp(-Dt / P.DragSeconds);
				}
				P.V = P.Carrier + P.Throw;
			}
			const FVector3f Before = P.P;
			// Air or vacuum where it is now, eased over 0.2 s at a zone's border (v12 7c.4).
			P.Air = FMath::FInterpConstantTo(P.Air, P.AirTarget, Dt, 1.0f / 0.2f);
			P.Shaft = FMath::FInterpConstantTo(P.Shaft, P.ShaftTarget, Dt, 1.0f / 0.25f);
			if (P.Air > 0.0f)
			{
				// The gun's velocity it inherited goes into the air in ~0.2 s (a shot on the walk stops with the walker);
				// in a vacuum it keeps it.
				if (P.Cling <= 0.0f)
				{
					const FVector3f Lost = P.Carrier * ((1.0f - FMath::Exp(-Dt / P.CarrierTau)) * P.Air);
					P.Carrier -= Lost;
					P.V -= Lost;
				}
				// Old gas relaxes to the room's air (it moved forever otherwise); the young keep their ring. The room's
				// air is not still (v12 7a.3): its slow drift takes the haze wholly, a shot's gas from 1.5 s over a
				// second; warm gas rises 2-5 cm/s after its first second (7a.5) - the haze gathers under the ceiling.
				if (P.Age > 0.3f)
				{
					FVector3f Target = FVector3f::ZeroVector;
					const float DriftShare = Cloud.bZone ? 1.0f : FMath::SmoothStep(1.5f, 2.5f, P.Age);
					if (DriftShare > 0.0f && Cloud.DriftSeed != 0u)
					{
						FVector3f Drift, Grad[3];
						// The haze rides the room's own slow currents (v14b: room-sized, 6 cm/s - carried across the room in
						// about a minute); a shot's gas the room's drift as before.
						if (Cloud.bZone)
						{
							DriftAt(Origin + P.P, float(Cloud.SimToWorld + Cloud.SimTime), Cloud.DriftSeed, Cloud.HazeDriftCm, Drift, Grad, Cloud.HazeDriftScale);
							// The currents' vertical spent on mixing the height (v14c: x HazeDriftZ in gravity); a floor fog's
							// layer stays down (v15: the least of that and its stir).
							const float DriftZ = P.bFog && Cloud.bFogFloor ? FMath::Min(Cloud.HazeDriftZ, Cloud.FogStir) : GScale >= 0.05f ? Cloud.HazeDriftZ : 1.0f;
							Drift -= Down * ((Drift | Down) * (1.0f - DriftZ));
						}
						else
						{
							DriftAt(Origin + P.P, float(Cloud.SimToWorld + Cloud.SimTime), Cloud.DriftSeed, Cloud.DriftCm, Drift, Grad);
						}
						Target += Drift * DriftShare;
						if (Cloud.bZone)
						{
							// The strand: stretched along the flow's strain (its axis turns into it), volume kept, relaxing.
							const FVector3f GA(Grad[0] | P.StretchAxis, Grad[1] | P.StretchAxis, Grad[2] | P.StretchAxis);
							const float Rate = GA | P.StretchAxis;
							P.StretchAxis = (P.StretchAxis + (GA - P.StretchAxis * Rate) * Dt).GetSafeNormal(UE_SMALL_NUMBER, P.StretchAxis);
							P.Stretch = FMath::Clamp(1.0f + (P.Stretch * FMath::Exp(Rate * Dt) - 1.0f) * FMath::Exp(-Dt / StretchSeconds), 1.0f, MaxStretch);
						}
					}
					// Warm gas rises as a thermal (owner 24.10: it hung): mixing keeps its buoyancy, so it slows as 1/sqrt(t) and
					// rises till the ceiling; eased in after the muzzle's jet (0.3-0.8 s). By the parcel's age: the haze's copy
					// rises on at the hand-over. os.Gas.Rise 0: the residual warmth's lift from 1 s, as before.
					if (Cloud.Rise > 0.0f && P.Age > 0.3f)
					{
						const float Thermal = Cloud.Rise * FMath::Sqrt(1.0f / FMath::Max(P.Age, 1.0f)) * FMath::SmoothStep(0.3f, 0.8f, P.Age);
						Target -= Down * (FMath::Max(Cloud.WarmLift, Thermal) * P.Warm * GScale);
					}
					else if (Cloud.Rise <= 0.0f && P.Age > 1.0f)
					{
						Target -= Down * (Cloud.WarmLift * P.Warm * GScale);
					}
					// The haze's own wander (v14): an Ornstein-Uhlenbeck velocity per parcel - sigma^2 = 2 D t past its eddy
					// time - the haze spreads and thins through the room instead of sitting where it was. Exact for any step,
					// its draws by the step's number (the same at any frame rate); vertical damped by the stable layering,
					// isotropic and slower in zero-G.
					if (Cloud.bZone && Cloud.HazeDiffuse > 0.0f)
					{
						const bool bZeroG = GScale < 0.05f;
						const float D = Cloud.HazeDiffuse * (bZeroG ? Cloud.HazeZeroG : 1.0f);
						const float Across = FMath::Sqrt(D / Cloud.HazeEddy);
						const float Up = bZeroG ? Across : FMath::Sqrt(D * Cloud.HazeDiffuseZ / Cloud.HazeEddy);
						const float Keep = FMath::Exp(-Dt / Cloud.HazeEddy);
						const float Kick = FMath::Sqrt(FMath::Max(0.0f, 1.0f - Keep * Keep));
						FVector3f Gauss;
						for (int32 Axis = 0; Axis < 3; ++Axis)
						{
							const float U1 = (float(Hash3(int32(P.Seed), int32(Cloud.StepIndex), Axis * 2, 0x5EEDu)) + 1.0f) / 4294967297.0f;
							const float U2 = float(Hash3(int32(P.Seed), int32(Cloud.StepIndex), Axis * 2 + 1, 0x5EEDu)) / 4294967296.0f;
							Gauss[Axis] = FMath::Sqrt(-2.0f * FMath::Loge(U1)) * FMath::Cos(UE_TWO_PI * U2);
						}
						const FVector3f GUp = Down * (Gauss | Down);
						P.Wander = P.Wander * Keep + ((Gauss - GUp) * Across + GUp * Up) * Kick;
						// Neighbours wander together (v14b): most of the wander is a world-anchored eddy field (80 cm, its
						// eddy time) so the haze stretches into strands and sleeves instead of breaking into round puffs; a
						// share is each parcel's own.
						FVector3f Eddy, EddyGrad[3];
						DriftAt(Origin + P.P, float(Cloud.SimToWorld + Cloud.SimTime), Cloud.DriftSeed ^ 0x9E3779B9u, 2.0f * Across, Eddy, EddyGrad, Cloud.HazeEddyCm, Cloud.HazeEddy);
						const FVector3f EddyUp = Down * (Eddy | Down);
						Eddy = (Eddy - EddyUp) + EddyUp * (Up / FMath::Max(Across, 1e-3f));
						FVector3f Wanders = P.Wander * Cloud.HazeOwn + Eddy * (1.0f - Cloud.HazeOwn);
						if (P.bFog && Cloud.bFogFloor)
						{
							Wanders -= Down * ((Wanders | Down) * (1.0f - Cloud.FogStir));
						}
						Target += Wanders;
					}
					if (Cloud.bZone)
					{
						Target += SlumpAt(P);
						P.Flat += (FlatTarget - P.Flat) * (1.0f - FMath::Exp(-Dt / 6.0f));
					}
					P.V = Target + (P.V - Target) * FMath::Exp(-Dt * P.Air / AirRestSeconds);
				}
				if (P.Age > 1.0f)
				{
					P.Warm *= FMath::Exp(-Dt / WarmSeconds);
				}

			}
			// Heavy matter settles (dust): in air against the drag above, in a vacuum freely; in zero-G |g| = 0 holds it.
			// Heavy matter settles, steam (negative) rises - against the air's rest: a terminal speed of Fall x 0.7 s.
			if (P.Fall != 0.0f)
			{
				P.V += Down * (P.Fall * GScale * Dt);
			}
			// Its own drag, in a vacuum too (a hole's pop: 450 cm/s stopped within ~12 cm - without it a vacuum threw the
			// 16 holes' puffs across the view as blobs).
			if (P.DragSeconds > 0.0f && P.Cling <= 0.0f)
			{
				P.V *= FMath::Exp(-Dt / P.DragSeconds);
			}
			const float Speed = P.V.Size();
			P.P += P.V * Dt;
			// The haze diffuses on (to 60 cm over ~15 s): a veil, not lumps (merged parcels at 20 cm read as dark blots, owner 24.10).
			// A haze parcel spreads as the haze does (v14: parcels wandering apart at a fixed 60 cm read as round puffs):
			// r^2 grows by 2 x share x D a second, to os.Gas.HazeRadiusMax; its density keeps its matter (ParcelThin).
			// A fog's parcel keeps its own size (v15: a 2.5 m kernel would erase a floor layer and a steam tongue).
			P.RadiusCm = Cloud.bZone && P.bFog ? FMath::Min(P.RadiusCm + AirGrowCmPerSecond * Dt, FMath::Max(P.MaxRadiusCm, P.RadiusCm))
				: Cloud.bZone && Cloud.HazeDiffuse > 0.0f
				? FMath::Min(FMath::Max(P.RadiusCm + AirGrowCmPerSecond * Dt, FMath::Sqrt(P.RadiusCm * P.RadiusCm + 2.0f * HazeSpreadShare * Cloud.HazeDiffuse * Dt)), FMath::Max(Cloud.HazeRadiusMax, Cloud.HazeRadius))
				: Cloud.bZone ? FMath::Min(P.RadiusCm + AirGrowCmPerSecond * Dt, FMath::Max3(P.MaxRadiusCm, 1.5f * Cloud.PackedCellCm, Cloud.HazeRadius))
				: P.Air > 0.5f ? FMath::Min(P.RadiusCm + AirGrowCmPerSecond * Dt, FMath::Max(P.MaxRadiusCm, P.RadiusCm)) : P.RadiusCm + VacuumGrowPerCm * Speed * Dt;
			P.Heat *= FMath::Exp(-Dt / HeatSeconds);
			// Bodies push it out of themselves (within two steps) at their own speed.
			for (const FOSSmokeSim::FMover& M : Cloud.Movers)
			{
				if (M.Extent.IsZero())
				{
					const FVector3f Off = P.P - M.Centre;
					const float Reach = M.RadiusCm + P.RadiusCm * 0.5f;
					const float D = Off.Size();
					if (D < Reach)
					{
						// A moving body: out sideways of its motion, so the gas slides round it and stays behind (pushed
						// out along the motion it rode ahead of the body every step). A still one: straight out.
						const float BodySpeed = M.Velocity.Size();
						FVector3f Out = Off.GetSafeNormal(UE_SMALL_NUMBER, FVector3f::UpVector);
						if (BodySpeed > 20.0f)
						{
							const FVector3f Along = M.Velocity / BodySpeed;
							FVector3f Side = Off - Along * (Off | Along);
							if (Side.SizeSquared() < 1.0f)
							{
								Side = Along.Cross(FVector3f::UpVector);
								Side = Side.SizeSquared() < 1e-3f ? FVector3f::ForwardVector : Side;
							}
							Out = Side.GetSafeNormal();
							const float Wide = FMath::Sqrt(FMath::Max(Reach * Reach - FMath::Square(Off | Along), 0.0f));
							// Out of its way by velocity (capped), not by a jump of position.
							P.V += Out * FMath::Min(FMath::Max(Wide - (Off - Along * (Off | Along)).Size(), 0.0f) * 0.6f / Dt, BodyPushCap);
							// The wake: a little of the body's speed, and out to the side as it parts.
							P.V += (M.Velocity * WakeShare - P.V) * 0.3f + Out * FMath::Min(BodySpeed * 0.15f, 40.0f);
							// In the haze a turbulent wake (v14): the wander kicked - the corridor a walker cuts swirls and closes.
							if (Cloud.bZone && Cloud.HazeWake > 0.0f)
							{
								FVector3f Kick;
								for (int32 Axis = 0; Axis < 3; ++Axis)
								{
									const float U1 = (float(Hash3(int32(P.Seed), int32(Cloud.StepIndex), Axis * 2, 0x3A4Eu)) + 1.0f) / 4294967297.0f;
									const float U2 = float(Hash3(int32(P.Seed), int32(Cloud.StepIndex), Axis * 2 + 1, 0x3A4Eu)) / 4294967296.0f;
									Kick[Axis] = FMath::Sqrt(-2.0f * FMath::Loge(U1)) * FMath::Cos(UE_TWO_PI * U2);
								}
								P.Wander += Kick * FMath::Min(Cloud.HazeWake * BodySpeed, 30.0f);
							}
						}
						else
						{
							P.V += Out * FMath::Min((Reach - D) * 0.6f / Dt, BodyPushCap);
						}
					}
				}
				else
				{
					const FVector3f L = M.Rotation.UnrotateVector(P.P - M.Centre);
					const FVector3f E = M.Extent + FVector3f(P.RadiusCm * 0.5f);
					if (FMath::Abs(L.X) < E.X && FMath::Abs(L.Y) < E.Y && FMath::Abs(L.Z) < E.Z)
					{
						const FVector3f Pen(E.X - FMath::Abs(L.X), E.Y - FMath::Abs(L.Y), E.Z - FMath::Abs(L.Z));
						FVector3f Out = Pen.X < Pen.Y && Pen.X < Pen.Z ? FVector3f(FMath::Sign(L.X), 0, 0) : Pen.Y < Pen.Z ? FVector3f(0, FMath::Sign(L.Y), 0) : FVector3f(0, 0, FMath::Sign(L.Z));
						const float Depth = FMath::Min3(Pen.X, Pen.Y, Pen.Z);
						Out = M.Rotation.RotateVector(Out);
						P.V += Out * FMath::Min(Depth * 0.6f / Dt, BodyPushCap);
						const float Push = BodyShare * (M.Velocity | Out) - (P.V | Out);
						if (Push > 0.0f)
						{
							P.V += Out * Push;
						}
					}
				}
			}
			// The floor and the walls last: whatever pushed it, it slides on them (no speed into them, never through) -
			// inside each surface's bounds, and only coming from its front (one already behind is let be).
			for (int32 I = 0; I < Cloud.Planes.Num(); ++I)
			{
				const FPlane& Plane = Cloud.Planes[I];
				const FVector3f Normal = FVector3f(FVector(Plane));
				const float Dist = (FVector3f(Origin + P.P) | Normal) - float(Plane.W);
				const float Was = (FVector3f(Origin + Before) | Normal) - float(Plane.W);
				const float Keep = P.RadiusCm * 0.5f;
				if (Dist < Keep && Was > -1.0f && Cloud.PlaneBoxes[I].IsInside(Cloud.Origin + FVector(P.P)))
				{
					P.P += Normal * (Keep - Dist);
					const float In = P.V | Normal;
					if (In < 0.0f)
					{
						P.V -= Normal * In;
					}
					// The wander reflects off a wall (v14): nothing piles up against it, nothing leaks through.
					const float WIn = P.Wander | Normal;
					if (WIn < 0.0f)
					{
						P.Wander -= Normal * (2.0f * WIn);
					}
				}
			}
			// The room's walls (v14): a parcel that stepped into a solid cell goes back along the axes it crossed, its speed
			// into the wall dropped and its wander reflected; one inside a solid cell (a door closed on it) is pushed to the
			// nearest air at 60 cm/s at most - by velocity, not a jump.
			if (Cloud.bUseRoom && Cloud.Room.IsValid())
			{
				const FOSGasRoom& Room = *Cloud.Room;
				const FVector NowAt = Cloud.Origin + FVector(P.P);
				if (Room.IsSolid(NowAt))
				{
					const FVector WasAt = Cloud.Origin + FVector(Before);
					if (!Room.IsSolid(WasAt))
					{
						const FIntVector CN = Room.CellOf(NowAt), CW = Room.CellOf(WasAt);
						for (int32 Axis = 0; Axis < 3; ++Axis)
						{
							if (CN[Axis] != CW[Axis])
							{
								P.P[Axis] = Before[Axis];
								P.V[Axis] = 0.0f;
								P.Wander[Axis] = -P.Wander[Axis];
							}
						}
					}
					else
					{
						const FIntVector C = Room.CellOf(NowAt);
						for (const FIntVector& Way : { FIntVector(0, 0, 1), FIntVector(1, 0, 0), FIntVector(-1, 0, 0), FIntVector(0, 1, 0), FIntVector(0, -1, 0), FIntVector(0, 0, -1) })
						{
							const FIntVector Next = C + Way;
							if (Room.Inside(Next) && Room.Cells[Room.Index(Next)] != 2)
							{
								P.V = FVector3f(Way) * 60.0f;
								break;
							}
						}
					}
				}
			}
			// The haze thins by the zone's ventilation.
			if (Cloud.bZone && Cloud.HazeHalfLife > 0.0f)
			{
				// By a vent (GasVent, v14) 4x faster: the last of the haze goes into the grille, not into thin air.
				float Rate = 1.0f;
				for (const FVector3f& Vent : Cloud.Vents)
				{
					Rate = FVector3f::DistSquared(P.P, Vent) < 2500.0f ? 4.0f : Rate;
				}
				// A parcel's own ventilation (v15: steam goes in 12 s) or the zone's - on its mass over a fog's background
				// only; the background refills a furrow over os.Gas.FogRefill.
				const float Over = FMath::Max(P.Mass - P.Base, 0.0f);
				P.Mass = FMath::Min(P.Mass, P.Base) + Over * FMath::Exp(-Dt * Rate * (P.Vent > 0.0f ? P.Vent : UE_LN2 / Cloud.HazeHalfLife));
			}
			if (Cloud.bZone && P.Base > 0.0f && P.Mass < P.Base)
			{
				P.Mass += (P.Base - P.Mass) * (1.0f - FMath::Exp(-Dt / FMath::Max(GFogRefill.load(), 0.1f)));
			}
			// A parcel handed over goes when its ramp is done (not by its age: the haze has it at full weight by then).
			const bool bGone = P.HandOut >= 0.0f ? Cloud.SimToWorld + Cloud.SimTime >= P.HandOut + HandRamp : P.Age >= P.Life;
			if (bGone || (P.Mass < 0.002f && P.Base <= 0.0f))
			{
				Parcels.RemoveAtSwap(Index, EAllowShrinking::No);
			}
		}
	}
	// Vacuum: a parcel grows to at least 0.6 of the way to its second nearest neighbour (14 cm at most) - the cone stays
	// one body as the parcels fly apart, its edges not cut away.
	if (!Cloud.bAir && Parcels.Num() > 2)
	{
		TArray<float> Need;
		Need.SetNumUninitialized(Parcels.Num());
		GasFor(Parcels.Num(), [&](int32 I)
		{
			float First = 1e12f, Second = 1e12f;
			for (int32 J = 0; J < Parcels.Num(); ++J)
			{
				if (J != I)
				{
					const float D2 = FVector3f::DistSquared(Parcels[I].P, Parcels[J].P);
					if (D2 < First)
					{
						Second = First;
						First = D2;
					}
					else if (D2 < Second)
					{
						Second = D2;
					}
				}
			}
			// 0.6 of the way to the second nearest, 14 cm at most: the streamers stay apart (1.2 / 30 cm made a round
			// cloud, 0.9 / 20 cm a row of balls, 0.7 / 15 cm one capsule).
			Need[I] = FMath::Min(0.6f * FMath::Sqrt(Second), 14.0f);
		});
		for (int32 I = 0; I < Parcels.Num(); ++I)
		{
			Parcels[I].RadiusCm = FMath::Max(Parcels[I].RadiusCm, Need[I]);
		}
	}
	(void)Down;
	const double SplatStart = FPlatformTime::Seconds();
	Splat(Cloud);
	GSplatUs += int64((FPlatformTime::Seconds() - SplatStart) * 1e6);
	++GSplats;
}

void UOSGasSubsystem::Splat(FCloud& Cloud)
{
	const TArray<FOSGasParcel>& Parcels = Cloud.Parcels;
	const int32 N = RenderCells;
	// The box: on the gas (its mass centre, snapped to a cell). A vacuum cloud outgrowing 96 cm draws at 3 cm cells.
	FVector3f Centre = FVector3f::ZeroVector;
	float Mass = 0.0f;
	for (const FOSGasParcel& P : Parcels)
	{
		Centre += P.P * P.Mass;
		Mass += P.Mass;
	}
	Centre /= FMath::Max(Mass, 1e-3f);
	float Spread = 0.0f;
	for (const FOSGasParcel& P : Parcels)
	{
		Spread += FVector3f::Dist(P.P, Centre) * P.Mass;
	}
	Spread /= FMath::Max(Mass, 1e-3f);
	// The box: the frame round every parcel with its reach, 10% more; it grows and shrinks with the gas, never jumps
	// (owner 23.10: В«СЂРµСЃРїР°РІРЅРёС‚СЃСЏ Сѓ РѕСЂСѓР¶РёСЏВ», В«РєСѓР±РѕРј РѕР±СЂРµР·Р°РµС‚СЃСЏВ»). Its cell: the size over 48, 2 to 8 cm.
	// An outlier (farther than 3x the mean spread) does not set the box; its gas fades at the box's faces (the shader).
	// The haze's far cut 4 m (v14: a finger flowing into a doorway was cut off at 1.5 m).
	const float FarCm = FMath::Max(3.0f * Spread, Cloud.bZone ? 400.0f : 40.0f);
	// The haze: box / 48, 4-24 cm.
	const float MinCell = Cloud.bZone ? 4.0f : 2.0f;
	const float MaxCell = Cloud.bZone ? 24.0f : 8.0f;
	FVector3f BoxLo(1e9f), BoxHi(-1e9f);
	for (const FOSGasParcel& P : Parcels)
	{
		if (FVector3f::Dist(P.P, Centre) > FarCm)
		{
			continue;
		}
		const float Reach = FMath::Max3(P.RadiusCm, P.V.Size() * 0.08f, Cloud.bZone ? P.RadiusCm * FMath::Pow(P.Stretch, 2.0f / 3.0f) : 0.0f) * (Cloud.bZone && P.Flat < 1.0f ? FMath::InvSqrt(P.Flat) : 1.0f);
		BoxLo = BoxLo.ComponentMin(P.P - FVector3f(Reach));
		BoxHi = BoxHi.ComponentMax(P.P + FVector3f(Reach));
	}
	if (Parcels.IsEmpty())
	{
		BoxLo = BoxHi = Centre;
	}
	const float Need = FMath::Clamp((BoxHi - BoxLo).GetMax() * 1.1f / N, MinCell, MaxCell);
	float Cell = Need;
	const FVector3f MassCentre = Centre;
	Centre = SnapTo((BoxLo + BoxHi) * 0.5f, 0.5f);
	// The box: kept while the gas fits it; grown at once with 15% to spare when it does not (the gas is never cut);
	// shrunk (by a fifth at most) only once the gas has filled less than 60% of it for 0.5 s and 0.5 s after the last
	// re-box. Each atlas is drawn in its own box and the two are blended: a re-box shows no jump (owner 23.10:
	// В«СЃРєР°С‡РєР°РјРё СЂР°СЃС€РёСЂСЏРµС‚СЃСЏВ» - a re-box each step).
	{
		const float OldCell = Cloud.PackedCellCm;
		const FVector3f OldCentre = Cloud.PackedCentre;
		const float OldHalf = 0.5f * N * OldCell;
		const bool bFits = (BoxLo - (OldCentre - FVector3f(OldHalf))).GetMin() >= 0.0f && ((OldCentre + FVector3f(OldHalf)) - BoxHi).GetMin() >= 0.0f;
		const float Now = Cloud.SimTime;
		if (Cloud.bHasGas && bFits)
		{
			Cloud.LooseSince = Need < 0.6f * OldCell ? (Cloud.LooseSince < 0.0f ? Now : Cloud.LooseSince) : -1.0f;
			if (Cloud.LooseSince >= 0.0f && Now - Cloud.LooseSince >= 0.5f && Now - Cloud.LastRebox >= 0.5f)
			{
				Cell = FMath::Clamp(FMath::Max(Need * 1.15f, OldCell * 0.8f), MinCell, MaxCell);
				Cloud.LastRebox = Now;
				Cloud.LooseSince = -1.0f;
			}
			else
			{
				Cell = OldCell;
				Centre = OldCentre;
			}
		}
		else
		{
			Cell = FMath::Clamp(FMath::Max(Need * 1.15f, Cloud.bHasGas ? OldCell : 0.0f), MinCell, MaxCell);
			Cloud.LastRebox = Now;
			Cloud.LooseSince = -1.0f;
		}
	}
	const float Half = 0.5f * N * Cell;
	// Each parcel's weight in everything drawn: in over 0.12 s at its birth (the muzzle's blast at once), out with its
	// life's fade - a parcel never pops in or out (it went in at once and out below a mass of 0.05).
	TArray<float> Weight;
	Weight.SetNumUninitialized(Parcels.Num());
	for (int32 I = 0; I < Parcels.Num(); ++I)
	{
		const FOSGasParcel& P = Parcels[I];
		// 0.12 s, not the contract's 0.05: a thin parcel half a metre from the eye came in by 0.3 of opacity a frame.
		const float In = P.bInstant ? 1.0f : FMath::SmoothStep(0.0f, 0.12f, P.Age);
		Weight[I] = In * (1.0f - FMath::SmoothStep(0.6f * P.Life, P.Life, P.Age));
		// The hand-over (v12 7a.2), by the world's time this atlas is shown at (a step after it is packed): out of a
		// shot's cloud and into the haze over the same 0.5 s - the two weights sum to 1.
		if (P.HandOut >= 0.0f || P.HandIn >= 0.0f)
		{
			const double Shown = Cloud.SimToWorld + Cloud.SimTime + Cloud.StepDt;
			Weight[I] *= P.HandOut >= 0.0f ? 1.0f - FMath::Clamp(float(Shown - P.HandOut) / HandRamp, 0.0f, 1.0f) : FMath::Clamp(float(Shown - P.HandIn) / HandRamp, 0.0f, 1.0f);
		}
		// In a vacuum the gas thins with the way it went (~1/d^2 past 90 cm): dense by the muzzle, sparse at the front.
		if (!Cloud.bAir)
		{
			Weight[I] /= 1.0f + FVector3f::DistSquared(P.P, P.Birth) / (90.0f * 90.0f);
		}
	}
	double LapAt = FPlatformTime::Seconds();
	int32 LapPart = -1;
	const auto SplatLap = [&LapAt, &LapPart](int32 Part)
	{
		const double Now = FPlatformTime::Seconds();
		if (LapPart >= 0)
		{
			GSplatPartUs[LapPart] += int64((Now - LapAt) * 1e6);
		}
		LapAt = Now;
		LapPart = Part;
	};
	SplatLap(0);
	// Each parcel's light: the others in front of it toward the key light (a cylinder of their summed radii).
	const FVector3f Key = Cloud.KeyDir;
	TArray<float> Light;
	Light.SetNumUninitialized(Parcels.Num());
	GasFor(Parcels.Num(), [&](int32 I)
	{
		float Sum = 0.0f;
		const FOSGasParcel& A = Parcels[I];
		for (int32 J = 0; J < Parcels.Num(); ++J)
		{
			if (J == I)
			{
				continue;
			}
			const FOSGasParcel& B = Parcels[J];
			const FVector3f D = B.P - A.P;
			const float Along = D | Key;
			// Gas more than 60 cm toward the light is past the reach of its shadow here (a cutoff: the pairs cost N^2).
			if (Along <= 0.0f || Along > 60.0f)
			{
				continue;
			}
			const float R = A.RadiusCm + B.RadiusCm;
			const float Across2 = D.SizeSquared() - Along * Along;
			if (Across2 < R * R)
			{
				Sum += B.Mass * Weight[J] * (1.0f - Across2 / (R * R)) * B.RadiusCm / 4.0f;
			}
		}
		Light[I] = FMath::Exp(-KeyShadow * Sum);
	});
	// A vacuum's edges torn a little harder (0.18), as the holes' threads; more tore the thin streamers into dots.
	// The haze's noise cut lower (v14b): its overlapping kernels read as one veil, no holes at their rims.
	const float Cut = Cloud.bZone ? Cloud.HazeNoiseCut : Cloud.bAir ? NoiseCut + 0.1f : 0.18f;
	// The haze: a coarse noise (~40 cm) on its coarse cells.
	const float Scale = Cloud.bZone ? 40.0f : Cloud.bAir ? NoiseCm : 12.0f;
	const uint32 CloudSeed = Cloud.Seed;
	SplatLap(1);
	// The splat, slice by slice: the parcels' smooth kernels only (the noise comes once per cell below).
	const int32 Cells = N * N * N;
	// The fields: one from a pool, kept between splats - only the last splat's cells are cleared (zeroing 3.5 MB of
	// fresh arrays every step was much of its cost). A pool, not thread_local: a thread waiting in a ParallelFor may
	// run another cloud's step.
	struct FScratch
	{
		TArray<float> D, H, L, W, TX, TY, TZ, T0, T1;
		FIntVector Lo = FIntVector(0), Hi = FIntVector(-1);
	};
	static FCriticalSection PoolLock;
	static TArray<TUniquePtr<FScratch>> Pool;
	TUniquePtr<FScratch> Taken;
	{
		FScopeLock Lock(&PoolLock);
		Taken = Pool.IsEmpty() ? MakeUnique<FScratch>() : Pool.Pop();
	}
	ON_SCOPE_EXIT
	{
		FScopeLock Lock(&PoolLock);
		Pool.Add(MoveTemp(Taken));
	};
	FScratch& Scratch = *Taken;
	if (Scratch.D.Num() != Cells)
	{
		for (TArray<float>* F : { &Scratch.D, &Scratch.H, &Scratch.L, &Scratch.W, &Scratch.TX, &Scratch.TY, &Scratch.TZ, &Scratch.T0, &Scratch.T1 })
		{
			F->SetNumZeroed(Cells);
		}
	}
	else
	{
		for (int32 Z = Scratch.Lo.Z; Z <= Scratch.Hi.Z; ++Z)
		{
			for (int32 Y = Scratch.Lo.Y; Y <= Scratch.Hi.Y; ++Y)
			{
				const int32 Row = (Z * N + Y) * N;
				const int32 Count = Scratch.Hi.X - Scratch.Lo.X + 1;
				for (TArray<float>* F : { &Scratch.D, &Scratch.H, &Scratch.L, &Scratch.W, &Scratch.TX, &Scratch.TY, &Scratch.TZ, &Scratch.T0, &Scratch.T1 })
				{
					FMemory::Memzero(F->GetData() + Row + Scratch.Lo.X, Count * sizeof(float));
				}
			}
		}
	}
	TArray<float>& D = Scratch.D;
	TArray<float>& H = Scratch.H;
	TArray<float>& L = Scratch.L;
	TArray<float>& W = Scratch.W;
	TArray<float>& TX = Scratch.TX;
	TArray<float>& TY = Scratch.TY;
	TArray<float>& TZ = Scratch.TZ;
	const float Fade = 1.0f;
	// How much a parcel's kernel fills its cell (the calibration of the cloud's opacity, os.Fx.SmokeCost's T).
	const float MassScale = Cloud.bAir ? AirMassScale : VacuumMassScale;
	// Only the cells round the gas (its parcels' reach, 3 cells more for the blur and the shadows): every pass below.
	FIntVector Lo(N - 1), Hi(0);
	for (const FOSGasParcel& P : Parcels)
	{
		const float Reach = FMath::Max3(P.RadiusCm, P.V.Size() * 0.08f, Cloud.bZone ? P.RadiusCm * FMath::Pow(P.Stretch, 2.0f / 3.0f) : 0.0f) * (Cloud.bZone && P.Flat < 1.0f ? FMath::InvSqrt(P.Flat) : 1.0f);
		for (int32 Axis = 0; Axis < 3; ++Axis)
		{
			Lo[Axis] = FMath::Min(Lo[Axis], FMath::Max(0, FMath::FloorToInt((P.P[Axis] - Reach - Centre[Axis] + Half) / Cell) - 3));
			Hi[Axis] = FMath::Max(Hi[Axis], FMath::Min(N - 1, FMath::FloorToInt((P.P[Axis] + Reach - Centre[Axis] + Half) / Cell) + 3));
		}
	}
	if (Parcels.IsEmpty() || Hi.X < Lo.X || Hi.Y < Lo.Y || Hi.Z < Lo.Z)
	{
		Lo = FIntVector(0);
		Hi = FIntVector(-1);
	}
	Scratch.Lo = Lo;
	Scratch.Hi = Hi;
	const int32 Slices = FMath::Max(0, Hi.Z - Lo.Z + 1);
	const FVector3f SquashDown = FVector3f(Cloud.Gravity.GetSafeNormal());
	GasFor(Slices, [&](int32 K)
	{
		const int32 Z = Lo.Z + K;
		const float CZ = Centre.Z + (Z + 0.5f - 0.5f * N) * Cell;
		for (int32 Index = 0; Index < Parcels.Num(); ++Index)
		{
			const FOSGasParcel& P = Parcels[Index];
			// An ellipsoid: semi-axis max(radius, |v| x 0.08 s) along the velocity, the radius across - a fast parcel
			// draws a streak of jet, not a ball (owner 23.10: В«РІ РЅРµРІРµСЃРѕРјРѕСЃС‚Рё РґС‹Рј РІС‹Р»РµС‚Р°РµС‚ РєР°Рє Р±СѓРґС‚Рѕ РґСЂРѕР±РёРЅРєР°РјРёВ»).
			const float Speed = P.V.Size();
			FVector3f Along = Speed > 1.0f ? P.V / Speed : FVector3f::ZeroVector;
			// The streak along the velocity joins a streamer's parcels into a thread; in a vacuum half of it (0.04 s) and
			// 4 radii at most - a full streak of fast parcels drew bright comets (owner 23.10: В«Сѓ РїРёСЃС‚РѕР»РµС‚Р° РєР°РєРёРµ-С‚Рѕ
			// РєРѕРјРµС‚С‹В»), and merged across into a cylinder before the jet became a fan.
			float Long = Cloud.bAir ? FMath::Max(P.RadiusCm, Speed * 0.08f) : FMath::Clamp(Speed * 0.04f, P.RadiusCm, 4.0f * P.RadiusCm);
			float Across = P.RadiusCm;
			// The haze's strand: along its stretch axis, volume kept (v12 7a.4).
			if (Cloud.bZone)
			{
				Along = P.StretchAxis;
				Long = P.RadiusCm * FMath::Pow(P.Stretch, 2.0f / 3.0f);
				Across = P.RadiusCm * FMath::Pow(P.Stretch, -1.0f / 3.0f);
			}
			const float InvAcross2 = 1.0f / (Across * Across);
			const float InvLong2 = 1.0f / (Long * Long);
			// The ellipsoid's own bounds per axis (a streak along X is long in X only): its sphere's box made a fast
			// parcel visit ~14000 cells a splat.
			const float Stretch = Long * Long - Across * Across;
			FVector3f Ext(FMath::Sqrt(Stretch * Along.X * Along.X + Across * Across), FMath::Sqrt(Stretch * Along.Y * Along.Y + Across * Across),
				FMath::Sqrt(Stretch * Along.Z * Along.Z + Across * Across));
			// The squash along gravity (v14c): the offset taken through S = sqrt(F)(I - dd') + dd'/F before the strand's
			// quadric (det 1: the volume kept); the box per axis through u = S^-1 e = e/sqrt(F) + d d_i (F - 1/sqrt(F)).
			const float Fl = Cloud.bZone && P.Flat < 0.999f && !SquashDown.IsZero() ? P.Flat : 1.0f;
			const float RootF = FMath::Sqrt(Fl);
			if (Fl < 1.0f)
			{
				for (int32 Axis = 0; Axis < 3; ++Axis)
				{
					FVector3f E = FVector3f::ZeroVector;
					E[Axis] = 1.0f;
					const FVector3f U = E / RootF + SquashDown * (SquashDown[Axis] * (Fl - 1.0f / RootF));
					Ext[Axis] = FMath::Sqrt(Stretch * FMath::Square(Along | U) + Across * Across * U.SizeSquared());
				}
			}
			if (FMath::Abs(P.P.Z - CZ) >= Ext.Z || P.Mass * Weight[Index] < 1e-4f)
			{
				continue;
			}
			const FVector3f Travelled = P.P - P.Birth;
			// The same matter in more room: a parcel thins as it grows (air: (3 cm / r)^2; vacuum: 5 cm / r, it spreads metres).
			const float Thin = ParcelThin(Cloud, P.RadiusCm);
			const float M = P.Mass * Weight[Index] * Fade * Thin;
			const int32 X0 = FMath::Max(0, FMath::FloorToInt((P.P.X - Ext.X - Centre.X + Half) / Cell));
			const int32 X1 = FMath::Min(N - 1, FMath::FloorToInt((P.P.X + Ext.X - Centre.X + Half) / Cell));
			const int32 Y0 = FMath::Max(0, FMath::FloorToInt((P.P.Y - Ext.Y - Centre.Y + Half) / Cell));
			const int32 Y1 = FMath::Min(N - 1, FMath::FloorToInt((P.P.Y + Ext.Y - Centre.Y + Half) / Cell));

			for (int32 Y = Y0; Y <= Y1; ++Y)
			{
				const float CY = Centre.Y + (Y + 0.5f - 0.5f * N) * Cell;
				for (int32 X = X0; X <= X1; ++X)
				{
					const FVector3f At(Centre.X + (X + 0.5f - 0.5f * N) * Cell, CY, CZ);
					FVector3f Off = At - P.P;
					if (Fl < 1.0f)
					{
						const float Dn = Off | SquashDown;
						Off = (Off - SquashDown * Dn) * RootF + SquashDown * (Dn / Fl);
					}
					const float A = Off | Along;
					const float Q = A * A * InvLong2 + (Off.SizeSquared() - A * A) * InvAcross2;
					if (Q >= 1.0f)
					{
						continue;
					}
					// Smooth to its rim (smoothstep of the distance): neighbours melt into one body.
					const float Sd = FMath::Sqrt(Q);
					const float Kernel = 1.0f - Sd * Sd * (3.0f - 2.0f * Sd);
					const int32 I = (Z * N + Y) * N + X;
					const float KM = Kernel * M;
					// The union in log space: D sums -ln(1 - k), the density is 1 - e^-D (1 - prod(1 - k_i)).
					D[I] -= FMath::Loge(1.0f - FMath::Min(KM * MassScale, 0.95f));
					H[I] += KM * (Cloud.bShafts ? P.Shaft : P.Heat);
					L[I] += KM * Light[Index];
					W[I] += KM;
					TX[I] += KM * Travelled.X;
					TY[I] += KM * Travelled.Y;
					TZ[I] += KM * Travelled.Z;
				}
			}
		}
	});
	SplatLap(2);
	// The rags: one two-octave noise per cell with gas, torn by a threshold (was one per parcel per cell: 4x the budget).
	GasFor(Slices, [&](int32 K)
	{
		const int32 Z = Lo.Z + K;
		for (int32 Y = Lo.Y; Y <= Hi.Y; ++Y)
		{
			for (int32 X = Lo.X; X <= Hi.X; ++X)
			{
				const int32 I = (Z * N + Y) * N + X;
				if (D[I] <= 1e-4f)
				{
					continue;
				}
				const FVector3f At(Centre.X + (X + 0.5f - 0.5f * N) * Cell, Centre.Y + (Y + 0.5f - 0.5f * N) * Cell, Centre.Z + (Z + 0.5f - 0.5f * N) * Cell);
				D[I] = 1.0f - FMath::Exp(-D[I]);
				const FVector3f Travel = W[I] > 1e-6f ? FVector3f(TX[I], TY[I], TZ[I]) / W[I] : FVector3f::ZeroVector;
				const FVector3f Flow = (At - Travel) / Scale;
				const float Noise = Noise3(Flow, CloudSeed) * 0.67f + Noise3(Flow * 2.0f, CloudSeed ^ 0x51ED270Bu) * 0.33f;
				D[I] *= FMath::Clamp((Noise - Cut) * NoiseGain, 0.0f, 1.0f);
			}
		}
	});
	SplatLap(3);
	// A soft sharpen: the density less 0.4 of its blur (5 cells a side) - the low cushion goes, the lumps stay.
	{
		TArray<float>& T0 = Scratch.T0;
		TArray<float>& T1 = Scratch.T1;
		// A 5-cell box along one axis by running sums (a cell costs two reads, not five), rows contiguous in X.
		const auto Clamp = [N](int32 V) { return FMath::Clamp(V, 0, N - 1); };
		const auto At3 = [N](int32 X, int32 Y, int32 Z) { return (Z * N + Y) * N + X; };
		const int32 Rows = FMath::Max(0, Hi.Y - Lo.Y + 1);
		// Along X.
		GasFor(Slices, [&](int32 KZ)
		{
			const int32 Z = Lo.Z + KZ;
			for (int32 Y = Lo.Y; Y <= Hi.Y; ++Y)
			{
				float Sum = 0.0f;
				for (int32 K = -2; K <= 2; ++K)
				{
					Sum += D[At3(Clamp(Lo.X + K), Y, Z)];
				}
				for (int32 X = Lo.X; X <= Hi.X; ++X)
				{
					T0[At3(X, Y, Z)] = Sum * 0.2f;
					Sum += D[At3(Clamp(X + 3), Y, Z)] - D[At3(Clamp(X - 2), Y, Z)];
				}
			}
			// Along Y in the same slice (a row of sums slides down it): one pass over the slices for both. The rows
			// outside the region the X pass did not write are zero (cleared).
			TArray<float, TInlineAllocator<64>> Sum;
			Sum.SetNumZeroed(N);
			for (int32 K = -2; K <= 2; ++K)
			{
				for (int32 X = Lo.X; X <= Hi.X; ++X)
				{
					Sum[X] += T0[At3(X, Clamp(Lo.Y + K), Z)];
				}
			}
			for (int32 Y = Lo.Y; Y <= Hi.Y; ++Y)
			{
				const int32 In = Clamp(Y + 3), Out = Clamp(Y - 2);
				for (int32 X = Lo.X; X <= Hi.X; ++X)
				{
					T1[At3(X, Y, Z)] = Sum[X] * 0.2f;
					Sum[X] += T0[At3(X, In, Z)] - T0[At3(X, Out, Z)];
				}
			}
		});
		// Along Z (by rows of Y: a row of sums slides through the slices), and the sharpen with it.
		GasFor(Rows, [&](int32 KY)
		{
			const int32 Y = Lo.Y + KY;
			TArray<float, TInlineAllocator<64>> Sum;
			Sum.SetNumZeroed(N);
			for (int32 K = -2; K <= 2; ++K)
			{
				for (int32 X = Lo.X; X <= Hi.X; ++X)
				{
					Sum[X] += T1[At3(X, Y, Clamp(Lo.Z + K))];
				}
			}
			for (int32 Z = Lo.Z; Z <= Hi.Z; ++Z)
			{
				const int32 In = Clamp(Z + 3), Out = Clamp(Z - 2);
				for (int32 X = Lo.X; X <= Hi.X; ++X)
				{
					const int32 I = At3(X, Y, Z);
					D[I] = FMath::Max(0.0f, D[I] - 0.4f * Sum[X] * 0.2f) / 0.6f;
					Sum[X] += T1[At3(X, Y, In)] - T1[At3(X, Y, Out)];
				}
			}
		});
	}
	SplatLap(4);
	// The atlas: R density, G heat, B the key light, A the ambient's shadow (the gas around, 2 cells each way).
	const FIntPoint Size = FOSSmokeSim::AtlasSize(N);
	// The atlas kept: the last one's cells cleared, this one's all written (zeros too).
	if (Cloud.Atlas.Num() != Size.X * Size.Y)
	{
		Cloud.Atlas.SetNumZeroed(Size.X * Size.Y);
	}
	else
	{
		for (int32 Z = Cloud.AtlasLo.Z; Z <= Cloud.AtlasHi.Z; ++Z)
		{
			const int32 OX = (Z % 8) * N, OY = (Z / 8) * N;
			for (int32 Y = Cloud.AtlasLo.Y; Y <= Cloud.AtlasHi.Y; ++Y)
			{
				FMemory::Memzero(Cloud.Atlas.GetData() + (OY + Y) * Size.X + OX + Cloud.AtlasLo.X, (Cloud.AtlasHi.X - Cloud.AtlasLo.X + 1) * sizeof(FColor));
			}
		}
	}
	Cloud.AtlasLo = Lo;
	Cloud.AtlasHi = Hi;
	const auto Byte = [](float V) { return uint8(FMath::Clamp(FMath::RoundToInt(V * 255.0f), 0, 255)); };
	const auto At = [&D, N](int32 X, int32 Y, int32 Z) { return D[(FMath::Clamp(Z, 0, N - 1) * N + FMath::Clamp(Y, 0, N - 1)) * N + FMath::Clamp(X, 0, N - 1)]; };
	bool bAny = false;
	constexpr int32 KeySteps = 6;
	const float KeyAt[KeySteps] = { 1.0f, 2.0f, 4.0f, 6.0f, 9.0f, 12.0f };
	const float KeyWeights[KeySteps] = { 1.0f, 1.0f, 1.5f, 1.5f, 2.0f, 2.5f };
	FIntVector KeyCells[KeySteps];
	for (int32 S = 0; S < KeySteps; ++S)
	{
		const FVector3f U = Cloud.KeyDir * KeyAt[S];
		KeyCells[S] = FIntVector(FMath::RoundToInt(U.X), FMath::RoundToInt(U.Y), FMath::RoundToInt(U.Z));
	}
	GasFor(Slices, [&](int32 K)
	{
		const int32 Z = Lo.Z + K;
		const int32 OX = (Z % 8) * N, OY = (Z / 8) * N;
		for (int32 Y = Lo.Y; Y <= Hi.Y; ++Y)
		{
			for (int32 X = Lo.X; X <= Hi.X; ++X)
			{
				const int32 I = (Z * N + Y) * N + X;
				if (D[I] <= 1e-4f)
				{
					continue;
				}
				const float Dens = FMath::Min(D[I] * DensityGain, 1.0f);
				const float Around = At(X + 2, Y, Z) + At(X - 2, Y, Z) + At(X, Y + 2, Z) + At(X, Y - 2, Z) + At(X, Y, Z + 2) + At(X, Y, Z - 2);
				// The warm core only while the gas is hot (heat over 0.3: its first 0.3-0.5 s), not a brown tint after.
				const float Heat = W[I] > 1e-4f ? FMath::Clamp((H[I] / W[I] - 0.3f) / 0.7f, 0.0f, 1.0f) : 0.0f;
				// A haze with shafts (v13 13c): G is its parcels' light from the shaft source, not heat.
				const float G = Cloud.bShafts ? (W[I] > 1e-4f ? FMath::Clamp(H[I] / W[I], 0.0f, 1.0f) : 1.0f) : Heat * FMath::Min(W[I] * 2.0f, 1.0f);
				// The key light's shadow through the drawn gas itself (1-12 cells toward it, as the old grid's Pack): a
				// lit flank and a dark belly - the relief (owner 23.10: В«РЅРµРґРѕСЃС‚Р°С‚РѕС‡РЅРѕ СЂРµР»СЊРµС„РЅС‹РјВ»). The parcels' own
				// light (L / W) shades it further.
				float Toward = 0.0f;
				for (int32 S = 0; S < KeySteps; ++S)
				{
					Toward += At(X + KeyCells[S].X, Y + KeyCells[S].Y, Z + KeyCells[S].Z) * KeyWeights[S];
				}
				const float Lit = FMath::Exp(-GridKeyShadow * Toward) * (W[I] > 1e-4f ? L[I] / W[I] : 1.0f);
				Cloud.Atlas[(OY + Y) * Size.X + OX + X] = FColor(Byte(Dens), Byte(G), Byte(Lit), Byte(FMath::Exp(-AmbientOcclusion * FMath::Min(Around * DensityGain, 6.0f))));
			}
		}
	});
	for (int32 Z = Lo.Z; Z <= Hi.Z && !bAny; ++Z)
	{
		for (int32 Y = Lo.Y; Y <= Hi.Y && !bAny; ++Y)
		{
			for (int32 X = Lo.X; X <= Hi.X; ++X)
			{
				if (D[(Z * N + Y) * N + X] > 0.004f)
				{
					bAny = true;
					break;
				}
			}
		}
	}
	SplatLap(5);
	{
		// Along X through the gas's mass centre (the box's own centre often misses the gas).
		float Tau = 0.0f;
		const int32 MY = FMath::Clamp(FMath::FloorToInt((MassCentre.Y - Centre.Y + Half) / Cell), 0, N - 1);
		const int32 MZ = FMath::Clamp(FMath::FloorToInt((MassCentre.Z - Centre.Z + Half) / Cell), 0, N - 1);
		for (int32 X = 0; X < N; ++X)
		{
			Tau += FMath::Min(D[(MZ * N + MY) * N + X] * DensityGain, 1.0f) * Cloud.Look.Extinction * Cell;
		}
		Cloud.CentreT = FMath::Exp(-Tau);
	}
	// The densest drawn cell (the haze's end, TickCloud).
	if (Cloud.bZone)
	{
		float Most = 0.0f;
		for (int32 Z = Lo.Z; Z <= Hi.Z; ++Z)
		{
			for (int32 Y = Lo.Y; Y <= Hi.Y; ++Y)
			{
				for (int32 X = Lo.X; X <= Hi.X; ++X)
				{
					Most = FMath::Max(Most, D[(Z * N + Y) * N + X]);
				}
			}
		}
		Cloud.MaxDensity = FMath::Min(Most * DensityGain, 1.0f);
	}
	Cloud.PackedCentre = Centre;
	Cloud.PackedCellCm = Cell;
	Cloud.bHasGas = bAny;
	Cloud.bPendingAtlas = true;
}

// --- rendering helpers ----------------------------------------------------------------------

void UOSGasSubsystem::EnsureRender(FCloud& Cloud)
{
	if (Cloud.Texture)
	{
		return;
	}
	// Two atlases one above the other: the current and the one before (blended by the step's phase).
	const FIntPoint Size = FOSSmokeSim::AtlasSize(RenderCells);
	UTexture2D* Texture = UTexture2D::CreateTransient(Size.X, 2 * Size.Y, PF_B8G8R8A8);
	Texture->SRGB = false;
	Texture->Filter = TF_Bilinear;
	Texture->AddressX = TA_Clamp;
	Texture->AddressY = TA_Clamp;
	Texture->NeverStream = true;
	Texture->UpdateResource();
	Cloud.Texture = Texture;
	Keep.Add(Texture);
}

void UOSGasSubsystem::FindMovers(FCloud& Cloud, float DeltaTime, TSet<TObjectKey<UPrimitiveComponent>>& Seen)
{
	Cloud.NextMovers.Reset();
	const float Half = 0.5f * RenderCells * Cloud.RenderCellCm;
	const FVector Centre = Cloud.Origin + FVector(Cloud.DrawnCentre);
	FCollisionObjectQueryParams Types;
	Types.AddObjectTypesToQuery(ECC_PhysicsBody);
	Types.AddObjectTypesToQuery(ECC_WorldDynamic);
	Types.AddObjectTypesToQuery(ECC_Pawn);
	TArray<FOverlapResult> Hits;
	GetWorld()->OverlapMultiByObjectType(Hits, Centre, FQuat::Identity, Types, FCollisionShape::MakeBox(FVector(Half)), FCollisionQueryParams(SCENE_QUERY_STAT(OSGasMovers), false));
	const APlayerController* Controller = GetWorld()->GetFirstPlayerController();
	const AActor* Player = Controller ? Controller->GetPawn() : nullptr;
	for (const FOverlapResult& Hit : Hits)
	{
		UPrimitiveComponent* Body = Hit.GetComponent();
		const float Radius = Body ? Body->Bounds.SphereRadius : 0.0f;
		// Not a spent case or a shard (under 6 cm): a case ejected through the fresh jet stirred it wider the higher the
		// frame rate (os.Gas.WalkProbe at 240 FPS: spread 35 cm against 21 at 60).
		if (!Body || Radius < 6.0f || Radius > 400.0f || Cloud.NextMovers.Num() >= 8)
		{
			continue;
		}
		// Of the player only its body: the gun in the hands would carve its own muzzle's gas.
		if (Player && Body->GetOwner() == Player && Body != Player->GetRootComponent())
		{
			continue;
		}
		const TObjectKey<UPrimitiveComponent> Key(Body);
		const FVector At = Body->Bounds.Origin;
		const FVector* Last = LastMoverAt.Find(Key);
		const FVector Velocity = Body->IsSimulatingPhysics() ? Body->GetPhysicsLinearVelocity() : Last && DeltaTime > 0.0f ? (At - *Last) / DeltaTime : FVector::ZeroVector;
		LastMoverAt.Add(Key, At);
		Seen.Add(Key);
		// A pawn (the player included) is a sphere of its capsule's radius, never its bounds' box: that box (68 x 68 x
		// 176 cm) swept the whole cloud along with the walking player (owner 23.10: В«РѕР±Р»Р°РєРѕ СЃР»РµРґСѓРµС‚ Р·Р° РїСѓС€РєРѕР№В»).
		if (const APawn* PawnOwner = Cast<APawn>(Body->GetOwner()); PawnOwner && Body == PawnOwner->GetRootComponent())
		{
			float CapsuleRadius = 0.0f, CapsuleHalf = 0.0f;
			PawnOwner->GetSimpleCollisionCylinder(CapsuleRadius, CapsuleHalf);
			FOSSmokeSim::FMover& M = Cloud.NextMovers.AddDefaulted_GetRef();
			M.Centre = FVector3f(At - Cloud.Origin);
			M.RadiusCm = FMath::Clamp(CapsuleRadius, 15.0f, 45.0f);
			M.Velocity = FVector3f(Velocity.GetClampedToMaxSize(1500.0f)) * 0.25f;
			continue;
		}
		const bool bBig = Radius > 90.0f;
		if (bBig && Velocity.SizeSquared() < 25.0f && !Body->IsSimulatingPhysics())
		{
			continue;
		}
		FOSSmokeSim::FMover& M = Cloud.NextMovers.AddDefaulted_GetRef();
		M.Velocity = FVector3f(Velocity.GetClampedToMaxSize(1500.0f));
		if (bBig)
		{
			const FBoxSphereBounds Local = Body->CalcBounds(FTransform::Identity);
			const FTransform Frame = Body->GetComponentTransform();
			M.Centre = FVector3f(Frame.TransformPosition(Local.Origin) - Cloud.Origin);
			M.RadiusCm = 0.0f;
			M.Extent = FVector3f(Local.BoxExtent * Frame.GetScale3D().GetAbs());
			M.Rotation = FQuat4f(Frame.GetRotation());
		}
		else
		{
			const FVector E = Body->Bounds.BoxExtent;
			M.Centre = FVector3f(At - Cloud.Origin);
			M.RadiusCm = FMath::Min(Radius, FMath::Max3(FMath::Min(E.X, E.Y), FMath::Min(E.Y, E.Z), FMath::Min(E.X, E.Z)));
		}
	}
}

namespace
{
	/**
	 * A lamp's light at P in the renderer's units (colour x brightness x falloff x cone, as the deferred light shader:
	 * inverse square windowed by the radius, or (1 - d^2/R^2)^exponent; a spot's cone, a rect's cosine to its normal),
	 * its luminance - and Toward, P to the lamp. 0 out of its reach.
	 */
	float LampAt(const ULightComponent* L, const FVector& P, FVector& Toward, const FVector& Centre, float Reach = 1.0f)
	{
		if (L->IsA<UDirectionalLightComponent>())
		{
			Toward = -L->GetDirection();
			return L->ComputeLightBrightness() * L->GetLightColor().GetLuminance();
		}
		const ULocalLightComponent* Local = Cast<ULocalLightComponent>(L);
		if (!Local)
		{
			return 0.0f;
		}
		const FVector Offset = L->GetComponentLocation() - P;
		const double D2 = Offset.SizeSquared();
		const double R = Local->AttenuationRadius * Reach;
		if (D2 >= R * R)
		{
			return 0.0f;
		}
		// A lamp inside the box (the torch, the muzzle's flash): its way is toward the box's centre (P is the lamp itself).
		Toward = D2 > 1.0 ? Offset * FMath::InvSqrt(D2) : (L->GetComponentLocation() - Centre).GetSafeNormal(UE_SMALL_NUMBER, FVector::UpVector);
		const UPointLightComponent* Point = Cast<UPointLightComponent>(L);
		float Falloff = Point && !Point->bUseInverseSquaredFalloff
			? FMath::Pow(1.0f - float(D2 / (R * R)), Point->LightFalloffExponent)
			: FMath::Square(FMath::Clamp(1.0f - float(FMath::Square(D2 / (R * R))), 0.0f, 1.0f)) / float(D2 + 1.0);
		float CosOuter, CosInner;
		FVector Axis;
		// The cone only for a lamp outside the box: one inside it lights some of it whichever way it points (by the way to
		// the box's centre a spot aimed past the centre scored 0 and never lit the haze it stood in).
		if (D2 > 1.0 && OSGasLampCone(L, Axis, CosOuter, CosInner))
		{
			Falloff *= FMath::SmoothStep(CosOuter, CosInner, float(FVector::DotProduct(-Toward, Axis)));
		}
		return L->ComputeLightBrightness() * L->GetLightColor().GetLuminance() * Falloff;
	}
}

bool OSGasLampCone(const ULightComponent* L, FVector& OutAxis, float& OutCosOuter, float& OutCosInner)
{
	OutAxis = L->GetDirection();
	if (const USpotLightComponent* Spot = Cast<USpotLightComponent>(L))
	{
		const float Outer = FMath::Clamp(Spot->OuterConeAngle, 1.0f, 89.0f);
		OutCosOuter = FMath::Cos(FMath::DegreesToRadians(Outer));
		OutCosInner = FMath::Cos(FMath::DegreesToRadians(FMath::Min(Spot->InnerConeAngle, Outer - 0.5f)));
		return true;
	}
	if (L->IsA<URectLightComponent>())
	{
		// A rect as a point with the cosine to its normal (contract 6, 1.3).
		OutCosOuter = 0.0f;
		OutCosInner = 1.0f;
		return true;
	}
	return false;
}

void UOSGasSubsystem::UpdateLamps(FCloud& Cloud, const FVector& Centre, float DeltaTime, FVector3f& OutKeyDir)
{
	UWorld* World = GetWorld();
	// The world's lamps (KeyLights, refreshed once a second in Tick); their brightness read now.
	const APawn* Player = World->GetFirstPlayerController() ? World->GetFirstPlayerController()->GetPawn() : nullptr;
	const FVector Half(0.5f * RenderCells * Cloud.RenderCellCm);
	const FBox Box(Centre - Half, Centre + Half);
	// Each lamp scored at the box's point nearest it (a lamp at the edge of a big cloud counts as it lights that edge).
	struct FCandidate
	{
		const ULightComponent* Lamp;
		float Score;
		FVector Toward;
		bool bPlayers;
	};
	TArray<FCandidate, TInlineAllocator<16>> Candidates;
	// The world's lamps from KeyLights (refreshed each second), the player's own from this frame (PlayerLights).
	const auto OwnedByPlayer = [Player](const ULightComponent* L)
	{
		const AActor* Owner = L->GetOwner();
		return Player && Owner && (Owner == Player || Owner->GetAttachParentActor() == Player || Owner->GetOwner() == Player);
	};
	// The muzzle's flash: the player's point lamp without a cone that lights the world (not a GasOnly glow).
	const auto IsFlash = [&OwnedByPlayer](const ULightComponent* L)
	{
		return L->IsA<UPointLightComponent>() && !L->IsA<USpotLightComponent>() && L->bAffectsWorld && OwnedByPlayer(L);
	};
	TArray<const ULightComponent*, TInlineAllocator<64>> Lamps;
	for (const TWeakObjectPtr<ULightComponent>& Each : KeyLights)
	{
		if (const ULightComponent* L = Each.Get(); L && !OwnedByPlayer(L))
		{
			Lamps.Add(L);
		}
	}
	for (const TWeakObjectPtr<ULightComponent>& Each : PlayerLights)
	{
		if (const ULightComponent* L = Each.Get())
		{
			Lamps.Add(L);
		}
	}
	for (const ULightComponent* L : Lamps)
	{
		// A lamp for the gas only (tag GasOnly: the Nixie's glow) counts though it lights nothing in the world.
		if (!L->IsVisible() || (!L->bAffectsWorld && !L->ComponentHasTag(TEXT("GasOnly"))) || L->Intensity <= 0.0f)
		{
			continue;
		}
		// The box's point nearest the lamp - for a spot or a rect the one nearest its beam at the box centre's distance (the
		// point nearest the lamp itself is mostly outside its cone: a spot outside a haze scored 0).
		const FVector Pos = L->GetComponentLocation();
		const bool bBeam = L->IsA<USpotLightComponent>() || L->IsA<URectLightComponent>();
		const ULocalLightComponent* AsLocal = Cast<ULocalLightComponent>(L);
		const double Along = FMath::Min(FVector::Dist(Pos, Centre), AsLocal ? 0.5 * AsLocal->AttenuationRadius : 0.0);
		const FVector Near = L->IsA<UDirectionalLightComponent>() ? Centre
			: Box.IsInside(Pos) ? Pos : Box.GetClosestPointTo(bBeam ? Pos + L->GetDirection() * Along : Pos);
		FVector Toward;
		const float Score = LampAt(L, Near, Toward, Centre, IsFlash(L) ? CVarGasFlashReach.GetValueOnGameThread() : 1.0f);
		if (Score > 0.0f)
		{
			const AActor* Owner = L->GetOwner();
			const bool bPlayers = Player && Owner && (Owner == Player || Owner->GetAttachParentActor() == Player || Owner->GetOwner() == Player);
			Candidates.Add({ L, Score, Toward, bPlayers });
		}
	}
	// A haze with tile lamps (v13 13d): the player's lamps in their own 2 slots, never against the zone's; the zone's
	// strongest WorldSlots seen from 9 points of its box. Otherwise the strongest six traced from the centre, the best
	// four that see it kept (the player's among them).
	const bool bTiled = Cloud.bZone && CVarGasTileLamps.GetValueOnGameThread() > 0;
	const int32 WorldSlots = bTiled ? FMath::Clamp(CVarGasZoneLamps.GetValueOnGameThread(), 4, MaxZoneLamps) : MaxLamps;
	const int32 Used = bTiled ? MaxPlayerSlots + WorldSlots : MaxLamps;
	Candidates.Sort([](const FCandidate& A, const FCandidate& B) { return A.Score > B.Score; });
	TArray<FCandidate, TInlineAllocator<16>> Players, Zone;
	for (const FCandidate& C : Candidates)
	{
		(bTiled && C.bPlayers ? Players : Zone).Add(C);
	}
	const FBox Inset(Centre - Half * 0.8, Centre + Half * 0.8);
	const auto See = [&](TArray<FCandidate, TInlineAllocator<16>>& List, int32 Traced, int32 Keep, bool bNine)
	{
		List.SetNum(FMath::Min(List.Num(), Traced));
		for (FCandidate& Each : List)
		{
			const FVector Toward = Each.Lamp->IsA<UDirectionalLightComponent>() ? Each.Toward : (Each.Lamp->GetComponentLocation() - Centre).GetSafeNormal();
			Each.Score *= LampSees(Cloud, Each.Lamp, Centre, Toward, Each.Lamp->IsA<UDirectionalLightComponent>(), bNine ? &Inset : nullptr);
		}
		List.Sort([](const FCandidate& A, const FCandidate& B) { return A.Score > B.Score; });
		while (List.Num() > 0 && List.Last().Score <= 0.0f)
		{
			List.Pop();
		}
		List.SetNum(FMath::Min(List.Num(), Keep));
	};
#if !UE_BUILD_SHIPPING
	// os.Gas.Debug 2: a haze's shaft sources among its candidates (raw score, before the 9 points).
	if (Cloud.bZone && CVarGasDebug.GetValueOnGameThread() > 1 && FMath::FloorToInt(World->GetTimeSeconds()) != FMath::FloorToInt(World->GetTimeSeconds() - World->GetDeltaSeconds()))
	{
		for (const ULightComponent* L : Lamps)
		{
			if (L->ComponentHasTag(TEXT("GasShaft")))
			{
				FVector Way;
				const FVector P = L->GetComponentLocation();
				UE_LOG(LogMindKore, Log, TEXT("Gas: haze lamp %s visible %d world %d intensity %.0f, in box %d, raw at itself %.3g"), *L->GetOwner()->GetName(), L->IsVisible() ? 1 : 0,
					L->bAffectsWorld ? 1 : 0, L->Intensity, Box.IsInside(P) ? 1 : 0, LampAt(L, P, Way, Centre));
			}
		}
		int32 Rank = 0;
		for (const FCandidate& C : Zone)
		{
			if (C.Lamp->ComponentHasTag(TEXT("GasShaft")) || C.Lamp->IsA<UDirectionalLightComponent>())
			{
				const FCloud::FLampSeen* Seen = Cloud.LampsSeen.Find(C.Lamp);
				UE_LOG(LogMindKore, Log, TEXT("Gas: haze candidate %s rank %d of %d, raw %.3g, seen %.2f"), *C.Lamp->GetOwner()->GetName(), Rank, Zone.Num(), C.Score, Seen ? Seen->Seen : -1.0f);
			}
			++Rank;
		}
	}
#endif
	See(Zone, bTiled ? WorldSlots + 2 : 6, WorldSlots, bTiled);
	See(Players, MaxPlayerSlots, MaxPlayerSlots, false);
	// The crossfade: a lamp out of the set fades over 0.25 s, a new one comes in from 0 (the player's own at once; a new
	// cloud's first set at once too - it was born dimmer than the cloud beside it).
	const float Step = DeltaTime / 0.25f;
	const bool bFresh = Cloud.bLampsFresh;
	Cloud.bLampsFresh = false;
	const auto Assign = [&](int32 Begin, int32 End, const TArray<FCandidate, TInlineAllocator<16>>& List)
	{
		for (int32 Index = Begin; Index < End; ++Index)
		{
			FCloud::FLampSlot& Slot = Cloud.Lamps[Index];
			const FCandidate* In = List.FindByPredicate([&Slot](const FCandidate& C) { return Slot.Lamp.Get() == C.Lamp; });
			Slot.Weight = In ? FMath::Min(1.0f, Slot.Weight + Step) : Slot.Weight - Step;
			Slot.Score = In ? In->Score : Slot.Score;
			if (Slot.Weight <= 0.0f || !Slot.Lamp.IsValid())
			{
				Slot = FCloud::FLampSlot();
			}
		}
		for (const FCandidate& C : List)
		{
			bool bHas = false;
			FCloud::FLampSlot* Free = nullptr;
			for (int32 Index = Begin; Index < End; ++Index)
			{
				bHas |= Cloud.Lamps[Index].Lamp.Get() == C.Lamp;
				Free = !Free && !Cloud.Lamps[Index].Lamp.IsValid() ? &Cloud.Lamps[Index] : Free;
			}
			if (bHas)
			{
				continue;
			}
			if (!Free && C.bPlayers)
			{
				// The flash cannot wait for a slot: the faintest goes.
				Free = &Cloud.Lamps[Begin];
				for (int32 Index = Begin; Index < End; ++Index)
				{
					FCloud::FLampSlot& S = Cloud.Lamps[Index];
					Free = S.Weight * S.Score < Free->Weight * Free->Score ? &S : Free;
				}
			}
			if (Free)
			{
				Free->Lamp = C.Lamp;
				Free->Weight = C.bPlayers || bFresh ? 1.0f : 0.0f;
				Free->Score = C.Score;
				Free->KeyShare = 0.0f;
			}
		}
	};
	if (bTiled)
	{
		Assign(0, MaxPlayerSlots, Players);
		Assign(MaxPlayerSlots, Used, Zone);
	}
	else
	{
		Assign(0, MaxLamps, Zone);
	}
	for (int32 Index = Used; Index < MaxSlots; ++Index)
	{
		Cloud.Lamps[Index] = FCloud::FLampSlot();
	}
	// The key: the strongest (weighted); its direction from the centre. Its share of the baked self-shadow moves over
	// 0.25 s (the key changing flipped the shadow's model in a frame).
	Cloud.KeyLamp = INDEX_NONE;
	float Best = 0.0f;
	for (int32 Index = 0; Index < Used; ++Index)
	{
		const FCloud::FLampSlot& S = Cloud.Lamps[Index];
		if (const ULightComponent* L = S.Lamp.Get(); L && S.Weight * S.Score > Best)
		{
			Best = S.Weight * S.Score;
			Cloud.KeyLamp = Index;
			OutKeyDir = FVector3f(L->IsA<UDirectionalLightComponent>() ? -L->GetDirection() : (L->GetComponentLocation() - Centre).GetSafeNormal());
		}
	}
	for (int32 Index = 0; Index < Used; ++Index)
	{
		FCloud::FLampSlot& S = Cloud.Lamps[Index];
		S.KeyShare = FMath::Clamp(S.KeyShare + (Index == Cloud.KeyLamp ? Step : -Step), 0.0f, 1.0f);
	}
	// The haze's shaft source (v13 13c): the strongest sun, or spot/rect lamp tagged GasShaft, among its lamps.
	if (Cloud.bZone)
	{
		const ULightComponent* Shaft = nullptr;
		float Strongest = 0.0f;
		for (int32 Index = 0; Index < Used; ++Index)
		{
			const FCloud::FLampSlot& S = Cloud.Lamps[Index];
			const ULightComponent* L = S.Lamp.Get();
			if (L && (L->IsA<UDirectionalLightComponent>() || ((L->IsA<USpotLightComponent>() || L->IsA<URectLightComponent>()) && L->ComponentHasTag(TEXT("GasShaft")))) && S.Weight * S.Score > Strongest)
			{
				Strongest = S.Weight * S.Score;
				Shaft = L;
			}
		}
		Cloud.ShaftLamp = Shaft;
	}
#if !UE_BUILD_SHIPPING
	// os.Gas.Debug 2: the cloud's lamps in the log once a second.
	if (CVarGasDebug.GetValueOnGameThread() > 1 && FMath::FloorToInt(World->GetTimeSeconds()) != FMath::FloorToInt(World->GetTimeSeconds() - World->GetDeltaSeconds()))
	{
		FString Line;
		for (int32 Index = 0; Index < MaxSlots; ++Index)
		{
			const FCloud::FLampSlot& S = Cloud.Lamps[Index];
			if (const ULightComponent* L = S.Lamp.Get())
			{
				Line += FString::Printf(TEXT(" %s%s w%.2f s%.3g;"), Index == Cloud.KeyLamp ? TEXT("*") : TEXT(""), *L->GetOwner()->GetName(), S.Weight, S.Score);
			}
		}
		UE_LOG(LogMindKore, Log, TEXT("Gas: lamps%s"), Line.IsEmpty() ? TEXT(" none") : *Line);
	}
#endif
}

void UOSGasSubsystem::UpdateTorchShadow(float DeltaTime)
{
	// The torch: the player's lit spot lamp.
	const USpotLightComponent* Lamp = nullptr;
	for (const TWeakObjectPtr<ULightComponent>& Each : PlayerLights)
	{
		if (const USpotLightComponent* Spot = Cast<USpotLightComponent>(Each.Get()); Spot && Spot->IsVisible() && Spot->Intensity > 0.0f)
		{
			Lamp = Spot;
			break;
		}
	}
	if (!Lamp || CVarGasTorchShadow.GetValueOnGameThread() == 0)
	{
		Torch.bOn = false;
		return;
	}
	const int32 Res = FMath::Clamp(CVarGasTorchShadowRes.GetValueOnGameThread(), 4, MaxTorchRes);
	const FTransform Frame = Lamp->GetComponentTransform();
	const FVector Pos = Frame.GetLocation();
	const FVector Axis = Frame.GetUnitAxis(EAxis::X), U = Frame.GetUnitAxis(EAxis::Y), V = Frame.GetUnitAxis(EAxis::Z);
	const float TanOuter = FMath::Tan(FMath::DegreesToRadians(FMath::Clamp(Lamp->OuterConeAngle, 1.0f, 80.0f)));
	const float Range = Lamp->AttenuationRadius;
	const auto DirOf = [&](int32 I, const FVector& A, const FVector& UU, const FVector& VV, float Tan)
	{
		const float Uc = (I % Res + 0.5f) / Res * 2.0f - 1.0f, Vc = (I / Res + 0.5f) / Res * 2.0f - 1.0f;
		return (A + (UU * Uc + VV * Vc) * Tan).GetSafeNormal();
	};
	FCollisionQueryParams Params(SCENE_QUERY_STAT(OSGasTorchShadow), false);
	if (const APlayerController* Controller = GetWorld()->GetFirstPlayerController(); Controller && Controller->GetPawn())
	{
		Params.AddIgnoredActor(Controller->GetPawn());
		TArray<AActor*> Carried;
		Controller->GetPawn()->GetAttachedActors(Carried, true, true);
		Params.AddIgnoredActors(Carried);
	}
	const auto Trace = [&](const FVector& Dir)
	{
		FHitResult Hit;
		return GetWorld()->LineTraceSingleByChannel(Hit, Pos, Pos + Dir * Range, ECC_Visibility, Params) ? float(Hit.Distance) : Range;
	};
	const int32 Count = Res * Res;
	if (!Torch.bOn || Torch.Res != Res)
	{
		// Switched on: the map fills over its usual round (no 256 traces in one frame), its texels taken as traced (no ease
		// up from nothing); no shadow until the round is done, then it comes in over 0.1 s.
		Torch.Res = Res;
		Torch.Depth.Init(Range, Count);
		Torch.Target.Init(Range, Count);
		Torch.Cursor = Count - 1;
		Torch.Filled = 0;
		Torch.Strength = 0.0f;
		Torch.Axis = Axis;
		Torch.U = U;
		Torch.V = V;
		Torch.TanOuter = TanOuter;
	}
	{
		// The map re-sampled from the lamp's last frame into this one (its texels not yet traced again would ride with
		// the screen as it turns); outside the last map: the range.
		const TArray<float> OldDepth = Torch.Depth, OldTarget = Torch.Target;
		const auto Sample = [&](const TArray<float>& From, float Uo, float Vo)
		{
			const float X = (Uo * 0.5f + 0.5f) * Res - 0.5f, Y = (Vo * 0.5f + 0.5f) * Res - 0.5f;
			const int32 X0 = FMath::Clamp(FMath::FloorToInt(X), 0, Res - 1), Y0 = FMath::Clamp(FMath::FloorToInt(Y), 0, Res - 1);
			const int32 X1 = FMath::Min(X0 + 1, Res - 1), Y1 = FMath::Min(Y0 + 1, Res - 1);
			const float FX = FMath::Clamp(X - X0, 0.0f, 1.0f), FY = FMath::Clamp(Y - Y0, 0.0f, 1.0f);
			return FMath::Lerp(FMath::Lerp(From[Y0 * Res + X0], From[Y0 * Res + X1], FX), FMath::Lerp(From[Y1 * Res + X0], From[Y1 * Res + X1], FX), FY);
		};
		for (int32 I = 0; I < Count; ++I)
		{
			const FVector Dir = DirOf(I, Axis, U, V, TanOuter);
			const float Along = Dir | Torch.Axis;
			const float Uo = Along > 0.0f ? (Dir | Torch.U) / (Along * Torch.TanOuter) : 2.0f;
			const float Vo = Along > 0.0f ? (Dir | Torch.V) / (Along * Torch.TanOuter) : 2.0f;
			const bool bIn = FMath::Abs(Uo) <= 1.0f && FMath::Abs(Vo) <= 1.0f;
			Torch.Depth[I] = bIn ? Sample(OldDepth, Uo, Vo) : Range;
			Torch.Target[I] = bIn ? Sample(OldTarget, Uo, Vo) : Range;
		}
		// Traces in turn, then the ease.
		const int32 Traces = FMath::Clamp(CVarGasTorchTracesPerFrame.GetValueOnGameThread(), 1, Count);
		const bool bFirstRound = Torch.Filled < Count;
		for (int32 K = 0; K < Traces; ++K)
		{
			Torch.Cursor = (Torch.Cursor + 1) % Count;
			Torch.Target[Torch.Cursor] = Trace(DirOf(Torch.Cursor, Axis, U, V, TanOuter));
			if (bFirstRound)
			{
				Torch.Depth[Torch.Cursor] = Torch.Target[Torch.Cursor];
			}
		}
		Torch.Filled = FMath::Min(Torch.Filled + Traces, Count);
		Torch.Strength = Torch.Filled < Count ? 0.0f : FMath::Min(1.0f, Torch.Strength + DeltaTime / 0.1f);
		const float Ease = 1.0f - FMath::Exp(-DeltaTime / FMath::Max(CVarGasTorchShadowEase.GetValueOnGameThread(), 0.005f));
		for (int32 I = 0; I < Count && !bFirstRound; ++I)
		{
			Torch.Depth[I] += (Torch.Target[I] - Torch.Depth[I]) * Ease;
		}
	}
#if !UE_BUILD_SHIPPING
	// os.Gas.Debug 2: the torch's map once a second (its nearest, middle and farthest texel).
	if (CVarGasDebug.GetValueOnGameThread() > 1 && FMath::FloorToInt(GetWorld()->GetTimeSeconds()) != FMath::FloorToInt(GetWorld()->GetTimeSeconds() - DeltaTime))
	{
		TArray<float> Sorted = Torch.Depth;
		Sorted.Sort();
		FHitResult Probe;
		GetWorld()->LineTraceSingleByChannel(Probe, Pos, Pos + Axis * Range, ECC_Visibility, Params);
		UE_LOG(LogMindKore, Log, TEXT("Gas: torch map %d^2 near %.0f mid %.0f far %.0f cm; the axis hits %s at %.0f cm"), Res, Sorted[0], Sorted[Sorted.Num() / 2], Sorted.Last(),
			Probe.GetActor() ? *Probe.GetActor()->GetName() : TEXT("nothing"), Probe.Distance);
	}
#endif
	Torch.Pos = Pos;
	Torch.Axis = Axis;
	Torch.U = U;
	Torch.V = V;
	Torch.TanOuter = TanOuter;
	Torch.Range = Range;
	Torch.bOn = true;
	TArray<FLinearColor> Texels;
	Texels.SetNumZeroed(MaxTorchRes * MaxTorchRes);
	for (int32 I = 0; I < Count; ++I)
	{
		Texels[(I / Res) * MaxTorchRes + I % Res] = FLinearColor(Torch.Depth[I], 0.0f, 0.0f, 0.0f);
	}
	uint8* Bytes = static_cast<uint8*>(FMemory::Malloc(Texels.Num() * sizeof(FLinearColor)));
	FMemory::Memcpy(Bytes, Texels.GetData(), Texels.Num() * sizeof(FLinearColor));
	FUpdateTextureRegion2D* Region = new FUpdateTextureRegion2D(0, 0, 0, 0, MaxTorchRes, MaxTorchRes);
	TorchTexture->UpdateTextureRegions(0, 1, Region, MaxTorchRes * sizeof(FLinearColor), sizeof(FLinearColor), Bytes,
		[](uint8* Src, const FUpdateTextureRegion2D* Regions) { FMemory::Free(Src); delete Regions; });
}

TSharedPtr<FOSGasRoom> UOSGasSubsystem::RoomFor(const AOSGravityZone* Zone, const FVector& Where)
{
	if (TSharedPtr<FOSGasRoom>* Found = Rooms.Find(Zone))
	{
		return *Found;
	}
	// The zone's box within 15 m of the first shot, 25 cm cells, 32768 at most (the radius shrunk to fit).
	FVector Centre;
	const FCollisionShape Shape = Zone->GetQueryShape(Centre);
	const FVector Extent = Shape.IsSphere() ? FVector(Shape.GetSphereRadius()) : Shape.GetExtent();
	const FBox ZoneBox(Centre - Extent, Centre + Extent);
	TSharedPtr<FOSGasRoom> Room = MakeShared<FOSGasRoom>();
	float Reach = 1500.0f;
	FBox Box;
	for (;;)
	{
		Box = ZoneBox.Overlap(FBox(Where - FVector(Reach), Where + FVector(Reach)));
		const FVector Size = Box.GetSize();
		Room->Dim = FIntVector(FMath::Max(1, FMath::CeilToInt(Size.X / Room->Cell)), FMath::Max(1, FMath::CeilToInt(Size.Y / Room->Cell)), FMath::Max(1, FMath::CeilToInt(Size.Z / Room->Cell)));
		if (int64(Room->Dim.X) * Room->Dim.Y * Room->Dim.Z <= 32768 || Reach < 200.0f)
		{
			break;
		}
		Reach *= 0.85f;
	}
	Room->Lo = Box.Min;
	Room->Cells.SetNumZeroed(Room->Dim.X * Room->Dim.Y * Room->Dim.Z);
	const FIntVector Seed = Room->CellOf(Where);
	if (Room->Inside(Seed))
	{
		Room->Cells[Room->Index(Seed)] = 3;
		Room->Queue.Add(Room->Index(Seed));
	}
	Rooms.Add(Zone, Room);
	UE_LOG(LogMindKore, Log, TEXT("Gas: the room of %s floods from %s (%dx%dx%d cells of %.0f cm)"), *Zone->GetName(), *Where.ToCompactString(), Room->Dim.X, Room->Dim.Y, Room->Dim.Z, Room->Cell);
	return Room;
}

void UOSGasSubsystem::FloodRooms()
{
	if (CVarGasHazeRoom.GetValueOnGameThread() == 0 || Rooms.IsEmpty())
	{
		return;
	}
	const double Until = FPlatformTime::Seconds() + CVarGasHazeFillMs.GetValueOnGameThread() / 1000.0;
	UWorld* World = GetWorld();
	FCollisionQueryParams Params(SCENE_QUERY_STAT(OSGasRoom), false);
	if (const APlayerController* Controller = World->GetFirstPlayerController(); Controller && Controller->GetPawn())
	{
		Params.AddIgnoredActor(Controller->GetPawn());
		TArray<AActor*> Carried;
		Controller->GetPawn()->GetAttachedActors(Carried, true, true);
		Params.AddIgnoredActors(Carried);
	}
	FCollisionObjectQueryParams Types;
	Types.AddObjectTypesToQuery(ECC_WorldStatic);
	Types.AddObjectTypesToQuery(ECC_WorldDynamic);
	for (auto& Pair : Rooms)
	{
		FOSGasRoom& Room = *Pair.Value;
		// Written only while its haze takes no step (the step reads the cells).
		const AOSGravityZone* Zone = Pair.Key.ResolveObjectPtr();
		if (!Zone || Room.Head >= Room.Queue.Num() || Clouds.ContainsByPredicate([Zone](const FCloud& C) { return C.bZone && C.IsAlive() && C.Zone.Get() == Zone && C.IsBusy(); }))
		{
			continue;
		}
		while (Room.Head < Room.Queue.Num() && FPlatformTime::Seconds() < Until)
		{
			const int32 I = Room.Queue[Room.Head++];
			const FIntVector C(I % Room.Dim.X, (I / Room.Dim.X) % Room.Dim.Y, I / (Room.Dim.X * Room.Dim.Y));
			const FVector At = Room.CentreOf(C);
			// Solid: something static in the cell (a box 0.4 of it: a jamb does not close a doorway), or no air there.
			const bool bSolid = World->OverlapAnyTestByObjectType(At, FQuat::Identity, Types, FCollisionShape::MakeBox(FVector(0.4f * Room.Cell)), Params)
				|| OSAirZoneAt(World, At) != Zone;
			Room.Cells[I] = bSolid ? 2 : 1;
			// The fog seeds from the flood's air cells; a floor found under an air cell found earlier sends that one again.
			if (FFogZone* Fog = FogZones.Find(Pair.Key); Fog && Fog->bAwake)
			{
				const FIntVector Up = C + FIntVector(0, 0, 1);
				if (!bSolid)
				{
					Fog->SeedQueue.Add(I);
				}
				else if (Room.Inside(Up) && Room.Cells[Room.Index(Up)] == 1)
				{
					Fog->SeedQueue.Add(Room.Index(Up));
				}
			}
			if (bSolid)
			{
				continue;
			}
			for (const FIntVector& Step : { FIntVector(1, 0, 0), FIntVector(-1, 0, 0), FIntVector(0, 1, 0), FIntVector(0, -1, 0), FIntVector(0, 0, 1), FIntVector(0, 0, -1) })
			{
				const FIntVector N = C + Step;
				if (Room.Inside(N) && Room.Cells[Room.Index(N)] == 0)
				{
					Room.Cells[Room.Index(N)] = 3;
					Room.Queue.Add(Room.Index(N));
				}
			}
		}
		if (Room.Head >= Room.Queue.Num())
		{
			int32 Air = 0, Solid = 0;
			for (const uint8 Each : Room.Cells)
			{
				Air += Each == 1 ? 1 : 0;
				Solid += Each == 2 ? 1 : 0;
			}
			UE_LOG(LogMindKore, Log, TEXT("Gas: the room of %s flooded - %d air cells (%.1f m3), %d walls"), *Zone->GetName(), Air, Air * FMath::Cube(Room.Cell / 100.0f), Solid);
			// Done: the queue's memory back.
			Room.Queue.Empty();
			Room.Head = 0;
		}
	}
}

void UOSGasSubsystem::RoomChanged(const FBox& Bounds)
{
	for (auto& Pair : Rooms)
	{
		FOSGasRoom& Room = *Pair.Value;
		const FIntVector A = Room.CellOf(Bounds.Min), B = Room.CellOf(Bounds.Max);
		for (int32 Z = FMath::Max(A.Z, 0); Z <= FMath::Min(B.Z, Room.Dim.Z - 1); ++Z)
		{
			for (int32 Y = FMath::Max(A.Y, 0); Y <= FMath::Min(B.Y, Room.Dim.Y - 1); ++Y)
			{
				for (int32 X = FMath::Max(A.X, 0); X <= FMath::Min(B.X, Room.Dim.X - 1); ++X)
				{
					const int32 I = Room.Index(FIntVector(X, Y, Z));
					if (Room.Cells[I] != 3)
					{
						Room.Cells[I] = 3;
						Room.Queue.Add(I);
					}
				}
			}
		}
	}
}

UOSGasSubsystem::FFogZone* UOSGasSubsystem::FogOf(const AOSGravityZone* Zone)
{
	FFogZone& Fog = FogZones.FindOrAdd(Zone);
	if (!Fog.bOverride)
	{
		Fog.Fog = Zone->Fog;
		Fog.Density = Zone->FogDensity;
		Fog.HeightCm = Zone->FogHeightCm;
		Fog.Colour = Zone->FogColour;
	}
	return &Fog;
}

void UOSGasSubsystem::SetFogOverride(const AOSGravityZone* Zone, EOSZoneFog Fog, float Density, float HeightCm)
{
	FFogZone& State = *FogOf(Zone);
	const bool bChanged = State.Fog != Fog || State.Density != Density || State.HeightCm != HeightCm;
	State.bOverride = true;
	State.Fog = Fog;
	State.Density = Density;
	State.HeightCm = HeightCm;
	State.Colour = Fog == EOSZoneFog::Haze ? FLinearColor(0.84f, 0.84f, 0.84f) : FLinearColor(0.88f, 0.88f, 0.88f);
	// A new fog over an old one: the old background goes (its Base to 0 - it ventilates out), the new one seeds afresh.
	if (bChanged)
	{
		for (FCloud& Cloud : Clouds)
		{
			if (Cloud.bZone && Cloud.IsAlive() && Cloud.Zone.Get() == Zone && !Cloud.IsBusy())
			{
				for (FOSGasParcel& P : Cloud.Parcels)
				{
					P.Base = 0.0f;
				}
				for (FOSGasParcel& P : Cloud.Incoming)
				{
					P.Base = 0.0f;
				}
			}
		}
		State.Seeded.Reset();
		State.SeedQueue.Reset();
		State.SeedHead = 0;
		State.bAwake = false;
		State.RetryIn = 0.0f;
	}
}

void UOSGasSubsystem::TickFog(float DeltaTime)
{
	GFogRefill = CVarGasFogRefill.GetValueOnGameThread();
	UWorld* World = GetWorld();
	const bool bOn = CVarGasFog.GetValueOnGameThread() > 0;
	FVector Eye = FVector::ZeroVector;
	if (const APlayerController* Controller = World->GetFirstPlayerController(); Controller && Controller->PlayerCameraManager)
	{
		Eye = Controller->PlayerCameraManager->GetCameraLocation();
	}
	// The zones with a fog (their fields or an override), each a frame.
	for (TActorIterator<AOSGravityZone> It(World); It; ++It)
	{
		if (It->Fog != EOSZoneFog::None)
		{
			FogOf(*It);
		}
	}
	const double Until = FPlatformTime::Seconds() + 0.0001;
	const float Wake = CVarGasFogWakeCm.GetValueOnGameThread();
	for (auto& Pair : FogZones)
	{
		const AOSGravityZone* Zone = Pair.Key.ResolveObjectPtr();
		FFogZone& Fog = Pair.Value;
		if (!Zone)
		{
			continue;
		}
		if (!Fog.bOverride)
		{
			FogOf(Zone);
		}
		FVector Centre;
		const FCollisionShape Shape = Zone->GetQueryShape(Centre);
		const FVector Extent = Shape.IsSphere() ? FVector(Shape.GetSphereRadius()) : Shape.GetExtent();
		const FBox Box(Centre - Extent, Centre + Extent);
		const double Away = FMath::Sqrt(Box.ComputeSquaredDistanceToPoint(Eye));
		int32 Slot = INDEX_NONE;
		for (int32 Index = MaxClouds; Index < Clouds.Num(); ++Index)
		{
			Slot = Clouds[Index].IsAlive() && Clouds[Index].Zone.Get() == Zone ? Index : Slot;
		}
		const bool bWant = bOn && Fog.Fog != EOSZoneFog::None && (Fog.bAwake ? Away < 1.25 * Wake : Away < Wake);
		if (!bWant)
		{
			// Asleep (or off): the fog ramps out over 1 s, then its cloud gives its slot back (as an eviction).
			if (Fog.bAwake && Slot != INDEX_NONE)
			{
				FCloud& Cloud = Clouds[Slot];
				Cloud.FogIn = FMath::Max(0.0f, Cloud.FogIn - DeltaTime);
				if (Cloud.FogIn <= 0.0f && Cloud.Evict <= 0.0f)
				{
					Cloud.Evict = 1e-4f;
					Fog.bAwake = false;
					Fog.Seeded.Reset();
					Fog.SeedQueue.Reset();
					Fog.SeedHead = 0;
					UE_LOG(LogMindKore, Log, TEXT("Gas: the fog of %s sleeps"), *Zone->GetName());
				}
			}
			else if (Fog.bAwake)
			{
				Fog.bAwake = false;
			}
			continue;
		}
		if (!Fog.bAwake)
		{
			// Waking: its room, a haze slot (none: again in 0.5 s), the seeding from the room's air.
			if ((Fog.RetryIn -= DeltaTime) > 0.0f)
			{
				continue;
			}
			Fog.RetryIn = 0.5f;
			// The seed: the zone's centre, or the nearest free cell round it (26 ways, then outward to 2 m).
			FVector Seed = Centre;
			{
				FCollisionQueryParams Params(SCENE_QUERY_STAT(OSGasFogSeed), false);
				FCollisionObjectQueryParams Types;
				Types.AddObjectTypesToQuery(ECC_WorldStatic);
				Types.AddObjectTypesToQuery(ECC_WorldDynamic);
				const auto Free = [&](const FVector& At) { return !World->OverlapAnyTestByObjectType(At, FQuat::Identity, Types, FCollisionShape::MakeBox(FVector(10.0f)), Params) && OSAirZoneAt(World, At) == Zone; };
				bool bFound = Free(Seed);
				for (float Step = 25.0f; !bFound && Step <= 200.0f; Step += 25.0f)
				{
					for (int32 K = 0; K < 27 && !bFound; ++K)
					{
						const FVector Try = Centre + FVector(K % 3 - 1, (K / 3) % 3 - 1, K / 9 - 1) * Step;
						if (Free(Try))
						{
							Seed = Try;
							bFound = true;
						}
					}
				}
			}
			const TSharedPtr<FOSGasRoom> Room = RoomFor(Zone, Seed);
			Slot = ZoneCloudFor(Zone);
			if (Slot == INDEX_NONE)
			{
				continue;
			}
			FCloud& Cloud = Clouds[Slot];
			Cloud.FogIn = Cloud.Parcels.IsEmpty() && Cloud.Incoming.IsEmpty() ? 0.0f : Cloud.FogIn;
			Fog.bAwake = true;
			Fog.Seeded.Reset();
			Fog.SeedQueue.Reset();
			Fog.SeedHead = 0;
			// Cells the flood already found: all of them to the seeding.
			for (int32 I = 0; I < Room->Cells.Num(); ++I)
			{
				if (Room->Cells[I] == 1)
				{
					Fog.SeedQueue.Add(I);
				}
			}
			const bool bFloor = Fog.Fog == EOSZoneFog::Floor && Zone->bWalk && Zone->GravityCm > 0.0f;
			const FVector Size = Box.GetSize();
			// The lattice: neighbours 1.6 radii apart (closer, the spheres show as separate patches), ~250 buckets over the zone's box at most.
			const float FloorRadius = FMath::Min(CVarGasHazeRadius.GetValueOnGameThread(), 0.8f * Fog.HeightCm);
			Fog.Spacing = bFloor ? FMath::Max(1.6f * FloorRadius, float(FMath::Sqrt(Size.X * Size.Y / 250.0))) : FMath::Max(80.0f, float(FMath::Pow(Size.X * Size.Y * Size.Z / 250.0, 1.0 / 3.0)));
			UE_LOG(LogMindKore, Log, TEXT("Gas: the fog of %s wakes (%s, density %.2f, spacing %.0f cm)"), *Zone->GetName(), bFloor ? TEXT("floor") : TEXT("haze"), Fog.Density, Fog.Spacing);
		}
		if (Slot == INDEX_NONE)
		{
			Fog.bAwake = false;
			continue;
		}
		FCloud& Cloud = Clouds[Slot];
		Cloud.FogIn = FMath::Min(1.0f, Cloud.FogIn + DeltaTime);
		Cloud.Evict = 0.0f;
		// The seeding, within 0.1 ms a frame, only while the haze takes no step.
		const TSharedPtr<FOSGasRoom>* RoomRef = Rooms.Find(Zone);
		if (!RoomRef || Cloud.IsBusy())
		{
			continue;
		}
		const FOSGasRoom& Room = **RoomRef;
		const bool bFloor = Fog.Fog == EOSZoneFog::Floor && Zone->bWalk && Zone->GravityCm > 0.0f;
		// A haze's parcels 0.65 of the lattice wide (neighbours ~1.5 radii apart; the haze's 60 cm left round blotches).
		const float Radius = bFloor ? FMath::Min(Cloud.HazeRadius, 0.8f * Fog.HeightCm) : FMath::Min(CVarGasHazeRadiusMax.GetValueOnGameThread(), 0.65f * Fog.Spacing);
		// A haze half a floor layer's mass a parcel (calibrated: TestRoom 0.5 - a floor CentreT ~0.35, a haze ~0.6).
		const float Mass = Fog.Density * CVarGasFogMass.GetValueOnGameThread() * (bFloor ? 1.0f : 0.5f);
		const float Fall = bFloor ? CVarGasHazeDiffuseZ.GetValueOnGameThread() * CVarGasHazeDiffuse.GetValueOnGameThread() / FMath::Max(Fog.HeightCm, 10.0f) / AirRestSeconds * CVarGasFogFallScale.GetValueOnGameThread() : 0.0f;
		while (Fog.SeedHead < Fog.SeedQueue.Num() && FPlatformTime::Seconds() < Until && Cloud.Parcels.Num() + Cloud.Incoming.Num() < MaxZoneParcels)
		{
			const int32 I = Fog.SeedQueue[Fog.SeedHead++];
			const FIntVector C(I % Room.Dim.X, (I / Room.Dim.X) % Room.Dim.Y, I / (Room.Dim.X * Room.Dim.Y));
			// A floor layer: a cell on the floor (solid or the grid's edge below it); a haze: any air cell.
			if (bFloor)
			{
				const FIntVector Below = C - FIntVector(0, 0, 1);
				// Not yet known below: sent again once the flood finds a floor there.
				if (Room.Inside(Below) && Room.Cells[Room.Index(Below)] != 2)
				{
					continue;
				}
			}
			const FVector At = Room.CentreOf(C);
			const FIntVector Bucket = bFloor ? FIntVector(FMath::FloorToInt(At.X / Fog.Spacing), FMath::FloorToInt(At.Y / Fog.Spacing), 0)
				: FIntVector(FMath::FloorToInt(At.X / Fog.Spacing), FMath::FloorToInt(At.Y / Fog.Spacing), FMath::FloorToInt(At.Z / Fog.Spacing));
			if (Fog.Seeded.Contains(Bucket))
			{
				continue;
			}
			Fog.Seeded.Add(Bucket);
			FOSGasParcel& P = Cloud.Incoming.AddDefaulted_GetRef();
			const FVector Jitter(FMath::FRandRange(-0.3f, 0.3f) * Fog.Spacing, FMath::FRandRange(-0.3f, 0.3f) * Fog.Spacing, 0.0f);
			P.P = FVector3f(At + Jitter - Cloud.Origin);
			P.Birth = P.P;
			P.RadiusCm = P.MaxRadiusCm = Radius;
			P.Mass = P.Base = Mass;
			P.Heat = P.Warm = 0.0f;
			P.Age = 2.0f;
			P.Life = 1e9f;
			P.bFog = true;
			P.Fall = Fall;
			P.Seed = FMath::Rand();
			P.Air = P.AirTarget = 1.0f;
			P.Flat = Zone->GravityCm >= 0.05f * 980.0f ? FMath::Clamp(CVarGasHazeFlat.GetValueOnGameThread(), 0.2f, 1.0f) : 1.0f;
		}
	}
}

float UOSGasSubsystem::LampSees(FCloud& Cloud, const ULightComponent* Lamp, const FVector& Where, const FVector& Toward, bool bDirectional, const FBox* Points)
{
	UWorld* World = GetWorld();
	const double Now = World->GetTimeSeconds();
	const bool bNew = !Cloud.LampsSeen.Contains(Lamp);
	FCloud::FLampSeen& Seen = Cloud.LampsSeen.FindOrAdd(Lamp);
	// A haze's lamp (v13 13d): the share of 9 points of its box that see it, one trace in turn (all 9 in 0.25 s) - a lamp
	// behind a door no longer switched the whole haze on or off with one trace from its centre.
	if (Points && Now - Seen.Traced >= 0.25 / 9.0)
	{
		Seen.Traced = Now;
		const int32 K = Seen.Next;
		Seen.Next = (Seen.Next + 1) % 9;
		const FVector From = K == 0 ? Points->GetCenter() : FVector((K - 1) & 1 ? Points->Max.X : Points->Min.X, (K - 1) & 2 ? Points->Max.Y : Points->Min.Y, (K - 1) & 4 ? Points->Max.Z : Points->Min.Z);
		const FVector Way = bDirectional ? Toward : (Lamp->GetComponentLocation() - From).GetSafeNormal();
		const FVector End = bDirectional ? From + Way * 20000.0 : Lamp->GetComponentLocation() - Way * 30.0;
		FCollisionQueryParams Params(SCENE_QUERY_STAT(OSGasLampSees), false);
		if (const APlayerController* Controller = World->GetFirstPlayerController(); Controller && Controller->GetPawn())
		{
			const APawn* Pawn = Controller->GetPawn();
			Params.AddIgnoredActor(Pawn);
			TArray<AActor*> Carried;
			Pawn->GetAttachedActors(Carried, true, true);
			Params.AddIgnoredActors(Carried);
		}
		Params.AddIgnoredActor(Lamp->GetOwner());
		const float Hit = World->LineTraceTestByChannel(From, End, ECC_Visibility, Params) ? 0.0f : 1.0f;
		if (bNew)
		{
			for (float& Each : Seen.Hits)
			{
				Each = Hit;
			}
		}
		Seen.Hits[K] = Hit;
		float Sum = 0.0f;
		for (const float Each : Seen.Hits)
		{
			Sum += Each;
		}
		Seen.Target = Sum / 9.0f;
	}
	else if (!Points && Now - Seen.Traced >= 0.25)
	{
		Seen.Traced = Now;
		// To the lamp (30 cm short of it: its own shade) or 200 m toward a sun; the player and what they carry left out.
		const FVector End = bDirectional ? Where + Toward * 20000.0 : Lamp->GetComponentLocation() - Toward * 30.0;
		FCollisionQueryParams Params(SCENE_QUERY_STAT(OSGasLampSees), false);
		if (const APlayerController* Controller = World->GetFirstPlayerController(); Controller && Controller->GetPawn())
		{
			const APawn* Pawn = Controller->GetPawn();
			Params.AddIgnoredActor(Pawn);
			TArray<AActor*> Carried;
			Pawn->GetAttachedActors(Carried, true, true);
			Params.AddIgnoredActors(Carried);
		}
		Params.AddIgnoredActor(Lamp->GetOwner());
		Seen.Target = World->LineTraceTestByChannel(Where, End, ECC_Visibility, Params) ? 0.0f : 1.0f;
	}
	Seen.Seen = bNew ? Seen.Target : Seen.Seen + (Seen.Target - Seen.Seen) * (1.0f - FMath::Exp(-World->GetDeltaSeconds() / 0.15f));
	return Seen.Seen;
}

void UOSGasSubsystem::PickKey(const FVector& Where, FVector3f& OutDir, FLinearColor& OutColour)
{
	float Best = 0.0f;
	FVector Dir = FVector::UpVector;
	FLinearColor Colour = FLinearColor(0.6f, 0.65f, 0.75f);
	for (const TWeakObjectPtr<ULightComponent>& Each : KeyLights)
	{
		const ULightComponent* L = Each.Get();
		if (!L || !L->IsVisible() || !L->bAffectsWorld || L->Intensity <= 0.0f)
		{
			continue;
		}
		const float Brightness = L->ComputeLightBrightness();
		float Score;
		FVector Toward;
		if (L->IsA<UDirectionalLightComponent>())
		{
			Toward = -L->GetDirection();
			Score = Brightness;
		}
		else
		{
			const ULocalLightComponent* Local = Cast<ULocalLightComponent>(L);
			const FVector Offset = L->GetComponentLocation() - Where;
			const float Cm = Offset.Size();
			if (!Local || Cm > Local->AttenuationRadius)
			{
				continue;
			}
			Toward = Offset / FMath::Max(Cm, 1.0f);
			const UPointLightComponent* Point = Cast<UPointLightComponent>(L);
			Score = Point && !Point->bUseInverseSquaredFalloff
				? Brightness * FMath::Pow(1.0f - Cm / Local->AttenuationRadius, Point->LightFalloffExponent) * 10.0f
				: Brightness / FMath::Max(FMath::Square(Cm / 100.0f), 0.1f);
		}
		if (Score > Best)
		{
			Best = Score;
			Dir = Toward;
			Colour = L->GetLightColor();
		}
	}
	OutDir = FVector3f(Dir);
	OutColour = Colour * FMath::Min(Best / UE_PI * KeyStrength, KeyMax);

}

bool OSGasCardsEnabled()
{
	return CVarGasCards.GetValueOnGameThread() != 0;
}

#if !UE_BUILD_SHIPPING
namespace
{
	/**
	 * os.Gas.TestShaft [distance cm]: a transient spot lamp tagged GasShaft that far ahead of the view (behind a window),
	 * aimed back at the eye - the haze's shafts (v13 13c) on a map without a sun through its windows. Not saved.
	 */
	FAutoConsoleCommandWithWorldAndArgs GGasTestShaft(
		TEXT("os.Gas.TestShaft"),
		TEXT("[distance cm]: a transient GasShaft spot lamp that far ahead of the view, aimed back (the haze's shafts on a map without a sun in its windows)."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
		{
			const APlayerController* Controller = World ? World->GetFirstPlayerController() : nullptr;
			if (!Controller)
			{
				return;
			}
			FVector Eye;
			FRotator View;
			Controller->GetPlayerViewPoint(Eye, View);
			const float Distance = Args.Num() > 0 ? FCString::Atof(*Args[0]) : 600.0f;
			const FVector At = Eye + View.Vector() * Distance + FVector::UpVector * 30.0f;
			FActorSpawnParameters Params;
			Params.ObjectFlags |= RF_Transient;
			ASpotLight* Lamp = World->SpawnActor<ASpotLight>(At, (Eye - At).Rotation(), Params);
			if (!Lamp)
			{
				return;
			}
			USpotLightComponent* Spot = Cast<USpotLightComponent>(Lamp->GetLightComponent());
			Spot->SetMobility(EComponentMobility::Movable);
			Spot->SetIntensityUnits(ELightUnits::Candelas);
			Spot->SetIntensity(3000.0f);
			Spot->SetOuterConeAngle(30.0f);
			Spot->SetInnerConeAngle(20.0f);
			Spot->SetAttenuationRadius(Distance * 2.5f);
			Spot->SetLightColor(FLinearColor(1.0f, 0.92f, 0.8f));
			Spot->ComponentTags.Add(TEXT("GasShaft"));
			UE_LOG(LogMindKore, Log, TEXT("os.Gas.TestShaft: a GasShaft spot at %s, aimed at the eye"), *At.ToCompactString());
		}));
}
#endif

#if !UE_BUILD_SHIPPING
namespace
{
	/** os.Gas.Fog <floor|haze|off> [density] [heightCm]: the fog of the zone the player stands in, in memory (the map untouched). */
	FAutoConsoleCommandWithWorldAndArgs GGasFogCommand(
		TEXT("os.Gas.FogSet"),
		TEXT("<floor|haze|off> [density 0-1] [height cm]: the fog of the zone the player stands in, for this play only (the map is untouched)."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
		{
			const APlayerController* Controller = World ? World->GetFirstPlayerController() : nullptr;
			UOSGasSubsystem* Gas = UOSGasSubsystem::Get(World);
			if (!Controller || !Controller->GetPawn() || !Gas || Args.IsEmpty())
			{
				return;
			}
			const AOSGravityZone* Zone = OSAirZoneAt(World, Controller->GetPawn()->GetActorLocation());
			if (!Zone)
			{
				UE_LOG(LogMindKore, Warning, TEXT("os.Gas.FogSet: no air zone here"));
				return;
			}
			const EOSZoneFog Fog = Args[0].Equals(TEXT("floor"), ESearchCase::IgnoreCase) ? EOSZoneFog::Floor : Args[0].Equals(TEXT("haze"), ESearchCase::IgnoreCase) ? EOSZoneFog::Haze : EOSZoneFog::None;
			const float Density = Args.Num() > 1 ? FMath::Clamp(FCString::Atof(*Args[1]), 0.0f, 1.0f) : 0.5f;
			const float Height = Args.Num() > 2 ? FMath::Clamp(FCString::Atof(*Args[2]), 30.0f, 120.0f) : 60.0f;
			Gas->SetFogOverride(Zone, Fog, Density, Height);
			UE_LOG(LogMindKore, Log, TEXT("os.Gas.FogSet: %s %s %.2f %.0f cm"), *Zone->GetName(), *Args[0], Density, Height);
		}));
}
#endif
