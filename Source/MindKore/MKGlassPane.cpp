#include "MKGlassPane.h"

#include "AI/NavigationSystemBase.h"
#include "Engine/World.h"
#include "UObject/ConstructorHelpers.h"
#include "Kismet/GameplayStatics.h"
#include "Materials/MaterialInterface.h"
#include "MKGlassMesh.h"
#include "MKGlassShardDebris.h"
#include "MindKore.h"
#include "NiagaraFunctionLibrary.h"
#include "NiagaraSystem.h"
#include "Perception/AISense_Hearing.h"
#include "ProceduralMeshComponent.h"
#include "Sound/SoundBase.h"
#include "TimerManager.h"

namespace
{
	constexpr float MKGlassMinThicknessCm = 0.2f;
}

int32 MKShedStepCount(int32 StepIndex, int32 FirstStepCount, float Falloff)
{
	const int32 First = FMath::Max(FirstStepCount, 1);
	const float Decay = FMath::Pow(
		FMath::Clamp(Falloff, 0.05f, 1.0f), static_cast<float>(FMath::Max(StepIndex, 0)));
	// Never zero: a collapse that stops halfway leaves glass hanging off a hole
	// that cannot hold it, and the pane would then wait for the next round to
	// finish a fall the player already watched start.
	return FMath::Max(FMath::RoundToInt(First * Decay), 1);
}

float MKGlassBreakRadiusFor(EMKImpactKind Kind, float RoundRadiusCm, float BluntRadiusCm)
{
	const float Radius = Kind == EMKImpactKind::Blunt ? BluntRadiusCm : RoundRadiusCm;
	return FMath::IsFinite(Radius) ? FMath::Max(Radius, 0.0f) : 0.0f;
}

float MKShedStepInterval(int32 StepIndex, float BaseIntervalSeconds, float Growth)
{
	const float Base = FMath::Max(BaseIntervalSeconds, 0.01f);
	const float Stretch = FMath::Pow(
		FMath::Max(Growth, 1.0f), static_cast<float>(FMath::Max(StepIndex, 0)));
	// A second and a half between stragglers is the outside of what still reads
	// as one event; past that the pane is dropping glass at something that
	// already finished happening.
	return FMath::Min(Base * Stretch, 1.5f);
}

AMKGlassPane::AMKGlassPane()
{
	PrimaryActorTick.bCanEverTick = false;

	PaneRoot = CreateDefaultSubobject<USceneComponent>(TEXT("PaneRoot"));
	SetRootComponent(PaneRoot);

	GlassMesh = CreateDefaultSubobject<UProceduralMeshComponent>(TEXT("GlassMesh"));
	GlassMesh->SetupAttachment(PaneRoot);
	GlassMesh->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
	GlassMesh->SetCollisionProfileName(TEXT("BlockAll"));
	GlassMesh->SetCastShadow(false);
	// UMKFirearmComponent traces with bTraceComplex on, so the per-section
	// triangle collision is what the bullet actually meets. The convex hulls
	// added per shard are the SIMPLE geometry, for overlaps and the navmesh -
	// which is why complex-as-simple has to be off, or the hulls are ignored.
	GlassMesh->bUseComplexAsSimpleCollision = false;
	// The hole has to be a hole for the AI too: with dynamic navigation on
	// (DESIGN_LEVEL section 10, wave L2) removing shards reopens the route.
	GlassMesh->SetCanEverAffectNavigation(true);

	// These are authored defaults, not mandatory dependencies: a level artist
	// can still replace either per pane. The previous pass shipped both slots
	// empty, which made a mechanically correct break read like geometry silently
	// vanishing from the frame.
	static ConstructorHelpers::FObjectFinder<UMaterialInterface> DefaultEdgeMaterial(
		TEXT("/Game/MindKore/Materials/M_MK_Glass_FractureEdge"));
	if (DefaultEdgeMaterial.Succeeded())
	{
		FractureEdgeMaterial = DefaultEdgeMaterial.Object;
	}

	// TEMPORARY: default BreakVfx (NS_MK_GlassBreak) cut - it references the
	// third-party /Game/A_Surface_Footstep pack, which the starter does not ship.
	// The pane breaks without FX. Remove this note when the starter has its own
	// licence-clean glass Niagara system and assigns it here.

	// PLACEHOLDER, and the same one three times over: the only glass in the audio
	// kit is a footstep cue. Volume and pitch carry the break, the collapse and
	// the landings apart, which is enough to hear the SHAPE of the event and not
	// enough to hear glass. Removal condition: a donor cut into layers the way
	// the shot was (DESIGN_AUDIO 13) - then these three take three assets and
	// the pitch tricks below go with the placeholder.
	// TEMPORARY: default BreakSound/ShedSound/DebrisLandingSound cut - the cue
	// came from the third-party Essential_Foosteps_SK pack. The pane breaks
	// silently. Remove this note when the starter has its own licence-clean
	// glass sound and assigns it here.
}

