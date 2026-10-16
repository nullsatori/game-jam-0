#include "MKGlassFracture.h"

namespace
{
	// Distances here are centimetres of glass, so a tenth of a millimetre is
	// comfortably below anything that can be seen or shot at.
	constexpr double MKGlassEpsilon = 0.01;

	bool IsDegenerate(TArrayView<const FVector2D> Polygon)
	{
		return Polygon.Num() < 3 || MKPolygonArea(Polygon) < MKGlassEpsilon;
	}
}

double MKPolygonArea(TArrayView<const FVector2D> Polygon)
{
	if (Polygon.Num() < 3)
	{
		return 0.0;
	}

	double Twice = 0.0;
	for (int32 Index = 0; Index < Polygon.Num(); ++Index)
	{
		const FVector2D& A = Polygon[Index];
		const FVector2D& B = Polygon[(Index + 1) % Polygon.Num()];
		Twice += static_cast<double>(A.X) * B.Y - static_cast<double>(B.X) * A.Y;
	}
	return FMath::Abs(Twice) * 0.5;
}

FVector2D MKPolygonCentroid(TArrayView<const FVector2D> Polygon)
{
	if (Polygon.Num() == 0)
	{
		return FVector2D::ZeroVector;
	}

	double Twice = 0.0;
	double CentroidX = 0.0;
	double CentroidY = 0.0;
	for (int32 Index = 0; Index < Polygon.Num(); ++Index)
	{
		const FVector2D& A = Polygon[Index];
		const FVector2D& B = Polygon[(Index + 1) % Polygon.Num()];
		const double Cross = static_cast<double>(A.X) * B.Y - static_cast<double>(B.X) * A.Y;
		Twice += Cross;
		CentroidX += (static_cast<double>(A.X) + B.X) * Cross;
		CentroidY += (static_cast<double>(A.Y) + B.Y) * Cross;
	}

	// A collapsed piece has no area to weight by; the vertex average is still a
	// point inside it, which is all the caller wants.
	if (FMath::Abs(Twice) < MKGlassEpsilon)
	{
		FVector2D Sum = FVector2D::ZeroVector;
		for (const FVector2D& Vertex : Polygon)
		{
			Sum += Vertex;
		}
		return Sum / Polygon.Num();
	}

	return FVector2D(
		static_cast<float>(CentroidX / (3.0 * Twice)),
		static_cast<float>(CentroidY / (3.0 * Twice)));
}

bool MKPolygonContains(TArrayView<const FVector2D> Polygon, const FVector2D& Point)
{
	if (Polygon.Num() < 3)
	{
		return false;
	}

	// Convex and counter-clockwise: inside means left of every edge.
	for (int32 Index = 0; Index < Polygon.Num(); ++Index)
	{
		const FVector2D& A = Polygon[Index];
		const FVector2D& B = Polygon[(Index + 1) % Polygon.Num()];
		const FVector2D Edge = B - A;
		const FVector2D ToPoint = Point - A;
		if (Edge.X * ToPoint.Y - Edge.Y * ToPoint.X < -MKGlassEpsilon)
		{
			return false;
		}
	}
	return true;
}

float MKPolygonDistanceTo(TArrayView<const FVector2D> Polygon, const FVector2D& Point)
{
	if (Polygon.Num() == 0)
	{
		return TNumericLimits<float>::Max();
	}
	if (MKPolygonContains(Polygon, Point))
	{
		return 0.0f;
	}

	float Nearest = TNumericLimits<float>::Max();
	for (int32 Index = 0; Index < Polygon.Num(); ++Index)
	{
		const FVector2D& A = Polygon[Index];
		const FVector2D& B = Polygon[(Index + 1) % Polygon.Num()];
		const FVector2D Edge = B - A;
		const float LengthSq = Edge.SizeSquared();
		const float Alpha = LengthSq > UE_KINDA_SMALL_NUMBER
			? FMath::Clamp(FVector2D::DotProduct(Point - A, Edge) / LengthSq, 0.0f, 1.0f)
			: 0.0f;
		Nearest = FMath::Min(Nearest, FVector2D::Distance(Point, A + Edge * Alpha));
	}
	return Nearest;
}

