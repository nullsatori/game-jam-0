// The gas drawn once per screen block (v11 stage 5): the data the game thread hands the render thread, and the pass.

#pragma once

#include "CoreMinimal.h"
#include "Templates/RefCounting.h"

class FRDGBuilder;
class FSceneView;
struct FPostProcessMaterialInputs;
struct FScreenPassTexture;
class FTextureResource;
struct IPooledRenderTarget;

/** One frame's gas for the pass: UOSGasSubsystem's textures and counts (the same as M_OS_GasPost's parameters). */
struct FOSGasRenderData
{
	static constexpr int32 MaxGrids = 14;
	FTextureResource* Grids[MaxGrids] = {};
	FTextureResource* Data = nullptr;
	FTextureResource* Tunnels = nullptr;
	int32 CloudCount = 0;
	int32 TunnelCount = 0;
	float BlockPx = 4.0f;
	float EyeClear = 30.0f;
	/** os.Gas.DebugAlpha: 1-2 the gas's opacity alone; 3 the block's opacity less the pixel's own march, x4 (an A/B check). */
	float DebugMode = 0.0f;
	/** Stage 6: os.Gas.Light (1 the room's light from the frame, the key lamp's full colour; 0 as before) and its tuning. */
	float Light = 1.0f;
	float AmbientScale = 1.0f;
	float LightScale = 1.0f;
	float Albedo = 1.0f;
	/** The Henyey-Greenstein share of the lamps' phase (os.Gas.Forward; the rest isotropic). */
	float Forward = 0.7f;
	/** The composite (v12 7a.6): os.Gas.Dither, os.Gas.Bands. */
	float Dither = 1.0f;
	float Bands = 4.0f;
	/** v12 7d: the Bayer threshold's cell, px; 1: the block's result read bilinearly between the blocks' centres. */
	float DitherPx = 2.0f;
	float SmoothAlpha = 0.0f;
	/** v13 13e: os.Gas.DitherHold, os.Gas.DitherGrain. */
	float DitherHold = 0.15f;
	float DitherGrain = 0.5f;
	/** v13 13d: os.Gas.TileSlots (the zone's lamps a block takes, 4-6). */
	float TileSlots = 4.0f;
	/** v13 13a: the torch's shadow map (distance per texel), its frame, on/off. */
	FTextureResource* TorchShadow = nullptr;
	float TorchOn = 0.0f;
	FVector3f TorchPos = FVector3f::ZeroVector;
	FVector3f TorchAxis = FVector3f::ForwardVector;
	FVector3f TorchU = FVector3f::RightVector;
	FVector3f TorchV = FVector3f::UpVector;
	float TorchTan = 0.3f;
	float TorchRes = 16.0f;
	/** v12 7b: the heat puffs (shimmer) and the laser's beam (lens, dot, light; black: off). */
	FTextureResource* Heat = nullptr;
	int32 HeatCount = 0;
	float Shimmer = 1.0f;
	FVector3f LaserFrom = FVector3f::ZeroVector;
	FVector3f LaserTo = FVector3f::ZeroVector;
	FLinearColor LaserLight = FLinearColor::Black;
	/** The game's frame time, s: the room's light follows the frame over AmbientSeconds (0.1 s; shorter while the view
	 * turns - a screen-space history not reprojected left a bright spot's trail, the torch's, in the haze). */
	float DeltaSeconds = 1.0f / 60.0f;
	float AmbientSeconds = 0.1f;
	/** 1: the history clamped into the current neighbours' range (a ghost of a spot the view has left cannot stay). */
	float AmbientClamp = 1.0f;
};

/**
 * After the tonemapper (where M_OS_GasPost drew): one compute thread per screen block marches the block's ray to its
 * farthest depth and flags a block whose depths differ; the composite takes the block's result, or marches its own
 * pixel in a flagged block. Returns the frame with the gas over it.
 */
MINDKORESHADERS_API FScreenPassTexture OSAddGasPasses(FRDGBuilder& GraphBuilder, const FSceneView& View, const FPostProcessMaterialInputs& Inputs,
	const FOSGasRenderData& Data, TRefCountPtr<IPooledRenderTarget>& AmbientHistory, TRefCountPtr<IPooledRenderTarget>& HoldHistory);