void AMKGlassPane::OnConstruction(const FTransform& Transform)
{
	Super::OnConstruction(Transform);
	RebuildPane();
}

void AMKGlassPane::BeginPlay()
{
	Super::BeginPlay();
	RebuildPane();
}

#if WITH_EDITOR
void AMKGlassPane::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);
	// Retyping a size or a material in the details panel has to redraw the pane,
	// or the numbers in the panel stop describing what is in the viewport.
	RebuildPane();
}
#endif

void AMKGlassPane::RebuildPane()
{
	FractureRandom = FRandomStream(FractureSeed);
	DebrisRandom = FRandomStream(FractureSeed + 1);

	Shards = MKMakeGlassSheet(PaneWidthCm, PaneHeightCm);
	MKSplitGlassIntoBlocks(Shards, BlockPitchCm, PaneWidthCm, PaneHeightCm);

	if (bStartBroken)
	{
		// Silent, and part of the rebuild rather than of BeginPlay: this hole
		// was made before the level loaded, so it gets no sound, no VFX and no
		// noise event - and whoever is dressing the level has to see it in the
		// viewport, not only after pressing play.
		FMKGlassFractureParams Params;
		Params.PaneWidthCm = PaneWidthCm;
		Params.PaneHeightCm = PaneHeightCm;
		Params.BreakRadiusCm = StartBrokenRadiusCm;
		Params.FractureRadiusCm = StartBrokenRadiusCm * 2.0f;
		Params.CrackedLookRadiusCm = CrackedLookRadiusCm;
		Params.CrackedLookJitter = CrackedLookJitter;
		Params.SpokeCount = SpokeCount;
		Params.RunnerCrackCount = RunnerCrackCount;
		Params.RingCount = RingCount;
		Params.MinShardAreaCm2 = MinShardAreaCm2;
		Params.MaxShardCount = MaxShardCount;
		MKFractureGlassAt(Shards, StartBrokenCentre, Params, FractureRandom);

		// An authored hole is old damage: whatever it left hanging fell long
		// before the level loaded, and none of it is thrown as debris.
		TArray<FMKGlassShard> Unsupported;
		MKDropUnsupportedShards(Shards, PaneWidthCm, PaneHeightCm, MinSupportedAreaCm2, Unsupported, bBottomSupportedOnly);
	}

	RefreshGlassMesh();
}