int32 MKDropUnsupportedShards(
	TArray<FMKGlassShard>& Shards,
	float WidthCm,
	float HeightCm,
	float MinSupportedAreaCm2,
	TArray<FMKGlassShard>& OutFallen,
	bool bBottomSupportedOnly)
{
	if (Shards.Num() == 0)
	{
		return 0;
	}

	// Chips go first, before anything is asked about support. A piece this small
	// does not hold itself and cannot hold a neighbour, so leaving it in the
	// graph would also let it prop up the piece next to it.
	int32 Fallen = 0;
	if (MinSupportedAreaCm2 > 0.0f)
	{
		TArray<FMKGlassShard> BigEnough;
		BigEnough.Reserve(Shards.Num());
		for (FMKGlassShard& Shard : Shards)
		{
			if (MKPolygonArea(Shard.Vertices) < MinSupportedAreaCm2)
			{
				++Fallen;
				OutFallen.Add(MoveTemp(Shard));
				continue;
			}
			BigEnough.Add(MoveTemp(Shard));
		}
		Shards = MoveTemp(BigEnough);
	}

	const int32 ShardCount = Shards.Num();
	if (ShardCount == 0)
	{
		return Fallen;
	}

	// Half a centimetre: wide enough to join two pieces whose shared corner was
	// computed twice, in two different cuts, and came out a float apart; far
	// below any gap a player could see.
	constexpr float MKGlassContactCm = 0.5f;

	const float HalfWidth = FMath::Max(WidthCm, 0.0f) * 0.5f;
	const float HalfHeight = FMath::Max(HeightCm, 0.0f) * 0.5f;

	TArray<FBox2D> Bounds;
	Bounds.Reserve(ShardCount);
	TArray<bool> bHeld;
	bHeld.Init(false, ShardCount);

	TArray<int32> Frontier;
	for (int32 Index = 0; Index < ShardCount; ++Index)
	{
		FBox2D Box(ForceInit);

		// A frame holds glass by an EDGE seated in it, not by a corner touching
		// it. Counting a single vertex was the first version, and it left pieces
		// standing in mid-air that hung off the frame by one point - which is
		// exactly what a corner of glass does in life: it falls.
		int32 SideContacts[4] = { 0, 0, 0, 0 };
		for (const FVector2D& Vertex : Shards[Index].Vertices)
		{
			Box += Vertex;
			SideContacts[0] += FMath::Abs(Vertex.X + HalfWidth) <= MKGlassContactCm ? 1 : 0;
			SideContacts[1] += FMath::Abs(Vertex.X - HalfWidth) <= MKGlassContactCm ? 1 : 0;
			SideContacts[2] += FMath::Abs(Vertex.Y + HalfHeight) <= MKGlassContactCm ? 1 : 0;
			SideContacts[3] += FMath::Abs(Vertex.Y - HalfHeight) <= MKGlassContactCm ? 1 : 0;
		}
		Bounds.Add(Box);

		const bool bSeatedInFrame =
			SideContacts[2] >= 2 || (!bBottomSupportedOnly &&
			(SideContacts[0] >= 2 || SideContacts[1] >= 2 || SideContacts[3] >= 2));
		if (bSeatedInFrame)
		{
			bHeld[Index] = true;
			Frontier.Add(Index);
		}
	}

	// Two pieces are neighbours when two of one's corners sit on the other's
	// outline. One shared corner is a touch, not a join: two pieces meeting at a
	// point hold nothing. Corners are tested against the whole outline rather
	// than against corners, so a T-joint - where a cut was skipped on one side
	// and its neighbour has the extra vertex - still counts.
	auto AreNeighbours = [&Bounds, &Shards](int32 A, int32 B)
	{
		if (!Bounds[A].ExpandBy(MKGlassContactCm).Intersect(Bounds[B]))
		{
			return false;
		}

		int32 Contacts = 0;
		for (const FVector2D& Vertex : Shards[A].Vertices)
		{
			if (MKPolygonDistanceTo(Shards[B].Vertices, Vertex) <= MKGlassContactCm && ++Contacts >= 2)
			{
				return true;
			}
		}

		Contacts = 0;
		for (const FVector2D& Vertex : Shards[B].Vertices)
		{
			if (MKPolygonDistanceTo(Shards[A].Vertices, Vertex) <= MKGlassContactCm && ++Contacts >= 2)
			{
				return true;
			}
		}
		return false;
	};

	// Flood out from the frame: whatever the flood never reaches is hanging in
	// mid-air.
	for (int32 Cursor = 0; Cursor < Frontier.Num(); ++Cursor)
	{
		const int32 Current = Frontier[Cursor];
		for (int32 Other = 0; Other < ShardCount; ++Other)
		{
			if (!bHeld[Other] && AreNeighbours(Current, Other))
			{
				bHeld[Other] = true;
				Frontier.Add(Other);
			}
		}
	}

	TArray<FMKGlassShard> Survivors;
	Survivors.Reserve(Frontier.Num());
	for (int32 Index = 0; Index < ShardCount; ++Index)
	{
		if (bHeld[Index])
		{
			Survivors.Add(MoveTemp(Shards[Index]));
			continue;
		}

		++Fallen;
		OutFallen.Add(MoveTemp(Shards[Index]));
	}

	Shards = MoveTemp(Survivors);
	return Fallen;
}

