// The gas drawn once per screen block (v11 stage 5): the data the game thread hands the render thread, and the pass.

#pragma once

#include "CoreMinimal.h"

class FRDGBuilder;
class FSceneView;
struct FPostProcessMaterialInputs;
struct FScreenPassTexture;
class FTextureResource;

/** One frame's gas for the pass: UOSGasSubsystem's textures and counts (the same as M_OS_GasPost's parameters). */
struct FOSGasRenderData
{
	static constexpr int32 MaxGrids = 10;
	FTextureResource* Grids[MaxGrids] = {};
	FTextureResource* Data = nullptr;
	FTextureResource* Tunnels = nullptr;
	int32 CloudCount = 0;
	int32 TunnelCount = 0;
	float BlockPx = 4.0f;
	float EyeClear = 30.0f;
	/** os.Gas.DebugAlpha: 1-2 the gas's opacity alone; 3 the block's opacity less the pixel's own march, x4 (an A/B check). */
	float DebugMode = 0.0f;
};

/**
 * After the tonemapper (where M_OS_GasPost drew): one compute thread per screen block marches the block's ray to its
 * farthest depth and flags a block whose depths differ; the composite takes the block's result, or marches its own
 * pixel in a flagged block. Returns the frame with the gas over it.
 */
MINDKORESHADERS_API FScreenPassTexture OSAddGasPasses(FRDGBuilder& GraphBuilder, const FSceneView& View, const FPostProcessMaterialInputs& Inputs,
	const FOSGasRenderData& Data);