void AMKGlassPane::RefreshGlassMesh()
{
	if (!GlassMesh)
	{
		return;
	}

	// Destructible glass is dynamic geometry. Loose shards ignore this channel
	// so adjacent convex margins cannot wedge a released piece in its old hole.
	GlassMesh->SetCollisionObjectType(ECC_WorldDynamic);

	const float HalfThickness = FMath::Max(PaneThicknessCm, MKGlassMinThicknessCm) * 0.5f;

	// Section 0 is whole glass, section 1 is cracked glass, and section 2 is the
	// thin exposed edge of cracked pieces. Separate sections rather
	// than one with a vertex-colour switch: the cracked look comes from a second
	// material out of the glass pack, and moving a shard between them needs no
	// edit to a bought material graph.
	FMKGlassMeshBuild Sections[3];

	TArray<TArray<FVector>> ShardHulls;
	ShardHulls.Reserve(Shards.Num());

	for (const FMKGlassShard& Shard : Shards)
	{
		if (Shard.Vertices.Num() < 3)
		{
			continue;
		}

		// The accent goes on glass a CRACK cut, not on glass that merely draws
		// frosted: that is what puts a hairline on a runner three metres from the
		// hole, and keeps it off the sheet's own border sitting in a frame.
		FMKGlassMeshBuild* EdgeSection = Shard.bFreshCut && FractureEdgeMaterial ? &Sections[2] : nullptr;
		MKAppendShardGeometry(
			Sections[Shard.bCracked ? 1 : 0], Shard, HalfThickness, UvTilingCm,
			FVector2D::ZeroVector, EdgeSection);
		ShardHulls.Add(MKMakeShardHull(Shard, HalfThickness));
	}

	GlassMesh->ClearAllMeshSections();

	const TArray<FLinearColor> NoColours;
	for (int32 SectionIndex = 0; SectionIndex < 3; ++SectionIndex)
	{
		const FMKGlassMeshBuild& Section = Sections[SectionIndex];
		if (Section.Triangles.Num() == 0)
		{
			continue;
		}

		GlassMesh->CreateMeshSection_LinearColor(
			SectionIndex,
			Section.Vertices,
			Section.Triangles,
			Section.Normals,
			Section.UVs,
			NoColours,
			Section.Tangents,
			/*bCreateCollision=*/SectionIndex < 2);
	}

	if (IntactMaterial)
	{
		GlassMesh->SetMaterial(0, IntactMaterial);
	}
	if (CrackedMaterial)
	{
		GlassMesh->SetMaterial(1, CrackedMaterial);
	}
	if (FractureEdgeMaterial)
	{
		GlassMesh->SetMaterial(2, FractureEdgeMaterial);
	}

	// One convex hull per shard, replacing the whole set: a hull that outlived
	// the shard it was built from would keep a bullet out of a hole the player
	// can see through.
	GlassMesh->SetCollisionConvexMeshes(ShardHulls);

	// Even when the pane ends up empty: an emptied frame still has to drop the
	// navigation data it published on the previous rebuild.
	FNavigationSystem::UpdateComponentData(*GlassMesh);
}

float AMKGlassPane::GetRemainingAreaCm2() const
{
	double Total = 0.0;
	for (const FMKGlassShard& Shard : Shards)
	{
		Total += MKPolygonArea(Shard.Vertices);
	}
	return static_cast<float>(Total);
}

int32 AMKGlassPane::BreakAt(const FVector2D& PaneUV, float BreakRadius, float FractureRadius, FVector PushDirection)
{
	FMKGlassFractureParams Params;
	Params.PaneWidthCm = PaneWidthCm;
	Params.PaneHeightCm = PaneHeightCm;
	Params.BreakRadiusCm = BreakRadius;
	Params.FractureRadiusCm = FractureRadius;
	Params.CrackedLookRadiusCm = CrackedLookRadiusCm;
	Params.CrackedLookJitter = CrackedLookJitter;
	Params.SpokeCount = SpokeCount;
	Params.RunnerCrackCount = RunnerCrackCount;
	Params.RingCount = RingCount;
	Params.MinShardAreaCm2 = MinShardAreaCm2;
	Params.MaxShardCount = MaxShardCount;

	const FMKGlassFractureResult Result = MKFractureGlassAt(Shards, PaneUV, Params, FractureRandom);
	if (Result.RemovedShards == 0 && Result.CrackedShards == 0)
	{
		// The round found no glass at all - through the hole, or off the pane.
		// Rebuilding the mesh for that would cook collision for nothing.
		return 0;
	}

	LastPushDirection = PushDirection;
	LastHitUV = PaneUV;
	// A fresh round restarts the collapse: the rush belongs to the hole that has
	// just been made, not to whatever was still dripping out of the last one.
	ShedStepIndex = 0;
	LandingSoundBudget = FMath::Max(MaxLandingSounds, 0);
	RefreshGlassMesh();
	ThrowDebris(Result.FallenShards, PushDirection, 1.0f);

	// What the hole left hanging comes down after it - a rush first, stragglers
	// after - so the break lasts long enough to be watched.
	ScheduleShedStep();

	return Result.RemovedShards;
}

