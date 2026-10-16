#pragma once

#include "CoreMinimal.h"
#include "MKGlassFracture.h"
#include "ProceduralMeshComponent.h"

/**
 * Turning a flat convex shard into the solid the renderer and the physics both
 * want. Shared by the pane (which builds many shards into two sections) and by
 * the debris actor (which builds exactly one), so a piece that flies out of a
 * window is the same solid it was a moment before, down to the rim.
 *
 * Winding. Unreal draws a triangle whose vertices run CLOCKWISE as seen from
 * the outside, which in its left-handed space means CrossProduct(V1 - V0,
 * V2 - V0) points AGAINST the outward normal. The note that stood here said
 * the opposite, and was believed for as long as every consumer was two-sided:
 * the pane's glass is, so its faces drew either way round. The debris material
 * is one-sided, and with the ring fanned in its own counter-clockwise order a
 * shard on a counter drew its far face - the underside, coincident with the
 * counter top - and z-fought with it. Checked by eye, not by argument: a
 * 120x80x60 probe with an opaque one-sided material rendered as an open box
 * seen from inside until the fans were reversed (2026-09-10).
 *
 * A shard's vertices are counter-clockwise in pane space (U along X, V along
 * Z). Fanned against that order they face -Y, the FRONT of the sheet; the back
 * face is the same ring fanned with it.
 */
struct FMKGlassMeshBuild
{
	TArray<FVector> Vertices;
	TArray<int32> Triangles;
	TArray<FVector> Normals;
	TArray<FVector2D> UVs;
	TArray<FProcMeshTangent> Tangents;
};

/**
 * Append one shard as a closed prism: front face, back face, and the rim around
 * the outline. When RimBuild is supplied, the thin cut edge is written to that
 * build so it can carry a brighter material without frosting the shard faces.
 *
 * UvOriginCm is subtracted from the shard's pane-space position before the
 * vertices are written, which is how a debris actor gets its geometry centred
 * on its own origin while keeping the texture it had in the frame: UVs are
 * always measured from the pane's centre, never from the piece's.
 */
MINDKORE_API void MKAppendShardGeometry(
	FMKGlassMeshBuild& Build,
	const FMKGlassShard& Shard,
	float HalfThicknessCm,
	float UvTilingCm,
	const FVector2D& UvOriginCm = FVector2D::ZeroVector,
	FMKGlassMeshBuild* RimBuild = nullptr);

/** The convex hull for one shard: its outline at both faces. */
MINDKORE_API TArray<FVector> MKMakeShardHull(
	const FMKGlassShard& Shard,
	float HalfThicknessCm,
	const FVector2D& OriginCm = FVector2D::ZeroVector);
