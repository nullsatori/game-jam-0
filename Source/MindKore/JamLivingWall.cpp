#include "JamLivingWall.h"

#include "Camera/CameraComponent.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Components/SphereComponent.h"
#include "Engine/OverlapResult.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/PlayerController.h"
#include "HAL/IConsoleManager.h"
#include "JamWeapons.h"
#include "JamZeroGPawn.h"
#include "Kismet/GameplayStatics.h"
#include "MKVitals.h"
#include "MindKore.h"
#include "NiagaraComponent.h"
#include "NiagaraFunctionLibrary.h"
#include "NiagaraSystem.h"
#include "PhysicalMaterials/PhysicalMaterial.h"
#include "Sound/SoundBase.h"
#include "TimerManager.h"
#include "UObject/ConstructorHelpers.h"

AJamLivingWall::AJamLivingWall()
{
	PrimaryActorTick.bCanEverTick = true;

	static ConstructorHelpers::FObjectFinder<UStaticMesh> Cylinder(TEXT("/Engine/BasicShapes/Cylinder.Cylinder"));
	Strands = CreateDefaultSubobject<UInstancedStaticMeshComponent>(TEXT("Strands"));
	SetRootComponent(Strands);
	Strands->SetStaticMesh(Cylinder.Object);
	// Movable: the instance bodies are moved every frame while the weave regrows.
	Strands->SetMobility(EComponentMobility::Movable);
	Strands->SetCollisionProfileName(TEXT("BlockAll"));
	Strands->SetGenerateOverlapEvents(false);
	Strands->SetCanEverAffectNavigation(false);
	// 0 thin spot, 1 vessel (threads), 2 per-cell colour variation.
	Strands->SetNumCustomDataFloats(3);
	// The vessel-like threads over the bundles: looks only, no collision.
	Threads = CreateDefaultSubobject<UInstancedStaticMeshComponent>(TEXT("Threads"));
	Threads->SetupAttachment(Strands);
	Threads->SetStaticMesh(Cylinder.Object);
	Threads->SetMobility(EComponentMobility::Movable);
	Threads->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Threads->SetGenerateOverlapEvents(false);
	Threads->SetCanEverAffectNavigation(false);
	Threads->SetNumCustomDataFloats(3);
	Skin = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Skin"));
	Skin->SetupAttachment(Strands);
	Skin->SetMobility(EComponentMobility::Movable);
	Skin->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Skin->SetGenerateOverlapEvents(false);
	Skin->SetCanEverAffectNavigation(false);

	// MindKore's own content (CREDITS.md lists the sound donors).
	static ConstructorHelpers::FObjectFinder<UPhysicalMaterial> Flesh(TEXT("/Game/MindKore/Materials/Physical/PM_MK_Flesh.PM_MK_Flesh"));
	FleshMaterial = Flesh.Object;
	static ConstructorHelpers::FObjectFinder<UNiagaraSystem> Chunks(TEXT("/Game/MindKore/FX/NS_MK_HitChunks.NS_MK_HitChunks"));
	TearVfx = Chunks.Object;
	static ConstructorHelpers::FObjectFinder<UNiagaraSystem> Jet(TEXT("/Game/MindKore/FX/NS_MK_WoundSpurt.NS_MK_WoundSpurt"));
	SpurtVfx = Jet.Object;
	static ConstructorHelpers::FObjectFinder<USoundBase> Limb(TEXT("/Game/MindKore/Audio/Impact/SC_MK_Hit_Limb.SC_MK_Hit_Limb"));
	TearSound = Limb.Object;
	static ConstructorHelpers::FObjectFinder<USoundBase> Grunt(TEXT("/Game/MindKore/Audio/Enemy/SC_MK_Enemy_Grunt.SC_MK_Enemy_Grunt"));
	GroanSound = Grunt.Object;
	static ConstructorHelpers::FObjectFinder<USoundBase> Torso(TEXT("/Game/MindKore/Audio/Impact/SC_MK_Hit_Torso.SC_MK_Hit_Torso"));
	CloseSound = Torso.Object;
}

void AJamLivingWall::OnConstruction(const FTransform& Transform)
{
	Super::OnConstruction(Transform);
	Build();
}

void AJamLivingWall::BeginPlay()
{
	Super::BeginPlay();
	Build();
	SkinMaterials.Reset();
	for (int32 Slot = 0; SkinMesh && Slot < Skin->GetNumMaterials(); ++Slot)
	{
		if (UMaterialInstanceDynamic* Material = Skin->CreateDynamicMaterialInstance(Slot, Skin->GetMaterial(Slot)))
		{
			SkinMaterials.Add(Material);
		}
	}
}

// --- layout -----------------------------------------------------------------------------

namespace
{
	float Smooth(float X)
	{
		X = FMath::Clamp(X, 0.0f, 1.0f);
		return X * X * (3.0f - 2.0f * X);
	}

	/** The engine cylinder (100 cm across, 100 cm along Z, pivot at its centre) laid from A to B. */
	FTransform Segment(const FVector& A, const FVector& B, float Radius)
	{
		const FVector Along = B - A;
		const float Length = Along.Size();
		// A gone piece gets exactly zero scale: the ISM drops a zero-scale
		// instance's body (and makes a new one when it grows back).
		if (Length < 0.5f || Radius <= 0.0f)
		{
			return FTransform(FQuat::Identity, (A + B) * 0.5f, FVector::ZeroVector);
		}
		return FTransform(FRotationMatrix::MakeFromZ(Along / Length).ToQuat(), (A + B) * 0.5f, FVector(Radius * 0.02f, Radius * 0.02f, Length * 0.01f));
	}
}

void AJamLivingWall::Build()
{
	if (SkinMesh)
	{
		HeightCm = WidthCm; // the mesh is square (a round wall is the ellipse inscribed in it)
	}
	Skin->SetStaticMesh(SkinMesh);
	Skin->SetVisibility(SkinMesh != nullptr);
	Skin->SetRelativeScale3D(FVector(WidthCm / SkinSizeCm));
	Strands->SetVisibility(SkinMesh == nullptr);
	Threads->SetVisibility(SkinMesh == nullptr);
	CellsX = FMath::Max(1, FMath::RoundToInt32(WidthCm / CellSizeCm));
	CellsZ = FMath::Max(1, FMath::RoundToInt32(HeightCm / CellSizeCm));
	Cells.Reset();
	Cells.SetNum(CellsX * CellsZ);
	Open.Reset();
	Wounds.Reset();
	Pending.Reset();
	Twitching.Reset();
	Random.Initialize(ThinSpotSeed);
	for (int32 Index = 0; Index < Cells.Num(); ++Index)
	{
		// Fibres, not rods: every bundle its own thickness and a slight lean;
		// one or two thin curved vessels across it, diagonal, denser in patches.
		FCell& Cell = Cells[Index];
		Cell.Phase = Random.FRandRange(0.0f, 2.0f * PI);
		Cell.Thickness = Random.FRandRange(BundleThicknessRange.X, BundleThicknessRange.Y);
		Cell.Lean[0] = FMath::DegreesToRadians(Random.FRandRange(-BundleLeanDeg, BundleLeanDeg));
		Cell.Lean[1] = FMath::DegreesToRadians(Random.FRandRange(-BundleLeanDeg, BundleLeanDeg));
		Cell.Variation = Random.FRand();
		const float Patch = FMath::Sin(Index % CellsX * 0.55f + ThinSpotSeed) * FMath::Cos(Index / CellsX * 0.45f);
		Cell.ThreadCount = Patch > -0.25f ? 2 : 1;
		for (int32 Thread = 0; Thread < 2; ++Thread)
		{
			const float Angle = FMath::DegreesToRadians((Thread == 0 ? 45.0f : -45.0f) + Random.FRandRange(-20.0f, 20.0f));
			const FVector2D Dir(FMath::Cos(Angle), FMath::Sin(Angle));
			const FVector2D Mid(Random.FRandRange(-0.35f, 0.35f) * CellSizeCm, Random.FRandRange(-0.35f, 0.35f) * CellSizeCm);
			const float Reach = CellSizeCm * Random.FRandRange(0.5f, 0.8f);
			Cell.ThreadPoints[Thread][0] = Mid - Dir * Reach;
			Cell.ThreadPoints[Thread][1] = Mid + FVector2D(-Dir.Y, Dir.X) * Random.FRandRange(-0.18f, 0.18f) * CellSizeCm;
			Cell.ThreadPoints[Thread][2] = Mid + Dir * Reach;
			Cell.ThreadDepth[Thread] = (Thread == 0 ? 1.0f : -1.0f) * (WeaveDepthCm + Random.FRandRange(2.0f, 5.0f));
			Cell.ThreadRadius[Thread] = 0.5f * Random.FRandRange(ThreadThicknessRange.X, ThreadThicknessRange.Y);
		}
	}
	// Thin spots: patches well inside the edges.
	const FVector2D Half(CellsX * CellSizeCm * 0.5f, CellsZ * CellSizeCm * 0.5f);
	if (bRound)
	{
		for (int32 Index = 0; Index < Cells.Num(); ++Index)
		{
			const FVector2D At = CellCentre(Index) / Half;
			Cells[Index].bExists = At.SizeSquared() <= 1.0;
		}
	}
	const FVector2D Inner(FMath::Max(0.0f, float(Half.X - ThinSpotRadiusCm - CellSizeCm)), FMath::Max(0.0f, float(Half.Y - ThinSpotRadiusCm - CellSizeCm)));
	for (int32 Spot = 0; Spot < ThinSpotCount; ++Spot)
	{
		const FVector2D At(Random.FRandRange(-Inner.X, Inner.X), Random.FRandRange(-Inner.Y, Inner.Y));
		for (int32 Index = 0; Index < Cells.Num(); ++Index)
		{
			Cells[Index].bThin |= Cells[Index].bExists && FVector2D::Distance(CellCentre(Index), At) <= ThinSpotRadiusCm;
		}
	}

	for (UInstancedStaticMeshComponent* Mesh : { Strands.Get(), Threads.Get() })
	{
		if (WallMaterial)
		{
			Mesh->SetMaterial(0, WallMaterial);
		}
		Mesh->ClearInstances();
	}
	Strands->SetPhysMaterialOverride(FleshMaterial);
	TArray<FTransform> Transforms;
	Transforms.SetNum(Cells.Num() * 2);
	Strands->AddInstances(Transforms, /*bShouldReturnIndices=*/false);
	if (!SkinMesh)
	{
		Transforms.SetNum(Cells.Num() * 4);
		Threads->AddInstances(Transforms, /*bShouldReturnIndices=*/false);
	}

	for (int32 Index = 0; Index < Cells.Num(); ++Index)
	{
		WriteCell(Index, 0.0, 0.0f);
		const float Thin = Cells[Index].bThin ? 1.0f : 0.0f;
		for (int32 Instance = Index * 2; Instance < Index * 2 + 2; ++Instance)
		{
			Strands->SetCustomData(Instance, { Thin, 0.0f, Cells[Index].Variation });
		}
		for (int32 Instance = Index * 4; !SkinMesh && Instance < Index * 4 + 4; ++Instance)
		{
			Threads->SetCustomData(Instance, { Thin, 1.0f, Cells[Index].Variation });
		}
	}
	Strands->MarkRenderStateDirty();
	Threads->MarkRenderStateDirty();
}