void AMKGlassPane::ScheduleShedStep()
{
	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	// One shot, re-armed by the step it fires: the interval is not constant, and
	// a looping timer cannot lengthen itself.
	World->GetTimerManager().SetTimer(
		ShedTimer, this, &AMKGlassPane::ShedUnsupportedShards,
		MKShedStepInterval(ShedStepIndex, ShedIntervalSeconds, ShedIntervalGrowth),
		/*bLoop=*/false);
}

void AMKGlassPane::ShedUnsupportedShards()
{
	TArray<FMKGlassShard> Unsupported;
	MKDropUnsupportedShards(Shards, PaneWidthCm, PaneHeightCm, MinSupportedAreaCm2, Unsupported, bBottomSupportedOnly);

	if (Unsupported.Num() == 0)
	{
		// The pane is holding again: everything left in the frame reaches it.
		if (UWorld* World = GetWorld())
		{
			World->GetTimerManager().ClearTimer(ShedTimer);
		}
		return;
	}

	// Nearest to the hole first, because that is the order glass lets go in: the
	// piece beside the hole loses its hold before the one three pieces out.
	const FVector2D HitUV = LastHitUV;
	Unsupported.Sort([&HitUV](const FMKGlassShard& A, const FMKGlassShard& B)
	{
		return FVector2D::DistSquared(MKPolygonCentroid(A.Vertices), HitUV) <
			FVector2D::DistSquared(MKPolygonCentroid(B.Vertices), HitUV);
	});

	// The rush is the first step and the drip is every step after it; what this
	// one does not take goes back into the frame and gets its turn on the next.
	const int32 StepCount = MKShedBatchCount(
		ShedStepIndex, ShedShardsPerStep, ShedFalloff, Unsupported.Num());
	for (int32 Index = StepCount; Index < Unsupported.Num(); ++Index)
	{
		Shards.Add(MoveTemp(Unsupported[Index]));
	}
	Unsupported.SetNum(StepCount);

	RefreshGlassMesh();
	ThrowDebris(Unsupported, LastPushDirection, ShedSpeedScale);
	PlayShedVoice(StepCount);

	++ShedStepIndex;
	ScheduleShedStep();
}

void AMKGlassPane::PlayShedVoice(int32 ShardsThisStep)
{
	if (!ShedSound || ShardsThisStep <= 0)
	{
		return;
	}

	// Quieter and higher every step: the collapse is one sound running down, not
	// a series of small breaks. Loudness says how much glass moved; the rising
	// pitch keeps the tail out of the way of the break's own body.
	const float StepFade = FMath::Pow(0.75f, static_cast<float>(FMath::Max(ShedStepIndex, 0)));
	const FVector HitWorld = GetActorTransform().TransformPosition(
		FVector(LastHitUV.X, 0.0f, LastHitUV.Y));

	UGameplayStatics::PlaySoundAtLocation(
		this,
		ShedSound,
		HitWorld,
		FMath::Max(ShedSoundVolume, 0.0f) * StepFade,
		FMath::Clamp(1.15f + 0.06f * ShedStepIndex, 1.0f, 1.6f));
}

