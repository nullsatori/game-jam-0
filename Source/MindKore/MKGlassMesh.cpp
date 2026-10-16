#include "MKGlassMesh.h"

namespace
{
	/** Pane space (U, V) to the actor's own frame: width X, thickness Y, height Z. */
	FVector MKPaneSpaceToLocal(const FVector2D& PaneUV, float OffsetY)
	{
		return FVector(PaneUV.X, OffsetY, PaneUV.Y);
	}
}

void MKAppendShardGeometry(
	FMKGlassMeshBuild& Build,
	const FMKGlassShard& Shard,
	float HalfThicknessCm,
	float UvTilingCm,
	const FVector2D& UvOriginCm,
	FMKGlassMeshBuild* RimBuild)
{
	const int32 VertexCount = Shard.Vertices.Num();
	if (VertexCount < 3)
	{
		return;
	}

	const float TilingCm = FMath::Max(UvTilingCm, 1.0f);

	auto AddVertex = [&UvOriginCm, TilingCm](FMKGlassMeshBuild& Target,
		const FVector2D& PaneUV, float OffsetY, const FVector& Normal, const FProcMeshTangent& Tangent)
	{
		const FVector2D Local = PaneUV - UvOriginCm;
		const int32 Index = Target.Vertices.Add(MKPaneSpaceToLocal(Local, OffsetY));
		Target.Normals.Add(Normal);
		// Measured in centimetres of glass, NOT normalised over the pane. UVs
		// normalised over the pane stretched one tile of the glass pack's dirt
		// and scratch maps across a whole 400 cm sheet, which read as smeared
		// bands rather than as glass.
		Target.UVs.Add(FVector2D(PaneUV.X / TilingCm, -PaneUV.Y / TilingCm));
		Target.Tangents.Add(Tangent);
		return Index;
	};

	auto AddTriangle = [](FMKGlassMeshBuild& Target, int32 A, int32 B, int32 C)
	{
		Target.Triangles.Add(A);
		Target.Triangles.Add(B);
		Target.Triangles.Add(C);
	};

	// Front face at -Y. The ring is counter-clockwise in pane space, and the
	// fan is taken the OTHER way round: Unreal draws a triangle when its
	// vertices run clockwise as seen from the outside (winding note in the
	// header). Fanned in ring order this face was culled and the -Y side of a
	// one-sided shard showed the inside of its +Y face instead.
	const int32 FrontStart = Build.Vertices.Num();
	for (const FVector2D& Vertex : Shard.Vertices)
	{
		AddVertex(Build, Vertex, -HalfThicknessCm, FVector(0.0f, -1.0f, 0.0f), FProcMeshTangent(1.0f, 0.0f, 0.0f));
	}
	for (int32 Index = 1; Index + 1 < VertexCount; ++Index)
	{
		AddTriangle(Build, FrontStart, FrontStart + Index + 1, FrontStart + Index);
	}

	// Back face: same ring at +Y, fanned in ring order.
	const int32 BackStart = Build.Vertices.Num();
	for (const FVector2D& Vertex : Shard.Vertices)
	{
		AddVertex(Build, Vertex, HalfThicknessCm, FVector(0.0f, 1.0f, 0.0f), FProcMeshTangent(-1.0f, 0.0f, 0.0f));
	}
	for (int32 Index = 1; Index + 1 < VertexCount; ++Index)
	{
		AddTriangle(Build, BackStart, BackStart + Index, BackStart + Index + 1);
	}

	// The rim. Without it a hole shows the pane as a paper cut-out: the lit
	// edge is the only thing that says the glass has a thickness at all.
	FMKGlassMeshBuild& EdgeBuild = RimBuild ? *RimBuild : Build;
	for (int32 Index = 0; Index < VertexCount; ++Index)
	{
		const FVector2D& A = Shard.Vertices[Index];
		const FVector2D& B = Shard.Vertices[(Index + 1) % VertexCount];
		const FVector2D Edge = B - A;
		if (Edge.IsNearlyZero())
		{
			continue;
		}

		// Interior is left of A->B on a counter-clockwise ring, so outward is
		// the right-hand side of the edge.
		const FVector2D OutwardUV = FVector2D(Edge.Y, -Edge.X).GetSafeNormal();
		const FVector Normal = MKPaneSpaceToLocal(OutwardUV, 0.0f);
		const FProcMeshTangent Tangent(MKPaneSpaceToLocal(Edge.GetSafeNormal(), 0.0f), false);

		const int32 AFront = AddVertex(EdgeBuild, A, -HalfThicknessCm, Normal, Tangent);
		const int32 ABack = AddVertex(EdgeBuild, A, HalfThicknessCm, Normal, Tangent);
		const int32 BFront = AddVertex(EdgeBuild, B, -HalfThicknessCm, Normal, Tangent);
		const int32 BBack = AddVertex(EdgeBuild, B, HalfThicknessCm, Normal, Tangent);

		// Same rule as the faces: clockwise from outside, so the quad runs
		// A-front, B-front, B-back, A-back when looked at along -Outward.
		AddTriangle(EdgeBuild, AFront, BFront, BBack);
		AddTriangle(EdgeBuild, AFront, BBack, ABack);
	}
}

TArray<FVector> MKMakeShardHull(const FMKGlassShard& Shard, float HalfThicknessCm, const FVector2D& OriginCm)
{
	TArray<FVector> Hull;
	Hull.Reserve(Shard.Vertices.Num() * 2);
	for (const FVector2D& Vertex : Shard.Vertices)
	{
		const FVector2D Local = Vertex - OriginCm;
		Hull.Add(MKPaneSpaceToLocal(Local, -HalfThicknessCm));
		Hull.Add(MKPaneSpaceToLocal(Local, HalfThicknessCm));
	}
	return Hull;
}
