#pragma once

#include "CoreMinimal.h"
#include "Math/RandomStream.h"

/**
 * Procedural glass fracture, in the plane of the pane and with no UWorld in it.
 *
 * The model is the one Half-Life 2 and the Counter-Strikes read as glass: a
 * sheet is whole until it is hit, a hit cuts radial cracks out of the impact
 * point and chords across them, and the pieces inside the impact fall out while
 * the cracked ones stay in the frame until something knocks them out too.
 *
 * Everything is convex on purpose. A shard is a convex polygon, every cut is a
 * half-plane, and a half-plane cut of a convex polygon gives two convex
 * polygons - which keeps the triangulation trivial and lets each shard become
 * one convex hull for collision. Nothing here needs a general polygon clipper.
 *
 * Pane space: U along the pane's width, V along its height, both measured from
 * the pane CENTRE, in centimetres.
 */

/** One piece of glass. Vertices are convex and counter-clockwise. */
struct FMKGlassShard
{
	TArray<FVector2D> Vertices;

	/**
	 * True once a fracture has cut this piece loose from its neighbours: still
	 * standing in the frame, but no longer part of an unbroken sheet. Drives
	 * which material it draws with, nothing else.
	 */
	bool bCracked = false;

	/**
	 * True once a CRACK cut this piece - as opposed to the bookkeeping cuts the
	 * fracture makes at its own boundary, or the grid a block wall starts as.
	 * Only these edges are fresh glass, and only they earn the accent material
	 * on the rim: a piece can be cut and never draw frosted (a runner crack
	 * three metres from the hole), and a piece can draw frosted and never be cut
	 * (a neighbour marked when the pane was over budget).
	 */
	bool bFreshCut = false;
};

/** The pane as it leaves the factory: one rectangle, no cracks. */
MINDKORE_API TArray<FMKGlassShard> MKMakeGlassSheet(float WidthCm, float HeightCm);

/**
 * Pre-cut a sheet into a grid of blocks. The vitrine in J is a glass-block wall
 * and has to keep reading as one; every other pane starts as a single sheet.
 * A pitch of zero or less leaves the sheet alone.
 */
MINDKORE_API void MKSplitGlassIntoBlocks(TArray<FMKGlassShard>& Shards, float PitchCm, float WidthCm, float HeightCm);

MINDKORE_API double MKPolygonArea(TArrayView<const FVector2D> Polygon);
MINDKORE_API FVector2D MKPolygonCentroid(TArrayView<const FVector2D> Polygon);
MINDKORE_API bool MKPolygonContains(TArrayView<const FVector2D> Polygon, const FVector2D& Point);

/** Zero when the point is inside, otherwise the distance to the nearest edge. */
MINDKORE_API float MKPolygonDistanceTo(TArrayView<const FVector2D> Polygon, const FVector2D& Point);

/**
 * Take out every piece that no longer hangs off the frame.
 *
 * Glass is held by its edges. A piece seated in the frame - two of its corners
 * on the same side of the pane, i.e. an edge in the frame and not a corner
 * touching it - stays up; a piece that touches only other pieces stays up as
 * long as one of THEM is seated; a piece that reaches the frame through nobody
 * is standing in mid-air, and until this function existed that is exactly what
 * a shot left behind - an island of glass hanging in the hole it just made.
 *
 * A piece smaller than MinSupportedAreaCm2 holds on to nothing whatever it
 * touches: a chip of glass left standing on its own in an empty frame is the
 * one thing a broken window never does. Such pieces neither stay up nor hold
 * their neighbours up. Pass 0 to keep every size.
 *
 * The pieces that come loose are moved into OutFallen in the order they were
 * found, so the caller can throw them as debris. Returns how many fell.
 */
MINDKORE_API int32 MKDropUnsupportedShards(
	TArray<FMKGlassShard>& Shards,
	float WidthCm,
	float HeightCm,
	float MinSupportedAreaCm2,
	TArray<FMKGlassShard>& OutFallen,
	bool bBottomSupportedOnly = false);

/**
 * Cut a convex polygon by the line through Origin whose normal is Normal.
 * OutFront keeps the side the normal points at, OutBack the other one. Either
 * can come back empty, which is how "the cut missed this piece" is reported.
 */
MINDKORE_API void MKSplitConvexPolygon(
	TArrayView<const FVector2D> Polygon,
	const FVector2D& Origin,
	const FVector2D& Normal,
	TArray<FVector2D>& OutFront,
	TArray<FVector2D>& OutBack);

/**
 * Cut one released piece into the pieces that actually reach the floor.
 *
 * A shard is sized for the FRAME: big enough that the hole reads, cheap enough
 * that the next round can keep cutting it. The same polygon lying on a counter
 * is a slab. Glass that leaves a frame breaks again on the way out, so a
 * released piece is split until every piece is under MaxPieceAreaCm2 or the
 * budget of MaxPieces is spent - whichever comes first, because a pane taken
 * out by one round is mostly big shards and dicing every one of them to the
 * target area would put a hundred bodies on the floor for a single shot.
 *
 * Each cut is a half-plane through the piece's centroid, so the pieces stay
 * convex and each one is still a single hull for collision. They are appended
 * to OutPieces and the return value is how many were appended; a piece nothing
 * can cut in two is appended at the size it has. MaxPieceAreaCm2 at or below
 * zero, or MaxPieces below two, appends the shard unchanged.
 */