void MKSplitConvexPolygon(
	TArrayView<const FVector2D> Polygon,
	const FVector2D& Origin,
	const FVector2D& Normal,
	TArray<FVector2D>& OutFront,
	TArray<FVector2D>& OutBack)
{
	OutFront.Reset();
	OutBack.Reset();

	if (Polygon.Num() < 3)
	{
		return;
	}

	const FVector2D UnitNormal = Normal.GetSafeNormal();
	if (UnitNormal.IsNearlyZero())
	{
		OutFront.Append(Polygon.GetData(), Polygon.Num());
		return;
	}

	auto SignedDistance = [&Origin, &UnitNormal](const FVector2D& Point)
	{
		return static_cast<double>(FVector2D::DotProduct(Point - Origin, UnitNormal));
	};

	for (int32 Index = 0; Index < Polygon.Num(); ++Index)
	{
		const FVector2D& A = Polygon[Index];
		const FVector2D& B = Polygon[(Index + 1) % Polygon.Num()];
		const double DistanceA = SignedDistance(A);
		const double DistanceB = SignedDistance(B);

		if (DistanceA >= -MKGlassEpsilon)
		{
			OutFront.Add(A);
		}
		if (DistanceA <= MKGlassEpsilon)
		{
			OutBack.Add(A);
		}

		// Only a genuine sign change makes a new vertex; a vertex sitting on the
		// line was already handed to both sides above.
		if ((DistanceA > MKGlassEpsilon && DistanceB < -MKGlassEpsilon) ||
			(DistanceA < -MKGlassEpsilon && DistanceB > MKGlassEpsilon))
		{
			const double Alpha = DistanceA / (DistanceA - DistanceB);
			const FVector2D Crossing = A + (B - A) * static_cast<float>(Alpha);
			OutFront.Add(Crossing);
			OutBack.Add(Crossing);
		}
	}

	if (IsDegenerate(OutFront))
	{
		OutFront.Reset();
	}
	if (IsDegenerate(OutBack))
	{
		OutBack.Reset();
	}
}