FVector2D AJamLivingWall::CellCentre(int32 Cell) const
{
	const int32 X = Cell % CellsX;
	const int32 Z = Cell / CellsX;
	return FVector2D((X + 0.5f - CellsX * 0.5f) * CellSizeCm, (Z + 0.5f - CellsZ * 0.5f) * CellSizeCm);
}

void AJamLivingWall::StrandSegments(int32 Cell, double Now, float Twitch, FVector2D OutA[2], FVector2D OutB[2], float OutDepth[2], float& OutRadius) const
{
	const FCell& State = Cells[Cell];
	const FVector2D Centre = CellCentre(Cell);
	// Over/under: the horizontal bundle in front on one cell, behind on the next.
	const float Weave = ((Cell % CellsX + Cell / CellsX) & 1) ? WeaveDepthCm : -WeaveDepthCm;
	const float Length = CellSizeCm + StrandOverlapCm;
	const float G = FMath::Clamp(State.Growth, 0.0f, 1.0f);
	// Two stages, as flesh knits: a thin thread shoots across, then thickens.
	const float Reach = Smooth(G / ThreadStage);
	const float Thicken = Smooth((G - ThreadStage) / (1.0f - ThreadStage));
	const FVector2D From(State.From.X, State.From.Y);
	for (int32 Strand = 0; Strand < 2; ++Strand)
	{
		const float Lean = State.Lean[Strand];
		const FVector2D Axis = Strand == 0 ? FVector2D(FMath::Cos(Lean), FMath::Sin(Lean)) : FVector2D(-FMath::Sin(Lean), FMath::Cos(Lean));
		const FVector2D Across(-Axis.Y, Axis.X);
		FVector2D Middle = Centre;
		float Depth = Strand == 0 ? Weave : -Weave;
		if (G < 1.0f)
		{
			// Out of the side of the whole neighbour: the bundle along it is rooted
			// at that edge, the crossing one slides in from it; both writhe until done.
			const bool bAlong = FMath::Abs(FVector2D::DotProduct(Axis, From)) > 0.5f;
			Middle += From * (bAlong ? Length * 0.5f : CellSizeCm * 0.5f) * (1.0f - Reach);
			const float Writhe = WobbleCm * (1.0f - Thicken);
			Middle += Across * Writhe * FMath::Sin(State.Phase + 23.0f * Now);
			Depth += Writhe * FMath::Cos(State.Phase * 1.7f + 19.0f * Now);
		}
		if (Twitch > 0.0f)
		{
			// A torn rim jerks: two beating sines, per cell out of phase.
			Middle += Across * Twitch * FMath::Sin(State.Phase * 3.0f + 37.0f * Now) * FMath::Sin(State.Phase + 11.0f * Now);
			Depth += Twitch * FMath::Cos(State.Phase * 2.0f + 29.0f * Now);
		}
		OutA[Strand] = Middle - Axis * Length * Reach * 0.5f;
		OutB[Strand] = Middle + Axis * Length * Reach * 0.5f;
		OutDepth[Strand] = Depth;
	}
	const float Full = StrandThicknessCm * 0.5f * State.Thickness;
	OutRadius = G <= 0.0f ? 0.0f : FMath::Lerp(FMath::Min(ThreadThicknessRange.X * 0.5f, Full), Full, Thicken);
}

void AJamLivingWall::WriteCell(int32 Cell, double Now, float Twitch)
{
	if (!Cells[Cell].bExists)
	{
		// Outside a round wall: zero-scale instances (no bodies).
		const FTransform None(FQuat::Identity, FVector::ZeroVector, FVector::ZeroVector);
		for (int32 Strand = 0; Strand < 2; ++Strand)
		{
			Strands->UpdateInstanceTransform(Cell * 2 + Strand, None, false, true, true);
		}
		for (int32 Thread = 0; !SkinMesh && Thread < 4; ++Thread)
		{
			Threads->UpdateInstanceTransform(Cell * 4 + Thread, None, false, true, true);
		}
		return;
	}
	FVector2D A[2], B[2];
	float Depth[2];
	float Radius = 0.0f;
	StrandSegments(Cell, Now, Twitch, A, B, Depth, Radius);
	for (int32 Strand = 0; Strand < 2; ++Strand)
	{
		Strands->UpdateInstanceTransform(Cell * 2 + Strand, Segment(FVector(A[Strand].X, Depth[Strand], A[Strand].Y),
			FVector(B[Strand].X, Depth[Strand], B[Strand].Y), Radius), /*bWorldSpace=*/false, /*bMarkRenderStateDirty=*/true, /*bTeleport=*/true);
	}
	if (SkinMesh)
	{
		return; // the skin draws it (its holes: UpdateHoles); the rods above are only the collision
	}

	// The vessels: grown in the thread stage, from the end nearer the neighbour
	// they grow out of; two segments each, the far one thinner (a taper).
	const FCell& State = Cells[Cell];
	const FVector2D Centre = CellCentre(Cell);
	const float G = FMath::Clamp(State.Growth, 0.0f, 1.0f);
	const float Reach = G >= 1.0f ? 1.0f : Smooth(G / ThreadStage);
	for (int32 Thread = 0; Thread < 2; ++Thread)
	{
		FVector P[3];
		const bool bFlip = FVector2D::DotProduct(State.ThreadPoints[Thread][2] - State.ThreadPoints[Thread][0], FVector2D(State.From.X, State.From.Y)) > 0.0f;
		for (int32 Point = 0; Point < 3; ++Point)
		{
			const FVector2D At = Centre + State.ThreadPoints[Thread][bFlip ? 2 - Point : Point];
			const float Shake = Twitch * FMath::Sin(State.Phase * (Point + 2.0f) + 31.0f * Now)
				+ WobbleCm * (1.0f - Reach) * FMath::Sin(State.Phase + Point + 17.0f * Now);
			P[Point] = FVector(At.X + Shake, State.ThreadDepth[Thread] + Shake, At.Y - Shake);
		}
		const float ThreadRadius = Thread < State.ThreadCount && G > 0.0f ? State.ThreadRadius[Thread] : 0.0f;
		const FVector Tip1 = FMath::Lerp(P[0], P[1], FMath::Min(1.0f, Reach * 2.0f));
		const FVector Tip2 = FMath::Lerp(P[1], P[2], FMath::Max(0.0f, Reach * 2.0f - 1.0f));
		Threads->UpdateInstanceTransform(Cell * 4 + Thread * 2, Segment(P[0], Tip1, ThreadRadius), false, true, true);
		Threads->UpdateInstanceTransform(Cell * 4 + Thread * 2 + 1, Segment(P[1], Tip2, ThreadRadius * 0.7f), false, true, true);
	}
}

namespace
{
	float Frac(float X)
	{
		return X - FMath::FloorToFloat(X);
	}

	/**
	 * How open the outline at angle Theta is, 0..1: it opens over Tear (cubic ease-out, along
	 * the fibres first, across them up to 1.6 x slower), waits, then closes over Close after a
	 * delay that differs by angle (Stun +- a quarter, along the fibres a little later).
	 * Mirrored in M_Jam_Gritty (Scripts/jam_flesh.py HOLE_HLSL).
	 */
	float OutlineOpen(float Seed, float Fibre, float Stun, float Close, float Tear, float Theta, float Age)
	{
		const float Across = 0.5f - 0.5f * FMath::Cos(2.0f * (Theta - Fibre));
		const float Opening = 1.0f - FMath::Pow(1.0f - FMath::Clamp(Age / FMath::Max(Tear * (1.0f + 0.6f * Across), 0.001f), 0.0f, 1.0f), 3.0f);
		const float Uneven = 0.6f * FMath::Sin(Theta + UE_TWO_PI * Frac(Seed * 1.3f)) + 0.4f * FMath::Sin(2.0f * Theta + UE_TWO_PI * Frac(Seed * 2.7f));
		const float Delay = Tear + Stun * (1.0f + 0.25f * Uneven) + 0.25f * Close * (1.0f - Across);
		return Opening * (1.0f - FMath::SmoothStep(Delay, Delay + Close, Age));
	}
}