void AMKGlassPane::ThrowDebris(TArrayView<const FMKGlassShard> FallenShards, const FVector& PushDirection, float SpeedScale)
{
	UWorld* World = GetWorld();
	if (!bSpawnDebris || !World || FallenShards.Num() == 0)
	{
		return;
	}

	// A pane in the editor has no business spawning actors; OnConstruction runs
	// there and would litter the level with debris nobody asked for.
	if (!World->IsGameWorld())
	{
		return;
	}

	TSubclassOf<AMKGlassShardDebris> SpawnClass = DebrisClass;
	if (!SpawnClass)
	{
		SpawnClass = AMKGlassShardDebris::StaticClass();
	}
	const FTransform PaneTransform = GetActorTransform();
	const FVector Push = PushDirection.GetSafeNormal();

	FActorSpawnParameters SpawnParams;
	SpawnParams.Owner = this;
	SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

	TArray<FMKGlassShard> Pieces;
	for (const FMKGlassShard& Shard : FallenShards)
	{
		// Glass that leaves a frame breaks again on the way out. The cut is made
		// here and not in the fracture on purpose: the pane keeps the shard sizes
		// that make its hole read, and only what falls gets diced.
		Pieces.Reset();
		MKShatterShard(Shard, DebrisPieceMaxAreaCm2, DebrisMaxPiecesPerShard, DebrisRandom, Pieces);

		// Only the first piece of a shard may be heard landing, and only while
		// the break's budget lasts. Before the extra break existed this counted
		// whole shards; letting every piece of one shard bid for the budget would
		// spend all of it on the first thing that left the frame.
		bool bShardMayBeHeard = true;

		for (const FMKGlassShard& Piece : Pieces)
		{
			AMKGlassShardDebris* Debris = World->SpawnActor<AMKGlassShardDebris>(
				SpawnClass, PaneTransform.GetLocation(), PaneTransform.Rotator(), SpawnParams);
			if (!Debris)
			{
				continue;
			}

			// Hardest at the impact point and weakest at the edge of the fracture:
			// the round blows the glass it hit out of the frame, and only shoves what
			// was standing next to it.
			const float DistanceFromHit = FVector2D::Distance(MKPolygonCentroid(Piece.Vertices), LastHitUV);
			const float FalloffAlpha = FMath::Clamp(DistanceFromHit / FMath::Max(FractureRadiusCm, 1.0f), 0.0f, 1.0f);
			const float Speed = DebrisLaunchSpeedCmS * SpeedScale *
				FMath::Lerp(1.0f, FMath::Max(DebrisEdgeSpeedScale, 0.0f), FalloffAlpha) *
				FMath::FRandRange(0.7f, 1.3f);

			// Away from the shooter, with enough scatter that no two pieces travel
			// as a pair, and a little downward: it is glass leaving a frame, not a
			// horizontal spray.
			const FVector Scatter(
				FMath::FRandRange(-0.3f, 0.3f),
				FMath::FRandRange(-0.3f, 0.3f),
				FMath::FRandRange(-0.4f, 0.05f));
			const FVector Velocity = (Push + Scatter).GetSafeNormal() * Speed;

			// Which few are heard is not chosen - it is whichever left the frame
			// first, which is the glass nearest the hit, which is the glass the
			// player is already looking at.
			if (DebrisLandingSound && LandingSoundBudget > 0 && bShardMayBeHeard)
			{
				Debris->LandingSound = DebrisLandingSound;
				bShardMayBeHeard = false;
				--LandingSoundBudget;
			}

			Debris->LaunchShard(
				Piece,
				PaneTransform,
				PaneThicknessCm,
				UvTilingCm,
				DebrisMaterial ? DebrisMaterial : (Piece.bCracked && CrackedMaterial ? CrackedMaterial : IntactMaterial),
				// Loose pieces catch light with the same glass on their faces and rim.
				// The pane's emissive crack accent made floor debris look neon-outlined.
				nullptr,
				Velocity);

			LiveDebris.Add(Debris);
		}
	}

	// A corridor's worth of broken glass is scenery, not a budget: past the cap
	// the oldest pieces go, and what the player is watching right now stays.
	while (LiveDebris.Num() > FMath::Max(MaxLiveDebris, 0))
	{
		if (AMKGlassShardDebris* Oldest = LiveDebris[0])
		{
			Oldest->Destroy();
		}
		LiveDebris.RemoveAt(0);
	}
}