int32 MKShatterShard(
	const FMKGlassShard& Shard,
	float MaxPieceAreaCm2,
	int32 MaxPieces,
	FRandomStream& Random,
	TArray<FMKGlassShard>& OutPieces)
{
	if (Shard.Vertices.Num() < 3)
	{
		return 0;
	}

	TArray<FMKGlassShard> Pieces;
	Pieces.Add(Shard);

	if (MaxPieceAreaCm2 > 0.0f && MaxPieces >= 2)
	{
		// A piece nothing could cut in two is retired rather than dropped: it
		// still falls, it just stops being offered to the loop, which would
		// otherwise pick the same slab every pass and never finish.
		TArray<bool> Retired;
		Retired.Add(false);

		while (Pieces.Num() < MaxPieces)
		{
			// The largest piece goes first, so a small budget is spent where the
			// slab is instead of on whatever sits at the front of the array.
			int32 WorstIndex = INDEX_NONE;
			double WorstArea = MaxPieceAreaCm2;
			for (int32 Index = 0; Index < Pieces.Num(); ++Index)
			{
				if (Retired[Index])
				{
					continue;
				}
				const double Area = MKPolygonArea(Pieces[Index].Vertices);
				if (Area > WorstArea)
				{
					WorstArea = Area;
					WorstIndex = Index;
				}
			}

			if (WorstIndex == INDEX_NONE)
			{
				break;
			}

			const FVector2D Centre = MKPolygonCentroid(Pieces[WorstIndex].Vertices);
			TArray<FVector2D> Front;
			TArray<FVector2D> Back;

			// More than one direction, because a cut through the centroid of a
			// long sliver can still land along it: both new edges fall inside
			// the epsilon and one side comes back empty.
			constexpr int32 MaxAttempts = 4;
			for (int32 Attempt = 0; Attempt < MaxAttempts; ++Attempt)
			{
				const float Angle = Random.FRandRange(0.0f, UE_PI);
				MKSplitConvexPolygon(
					Pieces[WorstIndex].Vertices,
					Centre,
					FVector2D(FMath::Cos(Angle), FMath::Sin(Angle)),
					Front,
					Back);
				if (Front.Num() >= 3 && Back.Num() >= 3)
				{
					break;
				}
			}

			if (Front.Num() < 3 || Back.Num() < 3)
			{
				Retired[WorstIndex] = true;
				continue;
			}

			FMKGlassShard Half = Pieces[WorstIndex];
			Pieces[WorstIndex].Vertices = MoveTemp(Front);
			Half.Vertices = MoveTemp(Back);
			// Both halves are fresh glass: the edge between them was made by the
			// piece breaking, not by the fracture's bookkeeping at its boundary.
			Pieces[WorstIndex].bFreshCut = true;
			Half.bFreshCut = true;
			Pieces.Add(MoveTemp(Half));
			Retired.Add(false);
		}
	}

	const int32 Added = Pieces.Num();
	OutPieces.Append(MoveTemp(Pieces));
	return Added;
}

TArray<FMKGlassShard> MKMakeGlassSheet(float WidthCm, float HeightCm)
{
	const float HalfWidth = FMath::Max(WidthCm, 0.0f) * 0.5f;
	const float HalfHeight = FMath::Max(HeightCm, 0.0f) * 0.5f;

	TArray<FMKGlassShard> Shards;
	if (HalfWidth <= 0.0f || HalfHeight <= 0.0f)
	{
		return Shards;
	}

	FMKGlassShard Sheet;
	Sheet.Vertices = {
		FVector2D(-HalfWidth, -HalfHeight),
		FVector2D(HalfWidth, -HalfHeight),
		FVector2D(HalfWidth, HalfHeight),
		FVector2D(-HalfWidth, HalfHeight)
	};
	Shards.Add(MoveTemp(Sheet));
	return Shards;
}

void MKSplitGlassIntoBlocks(TArray<FMKGlassShard>& Shards, float PitchCm, float WidthCm, float HeightCm)
{
	if (PitchCm <= 0.0f || Shards.Num() == 0)
	{
		return;
	}

	const int32 Columns = FMath::Max(1, FMath::RoundToInt(WidthCm / PitchCm));
	const int32 Rows = FMath::Max(1, FMath::RoundToInt(HeightCm / PitchCm));

	TArray<FVector2D> CutOrigins;
	TArray<FVector2D> CutNormals;
	for (int32 Column = 1; Column < Columns; ++Column)
	{
		CutOrigins.Add(FVector2D(-0.5f * WidthCm + Column * (WidthCm / Columns), 0.0f));
		CutNormals.Add(FVector2D(1.0f, 0.0f));
	}
	for (int32 Row = 1; Row < Rows; ++Row)
	{
		CutOrigins.Add(FVector2D(0.0f, -0.5f * HeightCm + Row * (HeightCm / Rows)));
		CutNormals.Add(FVector2D(0.0f, 1.0f));
	}

	TArray<FVector2D> Front;
	TArray<FVector2D> Back;
	for (int32 CutIndex = 0; CutIndex < CutOrigins.Num(); ++CutIndex)
	{
		TArray<FMKGlassShard> NextShards;
		NextShards.Reserve(Shards.Num() * 2);
		for (const FMKGlassShard& Shard : Shards)
		{
			MKSplitConvexPolygon(Shard.Vertices, CutOrigins[CutIndex], CutNormals[CutIndex], Front, Back);
			if (Front.Num() >= 3)
			{
				NextShards.Add(FMKGlassShard{ Front, Shard.bCracked });
			}
			if (Back.Num() >= 3)
			{
				NextShards.Add(FMKGlassShard{ Back, Shard.bCracked });
			}
			if (Front.Num() < 3 && Back.Num() < 3)
			{
				NextShards.Add(Shard);
			}
		}
		Shards = MoveTemp(NextShards);
	}
}