float AJamLivingWall::WoundGap(const FWound& Wound, const FVector2D& Point, float Age, bool bFullShape)
{
	// Keep in step with HOLE_HLSL in Scripts/jam_flesh.py: the skin draws this outline.
	const FVector2D Offset = Point - Wound.Centre;
	const float Theta = FMath::Atan2(float(Offset.Y), float(Offset.X));
	float Wobble = 1.0f;
	for (int32 K = 2; K <= 5; ++K)
	{
		const float Amp = 0.05f + 0.12f * Frac(Wound.Seed * 0.6180339f * K + 0.11f * K);
		const float Phase = UE_TWO_PI * Frac(Wound.Seed * 0.7548776f * K + 0.37f);
		Wobble += Amp * FMath::Sin(K * Theta + Phase);
	}
	// Lobes out, never a notch narrower than 3/4 of the radius (a blast must stay passable);
	// longer along the fibres, not narrower across them.
	const float Shape = Wound.Radius * FMath::Clamp(Wobble, 0.75f, 1.3f) * (1.0f + Wound.Stretch * (0.5f + 0.5f * FMath::Cos(2.0f * (Theta - Wound.Fibre))));
	const auto Open = [&Wound, Age, bFullShape](float Angle)
	{
		return bFullShape ? 1.0f : OutlineOpen(Wound.Seed, Wound.Fibre, Wound.Stun, Wound.Close, Wound.Tear, Angle, Age);
	};
	float Gap = float(Offset.Size()) - Shape * Open(Theta);
	for (int32 Blob = 0; Blob < Wound.Blobs; ++Blob)
	{
		const float Beta = UE_TWO_PI * Frac(Wound.Seed * (Blob + 1) * 0.4142136f + 0.17f * Blob);
		const FVector2D At = Wound.Centre + FVector2D(FMath::Cos(Beta), FMath::Sin(Beta)) * Wound.Radius * 0.8f;
		const float Size = Wound.Radius * (0.3f + 0.15f * Frac(Wound.Seed * (Blob + 3) * 0.381966f))
			* Open(Beta);
		Gap = FMath::Min(Gap, float(FVector2D::Distance(Point, At)) - Size);
	}
	return Gap;
}

float AJamLivingWall::WoundEnd(const FWound& Wound)
{
	return Wound.Tear + Wound.Stun * 1.25f + Wound.Close * 1.25f + 0.05f;
}

void AJamLivingWall::UpdateHoles(double Now)
{
	if (SkinMaterials.IsEmpty() || (Wounds.IsEmpty() && ShownHoles == 0))
	{
		return;
	}
	// Every wound's outline, the widest MaxHoles of them, in the skin's own space
	// (the wall scaled by WidthCm / SkinSizeCm): A = (centre, radius, age),
	// B = (seed, fibre angle, stretch, blobs), C = (stun, close).
	TArray<const FWound*> Widest;
	for (const FWound& Wound : Wounds)
	{
		Widest.Add(&Wound);
	}
	Widest.Sort([](const FWound& A, const FWound& B) { return A.Radius > B.Radius; });
	const float ToMesh = SkinSizeCm / FMath::Max(WidthCm, 1.0f);
	const int32 Shown = FMath::Min(Widest.Num(), MaxHoles);
	for (int32 Slot = 0; Slot < FMath::Max(Shown, ShownHoles); ++Slot)
	{
		FLinearColor A(1.0e5f, 1.0e5f, 0.0f, 0.0f);
		FLinearColor B(0.0f, 0.0f, 0.0f, 0.0f);
		FLinearColor C(1.0f, 1.0f, 0.0f, 0.0f);
		if (Slot < Shown)
		{
			const FWound& Wound = *Widest[Slot];
			A = FLinearColor(Wound.Centre.X * ToMesh, Wound.Centre.Y * ToMesh, Wound.Radius * ToMesh, float(Now - Wound.TornAt));
			B = FLinearColor(Wound.Seed, Wound.Fibre, Wound.Stretch, float(Wound.Blobs));
			C = FLinearColor(Wound.Stun, Wound.Close, Wound.Tear, 0.0f);
		}
		for (UMaterialInstanceDynamic* Material : SkinMaterials)
		{
			Material->SetVectorParameterValue(FName(*FString::Printf(TEXT("Hole%dA"), Slot)), A);
			Material->SetVectorParameterValue(FName(*FString::Printf(TEXT("Hole%dB"), Slot)), B);
			Material->SetVectorParameterValue(FName(*FString::Printf(TEXT("Hole%dC"), Slot)), C);
		}
	}
	ShownHoles = Shown;
}

void AJamLivingWall::UpdateRim(double Now)
{
	// Whole cells beside an open one twitch until the hole is gone.
	TSet<int32> Rim;
	for (const int32 Index : Open)
	{
		const int32 X = Index % CellsX;
		const int32 Z = Index / CellsX;
		for (const int32 Next : { CellAt(X + 1, Z), CellAt(X - 1, Z), CellAt(X, Z + 1), CellAt(X, Z - 1) })
		{
			if (IsWhole(Next) && Cells[Next].bExists)
			{
				Rim.Add(Next);
			}
		}
	}
	for (const int32 Index : Rim)
	{
		WriteCell(Index, Now, TwitchCm);
	}
	for (const int32 Index : Twitching)
	{
		if (!Rim.Contains(Index) && IsWhole(Index))
		{
			WriteCell(Index, Now, 0.0f);
		}
	}
	Twitching = MoveTemp(Rim);
}

// --- impacts ----------------------------------------------------------------------------

void AJamLivingWall::ReceiveResolvedImpact_Implementation(const FMKResolvedImpact& ResolvedImpact)
{
	const FVector Point = ResolvedImpact.Hit.ImpactPoint;
	const FVector Local = GetActorTransform().InverseTransformPosition(Point);
	// A shot travelling against the front normal came from the front.
	const float Side = FVector::DotProduct(ResolvedImpact.ShotDirection, GetFrontNormal()) <= 0.0f ? 1.0f : -1.0f;
	Pending.Add({ FVector2D(Local.X, Local.Z), Side, ResolvedImpact.SourceActor, Point, ResolvedImpact.ShotDirection });
	if (Pending.Num() == 1)
	{
		PendingSince = GetWorld()->GetTimeSeconds();
		UE_LOG(LogMindKore, Verbose, TEXT("Living wall %s: hit, surface %d"), *GetName(), int32(ResolvedImpact.SurfaceKind));
	}
}