void AMKGlassPane::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(ShedTimer);
	}
	Super::EndPlay(EndPlayReason);
}

void AMKGlassPane::ReceiveResolvedImpact_Implementation(const FMKResolvedImpact& ResolvedImpact)
{
	// A crowbar swing is one event however many frames its sweep spans; the
	// strike's owner delivers it once, and this is the pane's own second line.
	if (!ImpactHistory.Accept(ResolvedImpact.ShotId, ResolvedImpact.InteractionIndex))
	{
		UE_LOG(LogMindKore, Verbose, TEXT("%s: impact %s/%d already received, ignored"),
			*GetName(), *ResolvedImpact.ShotId.ToString(), ResolvedImpact.InteractionIndex);
		return;
	}

	const FVector ImpactPoint(ResolvedImpact.Hit.ImpactPoint);
	const FVector Local = GetActorTransform().InverseTransformPosition(ImpactPoint);

	const FVector2D HitUV(Local.X, Local.Z);
	const int32 ShardsBefore = Shards.Num();
	const float AreaBefore = GetRemainingAreaCm2();

	const int32 Removed = BreakAt(HitUV, MKGlassBreakRadiusFor(ResolvedImpact.Kind, BreakRadiusCm, BluntBreakRadiusCm),
		FractureRadiusCm, FVector(ResolvedImpact.ShotDirection));

	// A round that lands on glass and changes nothing is the report the author
	// needs to see: "there are parts that never break" is a claim about which of
	// these numbers is wrong, and guessing which one wasted a pass already.
	UE_LOG(LogMindKore, Verbose,
		TEXT("%s: round at UV (%.1f, %.1f) of %.0fx%.0f | shards %d -> %d | area %.0f -> %.0f cm2 | removed %d"),
		*GetName(), HitUV.X, HitUV.Y, PaneWidthCm, PaneHeightCm,
		ShardsBefore, Shards.Num(), AreaBefore, GetRemainingAreaCm2(), Removed);

	if (Removed <= 0)
	{
		// Nothing fell out: either the round went through the hole the previous
		// one made, or it only spread cracks. Neither is worth a second crash of
		// breaking glass, so nothing is announced.
		return;
	}

	if (BreakSound)
	{
		UGameplayStatics::PlaySoundAtLocation(
			this,
			BreakSound,
			ImpactPoint,
			FMath::Max(BreakSoundVolume, 0.0f),
			FMath::FRandRange(
				FMath::Min(BreakSoundPitchMin, BreakSoundPitchMax),
				FMath::Max(BreakSoundPitchMin, BreakSoundPitchMax)));
	}

	if (BreakVfx)
	{
		UNiagaraFunctionLibrary::SpawnSystemAtLocation(
			this, BreakVfx, ImpactPoint, FVector(ResolvedImpact.Hit.ImpactNormal).Rotation());
	}

	// The instigator is the pane, not the shooter: what a listener should end up
	// believing is that the GLASS made this noise and is worth walking to. The
	// shot itself is already reported separately by UMKFirearmComponent.
	static const FName GlassNoiseTag(TEXT("MK_GlassBreak"));
	UAISense_Hearing::ReportNoiseEvent(
		this,
		ImpactPoint,
		BreakNoiseLoudness,
		this,
		/*MaxRange=*/0.0f,
		GlassNoiseTag);
}