FMKGlassFractureResult MKFractureGlassAt(
	TArray<FMKGlassShard>& Shards,
	const FVector2D& HitUV,
	const FMKGlassFractureParams& Params,
	FRandomStream& RandomStream)
{
	FMKGlassFractureResult Result;

	// A round through an existing hole finds no glass to break. Reported as
	// zero rather than as a fracture of the nearest surviving shard: the player
	// aimed through a hole, and nothing should answer.
	bool bHitGlass = false;
	for (const FMKGlassShard& Shard : Shards)
	{
		if (MKPolygonContains(Shard.Vertices, HitUV))
		{
			bHitGlass = true;
			break;
		}
	}
	if (!bHitGlass)
	{
		return Result;
	}

	const float FractureRadius = FMath::Max(Params.FractureRadiusCm, Params.BreakRadiusCm);
	const int32 SpokeCount = FMath::Max(Params.SpokeCount, 3);
	const int32 RingCount = FMath::Max(Params.RingCount, 0);

	// Over budget the round is not refused, it is narrowed: only the piece it
	// landed in is cut up, and its neighbours are left as geometry and merely
	// marked cracked. A pane holding this many pieces is already a spiderweb, so
	// the round still opens its hole and still spreads cracks, but it stops
	// adding hulls to cook and rims to draw over the whole fracture radius.
	const bool bOverBudget = Shards.Num() >= FMath::Max(Params.MaxShardCount, 1);

	// The crack pattern is a spiderweb, and a spiderweb is built in two passes:
	// the spokes go all the way through the impact point, and then each wedge
	// between two spokes is barred by its own chords. Straight cuts only - a
	// ring built from chords stays convex, a ring built from an arc does not.
	//
	// The second pass has to be per wedge. Cutting every piece with every chord
	// instead is an arrangement of 27 lines, not a web: on a 400x260 sheet that
	// turned one round into 89 shards where the wedges and rings promise 27, and
	// RingCount stopped meaning "how many rings of shards a hit leaves". Per
	// wedge the same round leaves 25.
	const float SpokeStep = 2.0f * UE_PI / SpokeCount;
	TArray<float> SpokeAngles;
	SpokeAngles.Reserve(SpokeCount);
	for (int32 SpokeIndex = 0; SpokeIndex < SpokeCount; ++SpokeIndex)
	{
		// The jitter stays inside +-0.35 of a step, so two spokes never cross
		// and the angles come out sorted - which is what the wedge lookup below
		// relies on.
		const float Jitter = RandomStream.FRandRange(-0.35f, 0.35f) * SpokeStep;
		SpokeAngles.Add(SpokeIndex * SpokeStep + Jitter);
	}

	TArray<TArray<float>> WedgeRingRadii;
	WedgeRingRadii.Reserve(SpokeCount);
	for (int32 WedgeIndex = 0; WedgeIndex < SpokeCount; ++WedgeIndex)
	{
		TArray<float>& Radii = WedgeRingRadii.AddDefaulted_GetRef();
		Radii.Reserve(RingCount);
		for (int32 RingIndex = 1; RingIndex <= RingCount; ++RingIndex)
		{
			const float Base = FractureRadius * RingIndex / (RingCount + 1);
			Radii.Add(FMath::Max(Base * RandomStream.FRandRange(0.65f, 1.35f), 1.0f));
		}
	}

	auto IsWhollyInsideHole = [&HitUV, &Params](TArrayView<const FVector2D> Polygon)
	{
		for (const FVector2D& Vertex : Polygon)
		{
			if (FVector2D::Distance(Vertex, HitUV) > Params.BreakRadiusCm)
			{
				return false;
			}
		}
		return Polygon.Num() >= 3;
	};

	TArray<FMKGlassShard> Untouched;
	TArray<FMKGlassShard> Working;
	Untouched.Reserve(Shards.Num());
	for (FMKGlassShard& Shard : Shards)
	{
		const bool bWithinReach = MKPolygonDistanceTo(Shard.Vertices, HitUV) <= FractureRadius;

		// Over budget, "within reach" narrows to the piece the round landed in.
		// The rest still crack - that is a flag, not a cut - but keep their shape.
		const bool bCutThisOne = bOverBudget
			? MKPolygonContains(Shard.Vertices, HitUV)
			: bWithinReach;

		if (bCutThisOne)
		{
			Working.Add(MoveTemp(Shard));
			continue;
		}

		// A neighbour that lies WHOLLY inside the hole falls out even over
		// budget: dropping it costs no new geometry, keeps the hole the size the
		// break radius promises, and takes the pane back towards its budget.
		// Whole means every corner of it - a big piece whose centre happens to
		// sit near the hit stays in the frame instead of a quarter of the pane
		// disappearing on one round.
		if (bOverBudget && IsWhollyInsideHole(Shard.Vertices))
		{
			++Result.RemovedShards;
			Result.RemovedAreaCm2 += MKPolygonArea(Shard.Vertices);
			continue;
		}

		// Glass further out than the cracks reach is not glass this round
		// touched, and has to come out of the fracture unchanged.
		if (bOverBudget && bWithinReach && !Shard.bCracked)
		{
			Shard.bCracked = true;
			++Result.CrackedShards;
		}
		Untouched.Add(MoveTemp(Shard));
	}

	TArray<FVector2D> Front;
	TArray<FVector2D> Back;

	// The crack boundary. Every cut above is a half-plane and a half-plane
	// reaches the far corner of the pane, so without this the first round into a
	// whole sheet would spiderweb all 400 cm of it and the fracture radius would
	// mean nothing. The boundary is a polygon drawn AROUND the circle of radius
	// FractureRadius - each edge tangent to it - so the cracks reach the full
	// radius in every direction and no further than 1/cos(pi/N) of it.
	//
	// What falls outside the boundary is glass this round did not touch: it
	// comes out as convex leftovers rather than as one rectangle, because a
	// rectangle with a polygon bitten out of it is not convex. The leftovers
	// keep whatever cracked state they already had, so an unbroken sheet stays
	// unbroken glass that merely has more edges than before - which nothing but
	// this file can tell apart.
	{
		const int32 BoundaryEdges = FMath::Max(SpokeCount, 8);
		const float BoundaryRadius = FractureRadius / FMath::Cos(UE_PI / BoundaryEdges);

		TArray<FMKGlassShard> Inside;
		Inside.Reserve(Working.Num());
		for (FMKGlassShard& Shard : Working)
		{
			// The piece the round landed in is never handed back untouched,
			// whatever the boundary says: it is the one piece that has to fall
			// out, and a small piece near the pane's edge could otherwise be
			// judged "all but a sliver out of reach" and survive its own hit.
			const bool bContainsHit = MKPolygonContains(Shard.Vertices, HitUV);

			TArray<FVector2D> Current = MoveTemp(Shard.Vertices);
			bool bWhollyOutside = false;

			for (int32 EdgeIndex = 0; EdgeIndex < BoundaryEdges && !bWhollyOutside; ++EdgeIndex)
			{
				const float Angle = 2.0f * UE_PI * EdgeIndex / BoundaryEdges;
				const FVector2D Direction(FMath::Cos(Angle), FMath::Sin(Angle));
				MKSplitConvexPolygon(Current, HitUV + Direction * BoundaryRadius, Direction, Front, Back);

				if (Front.Num() < 3 || Back.Num() < 3)
				{
					// The whole piece is on one side of this edge: either it is
					// entirely within reach of the cracks, or entirely past this
					// edge and still to be judged by the remaining ones.
					continue;
				}

				if (MKPolygonArea(Back) < Params.MinShardAreaCm2)
				{
					// All but a sliver of it is out of reach. Cutting that sliver
					// off would be the sliver explosion MinShardAreaCm2 exists to
					// prevent, so the piece is left alone.
					bWhollyOutside = true;
					break;
				}

				if (MKPolygonArea(Front) >= Params.MinShardAreaCm2)
				{
					// The boundary is bookkeeping, not a crack: the leftover
					// keeps whatever fresh edges it already had and gains none.
					Untouched.Add(FMKGlassShard{ Front, Shard.bCracked, Shard.bFreshCut });
					Current = Back;
				}
			}

			TArray<FMKGlassShard>& Destination = (bWhollyOutside && !bContainsHit) ? Untouched : Inside;
			Destination.Add(FMKGlassShard{ MoveTemp(Current), Shard.bCracked, Shard.bFreshCut });
		}
		Working = MoveTemp(Inside);
	}

	// One cut applied to a list of pieces. A cut that misses a piece, or that
	// would shave a sliver off it, leaves that piece exactly as it was.
	auto ApplyCut = [&Front, &Back, &Params](TArray<FMKGlassShard>& Pieces, const FVector2D& Origin, const FVector2D& Normal)
	{
		TArray<FMKGlassShard> NextPieces;
		NextPieces.Reserve(Pieces.Num() * 2);
		for (FMKGlassShard& Piece : Pieces)
		{
			MKSplitConvexPolygon(Piece.Vertices, Origin, Normal, Front, Back);

			const bool bBothSides = Front.Num() >= 3 && Back.Num() >= 3;
			const bool bWouldShaveSliver = bBothSides &&
				(MKPolygonArea(Front) < Params.MinShardAreaCm2 || MKPolygonArea(Back) < Params.MinShardAreaCm2);

			if (!bBothSides || bWouldShaveSliver)
			{
				NextPieces.Add(MoveTemp(Piece));
				continue;
			}

			// Both halves inherit the piece's look. Whether they DRAW as cracked
			// is decided once, at the end, by how close they ended up to the hit.
			// Both are freshly cut whatever that decision is: this was a crack,
			// and the two new outlines are glass that was inside the sheet a
			// moment ago.
			NextPieces.Add(FMKGlassShard{ Front, Piece.bCracked, true });
			NextPieces.Add(FMKGlassShard{ Back, Piece.bCracked, true });
		}
		Pieces = MoveTemp(NextPieces);
	};

	// Pass one: the spokes, each a full line through the impact point.
	for (int32 SpokeIndex = 0; SpokeIndex < SpokeCount; ++SpokeIndex)
	{
		const float Angle = SpokeAngles[SpokeIndex];
		const FVector2D Direction(FMath::Cos(Angle), FMath::Sin(Angle));
		ApplyCut(Working, HitUV, FVector2D(-Direction.Y, Direction.X));
	}

	// Pass two: the rings, wedge by wedge. Each piece is barred only by the
	// chords of the wedge it came out of, so a hit leaves RingCount + 1 bands of
	// shards per wedge and not a lattice of every chord crossed with every other.
	if (RingCount > 0)
	{
		TArray<FMKGlassShard> NextWorking;
		NextWorking.Reserve(Working.Num() * (RingCount + 1));

		for (FMKGlassShard& Shard : Working)
		{
			const FVector2D Offset = MKPolygonCentroid(Shard.Vertices) - HitUV;

			// Which wedge this piece sits in: the spoke angles are sorted, so
			// the wedge is the last one whose spoke the centroid is past. The
			// angle is first brought into the turn that starts at spoke zero.
			float Angle = FMath::Atan2(Offset.Y, Offset.X);
			while (Angle < SpokeAngles[0])
			{
				Angle += 2.0f * UE_PI;
			}
			while (Angle >= SpokeAngles[0] + 2.0f * UE_PI)
			{
				Angle -= 2.0f * UE_PI;
			}

			int32 WedgeIndex = SpokeCount - 1;
			for (int32 SpokeIndex = 1; SpokeIndex < SpokeCount; ++SpokeIndex)
			{
				if (Angle < SpokeAngles[SpokeIndex])
				{
					WedgeIndex = SpokeIndex - 1;
					break;
				}
			}

			// The chords face along the wedge's own bisector, so each one cuts
			// its wedge square across rather than at a glancing angle.
			const float NextSpokeAngle = WedgeIndex + 1 < SpokeCount
				? SpokeAngles[WedgeIndex + 1]
				: SpokeAngles[0] + 2.0f * UE_PI;
			const float BisectorAngle = 0.5f * (SpokeAngles[WedgeIndex] + NextSpokeAngle);
			const FVector2D Bisector(FMath::Cos(BisectorAngle), FMath::Sin(BisectorAngle));

			TArray<FMKGlassShard> Pieces;
			Pieces.Add(MoveTemp(Shard));
			for (const float Radius : WedgeRingRadii[WedgeIndex])
			{
				ApplyCut(Pieces, HitUV + Bisector * Radius, Bisector);
			}
			NextWorking.Append(MoveTemp(Pieces));
		}

		Working = MoveTemp(NextWorking);
	}

	// Pass three: the runners. A pane hit by a round does not end at the edge of
	// its web - one or two cracks keep going and reach the frame, and that is
	// what makes a shot read as "this pane is finished" rather than as a neat
	// disc of damage. A runner is an existing spoke continued: the same line
	// through the hit, applied to the glass OUTSIDE the web, where inside the
	// web it lands on a cut that is already there and changes nothing.
	//
	// Over budget they are skipped with everything else that adds pieces.
	if (!bOverBudget && Params.RunnerCrackCount > 0)
	{
		TArray<int32> SpokeOrder;
		SpokeOrder.Reserve(SpokeCount);
		for (int32 SpokeIndex = 0; SpokeIndex < SpokeCount; ++SpokeIndex)
		{
			SpokeOrder.Add(SpokeIndex);
		}
		for (int32 Index = SpokeOrder.Num() - 1; Index > 0; --Index)
		{
			SpokeOrder.Swap(Index, RandomStream.RandRange(0, Index));
		}

		const int32 RunnerCount = FMath::Min(Params.RunnerCrackCount, SpokeCount);
		const int32 BeforeRunners = Untouched.Num();
		for (int32 RunnerIndex = 0; RunnerIndex < RunnerCount; ++RunnerIndex)
		{
			const float Angle = SpokeAngles[SpokeOrder[RunnerIndex]];
			const FVector2D Direction(FMath::Cos(Angle), FMath::Sin(Angle));
			ApplyCut(Untouched, HitUV, FVector2D(-Direction.Y, Direction.X));
		}
		Result.RunnerCutShards = Untouched.Num() - BeforeRunners;
	}

	TArray<FMKGlassShard> Survivors = MoveTemp(Untouched);
	Survivors.Reserve(Survivors.Num() + Working.Num());
	for (FMKGlassShard& Shard : Working)
	{
		const FVector2D Centroid = MKPolygonCentroid(Shard.Vertices);
		const bool bInsideHole = FVector2D::Distance(Centroid, HitUV) <= Params.BreakRadiusCm;

		// The piece the round actually landed in always falls out, whatever the
		// break radius says. Without that rule a break radius smaller than the
		// piece leaves glass standing where the player just shot it, and that
		// reads as a miss.
		const bool bStruck = MKPolygonContains(Shard.Vertices, HitUV);

		if (bInsideHole || bStruck)
		{
			Result.RemovedAreaCm2 += MKPolygonArea(Shard.Vertices);
			Result.FallenShards.Add(MoveTemp(Shard));
			continue;
		}

		// Cracked is a look, not a state of the geometry: only the glass close
		// enough to the hole draws as shattered, and the rest of the sheet stays
		// glass a player can see through.
		//
		// Each piece rolls its own edge of that zone. With one radius for all of
		// them the frost ends on a circle, and a circle is the shape damage
		// never has - it read as a decal stuck on the glass rather than as the
		// glass being wrecked.
		const float Jitter = FMath::Clamp(Params.CrackedLookJitter, 0.0f, 1.0f);
		const float LookRadius = Params.CrackedLookRadiusCm *
			RandomStream.FRandRange(1.0f - Jitter, 1.0f + Jitter);
		if (!Shard.bCracked && MKPolygonDistanceTo(Shard.Vertices, HitUV) <= LookRadius)
		{
			Shard.bCracked = true;
			++Result.CrackedShards;
		}
		Survivors.Add(MoveTemp(Shard));
	}

	Shards = MoveTemp(Survivors);
	Result.RemovedShards = Result.FallenShards.Num();

	// What the hole left hanging on nothing is NOT dropped here. The round makes
	// its hole in one frame, which is right - it is a bullet - but the glass
	// around it letting go in the same frame is what made the whole thing read
	// as a single flicker. The caller (AMKGlassPane) walks MKDropUnsupportedShards
	// over the next few tenths of a second instead.
	return Result;
}