void AJamLivingWall::ProcessHits(double Now)
{
	// Impacts arrive over BlastWindowSeconds (a shotgun's flying pellets);
	// one source's impacts of one window are one blast.
	TMap<AActor*, TArray<int32>> BySource;
	for (int32 Index = 0; Index < Pending.Num(); ++Index)
	{
		BySource.FindOrAdd(Pending[Index].Source.Get()).Add(Index);
	}
	const FTransform Wall = GetActorTransform();
	for (const TPair<AActor*, TArray<int32>>& Group : BySource)
	{
		TArray<FVector2D> Points;
		FVector2D Centroid = FVector2D::ZeroVector;
		float Side = 0.0f;
		for (const int32 Index : Group.Value)
		{
			Points.Add(Pending[Index].Point);
			Centroid += Pending[Index].Point / Group.Value.Num();
			Side += Pending[Index].Side;
		}
		const bool bBlast = Group.Value.Num() >= BurstMinHits;
		float Spread = 0.0f;
		for (const FVector2D& Point : Points)
		{
			Spread = FMath::Max(Spread, float(FVector2D::Distance(Point, Centroid)));
		}
		// The outline: a blast's disc (its hits plus the margin), a single hit's
		// TearRadiusCm; ragged, stretched along the fibres (diagonal, on the round
		// walls too), a blast with pellet blobs around it.
		FWound Shape;
		Shape.Centre = Centroid;
		Shape.Radius = bBlast ? FMath::Min(Spread + BurstMarginCm, BurstMaxRadiusCm) : TearRadiusCm * 1.2f;
		Shape.Seed = Random.FRand();
		Shape.Fibre = FMath::DegreesToRadians((Random.FRand() < 0.5f ? 45.0f : 135.0f) + Random.FRandRange(-25.0f, 25.0f));
		Shape.Stretch = Random.FRandRange(0.3f, 0.6f);
		Shape.Blobs = bBlast ? 3 : 0;
		TSet<int32> Torn;
		int32 Nearest = INDEX_NONE;
		float NearestDistance = TNumericLimits<float>::Max();
		for (int32 Index = 0; Index < Cells.Num(); ++Index)
		{
			if (!Cells[Index].bExists)
			{
				continue;
			}
			if (WoundGap(Shape, CellCentre(Index), 0.0f, /*bFullShape=*/true) < 0.0f)
			{
				Torn.Add(Index);
			}
			const float Distance = float(FVector2D::Distance(CellCentre(Index), Centroid));
			if (Distance < NearestDistance && Distance <= TearRadiusCm + CellSizeCm * 0.5f)
			{
				NearestDistance = Distance;
				Nearest = Index;
			}
		}
		if (Torn.IsEmpty() && Nearest != INDEX_NONE)
		{
			Torn.Add(Nearest); // a hit between cell centres still tears the one it is in
		}
		if (Torn.IsEmpty())
		{
			continue;
		}
		const int32 Count = Torn.Num();
		const int32 CentreCell = CellAt(FMath::FloorToInt32(float(Centroid.X) / CellSizeCm + CellsX * 0.5f),
			FMath::FloorToInt32(float(Centroid.Y) / CellSizeCm + CellsZ * 0.5f));
		const bool bThinCentre = CentreCell != INDEX_NONE && Cells[CentreCell].bThin;
		// A single hit (pistol, sniper, a thrown prop) is only a visible wound that snaps shut in under half
		// a second; its cells keep their collision, so no spray of rounds ever opens a way through.
		constexpr float HitStunSeconds = 0.05f;
		constexpr float HitCloseSeconds = 0.2f;
		const float Delay = (bBlast ? RegrowDelaySeconds + FMath::Min(FMath::Max(0, Count - StunFreeCells) * StunSecondsPerCell, MaxStunSeconds) : HitStunSeconds)
			* (bThinCentre ? ThinSpotSlowdown : 1.0f);
		Shape.Stun = Delay;
		// A blast: about StrandGrowSeconds per cell of radius, as the old ring-by-ring closing.
		Shape.Close = (bBlast ? FMath::Max(StrandGrowSeconds * 0.5f, Shape.Radius / CellSizeCm * StrandGrowSeconds) : HitCloseSeconds)
			* (bThinCentre ? ThinSpotSlowdown : 1.0f);
		Shape.Tear = bBlast ? BlastTearSeconds : TearSeconds;
		Shape.TornAt = Now;
		Shape.NextPulse = Now + Shape.Tear;
		Shape.Side = Side >= 0.0f ? 1.0f : -1.0f;
		FWound& Wound = Wounds.Add_GetRef(Shape);
		// The cells open as the outline reaches them (UpdateTearing), over Tear.
		if (bBlast)
		{
			Wound.Cells = Torn.Array();
			Wound.Unopened = Wound.Cells;
		}
		LastWoundCentre = Wound.Centre;
		const FVector WorldCentre = Wall.TransformPosition(FVector(Wound.Centre.X, 0.0f, Wound.Centre.Y));

		// Rim jets of a big hole: rim cells (a whole 4-neighbour) spread around it.
		int32 Spurting = 0;
		for (const FWound& Each : Wounds)
		{
			Spurting += Each.SpurtCells.Num();
		}
		if (Count >= SpurtHoleCells && Spurting < MaxActiveSpurts)
		{
			TArray<int32> Rim;
			for (const int32 Index : Wound.Cells)
			{
				const int32 X = Index % CellsX;
				const int32 Z = Index / CellsX;
				const auto Outside = [this, &Torn](int32 Next) { return Next != INDEX_NONE && Cells[Next].bExists && !Torn.Contains(Next); };
				if (Outside(CellAt(X + 1, Z)) || Outside(CellAt(X - 1, Z)) || Outside(CellAt(X, Z + 1)) || Outside(CellAt(X, Z - 1)))
				{
					Rim.Add(Index);
				}
			}
			Rim.Sort([this, &Wound](int32 A, int32 B)
			{
				const FVector2D DA = CellCentre(A) - Wound.Centre;
				const FVector2D DB = CellCentre(B) - Wound.Centre;
				return FMath::Atan2(DA.Y, DA.X) < FMath::Atan2(DB.Y, DB.X);
			});
			const int32 Jets = FMath::Min3(3, MaxActiveSpurts - Spurting, Rim.Num());
			for (int32 Jet = 0; Jet < Jets; ++Jet)
			{
				Wound.SpurtCells.Add(Rim[Jet * Rim.Num() / Jets]);
			}
		}

		// Feedback: chunks out of a few hits spread over the tear, a crunch (throttled), a groan for a big one.
		const FPendingHit& First = Pending[Group.Value[0]];
		const int32 Chunks = FMath::Min(Group.Value.Num(), 4);
		for (int32 Shown = 0; Shown < Chunks; ++Shown)
		{
			const FPendingHit& Hit = Pending[Group.Value[Shown]];
			const FVector At = Hit.WorldPoint;
			const FRotator Facing = (-Hit.Direction).Rotation();
			const int32 Droplets = FMath::Min(Count * 2, 24);
			const auto Burst = [this, At, Facing, Droplets]()
			{
				if (UNiagaraComponent* Effect = TearVfx ? UNiagaraFunctionLibrary::SpawnSystemAtLocation(this, TearVfx, At, Facing, FVector(0.3f),
					/*bAutoDestroy=*/true, /*bAutoActivate=*/false) : nullptr)
				{
					Effect->SetVariableInt(TEXT("User.BloodDropletCount"), Droplets);
					Effect->Activate();
				}
			};
			const float Later = Wound.Tear * Shown / Chunks;
			if (Later <= 0.0f)
			{
				Burst();
				continue;
			}
			FTimerHandle Handle;
			GetWorldTimerManager().SetTimer(Handle, FTimerDelegate::CreateWeakLambda(this, Burst), Later, false);
		}
		if (TearSound && Now - LastTearSound >= TearSoundIntervalSeconds)
		{
			LastTearSound = Now;
			UGameplayStatics::PlaySoundAtLocation(this, TearSound, First.WorldPoint, 1.0f, FMath::FRandRange(0.85f, 1.1f));
		}
		if (GroanSound && Count >= GroanCells)
		{
			UGameplayStatics::PlaySoundAtLocation(this, GroanSound, WorldCentre, 1.0f, GroanPitch);
		}
		UE_LOG(LogMindKore, Log, TEXT("Living wall %s: %s %d cells (%d hit(s)%s) around (%.0f, %.0f), regrowth after %.2f s, %d rim jet(s)"),
			*GetName(), bBlast ? TEXT("tore") : TEXT("wounded (collision kept)"), Count, Group.Value.Num(), bBlast ? *FString::Printf(TEXT(", one blast, hits within %.0f cm of their centre"), Spread) : TEXT(""),
			Wound.Centre.X, Wound.Centre.Y, Delay, Wound.SpurtCells.Num());
		UE_LOG(LogMindKore, Verbose, TEXT("Living wall %s: outline radius %.0f, seed %.3f, fibre %.0f deg, stretch %.2f, %d blob(s), closes in %.2f s"),
			*GetName(), Wound.Radius, Wound.Seed, FMath::RadiansToDegrees(Wound.Fibre), Wound.Stretch, Wound.Blobs, WoundEnd(Wound));
	}
	Pending.Reset();
}

// --- regrowth ---------------------------------------------------------------------------

void AJamLivingWall::UpdateCells(float DeltaSeconds, double Now)
{
	// A torn cell stays open while its centre is inside a wound's outline; once
	// the outline has passed it, its rods (the collision) grow back in place.
	for (int32 Slot = Open.Num() - 1; Slot >= 0; --Slot)
	{
		const int32 Index = Open[Slot];
		FCell& Cell = Cells[Index];
		const FVector2D Centre = CellCentre(Index);
		const bool bInside = Wounds.ContainsByPredicate([&Centre, Now](const FWound& Wound)
		{
			return WoundGap(Wound, Centre, float(Now - Wound.TornAt)) < 0.0f;
		});
		if (bInside)
		{
			continue;
		}
		Cell.Growth = FMath::Min(1.0f, Cell.Growth + DeltaSeconds / (0.5f * StrandGrowSeconds * (Cell.bThin ? ThinSpotSlowdown : 1.0f)));
		WriteCell(Index, Now, 0.0f);
		if (Cell.Growth >= 1.0f)
		{
			Open.RemoveAtSwap(Slot);
		}
	}
}

void AJamLivingWall::UpdateTearing(double Now)
{
	for (FWound& Wound : Wounds)
	{
		const float Age = float(Now - Wound.TornAt);
		for (int32 Slot = Wound.Unopened.Num() - 1; Slot >= 0; --Slot)
		{
			const int32 Index = Wound.Unopened[Slot];
			if (WoundGap(Wound, CellCentre(Index), Age) >= 0.0f && Age < Wound.Tear * 1.6f + 0.05f)
			{
				continue; // not reached yet (a cell the outline never reaches is dropped after the tear)
			}
			Wound.Unopened.RemoveAtSwap(Slot);
			FCell& Cell = Cells[Index];
			if (WoundGap(Wound, CellCentre(Index), Age) >= 0.0f)
			{
				continue;
			}
			if (Cell.Growth >= 1.0f)
			{
				Open.Add(Index);
			}
			Cell.Growth = 0.0f;
			Cell.From = FIntPoint::ZeroValue;
			WriteCell(Index, Now, 0.0f);
		}
	}
}

void AJamLivingWall::UpdateWounds(double Now)
{
	const FTransform Wall = GetActorTransform();
	for (int32 Slot = Wounds.Num() - 1; Slot >= 0; --Slot)
	{
		FWound& Wound = Wounds[Slot];
		// A jet stops once its rim cell has started to regrow.
		Wound.SpurtCells.RemoveAll([this, &Wound](int32 Index) { return Cells[Index].Growth > 0.0f && Wound.Unopened.IsEmpty(); });
		if (Now >= Wound.NextPulse && !Wound.SpurtCells.IsEmpty())
		{
			Wound.NextPulse = Now + SpurtPulseSeconds;
			for (const int32 Index : Wound.SpurtCells)
			{
				Spurt(Wound, Index);
			}
		}
		if (Now - Wound.TornAt >= WoundEnd(Wound) && Wound.Unopened.IsEmpty() && !Wound.Cells.ContainsByPredicate([this](int32 Index) { return Cells[Index].Growth < 1.0f; }))
		{
			UE_LOG(LogMindKore, Log, TEXT("Living wall %s: a hole of %d cells closed after %.2f s"), *GetName(), Wound.Cells.Num(), Now - Wound.TornAt);
			if (CloseSound)
			{
				UGameplayStatics::PlaySoundAtLocation(this, CloseSound, Wall.TransformPosition(FVector(Wound.Centre.X, 0.0f, Wound.Centre.Y)),
					FMath::Min(0.8f, 0.3f + 0.02f * Wound.Cells.Num()), 0.6f);
			}
			Wounds.RemoveAtSwap(Slot);
		}
	}
}

void AJamLivingWall::Spurt(const FWound& Wound, int32 Cell) const
{
	if (!SpurtVfx)
	{
		return;
	}
	// Out of the face the blast came from, leaning away from the hole's centre.
	const FVector2D Out = (CellCentre(Cell) - Wound.Centre).GetSafeNormal();
	const FVector Local = FVector(Out.X * 0.5f, Wound.Side, Out.Y * 0.5f).GetSafeNormal();
	const FVector2D At = CellCentre(Cell);
	const FTransform Wall = GetActorTransform();
	if (UNiagaraComponent* Effect = UNiagaraFunctionLibrary::SpawnSystemAtLocation(this, SpurtVfx,
		Wall.TransformPosition(FVector(At.X, Wound.Side * GetHalfDepthCm(), At.Y)), Wall.TransformVectorNoScale(Local).Rotation(),
		FVector(0.6f), /*bAutoDestroy=*/true, /*bAutoActivate=*/false))
	{
		Effect->SetVariableInt(TEXT("User.BloodDropletCount"), 10);
		Effect->SetVariableInt(TEXT("User.BloodMistCount"), 2);
		Effect->Activate();
	}
}

