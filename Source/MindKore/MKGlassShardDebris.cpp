#include "MKGlassShardDebris.h"

#include "Engine/World.h"
#include "Kismet/GameplayStatics.h"
#include "MKGlassMesh.h"
#include "ProceduralMeshComponent.h"
#include "Sound/SoundBase.h"
#include "TimerManager.h"

AMKGlassShardDebris::AMKGlassShardDebris()
{
	PrimaryActorTick.bCanEverTick = false;

	ShardMesh = CreateDefaultSubobject<UProceduralMeshComponent>(TEXT("ShardMesh"));
	SetRootComponent(ShardMesh);

	// Simple collision only: a falling shard is a convex hull, and cooking a
	// triangle mesh for every piece of a broken window would cost more than the
	// whole fracture.
	ShardMesh->bUseComplexAsSimpleCollision = false;
	ShardMesh->SetMobility(EComponentMobility::Movable);
	ShardMesh->SetCollisionProfileName(TEXT("PhysicsActor"));
	ShardMesh->SetCastShadow(false);

	// Falling glass hits the floor and nothing else. It must not trip the player
	// who runs through the window he just shot out, and it must not eat the next
	// round on its way past - both would make debris a mechanic, and it is
	// scenery.
	ShardMesh->SetCollisionResponseToChannel(ECC_Pawn, ECR_Ignore);
	ShardMesh->SetCollisionResponseToChannel(ECC_Visibility, ECR_Ignore);
	ShardMesh->SetCollisionResponseToChannel(ECC_Camera, ECR_Ignore);
	// Glass on the floor is scenery, not cover: it must not block the navmesh or
	// the player's shots once it lands.
	ShardMesh->SetCanEverAffectNavigation(false);

	// Physics contacts are not reported unless this is asked for, and without
	// the report the piece cannot know it has landed.
	ShardMesh->SetNotifyRigidBodyCollision(true);
}

void AMKGlassShardDebris::BeginPlay()
{
	Super::BeginPlay();

	if (ShardMesh)
	{
		ShardMesh->OnComponentHit.AddDynamic(this, &AMKGlassShardDebris::OnShardHit);
	}
}

void AMKGlassShardDebris::OnShardHit(
	UPrimitiveComponent* HitComponent,
	AActor* OtherActor,
	UPrimitiveComponent* OtherComponent,
	FVector NormalImpulse,
	const FHitResult& Hit)
{
	if (bHasLanded || !LandingSound || !ShardMesh)
	{
		return;
	}

	// The speed the piece was carrying INTO the surface, not its speed after the
	// bounce: a shard that skips off the floor is still one landing.
	const float ImpactSpeed = FMath::Abs(FVector::DotProduct(
		ShardMesh->GetPhysicsLinearVelocity(), Hit.ImpactNormal));
	if (ImpactSpeed < FMath::Max(LandingSoundMinSpeedCmS, 0.0f))
	{
		return;
	}

	bHasLanded = true;
	UGameplayStatics::PlaySoundAtLocation(
		this,
		LandingSound,
		Hit.ImpactPoint,
		FMath::Max(LandingSoundVolume, 0.0f),
		// High and thin: this is a piece of glass the size of a hand, not the
		// window it came out of.
		FMath::FRandRange(1.35f, 1.7f));
}