MINDKORE_API int32 MKShatterShard(
	const FMKGlassShard& Shard,
	float MaxPieceAreaCm2,
	int32 MaxPieces,
	FRandomStream& Random,
	TArray<FMKGlassShard>& OutPieces);

struct FMKGlassFractureParams
{
	/**
	 * The pane's own size. Needed because a fracture has to know where the frame
	 * is: the frame is the only thing holding glass up, and a piece that no
	 * longer reaches it comes down (MKDropUnsupportedShards).
	 */
	float PaneWidthCm = 400.0f;
	float PaneHeightCm = 260.0f;

	/** Pieces whose centroid lands inside this fall out of the frame. */
	float BreakRadiusCm = 45.0f;

	/**
	 * How far out the cracks reach. The cuts are bounded by a polygon drawn
	 * around this radius, so the cracks reach it in every direction and up to
	 * 8% past it at the polygon's corners; beyond that the glass is not touched.
	 */
	float FractureRadiusCm = 130.0f;

	/**
	 * How far from the hit the glass LOOKS cracked, as opposed to merely being
	 * cut. The two are not the same distance and were treated as one at first:
	 * every piece a cut touched drew with the cracked material, so a single
	 * round turned the whole sheet into frosted glass and the partition stopped
	 * being see-through. Zero means the pane never draws cracked at all.
	 */
	float CrackedLookRadiusCm = 90.0f;

	/**
	 * How ragged the edge of the frosted zone is, as a fraction of the radius
	 * above. Zero draws the frost inside a circle, and a circle is the one shape
	 * broken glass never makes: the boundary was visible AS a circle, and read
	 * as a decal rather than as damage. Each piece rolls its own radius inside
	 * 1 +- this.
	 */
	float CrackedLookJitter = 0.3f;

	/** Radial cracks out of the impact point. */
	int32 SpokeCount = 9;

	/**
	 * How many of those spokes keep going past the fracture radius and run to
	 * the frame. Real glass does this and a web bounded by a circle does not:
	 * the circle says "a bullet went through a sheet of ice", the runner says
	 * "that pane is finished". They cut the untouched leftovers rather than the
	 * web, so the cost is a couple of pieces per round, not a second web.
	 *
	 * A runner does NOT draw frosted - it is a hairline, and its whole visible
	 * width is the fresh cut edge (bFreshCut), which is why it needs the pane's
	 * edge accent material to read at all.
	 */
	int32 RunnerCrackCount = 2;

	/** Chords across the spokes, i.e. how many rings of shards the hit leaves. */
	int32 RingCount = 2;

	/**
	 * A cut that would leave a piece smaller than this is skipped. It keeps the
	 * pieces big enough to see; it does NOT keep the count down on its own -
	 * that is what MaxShardCount is for.
	 */
	float MinShardAreaCm2 = 45.0f;

	/**
	 * The shard budget for one pane. Measured on a 400x260 sheet with the
	 * defaults below: one round leaves 25 pieces, and ten rounds walked across
	 * the pane leave 334 - and every one of them is a convex hull to cook and a
	 * rim to draw on the next shot.
	 *
	 * Once the pane is at or over its budget a round still opens a hole and
	 * still cracks what it reaches, but cuts no new pieces. A pane that has been
	 * fired into a dozen times is already a wreck; the twelfth round's job is to
	 * take glass out of the frame, not to add detail nobody reads.
	 *
	 * A round is judged before it cuts, so the count can overshoot the budget by
	 * one round's worth of pieces.
	 *
	 * The number is a budget, not a profile: nothing has been measured in a
	 * frame yet, so it is set where the geometry is still generous.
	 */
	int32 MaxShardCount = 300;
};

struct FMKGlassFractureResult
{
	/**
	 * The pieces that left the frame on this hit, with their geometry, so the
	 * caller can throw them rather than make them vanish. Two kinds are in here
	 * and the caller does not need to tell them apart: what the round knocked
	 * out, and what was left hanging on nothing once that hole existed.
	 */
	TArray<FMKGlassShard> FallenShards;

	/** How many pieces that was. */
	int32 RemovedShards = 0;

	/** How much glass that was, in square centimetres. Zero means nothing changed. */
	double RemovedAreaCm2 = 0.0;

	/** Pieces the cracks cut loose but left standing. */
	int32 CrackedShards = 0;

	/** How many pieces the runner cracks cut outside the web. */
	int32 RunnerCutShards = 0;
};

/**
 * One round into the glass at HitUV. Rewrites Shards in place.
 *
 * A hit that lands where there is no glass left - through the hole the last
 * round made - changes nothing and reports zero, which is the caller's cue to
 * spend no sound and no noise event on it.
 *
 * Deterministic for a given RandomStream, so a pane can be replayed.
 */
MINDKORE_API FMKGlassFractureResult MKFractureGlassAt(
	TArray<FMKGlassShard>& Shards,
	const FVector2D& HitUV,
	const FMKGlassFractureParams& Params,
	FRandomStream& RandomStream);