void AJamLivingWall::PushOutBodies(double Now)
{
	// Growing cells' extent in wall space.
	FBox2D Growing(ForceInit);
	for (const int32 Index : Open)
	{
		if (Cells[Index].Growth > 0.0f)
		{
			Growing += CellCentre(Index);
		}
	}
	if (!Growing.bIsValid)
	{
		return;
	}
	constexpr float ReachCm = 100.0f;
	const FTransform Wall = GetActorTransform();
	const FVector2D Middle = Growing.GetCenter();
	const FVector2D Extent = Growing.GetExtent() + FVector2D(CellSizeCm * 0.5f + ReachCm);
	FCollisionObjectQueryParams Objects;
	Objects.AddObjectTypesToQuery(ECC_Pawn);
	Objects.AddObjectTypesToQuery(ECC_PhysicsBody);
	FCollisionQueryParams Params(SCENE_QUERY_STAT(JamLivingWall), false, this);
	TArray<FOverlapResult> Overlaps;
	GetWorld()->OverlapMultiByObjectType(Overlaps, Wall.TransformPosition(FVector(Middle.X, 0.0f, Middle.Y)), GetActorQuat(), Objects,
		FCollisionShape::MakeBox(FVector(Extent.X, GetHalfDepthCm() + ReachCm, Extent.Y)), Params);
	TSet<UPrimitiveComponent*> Seen;
	for (const FOverlapResult& Overlap : Overlaps)
	{
		UPrimitiveComponent* Body = Overlap.GetComponent();
		if (!Body || !Body->IsSimulatingPhysics() || Seen.Contains(Body))
		{
			continue;
		}
		Seen.Add(Body);
		// In a growing cell: its collision within reach of the cell's strands
		// (half a strand's length from the cell centre).
		const float Reach = (CellSizeCm + StrandOverlapCm) * 0.5f;
		float Deepest = -1.0f;
		for (const int32 Index : Open)
		{
			const float Growth = Cells[Index].Growth;
			if (Growth <= 0.0f || Growth <= Deepest)
			{
				continue;
			}
			const FVector2D Centre = CellCentre(Index);
			FVector Closest;
			const float Distance = Body->GetDistanceToCollision(Wall.TransformPosition(FVector(Centre.X, 0.0f, Centre.Y)), Closest);
			if (Distance >= 0.0f && Distance < Reach)
			{
				Deepest = Growth;
			}
		}
		if (Deepest < 0.0f)
		{
			continue;
		}
		// Out toward the side its centre is on (on the plane: the way it moves).
		const FVector Local = Wall.InverseTransformPosition(Body->Bounds.Origin);
		const FVector Front = GetFrontNormal();
		const FVector& Box = Body->Bounds.BoxExtent;
		const USphereComponent* Sphere = Cast<USphereComponent>(Body);
		const float Radius = Sphere ? Sphere->GetScaledSphereRadius()
			: float(FMath::Abs(Front.X) * Box.X + FMath::Abs(Front.Y) * Box.Y + FMath::Abs(Front.Z) * Box.Z);
		const FVector Velocity = Body->GetPhysicsLinearVelocity();
		const float Side = FMath::Abs(Local.Y) > 1.0f ? FMath::Sign(float(Local.Y)) : (FVector::DotProduct(Velocity, Front) >= 0.0f ? 1.0f : -1.0f);
		const FVector Out = Front * Side;
		const float Along = FVector::DotProduct(Velocity, Out);
		if (Along < PushOutSpeedCm)
		{
			Body->SetPhysicsLinearVelocity(Velocity + Out * (PushOutSpeedCm - Along));
		}
		if (Deepest >= PushOutTeleportGrowth)
		{
			const FVector Move = Out * FMath::Max(0.0f, GetHalfDepthCm() + Radius + 2.0f - float(FMath::Abs(Local.Y)));
			Body->SetWorldLocation(Body->GetComponentLocation() + Move, false, nullptr, ETeleportType::TeleportPhysics);
		}
		AActor* Squeezed = Body->GetOwner();
		double& Last = LastSqueeze.FindOrAdd(TWeakObjectPtr<AActor>(Squeezed), -1000.0);
		if (Now - Last >= SqueezeIntervalSeconds)
		{
			Last = Now;
			UMKVitalsComponent* Vitals = Squeezed ? Squeezed->FindComponentByClass<UMKVitalsComponent>() : nullptr;
			if (Vitals && SqueezeDamage > 0.0f)
			{
				Vitals->ApplyIncoming(SqueezeDamage, EMKImpactKind::Blunt, /*bBlocked=*/false, FGuid::NewGuid());
			}
			UE_LOG(LogMindKore, Log, TEXT("Living wall %s: squeezed %s out to the %s (growth %.2f)%s"), *GetName(), *GetNameSafe(Squeezed),
				Side > 0.0f ? TEXT("front") : TEXT("back"), Deepest, Vitals && SqueezeDamage > 0.0f ? *FString::Printf(TEXT(", %.0f damage"), SqueezeDamage) : TEXT(""));
		}
	}
}

void AJamLivingWall::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	const double Now = GetWorld()->GetTimeSeconds();
	if (!Pending.IsEmpty() && Now - PendingSince >= BlastWindowSeconds)
	{
		ProcessHits(Now);
	}
	if (!Wounds.IsEmpty())
	{
		UpdateTearing(Now);
	}
	if (!Open.IsEmpty())
	{
		UpdateCells(DeltaSeconds, Now);
		PushOutBodies(Now);
	}
	if (!Open.IsEmpty() || !Twitching.IsEmpty())
	{
		UpdateRim(Now);
	}
	if (!Wounds.IsEmpty())
	{
		UpdateWounds(Now);
	}
	UpdateHoles(Now);
}

// --- queries ----------------------------------------------------------------------------

int32 AJamLivingWall::GetOpenCellCount() const
{
	return Open.Num();
}

FVector AJamLivingWall::GetLastWoundCentre() const
{
	return GetActorTransform().TransformPosition(FVector(LastWoundCentre.X, 0.0f, LastWoundCentre.Y));
}

int32 AJamLivingWall::CountTornBodies(float& OutLargestScale) const
{
	int32 Count = 0;
	OutLargestScale = 0.0f;
	const TArray<FBodyInstance*>& Bodies = Strands->GetInstanceBodies();
	for (const int32 Index : Open)
	{
		for (int32 Instance = Index * 2; Cells[Index].Growth <= 0.0f && Instance <= Index * 2 + 1; ++Instance)
		{
			if (Bodies.IsValidIndex(Instance) && Bodies[Instance] && Bodies[Instance]->IsValidBodyInstance())
			{
				++Count;
				OutLargestScale = FMath::Max(OutLargestScale, float(Bodies[Instance]->Scale3D.GetMax()));
			}
		}
	}
	return Count;
}

FVector AJamLivingWall::NearestCellCentre(const FVector& WorldPoint) const
{
	const FVector Local = GetActorTransform().InverseTransformPosition(WorldPoint);
	const int32 X = FMath::Clamp(FMath::FloorToInt32(Local.X / CellSizeCm + CellsX * 0.5f), 0, CellsX - 1);
	const int32 Z = FMath::Clamp(FMath::FloorToInt32(Local.Z / CellSizeCm + CellsZ * 0.5f), 0, CellsZ - 1);
	const FVector2D Centre = CellCentre(X + Z * CellsX);
	return GetActorTransform().TransformPosition(FVector(Centre.X, 0.0f, Centre.Y));
}

float AJamLivingWall::ClearRadiusAt(const FVector& WorldPoint) const
{
	const FVector Local = GetActorTransform().InverseTransformPosition(WorldPoint);
	const FVector2D Point(Local.X, Local.Z);
	const double Now = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.0;
	float Clear = TNumericLimits<float>::Max();
	for (int32 Index = 0; Index < Cells.Num(); ++Index)
	{
		if (!Cells[Index].bExists || Cells[Index].Growth <= 0.0f || FVector2D::Distance(CellCentre(Index), Point) > Clear + CellSizeCm + StrandOverlapCm)
		{
			continue;
		}
		FVector2D A[2], B[2];
		float Depth[2];
		float Radius = 0.0f;
		StrandSegments(Index, Now, 0.0f, A, B, Depth, Radius);
		for (int32 Strand = 0; Strand < 2; ++Strand)
		{
			const FVector2D Closest = FMath::ClosestPointOnSegment2D(Point, A[Strand], B[Strand]);
			Clear = FMath::Min(Clear, float(FVector2D::Distance(Point, Closest)) - Radius);
		}
	}
	return FMath::Max(0.0f, Clear);
}

float AJamLivingWall::LargestClearRadius(FVector& OutCentre) const
{
	float Best = 0.0f;
	OutCentre = GetActorLocation();
	const FTransform Wall = GetActorTransform();
	for (const int32 Index : Open)
	{
		if (Cells[Index].Growth > 0.0f)
		{
			continue;
		}
		const FVector2D Centre = CellCentre(Index);
		const FVector At = Wall.TransformPosition(FVector(Centre.X, 0.0f, Centre.Y));
		if (const float Clear = ClearRadiusAt(At); Clear > Best)
		{
			Best = Clear;
			OutCentre = At;
		}
	}
	return Best;
}

// --- console probe --------------------------------------------------------------------------

namespace
{
	void WallAfter(AActor* Owner, float Seconds, TFunction<void()> Step)
	{
		FTimerHandle Handle;
		Owner->GetWorldTimerManager().SetTimer(Handle, FTimerDelegate::CreateWeakLambda(Owner, MoveTemp(Step)), Seconds, false);
	}

	AJamZeroGPawn* WallProbePawn(const UWorld* World)
	{
		const APlayerController* Controller = World->GetFirstPlayerController();
		return Controller ? Cast<AJamZeroGPawn>(Controller->GetPawn()) : nullptr;
	}

	void PlacePawn(AJamZeroGPawn* Pawn, const FVector& Where)
	{
		Pawn->Body->SetWorldLocation(Where, false, nullptr, ETeleportType::TeleportPhysics);
		Pawn->Body->SetPhysicsLinearVelocity(FVector::ZeroVector);
	}