void AMKGlassShardDebris::LaunchShard(
	const FMKGlassShard& Shard,
	const FTransform& PaneTransform,
	float ThicknessCm,
	float UvTilingCm,
	UMaterialInterface* Material,
	UMaterialInterface* EdgeMaterial,
	const FVector& LaunchVelocity)
{
	if (!ShardMesh || Shard.Vertices.Num() < 3)
	{
		Destroy();
		return;
	}

	const float HalfThickness = FMath::Max(ThicknessCm, 0.2f) * 0.5f;
	const FVector2D Centroid = MKPolygonCentroid(Shard.Vertices);

	FMKGlassMeshBuild Build;
	FMKGlassMeshBuild EdgeBuild;
	MKAppendShardGeometry(
		Build, Shard, HalfThickness, UvTilingCm, Centroid,
		EdgeMaterial ? &EdgeBuild : nullptr);

	ShardMesh->CreateMeshSection_LinearColor(
		0,
		Build.Vertices,
		Build.Triangles,
		Build.Normals,
		Build.UVs,
		TArray<FLinearColor>(),
		Build.Tangents,
		/*bCreateCollision=*/false);
	ShardMesh->AddCollisionConvexMesh(MKMakeShardHull(Shard, HalfThickness, Centroid));

	if (Material)
	{
		ShardMesh->SetMaterial(0, Material);
	}
	if (EdgeMaterial && EdgeBuild.Triangles.Num() > 0)
	{
		ShardMesh->CreateMeshSection_LinearColor(
			1,
			EdgeBuild.Vertices,
			EdgeBuild.Triangles,
			EdgeBuild.Normals,
			EdgeBuild.UVs,
			TArray<FLinearColor>(),
			EdgeBuild.Tangents,
			/*bCreateCollision=*/false);
		ShardMesh->SetMaterial(1, EdgeMaterial);
	}

	// The pane's frame is X across, Y through the glass, Z up the pane, so the
	// piece keeps the pane's rotation and only its own centre moves.
	const FVector WorldCentre = PaneTransform.TransformPosition(FVector(Centroid.X, 0.0f, Centroid.Y));
	SetActorTransform(FTransform(PaneTransform.GetRotation(), WorldCentre));

	ShardMesh->SetSimulatePhysics(true);
	// Hit the static room and counter, and each other - but not the remaining
	// pane, whose adjacent convex margins used to put released pieces to sleep
	// upright in the same opening they had just left.
	//
	// Piece-to-piece contacts are the reason a pile of glass reads as a pile.
	// Without them every piece of one break settles at exactly the same height:
	// measured on the reception counter, ten pieces came to rest between
	// z = 110.59996 and z = 110.60003, flat, overlapping, passing through one
	// another. Coplanar translucent faces at one depth flicker as the camera
	// moves, and a piece sunk halfway into its neighbour reads as a piece sunk
	// halfway into the counter.
	ShardMesh->SetCollisionResponseToChannel(ECC_WorldDynamic, ECR_Ignore);
	ShardMesh->SetLinearDamping(0.04f);
	ShardMesh->SetAngularDamping(0.12f);
	// Even a gently released piece accelerates through this threshold as it
	// falls. Keep thin shards from tunnelling through the floor later on.
	ShardMesh->SetUseCCD(true);
	ShardMesh->SetPhysicsLinearVelocity(LaunchVelocity);
	// A piece of glass that comes out of a frame turns as it falls; without this
	// they all sail out flat like cards.
	const float ShardArea = FMath::Max(static_cast<float>(MKPolygonArea(Shard.Vertices)), 1.0f);
	const float Spin = FMath::Clamp(1000.0f / FMath::Sqrt(ShardArea), 140.0f, 520.0f);
	ShardMesh->SetPhysicsAngularVelocityInDegrees(FVector(
		FMath::FRandRange(-Spin, Spin),
		FMath::FRandRange(-Spin, Spin),
		FMath::FRandRange(-Spin, Spin)));

	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().SetTimer(
			FreezeTimer, this, &AMKGlassShardDebris::FreezeInPlace, FMath::Max(FreezeAfterSeconds, 0.1f), false);
	}
}

void AMKGlassShardDebris::FreezeInPlace()
{
	if (!ShardMesh)
	{
		return;
	}

	// The timeout is a first settlement check, not permission to stop a body
	// in flight or halfway through a bounce. Let Chaos decide when it rests.
	if (ShardMesh->IsAnyRigidBodyAwake())
	{
		if (UWorld* World = GetWorld())
		{
			World->GetTimerManager().SetTimer(
				FreezeTimer, this, &AMKGlassShardDebris::FreezeInPlace, 0.5f, false);
		}
		return;
	}

	// Left where it landed, but no longer solved: the floor of a room that has
	// been shot up should cost drawing, not a solver island.
	//
	// It keeps its collision. A settled piece is what the NEXT piece lands on -
	// the second round through the same window arrives seconds after the first
	// one's glass has gone quiet - and switching collision off here would drop
	// that glass through the pile and back into one shared plane.
	ShardMesh->OnComponentHit.RemoveDynamic(this, &AMKGlassShardDebris::OnShardHit);
	ShardMesh->SetSimulatePhysics(false);
}