	/** Unlock and take the gun, aim the view at Target, fire through the pawn's own path. */
	bool AimAndFire(AJamZeroGPawn* Pawn, int32 Weapon, const FVector& Target)
	{
		Pawn->Guns->UnlockWeapon(Weapon);
		Pawn->Guns->FinishSwitch(); // no draw: the probe shoots at once
		if (AController* Controller = Pawn->GetController())
		{
			Controller->SetControlRotation((Target - Pawn->Camera->GetComponentLocation()).Rotation());
		}
		const bool bFired = Pawn->Fire();
		if (!bFired)
		{
			UE_LOG(LogMindKore, Warning, TEXT("Wall probe: %s did not fire"), *Pawn->Guns->Describe());
		}
		return bFired;
	}

	/** From a shotgun shot DistanceCm away to the wall working out the blast: the pellets' flight and the wall's blast window. */
	float BlastLandsAfter(const AJamLivingWall* Wall, const AJamZeroGPawn* Pawn, float DistanceCm)
	{
		const float Speed = Pawn->Guns->Weapons[1].ProjectileSpeed;
		return (Speed > 0.0f ? DistanceCm / Speed : 0.0f) + Wall->BlastWindowSeconds;
	}

	/** Would the pawn's sphere swept along the normal through Point touch this wall? */
	bool SweepBlocked(const AJamLivingWall* Wall, const AJamZeroGPawn* Pawn, const FVector& Point)
	{
		const float Radius = Pawn->Body->GetScaledSphereRadius();
		const FVector Reach = Wall->GetFrontNormal() * (Wall->GetHalfDepthCm() + Radius + 20.0f);
		FCollisionObjectQueryParams Objects(FCollisionObjectQueryParams::AllObjects);
		FCollisionQueryParams Params(SCENE_QUERY_STAT(JamWallProbe), false, Pawn);
		TArray<FHitResult> Hits;
		Wall->GetWorld()->SweepMultiByObjectType(Hits, Point + Reach, Point - Reach, FQuat::Identity, Objects, FCollisionShape::MakeSphere(Radius), Params);
		return Hits.ContainsByPredicate([Wall](const FHitResult& Hit) { return Hit.GetActor() == Wall; });
	}

	/** Would a sphere of Radius swept along the wall normal through Point hit anything at all (the wall, its frame, rock)? */
	bool SweepAnyBlocked(const AJamLivingWall* Wall, const AActor* Ignored, const FVector& Point, float Radius)
	{
		const FVector Reach = Wall->GetFrontNormal() * (Wall->GetHalfDepthCm() + Radius + 20.0f);
		FCollisionQueryParams Params(SCENE_QUERY_STAT(JamWallSeal), false, Ignored);
		return Wall->GetWorld()->SweepTestByObjectType(Point + Reach, Point - Reach, FQuat::Identity,
			FCollisionObjectQueryParams(FCollisionObjectQueryParams::AllObjects), FCollisionShape::MakeSphere(Radius), Params);
	}

	/**
	 * A whole wall must be sealed: the pawn sphere (r 40) and a 15 cm one swept
	 * through a 25 cm grid over the opening, corners included (a round wall:
	 * inside its circle), must all be stopped. A round wall also gets rings of
	 * 15 cm sweeps 15 cm inside its edge, on it and 15 cm outside: they must hit
	 * the wall or whatever holds it (a crater floor, a tunnel throat) - no gap
	 * around it. (Farther out a sweep can start and end inside the rock, which
	 * is a surface, and pass without meaning a gap.)
	 */
	void CheckSealed(const AJamLivingWall* Wall, const AActor* Ignored)
	{
		const FTransform Frame = Wall->GetActorTransform();
		const float HalfW = Wall->WidthCm * 0.5f;
		const float HalfH = Wall->HeightCm * 0.5f;
		int32 Points = 0;
		int32 PawnPasses = 0;
		int32 SmallPasses = 0;
		FString First;
		for (float X = -HalfW + 5.0f; X <= HalfW - 4.9f; X += 25.0f)
		{
			for (float Z = -HalfH + 5.0f; Z <= HalfH - 4.9f; Z += 25.0f)
			{
				if (Wall->bRound && FMath::Square(X / (HalfW - 8.0f)) + FMath::Square(Z / (HalfH - 8.0f)) > 1.0f)
				{
					continue;
				}
				const FVector At = Frame.TransformPosition(FVector(X, 0.0f, Z));
				++Points;
				const bool bPawn = !SweepAnyBlocked(Wall, Ignored, At, 40.0f);
				const bool bSmall = !SweepAnyBlocked(Wall, Ignored, At, 15.0f);
				PawnPasses += bPawn;
				SmallPasses += bSmall;
				if ((bPawn || bSmall) && First.IsEmpty())
				{
					First = FString::Printf(TEXT(", first at (%.0f, %.0f)"), X, Z);
				}
			}
		}
		FString Ring;
		int32 RingPasses = 0;
		if (Wall->bRound)
		{
			for (const float Out : { -15.0f, 0.0f, 15.0f })
			{
				int32 Passes = 0;
				for (int32 Step = 0; Step < 36; ++Step)
				{
					const float Angle = Step * 2.0f * PI / 36.0f;
					const FVector At = Frame.TransformPosition(FVector((HalfW + Out) * FMath::Cos(Angle), 0.0f, (HalfH + Out) * FMath::Sin(Angle)));
					Passes += !SweepAnyBlocked(Wall, Ignored, At, 15.0f);
				}
				RingPasses += Passes;
				Ring += FString::Printf(TEXT("%s%+.0f cm: %d/36"), Ring.IsEmpty() ? TEXT(", rings of 15 cm sweeps around the edge ") : TEXT(", "), Out, Passes);
			}
		}
		UE_LOG(LogMindKore, Log, TEXT("Wall seal %s (%s, %.0f x %.0f cm): %d grid points, the pawn sphere passes at %d, a 15 cm sphere at %d%s%s - %s"),
			*Wall->GetName(), Wall->bRound ? TEXT("round") : TEXT("rectangular"), Wall->WidthCm, Wall->HeightCm, Points, PawnPasses, SmallPasses,
			*First, *Ring, PawnPasses + SmallPasses + RingPasses == 0 ? TEXT("sealed") : TEXT("NOT sealed"));
	}

	/** Launch the pawn through Point along -front so its centre reaches Point at half of FlightSeconds. */
	void Launch(AJamLivingWall* Wall, AJamZeroGPawn* Pawn, const FVector& Point, const TCHAR* Label)
	{
		constexpr float StartCm = 120.0f;
		constexpr float FlightSeconds = 0.4f;
		const FVector Front = Wall->GetFrontNormal();
		const FVector Start = Point + Front * StartCm;
		PlacePawn(Pawn, Start);
		// The zones' walking gravity is 980 cm/s^2 (Docs/Ship-Blockout.md); zero-G: none.
		const FVector Gravity = Pawn->IsInGravity() ? Pawn->GetGravityDirection() * 980.0f : FVector::ZeroVector;
		const float Half = FlightSeconds * 0.5f;
		const FVector Velocity = (Point - Start) / Half - Gravity * Half * 0.5f;
		Pawn->Body->SetPhysicsLinearVelocity(Velocity);
		const float Health = Pawn->Vitals->Health;
		const FString Name = Label;
		UE_LOG(LogMindKore, Log, TEXT("Wall probe [%s]: pawn launched at %.0f cm/s from %.0f cm in front, open cells %d, clear radius at the point %.0f cm"),
			*Name, Velocity.Size(), StartCm, Wall->GetOpenCellCount(), Wall->ClearRadiusAt(Point));
		// The deepest the centre got behind the plane within a second (behind the
		// wall it may bounce off whatever the wall hides and come back).
		const TSharedRef<float> Deepest = MakeShared<float>(TNumericLimits<float>::Max());
		const TSharedRef<FTimerHandle> Handle = MakeShared<FTimerHandle>();
		const double End = Wall->GetWorld()->GetTimeSeconds() + 1.0;
		Wall->GetWorldTimerManager().SetTimer(*Handle, FTimerDelegate::CreateWeakLambda(Wall, [Wall, Pawn, Name, Health, Deepest, Handle, End]()
		{
			const FVector Local = Wall->GetActorTransform().InverseTransformPosition(Pawn->GetActorLocation());
			*Deepest = FMath::Min(*Deepest, float(Local.Y));
			if (Wall->GetWorld()->GetTimeSeconds() < End)
			{
				return;
			}
			const bool bPassed = *Deepest < -(Wall->GetHalfDepthCm() + Pawn->Body->GetScaledSphereRadius());
			UE_LOG(LogMindKore, Log, TEXT("Wall probe [%s]: passed %s - deepest %.0f cm %s the wall plane, now %.0f cm %s, HP %.0f -> %.0f"), *Name,
				bPassed ? TEXT("yes") : TEXT("no"), FMath::Abs(*Deepest), *Deepest < 0.0f ? TEXT("behind") : TEXT("in front of"),
				FMath::Abs(Local.Y), Local.Y < 0.0f ? TEXT("behind") : TEXT("in front"), Health, Pawn->Vitals->Health);
			// Last: clearing the timer destroys this lambda and its captures.
			Wall->GetWorldTimerManager().ClearTimer(*Handle);
		}), 0.02f, true);
	}

	/**
	 * The ship's cocoon: a Ship.Nav prop within 4 m of the wall (the probe's shots and the parked pawn may have nudged it) is pushed
	 * out through a shotgun hole at 250 cm/s (as a pull would), and the probe
	 * logs whether all of it got past the weave before it closed.
	 */
	void ProbeModuleOut(AJamLivingWall* Wall, AJamZeroGPawn* Pawn)
	{
		UPrimitiveComponent* Module = nullptr;
		for (TActorIterator<AActor> It(Wall->GetWorld()); It; ++It)
		{
			UPrimitiveComponent* Root = Cast<UPrimitiveComponent>(It->GetRootComponent());
			if (It->ActorHasTag(TEXT("Ship.Nav")) && Root && Root->IsSimulatingPhysics()
				&& FVector::Dist(It->GetActorLocation(), Wall->GetActorLocation()) < 400.0f)
			{
				Module = Root;
			}
		}
		if (!Module)
		{
			UE_LOG(LogMindKore, Log, TEXT("Wall probe: done"));
			return;
		}
		const FVector Front = Wall->GetFrontNormal();
		const FVector Aim = Wall->NearestCellCentre(Module->GetComponentLocation());
		PlacePawn(Pawn, Aim + Front * 400.0f);
		WallAfter(Wall, 0.5f, [Wall, Pawn, Module, Aim, Front]()
		{
			AimAndFire(Pawn, 1, Aim);
			// Out of the module's way, to the side.
			PlacePawn(Pawn, Aim + Front * 400.0f + Wall->GetActorForwardVector() * 300.0f);
			WallAfter(Wall, 0.1f + BlastLandsAfter(Wall, Pawn, 400.0f), [Wall, Module, Front]()
			{
				const FVector Hole = Wall->GetLastWoundCentre();
				Module->SetPhysicsAngularVelocityInDegrees(FVector::ZeroVector);
				Module->SetPhysicsLinearVelocity(((Hole + Front * 150.0f) - Module->GetComponentLocation()).GetSafeNormal() * 250.0f);
				UE_LOG(LogMindKore, Log, TEXT("Wall probe [module out]: %s (%.0f kg) pushed at the hole, clear radius %.0f cm (a 1 m face needs 71)"),
					*GetNameSafe(Module->GetOwner()), Module->GetMass(), Wall->ClearRadiusAt(Hole));
				WallAfter(Wall, 3.0f, [Wall, Module]()
				{
					const FVector Local = Wall->GetActorTransform().InverseTransformPosition(Module->GetComponentLocation());
					const float Half = float(Module->Bounds.BoxExtent.GetMax());
					UE_LOG(LogMindKore, Log, TEXT("Wall probe [module out]: +3.1 s centre %.0f cm %s the plane, wall open %d - %s"),
						FMath::Abs(Local.Y), Local.Y >= 0.0f ? TEXT("in front of") : TEXT("behind"), Wall->GetOpenCellCount(),
						Local.Y > Wall->GetHalfDepthCm() + Half ? TEXT("out") : TEXT("NOT out"));
					UE_LOG(LogMindKore, Log, TEXT("Wall probe: done"));
				});
			});
		});
	}

	struct FWatch
	{
		double FiredAt = 0.0;
		int32 MostOpen = 0;
		double LastPassable = -1.0;
		double LastSweepClear = -1.0;
		double NextLog = 0.0;
		double NextEarly = 0.0;
		FVector Hole = FVector::ZeroVector;
		bool bSawOpen = false;
	};

	/** Poll the wall from the shot until it is whole again, then run Done. */
	void WatchUntilClosed(AJamLivingWall* Wall, AJamZeroGPawn* Pawn, const FString& Label, bool bWindow, TFunction<void()> Done)
	{
		const TSharedRef<FWatch> Watch = MakeShared<FWatch>();
		Watch->FiredAt = Wall->GetWorld()->GetTimeSeconds();
		const TSharedRef<FTimerHandle> Handle = MakeShared<FTimerHandle>();
		Wall->GetWorldTimerManager().SetTimer(*Handle, FTimerDelegate::CreateWeakLambda(Wall, [Wall, Pawn, Label, bWindow, Done, Watch, Handle]()
		{
			const double Now = Wall->GetWorld()->GetTimeSeconds();
			const double Since = Now - Watch->FiredAt;
			const int32 Open = Wall->GetOpenCellCount();
			if (Open > 0 && !Watch->bSawOpen)
			{
				Watch->bSawOpen = true;
				Watch->Hole = Wall->GetLastWoundCentre();
				float BodyScale = 0.0f;
				const int32 Bodies = Wall->CountTornBodies(BodyScale);
				UE_LOG(LogMindKore, Log, TEXT("Wall probe [%s]: +%.2f s torn %d cells, hole centre %.0f cm from the wall centre, clear radius there %.0f cm, physics bodies left on torn strands %d (largest scale %.3f)"),
					*Label, Since, Open, FVector::Dist(Watch->Hole, Wall->GetActorLocation()), Wall->ClearRadiusAt(Watch->Hole), Bodies, BodyScale);
			}
			Watch->MostOpen = FMath::Max(Watch->MostOpen, Open);
			// The tear opening: the open cells and the clear radius at the hole centre over the first 0.4 s.
			if (Since <= 0.41 && Now >= Watch->NextEarly)
			{
				Watch->NextEarly = Now + 0.05;
				UE_LOG(LogMindKore, Log, TEXT("Wall probe [%s]: tearing +%.2f s, open %d cells, clear radius at the hole centre %.0f cm"),
					*Label, Since, Open, Wall->ClearRadiusAt(Wall->GetLastWoundCentre()));
			}
			if (bWindow && Watch->bSawOpen)
			{
				FVector Widest;
				const float Largest = Wall->LargestClearRadius(Widest);
				const float AtHole = Wall->ClearRadiusAt(Watch->Hole);
				const bool bBlocked = SweepBlocked(Wall, Pawn, Watch->Hole);
				const float PawnRadius = Pawn->Body->GetScaledSphereRadius();
				if (Largest >= PawnRadius)
				{
					Watch->LastPassable = Since;
				}
				if (!bBlocked)
				{
					Watch->LastSweepClear = Since;
				}
				if (Now >= Watch->NextLog)
				{
					Watch->NextLog = Now + 0.25;
					UE_LOG(LogMindKore, Log, TEXT("Wall probe [%s]: +%.2f s open %d, clear radius at the hole centre %.0f cm, largest %.0f cm, pawn sphere (r %.0f) through the centre %s"),
						*Label, Since, Open, AtHole, Largest, PawnRadius, bBlocked ? TEXT("blocked") : TEXT("passes"));
				}
			}
			if ((Watch->bSawOpen && Open == 0) || Since > 10.0)
			{
				UE_LOG(LogMindKore, Log, TEXT("Wall probe [%s]: %s at +%.2f s after the shot, most open %d cells%s"), *Label,
					Open == 0 && Watch->bSawOpen ? TEXT("whole again") : TEXT("NOT closed (timeout)"), Since, Watch->MostOpen,
					bWindow ? *FString::Printf(TEXT(", passable (largest clear radius >= pawn radius) until +%.2f s, the sweep through the hole centre clear until +%.2f s"),
						Watch->LastPassable, Watch->LastSweepClear) : TEXT(""));
				// Clearing the timer destroys this lambda and its captures: keep the next step.
				const TFunction<void()> Next = Done;
				Wall->GetWorldTimerManager().ClearTimer(*Handle);
				Next();
			}
		}), 0.02f, true);
	}

	/**
	 * The pistol held down from 3 m: PistolShots rounds at its fire rate, scattered over a 20 cm
	 * circle; after each one the open cells and a sweep of the pawn's sphere through the aim point.
	 * Single hits must never open the wall. Then Done once the last wound has had time to close.
	 */
	void PistolSpray(AJamLivingWall* Wall, AJamZeroGPawn* Pawn, const FVector& Centre, const FVector& Front, TFunction<void()> Done, int32 Shot = 0)
	{
		constexpr int32 PistolShots = 8;
		if (Shot == PistolShots)
		{
			WallAfter(Wall, 0.6f, [Wall, Done]()
			{
				UE_LOG(LogMindKore, Log, TEXT("Wall probe [pistol 3 m]: +0.60 s after the last round, open %d cells"), Wall->GetOpenCellCount());
				Done();
			});
			return;
		}
		const FVector Up = FVector::CrossProduct(Front, FVector::CrossProduct(FVector::UpVector, Front).GetSafeNormal());
		const FVector Side = FVector::CrossProduct(Up, Front);
		const float Angle = UE_TWO_PI * Shot / PistolShots;
		const FVector Aim = Centre + (Side * FMath::Cos(Angle) + Up * FMath::Sin(Angle)) * (Shot % 2 ? 20.0f : 8.0f);
		PlacePawn(Pawn, Centre + Front * 300.0f);
		AimAndFire(Pawn, 0, Aim);
		PlacePawn(Pawn, Centre + Front * 300.0f);
		WallAfter(Wall, 0.17f, [Wall, Pawn, Centre, Front, Done, Shot]()
		{
			UE_LOG(LogMindKore, Log, TEXT("Wall probe [pistol 3 m]: round %d, open %d cells, pawn sphere through the aim point %s"),
				Shot + 1, Wall->GetOpenCellCount(), SweepBlocked(Wall, Pawn, Centre) ? TEXT("blocked") : TEXT("passes"));
			PistolSpray(Wall, Pawn, Centre, Front, Done, Shot + 1);
		});
	}

	/**
	 * jam.Wall.Probe [Name]: on the wall named (or tagged Jam.WallProbe, or the
	 * first): the pistol held down from 3 m (PistolSpray); the
	 * shotgun from 5 m - torn cells, the clear radius and a sweep of the pawn's
	 * sphere through the hole over time, time to close; another shotgun blast and
	 * the pawn flown through the hole; then the same flight at the whole wall.
	 */
	void RunWallProbe(const TArray<FString>& Args, UWorld* World)
	{
		AJamLivingWall* Wall = nullptr;
		for (TActorIterator<AJamLivingWall> It(World); It; ++It)
		{
			if (Args.Num() > 0 ? It->GetName() == Args[0] : (!Wall || It->ActorHasTag(TEXT("Jam.WallProbe"))))
			{
				Wall = *It;
			}
		}
		AJamZeroGPawn* Pawn = WallProbePawn(World);
		if (!Wall || !Pawn)
		{
			UE_LOG(LogMindKore, Warning, TEXT("Wall probe: needs an AJamLivingWall and the AJamZeroGPawn"));
			return;
		}
		CheckSealed(Wall, Pawn);
		// Aim where a cell's strands cross: the weave has 8 cm gaps at the cell
		// corners (the wall centre is one when the cell count is even).
		const FVector Centre = Wall->NearestCellCentre(Wall->GetActorLocation());
		const FVector Front = Wall->GetFrontNormal();
		UE_LOG(LogMindKore, Log, TEXT("Wall probe: %s, aim at %s, front %s, %.0f x %.0f cm, sweep through the whole wall there %s, clear radius there %.0f cm"),
			*Wall->GetName(), *Centre.ToCompactString(), *Front.ToCompactString(), Wall->WidthCm, Wall->HeightCm,
			SweepBlocked(Wall, Pawn, Centre) ? TEXT("blocked") : TEXT("passes"), Wall->ClearRadiusAt(Centre));
		PlacePawn(Pawn, Centre + Front * 300.0f);
		WallAfter(Wall, 0.3f, [Wall, Pawn, Centre, Front]()
		{
			PistolSpray(Wall, Pawn, Centre, Front, [Wall, Pawn, Centre, Front]()
			{
				PlacePawn(Pawn, Centre + Front * 500.0f);
				WallAfter(Wall, 0.5f, [Wall, Pawn, Centre, Front]()
				{
					AimAndFire(Pawn, 1, Centre);
					PlacePawn(Pawn, Centre + Front * 500.0f);
					WatchUntilClosed(Wall, Pawn, TEXT("shotgun 5 m"), true, [Wall, Pawn, Centre, Front]()
					{
						PlacePawn(Pawn, Centre + Front * 500.0f);
						WallAfter(Wall, 0.9f, [Wall, Pawn, Centre, Front]()
						{
							AimAndFire(Pawn, 1, Centre);
							WallAfter(Wall, 0.1f + BlastLandsAfter(Wall, Pawn, 500.0f), [Wall, Pawn]() { Launch(Wall, Pawn, Wall->GetLastWoundCentre(), TEXT("fly through the shotgun hole, torn +0.1 s")); });
							WatchUntilClosed(Wall, Pawn, TEXT("shotgun 5 m, flight"), false, [Wall, Pawn, Centre, Front]()
							{
								PlacePawn(Pawn, Centre + Front * 500.0f);
								WallAfter(Wall, 0.9f, [Wall, Pawn, Centre]()
								{
									// Parked in the hole: the closing weave must push it out, not trap it.
									AimAndFire(Pawn, 1, Centre);
									WallAfter(Wall, 0.35f + BlastLandsAfter(Wall, Pawn, 500.0f), [Wall, Pawn]() // once the tear has opened (BlastTearSeconds)
									{
										// Only into a hole the sphere fits: teleported into strands, the
										// solver throws it out at tens of m/s (seen: 71 m/s, 325 damage).
										const float Clear = Wall->ClearRadiusAt(Wall->GetLastWoundCentre());
										const float Needs = Pawn->Body->GetScaledSphereRadius() + 2.0f;
										if (Clear < Needs)
										{
											UE_LOG(LogMindKore, Log, TEXT("Wall probe [parked in the hole]: SKIPPED - this blast left a clear radius of %.0f cm, the pawn needs %.0f"), Clear, Needs);
											return;
										}
										PlacePawn(Pawn, Wall->GetLastWoundCentre());
										UE_LOG(LogMindKore, Log, TEXT("Wall probe [parked in the hole]: HP %.0f, clear radius %.0f cm"), Pawn->Vitals->Health, Clear);
									});
									WatchUntilClosed(Wall, Pawn, TEXT("shotgun 5 m, parked"), false, [Wall, Pawn, Centre]()
									{
										const FVector Local = Wall->GetActorTransform().InverseTransformPosition(Pawn->GetActorLocation());
										UE_LOG(LogMindKore, Log, TEXT("Wall probe [parked in the hole]: closed with the pawn %.0f cm %s the plane (clear of the weave: %s), HP %.0f"),
											FMath::Abs(Local.Y), Local.Y >= 0.0f ? TEXT("in front of") : TEXT("behind"),
											FMath::Abs(Local.Y) >= Wall->GetHalfDepthCm() + Pawn->Body->GetScaledSphereRadius() ? TEXT("yes") : TEXT("NO"), Pawn->Vitals->Health);
										WallAfter(Wall, 0.3f, [Wall, Pawn, Centre]()
										{
											Launch(Wall, Pawn, Centre, TEXT("control: fly at the whole wall"));
											WallAfter(Wall, 1.5f, [Wall, Pawn]() { ProbeModuleOut(Wall, Pawn); });
										});
									});
								});
							});
						});
					});
				});
			});
		});
	}

	/**
	 * jam.Wall.Shots: rendered stills of the probe wall (needs a real RHI): the
	 * pawn 3.5 m in front of it - the whole weave, right after a shotgun blast,
	 * and mid-regrowth (a quarter of the hole grown back). Saved/Screenshots, in order.
	 */
	void RunWallShots(const TArray<FString>& Args, UWorld* World)
	{
		// The wall named, or the first with the tag given (Jam.Crater, Jam.CavePlug), else the probe wall.
		const FName Tag(Args.Num() > 0 ? *Args[0] : TEXT("Jam.WallProbe"));
		AJamLivingWall* Wall = nullptr;
		for (TActorIterator<AJamLivingWall> It(World); It; ++It)
		{
			if (Args.Num() > 0 && It->GetName() == Args[0])
			{
				Wall = *It;
				break;
			}
			if (!Wall || (It->ActorHasTag(Tag) && !Wall->ActorHasTag(Tag)))
			{
				Wall = *It;
			}
		}
		AJamZeroGPawn* Pawn = WallProbePawn(World);
		APlayerController* Controller = World->GetFirstPlayerController();
		if (!Wall || !Pawn || !Controller)
		{
			return;
		}
		const FVector Centre = Wall->NearestCellCentre(Wall->GetActorLocation());
		const FVector Stand = Centre + Wall->GetFrontNormal() * 350.0f;
		const auto Hold = [Pawn, Controller, Stand, Centre]()
		{
			PlacePawn(Pawn, Stand);
			Controller->SetControlRotation((Centre - Pawn->Camera->GetComponentLocation()).Rotation());
		};
		Hold();
		// Long first wait: the wall's material may still compile its shaders.
		WallAfter(Wall, 9.0f, [Hold]() { Hold(); });
		WallAfter(Wall, 10.0f, [Controller]() { UE_LOG(LogMindKore, Log, TEXT("Wall shots: 0 whole")); Controller->ConsoleCommand(TEXT("HighResShot 1")); });
		WallAfter(Wall, 13.0f, [Hold, Pawn, Centre]() { Hold(); AimAndFire(Pawn, 1, Centre); });
		WallAfter(Wall, 13.05f, [Hold]() { Hold(); });
		WallAfter(Wall, 13.35f, [Controller, Wall]()
		{
			UE_LOG(LogMindKore, Log, TEXT("Wall shots: 1 blast +0.35 s, open %d"), Wall->GetOpenCellCount());
			Controller->ConsoleCommand(TEXT("HighResShot 1"));
		});
		// Mid-regrowth: once a quarter of the hole has grown back (threads across, bundles thickening).
		const TSharedRef<int32> Most = MakeShared<int32>(0);
		const TSharedRef<FTimerHandle> Handle = MakeShared<FTimerHandle>();
		WallAfter(Wall, 13.4f, [Controller, Wall, Most, Handle, Hold]()
		{
			const double Blast = Wall->GetWorld()->GetTimeSeconds() - 0.4;
			Wall->GetWorldTimerManager().SetTimer(*Handle, FTimerDelegate::CreateWeakLambda(Wall, [Controller, Wall, Most, Handle, Blast, Hold]()
			{
				const int32 Open = Wall->GetOpenCellCount();
				*Most = FMath::Max(*Most, Open);
				if (Open > *Most * 3 / 4 && Wall->GetWorld()->GetTimeSeconds() - Blast < 10.0)
				{
					return;
				}
				Hold(); // in gravity the pawn may have fallen through the hole meanwhile
				UE_LOG(LogMindKore, Log, TEXT("Wall shots: 2 regrowing, blast +%.2f s, open %d of %d"), Wall->GetWorld()->GetTimeSeconds() - Blast, Open, *Most);
				Controller->ConsoleCommand(TEXT("HighResShot 1"));
				Wall->GetWorldTimerManager().ClearTimer(*Handle);
			}), 0.02f, true);
		});
		// A second blast once the first has closed: every tear has its own shape.
		WallAfter(Wall, 21.0f, [Hold, Pawn, Centre]() { Hold(); AimAndFire(Pawn, 1, Centre); });
		WallAfter(Wall, 21.05f, [Hold]() { Hold(); });
		WallAfter(Wall, 21.35f, [Controller, Wall]()
		{
			UE_LOG(LogMindKore, Log, TEXT("Wall shots: 3 second blast +0.35 s, open %d"), Wall->GetOpenCellCount());
			Controller->ConsoleCommand(TEXT("HighResShot 1"));
		});
	}

	FAutoConsoleCommandWithWorldAndArgs GWallShots(
		TEXT("jam.Wall.Shots"),
		TEXT("jam.Wall.Shots [Name|Tag]: rendered stills of a living wall: whole, right after a shotgun blast, closing, a second blast (HighResShot; needs a real RHI)."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&RunWallShots));

	/** jam.Wall.Seal: the seal check (CheckSealed) on every living wall of the map. */
	void RunWallSeal(UWorld* World)
	{
		const AActor* Ignored = WallProbePawn(World);
		for (TActorIterator<AJamLivingWall> It(World); It; ++It)
		{
			CheckSealed(*It, Ignored);
		}
	}

	FAutoConsoleCommandWithWorld GWallSeal(
		TEXT("jam.Wall.Seal"),
		TEXT("Sweep the pawn sphere and a 15 cm sphere through a grid over every whole living wall (round ones: and around their edge); logs any pass."),
		FConsoleCommandWithWorldDelegate::CreateStatic(&RunWallSeal));

	FAutoConsoleCommandWithWorldAndArgs GWallProbe(
		TEXT("jam.Wall.Probe"),
		TEXT("jam.Wall.Probe [Name]: pistol and shotgun at a living wall - torn cells, the passable window, close times, the pawn flown through."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&RunWallProbe));
}
