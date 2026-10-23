#include "OSGas.h"

#include "Async/ParallelFor.h"
#include "Camera/PlayerCameraManager.h"
#include "Components/DirectionalLightComponent.h"
#include "Engine/PostProcessVolume.h"
#include "Engine/GameViewportClient.h"
#include "Engine/Engine.h"
#include "Components/PointLightComponent.h"
#include "Components/LocalLightComponent.h"
#include "Engine/OverlapResult.h"
#include "DrawDebugHelpers.h"
#include "Engine/Texture2D.h"
#include "Engine/World.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "HAL/IConsoleManager.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"
#include "MindKore.h"
#include "OSGravity.h"
#include "UObject/UObjectIterator.h"

namespace
{
	/** The step's parallel loops at background priority (they took the render's workers). */
	template <typename FBody>
	void GasFor(int32 Num, const FBody& Body)
	{
		ParallelFor(Num, Body, EParallelForFlags::BackgroundPriority);
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
	/** A parcel's extra drag toward the flow, 1/s per cm² of radius, at most MaxDrag (the ring lives >= 0.5 s). */
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
	constexpr float VacuumMassScale = 0.3f;
	constexpr float KeyShadow = 0.1f;
	/** The key light's shadow through the render grid (relief), per unit of drawn density along the way. */
	constexpr float GridKeyShadow = 0.6f;
	constexpr float AmbientOcclusion = 0.225f;
	constexpr float KeyStrength = 0.014f;
	constexpr float KeyMax = 3.0f;
	constexpr float EyeClearCm = 30.0f;
	constexpr float EvictSeconds = 0.5f;
	/** In air: the gun's inherited velocity's time constant; old gas's relaxing to the still room; a body's push share. */
	constexpr float CarrierSeconds = 0.2f;
	constexpr float AirRestSeconds = 0.7f;
	constexpr float BodyShare = 0.5f;

	TAutoConsoleVariable<int32> CVarGasCards(
		TEXT("os.Gas.Cards"), 0,
		TEXT("1: the old card wisps (shroud, muzzle stream) are drawn too; 0 (v11): the gas is parcels only."));

	TAutoConsoleVariable<int32> CVarGasDebug(
		TEXT("os.Gas.Debug"), 0,
		TEXT("1: draw every cloud's box (white), parcels (points, red heavy to blue light), bodies (yellow, velocity arrow), tunnels (cyan), wall/floor planes (green) and sources (magenta)."));

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
		DataTexture = FloatTexture(MaxDrawn, 7);
		TunnelTexture = FloatTexture(MaxTunnels, 2);
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
	}
	// The pool: every cloud's volume, material, textures and grid made now, not on the first shot (a 30 ms frame).
	Clouds.SetNum(MaxClouds);
	for (FCloud& Cloud : Clouds)
	{
		EnsureRender(Cloud);
		if (!Cloud.Grid)
		{
			Cloud.Grid = MakeUnique<FOSSmokeSim>();
			Cloud.Grid->Reset(VelocityCells, VelocityCellCm, true, FVector3f(0.0f, 0.0f, -1.0f));
		}
	}
}

void UOSGasSubsystem::WaitAll() const
{
	for (const FCloud& Cloud : Clouds)
	{
		if (Cloud.Pending.IsValid())
		{
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
		if (Cloud.Age >= MinAge && (!Best || Cloud.Age < Best->Age))
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

bool UOSGasSubsystem::ProbeYoungest(FVector& OutCentroid, int32& OutParcels, int32& OutBehind, FVector& OutBox) const
{
	WaitAll();
	const FCloud* Best = nullptr;
	for (const FCloud& Cloud : Clouds)
	{
		if (Cloud.IsAlive() && (!Best || Cloud.Parcels.Num() > Best->Parcels.Num()))
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
	const FString Line = FString::Printf(TEXT("Gas cost: %d step(s) off the game thread, %.0f us mean, %.0f us max; game thread in %d frame(s), %.0f us mean, %.0f us max; %d cloud(s), %d parcel(s)"),
		CostSteps, CostSteps ? CostStepUs / CostSteps : 0.0, CostMaxStepUs, CostFrames, CostFrames ? CostFrameUs / CostFrames : 0.0, CostMaxFrameUs,
		GetCloudCount(), GetParcelCount());
	CostStepUs = CostMaxStepUs = CostFrameUs = CostMaxFrameUs = 0.0;
	CostSteps = CostFrames = 0;
	return Line;
}

// --- sources --------------------------------------------------------------------------

int32 UOSGasSubsystem::CloudFor(const FOSGasLook& Look, const FVector& Where, float JoinAge)
{
	UWorld* World = GetWorld();
	// One of this look with room, its gas close, young enough (a new shot never joins: JoinAge < 0).
	for (int32 Index = 0; Index < Clouds.Num() && JoinAge >= 0.0f; ++Index)
	{
		const FCloud& Cloud = Clouds[Index];
		if (Cloud.IsAlive() && Cloud.Age <= JoinAge && Cloud.Evict <= 0.0f && Cloud.Look == Look && Cloud.Parcels.Num() + Cloud.Incoming.Num() < MaxParcels
			&& FVector::Dist(Cloud.Origin + FVector(Cloud.DrawnCentre), Where) < JoinCm)
		{
			return Index;
		}
	}
	// A free slot, a new one, else the oldest (and the next oldest starts fading out so a slot frees).
	int32 Slot = Clouds.IndexOfByPredicate([](const FCloud& Each) { return !Each.IsAlive(); });
	if (Slot == INDEX_NONE && Clouds.Num() < MaxClouds)
	{
		Slot = Clouds.AddDefaulted();
	}
	if (Slot == INDEX_NONE)
	{
		Slot = 0;
		for (int32 Index = 1; Index < Clouds.Num(); ++Index)
		{
			Slot = Clouds[Index].Age > Clouds[Slot].Age ? Index : Slot;
		}
	}
	FCloud& Cloud = Clouds[Slot];
	if (Cloud.Pending.IsValid())
	{
		Cloud.Pending.Wait();
		Cloud.Pending = UE::Tasks::FTask();
	}
	int32 Free = 0;
	for (const FCloud& Each : Clouds)
	{
		Free += !Each.IsAlive() && &Each != &Cloud ? 1 : 0;
	}
	if (Free < 3 && Clouds.Num() >= MaxClouds)
	{
		FCloud* Oldest = nullptr;
		for (FCloud& Each : Clouds)
		{
			if (&Each != &Cloud && Each.IsAlive() && Each.Evict <= 0.0f && (!Oldest || Each.Age > Oldest->Age))
			{
				Oldest = &Each;
			}
		}
		if (Oldest)
		{
			Oldest->Evict = EvictSeconds;
		}
	}
	Cloud.Look = Look;
	Cloud.Origin = Where;
	Cloud.DrawnCentre = Cloud.PackedCentre = FVector3f::ZeroVector;
	Cloud.Parcels.Reset();
	Cloud.Incoming.Reset();
	Cloud.Commands.Reset();
	Cloud.Movers.Reset();
	Cloud.NextCommands.Reset();
	Cloud.NextMovers.Reset();
	Cloud.Age = 0.0f;
	Cloud.Evict = 0.0f;
	Cloud.StepCarry = 0.0f;
	Cloud.Seed = FMath::Rand();
	Cloud.bAir = OSAirZoneAt(World, Where) != nullptr;
	Cloud.Gravity = OSGravityAt(World, Where).Acceleration;
	Cloud.RenderCellCm = Cloud.PackedCellCm = 2.0f;
	Cloud.bHasGas = false;
	Cloud.bPendingAtlas = false;
	if (!Cloud.Grid)
	{
		Cloud.Grid = MakeUnique<FOSSmokeSim>();
	}
	Cloud.Grid->Reset(VelocityCells, VelocityCellCm, Cloud.bAir, FVector3f(Cloud.Gravity.GetSafeNormal()));
	// The floor and the walls round the birth: planes the gas slides on (Visibility; a few traces, once).
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
	return Slot;
}

void UOSGasSubsystem::Emit(FCloud& Cloud, const FOSGasRecipe& R, const FVector& Where, const FVector& Direction, const FVector& Carrier, int32 Count)
{
	const FVector3f At(Where - Cloud.Origin);
	const FVector3f Axis = FVector3f(Direction.GetSafeNormal(UE_SMALL_NUMBER, FVector::ForwardVector));
	FVector3f U, W;
	Axis.FindBestAxisVectors(U, W);
	for (int32 Index = 0; Index < Count && Cloud.Parcels.Num() + Cloud.Incoming.Num() < MaxParcels; ++Index)
	{
		FOSGasParcel& P = Cloud.Incoming.AddDefaulted_GetRef();
		const FVector3f Dir = FVector3f(FMath::VRandCone(FVector(Axis), FMath::DegreesToRadians(FMath::Max(R.ConeDeg, 0.5f))));
		const float Speed = R.SpeedCm * FMath::FRandRange(R.SpeedMin, 1.0f);
		P.P = At;
		P.Throw = Dir * Speed;
		P.Carrier = FVector3f(Carrier);
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
		P.Life = R.LifeSeconds * FMath::FRandRange(0.8f, 1.2f);
		P.Cling = R.ClingSeconds;
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
	if (Recipe.VacuumBurstCount > 0 && OSAirZoneAt(GetWorld(), Where) == nullptr)
	{
		Source.Recipe.BurstCount = Recipe.VacuumBurstCount;
		Source.Recipe.BurstSeconds = Recipe.VacuumBurstSeconds;
		Source.Recipe.SpeedMin = Recipe.VacuumSpeedMin;
		Source.Recipe.RadiusMinCm = Recipe.VacuumRadiusMinCm;
		Source.Recipe.RadiusMaxCm = Recipe.VacuumRadiusMaxCm;
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
	if (Used.BurstCount > 0)
	{
		// The first frame's share now, stamped into the atlas on the game thread this frame (TickCloud).
		const float Frame = FMath::Min(GetWorld()->GetDeltaSeconds() / Source.BurstLeft, 1.0f);
		// At most 20 on the game thread (the rest from the next step on: no hitch).
		const int32 Now = FMath::Clamp(FMath::RoundToInt(Source.BurstOwed * Frame), 1, 20);
		Source.BurstOwed = FMath::Max(0.0f, Source.BurstOwed - Now);
		Source.Cloud = CloudFor(Look, Where, Used.BurstCount >= 20 ? -1.0f : 0.3f);
		FCloud& Cloud = Clouds[Source.Cloud];
		Emit(Cloud, Used, Where, Direction, CarrierVelocity, Now);
		Cloud.bStamp = true;
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

void UOSGasSubsystem::EmitNow(const FOSGasRecipe& Recipe, const FOSGasLook& Look, const FVector& Where, const FVector& Direction, const FVector& CarrierVelocity, int32 Count)
{
	if (Count <= 0)
	{
		return;
	}
	// A seep or a hot case's puff joins a cloud it is well inside of (0.8 of its drawn box), else it is not let out: as
	// a cloud of its own it took a slot and evicted a shot's cloud (owner 23.10: «телепортируется», «резко пропадает»),
	// and joining by distance stretched a cloud after the walking gun.
	for (FCloud& Cloud : Clouds)
	{
		if (Cloud.IsAlive() && Cloud.Evict <= 0.0f && Cloud.bDrawable && Cloud.Look == Look)
		{
			const FVector Off = (Where - (Cloud.Origin + FVector(Cloud.DrawnCentre))).GetAbs();
			if (Off.GetMax() < 0.4f * RenderCells * Cloud.RenderCellCm)
			{
				Emit(Cloud, Recipe, Where, Direction, CarrierVelocity, Count);
				return;
			}
		}
	}
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
			if (S.Cloud == INDEX_NONE || !Clouds.IsValidIndex(S.Cloud) || !Clouds[S.Cloud].IsAlive() || !(Clouds[S.Cloud].Look == S.Look))
			{
				S.Cloud = CloudFor(S.Look, Where, S.Recipe.BurstCount >= 20 ? -1.0f : 0.3f);
			}
			Emit(Clouds[S.Cloud], S.Recipe, Where, Direction, S.Carrier, Count);
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
	Tunnel.Start = Start;
	Tunnel.End = End;
	Tunnel.RadiusCm = RadiusCm;
	Tunnel.bVacuum = OSAirZoneAt(GetWorld(), Where) == nullptr;
	if (Tunnels.Num() > MaxTunnels)
	{
		Tunnels.RemoveAt(0, Tunnels.Num() - MaxTunnels);
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
		}
	}
}

void UOSGasSubsystem::AddAirFlow(const FVector& Location, const FVector& Direction, float SpeedCm, float ReachCm, float Seconds)
{
	AirFlows.Add({ Location, Direction.GetSafeNormal(), SpeedCm, ReachCm, Seconds });
}

// --- the frame -------------------------------------------------------------------------

void UOSGasSubsystem::Tick(float DeltaTime)
{
	const double FrameStart = FPlatformTime::Seconds();
	const bool bAny = GetCloudCount() > 0 || !Sources.IsEmpty();
	TickSources(DeltaTime);
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
		for (FCloud& Cloud : Clouds)
		{
			if (Cloud.IsAlive() && FVector::Dist(Cloud.Origin + FVector(Cloud.DrawnCentre), Flow.Location) < Flow.ReachCm)
			{
				Cloud.NextCommands.Add({ FCommand::EKind::Burst, Flow.Location, -Flow.Direction, Flow.ReachCm, -Flow.SpeedCm * DeltaTime * 4.0f });
			}
		}
	}
	TSet<TObjectKey<UPrimitiveComponent>> Seen;
	for (FCloud& Cloud : Clouds)
	{
		if (Cloud.IsAlive())
		{
			FindMovers(Cloud, DeltaTime, Seen);
			TickCloud(Cloud, DeltaTime);
		}
	}
	for (auto It = LastMoverAt.CreateIterator(); It; ++It)
	{
		if (!Seen.Contains(It.Key()))
		{
			It.RemoveCurrent();
		}
	}
	UpdatePost();
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
		CostMaxFrameUs = FMath::Max(CostMaxFrameUs, Us);
		++CostFrames;
	}
}

void UOSGasSubsystem::TickCloud(FCloud& Cloud, float DeltaTime)
{
	UWorld* World = GetWorld();
	const bool bBusy = Cloud.IsBusy();
	if (!bBusy && Cloud.Pending.IsValid())
	{
		Cloud.Pending = UE::Tasks::FTask();
	}
	Cloud.Age += DeltaTime;
	const bool bEvicted = Cloud.Evict > 0.0f && (Cloud.Evict -= DeltaTime) <= 0.0f;
	const bool bEmpty = !bBusy && Cloud.Parcels.IsEmpty() && Cloud.Incoming.IsEmpty() && Cloud.Age > 0.3f;
	if (Cloud.Age >= Cloud.Look.LifeSeconds || bEvicted || bEmpty)
	{
		if (Cloud.Pending.IsValid())
		{
			Cloud.Pending.Wait();
			Cloud.Pending = UE::Tasks::FTask();
		}
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
		Splat(Cloud);
	}
	// The last step's atlas: the box moves with it.
	if (!bBusy && Cloud.bPendingAtlas)
	{
		Cloud.bPendingAtlas = false;
		Cloud.DrawnCentre = Cloud.PackedCentre;
		Cloud.RenderCellCm = Cloud.PackedCellCm;
		const FIntPoint Size = FOSSmokeSim::AtlasSize(RenderCells);
		uint8* Data = static_cast<uint8*>(FMemory::Malloc(Cloud.Atlas.Num() * sizeof(FColor)));
		FMemory::Memcpy(Data, Cloud.Atlas.GetData(), Cloud.Atlas.Num() * sizeof(FColor));
		FUpdateTextureRegion2D* Region = new FUpdateTextureRegion2D(0, 0, 0, 0, Size.X, Size.Y);
		Cloud.Texture->UpdateTextureRegions(0, 1, Region, Size.X * sizeof(FColor), sizeof(FColor), Data,
			[](uint8* Src, const FUpdateTextureRegion2D* Regions) { FMemory::Free(Src); delete Regions; });
		// Drawn from its first atlas with gas on; off only when the cloud ends (no flicker).
		Cloud.bDrawable |= Cloud.bHasGas;
	}
	// Air or vacuum and gravity where the gas is now.
	const FVector Centre = Cloud.Origin + FVector(Cloud.DrawnCentre);
	if (!bBusy)
	{
		Cloud.bAir = OSAirZoneAt(World, Centre) != nullptr;
		Cloud.Gravity = OSGravityAt(World, Centre).Acceleration;
		Cloud.Grid->bAir = Cloud.bAir;
		Cloud.Grid->Down = FVector3f(Cloud.Gravity.GetSafeNormal());
	}
	// Fixed steps: 60 Hz while young, 30 Hz after; never waited for.
	Cloud.StepCarry += DeltaTime;
	if (!bBusy)
	{
		// 60 Hz to 0.5 s, 30 Hz to 1 s, 15 Hz to 3 s, 10 Hz after (old gas moves slowly).
		const float Since = Cloud.Age - Cloud.StepCarry;
		const float Dt = Since < FastSeconds ? FastStep : Since < 1.0f ? SlowStep : Since < 3.0f ? 1.0f / 15.0f : 0.1f;
		const int32 Steps = FMath::Min(FMath::FloorToInt(Cloud.StepCarry / Dt + 1e-3f), 4);
		if (Steps > 0)
		{
			Cloud.StepCarry = Steps == 4 ? 0.0f : FMath::Max(0.0f, Cloud.StepCarry - Steps * Dt);
			LaunchStep(Cloud, Steps, Dt);
		}
	}
	// The material: the drawn box (world axes), the life's fade, the light, the eye and the body.
	{
		FVector3f Dir;
		FLinearColor Colour;
		PickKey(Centre, Dir, Colour);
		// Half white: a saturated lamp (the planetoid's red rock light) does not tint the gas whole.
		Colour = FMath::Lerp(Colour, FLinearColor(Colour.GetLuminance(), Colour.GetLuminance(), Colour.GetLuminance()), 0.5f);
		const float Ease = 1.0f - FMath::Exp(-DeltaTime / 0.25f);
		Cloud.KeyDir = (Cloud.KeyDir + (Dir - Cloud.KeyDir) * Ease).GetSafeNormal(UE_SMALL_NUMBER, Dir);
		Cloud.KeyColour = Cloud.KeyColour + (Colour - Cloud.KeyColour) * Ease;
	}
	// The rest is the post-process's (UpdatePost, once a frame for all clouds).
}

void UOSGasSubsystem::AmbientFor(bool bAir, FLinearColor& OutTop, FLinearColor& OutBottom) const
{
	// ponytail: by the map's name until the zones carry their own (stage 4: a field on the air zone).
	if (!bAir)
	{
		OutTop = FLinearColor(0.16f, 0.16f, 0.20f);
		OutBottom = FLinearColor(0.07f, 0.07f, 0.10f);
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
	// The nearest drawable clouds, MaxDrawn of them.
	TArray<FCloud*, TInlineAllocator<MaxClouds>> Drawn;
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
	// GasData: a column a cloud, seven rows (Scripts/os_muzzle_fx.py, GAS_POST_HLSL).
	constexpr int32 Rows = 7;
	TArray<FLinearColor> Data;
	Data.SetNumZeroed(MaxDrawn * Rows);
	BoundGrids.SetNum(MaxDrawn);
	for (int32 Index = 0; Index < Drawn.Num(); ++Index)
	{
		const FCloud& Cloud = *Drawn[Index];
		const FVector Centre = Cloud.Origin + FVector(Cloud.DrawnCentre);
		const float Half = 0.5f * RenderCells * Cloud.RenderCellCm;
		const float LifeFade = 1.0f - FMath::SmoothStep(0.6f * Cloud.Look.LifeSeconds, Cloud.Look.LifeSeconds, Cloud.Age);
		const float EvictFade = Cloud.Evict > 0.0f ? Cloud.Evict / EvictSeconds : 1.0f;
		// The key light in display terms (the post-process is after the tonemapper): a soft knee, never blown white.
		const FLinearColor K = Cloud.KeyColour;
		const FLinearColor Key(1.0f - FMath::Exp(-K.R * 0.8f), 1.0f - FMath::Exp(-K.G * 0.8f), 1.0f - FMath::Exp(-K.B * 0.8f));
		FLinearColor Top, Bottom;
		AmbientFor(Cloud.bAir, Top, Bottom);
		Data[0 * MaxDrawn + Index] = FLinearColor(Centre.X, Centre.Y, Centre.Z, Half);
		Data[1 * MaxDrawn + Index] = FLinearColor(float(RenderCells), Cloud.RenderCellCm, LifeFade * EvictFade, Cloud.Look.Extinction);
		Data[2 * MaxDrawn + Index] = Key;
		Data[3 * MaxDrawn + Index] = Cloud.Look.Colour;
		Data[4 * MaxDrawn + Index] = Cloud.Look.bEmbers ? Cloud.Look.WarmColour : FLinearColor::Black;
		Data[5 * MaxDrawn + Index] = Top;
		Data[6 * MaxDrawn + Index] = Bottom;
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
	// Tunnels (world), healing, and the bodies in the drawn clouds (a negative radius).
	TArray<FLinearColor> TRows;
	TRows.SetNumZeroed(MaxTunnels * 2);
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
		TRows[Count] = FLinearColor(T.Start.X, T.Start.Y, T.Start.Z, T.RadiusCm * (1.0f - Heal));
		TRows[MaxTunnels + Count] = FLinearColor(T.End.X, T.End.Y, T.End.Z, Ragged);
		++Count;
	}
	for (const FCloud* Cloud : Drawn)
	{
		for (const FOSSmokeSim::FMover& M : Cloud->NextMovers)
		{
			if (Count < MaxTunnels && M.Extent.IsZero())
			{
				const FVector C = Cloud->Origin + FVector(M.Centre);
				TRows[Count] = FLinearColor(C.X, C.Y, C.Z, -M.RadiusCm);
				TRows[MaxTunnels + Count] = FLinearColor(C.X, C.Y, C.Z, 0.0f);
				++Count;
			}
		}
	}
	{
		uint8* Bytes = static_cast<uint8*>(FMemory::Malloc(TRows.Num() * sizeof(FLinearColor)));
		FMemory::Memcpy(Bytes, TRows.GetData(), TRows.Num() * sizeof(FLinearColor));
		FUpdateTextureRegion2D* Region = new FUpdateTextureRegion2D(0, 0, 0, 0, MaxTunnels, 2);
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
	Post->SetScalarParameterValue(TEXT("BlockPx"), FMath::Max(1.0f, FMath::RoundToFloat(float(View.Y) / 270.0f)));
	Post->SetScalarParameterValue(TEXT("EyeClear"), EyeClearCm);
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
	FCloud* Ptr = &Cloud;
	const TArray<FTunnel> TunnelsNow = Tunnels;
	Cloud.KeyDir.Normalize();
	UOSGasSubsystem* Self = this;
	Cloud.Pending = UE::Tasks::Launch(UE_SOURCE_LOCATION, [Ptr, Steps, Dt, TunnelsNow, Self]()
	{
		const double T0 = FPlatformTime::Seconds();
		StepCloud(*Ptr, Steps, Dt, TunnelsNow);
		const double Us = (FPlatformTime::Seconds() - T0) * 1e6;
		FScopeLock Lock(&Self->CostLock);
		Self->CostStepUs += Us;
		Self->CostMaxStepUs = FMath::Max(Self->CostMaxStepUs, Us);
		++Self->CostSteps;
	});
}

// --- the step (off the game thread) ----------------------------------------------------------

void UOSGasSubsystem::StepCloud(FCloud& Cloud, int32 Steps, float Dt, const TArray<FTunnel>& TunnelsNow)
{
	TArray<FOSGasParcel>& Parcels = Cloud.Parcels;
	const FVector3f Origin(Cloud.Origin);
	// The commands: tunnels eat the parcels in them and push them out; blows throw them out.
	for (const FCommand& C : Cloud.Commands)
	{
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
					P.Mass *= 1.0f - Overlap;
					P.V += Off.GetSafeNormal(UE_SMALL_NUMBER, FVector3f::UpVector) * (120.0f * Overlap);
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
					P.V += Out * (C.SpeedCm * Fall);
					if (C.SpeedCm > 0.0f)
					{
						P.Mass *= 1.0f - 0.4f * Fall;
					}
				}
			}
		}
	}
	Cloud.Commands.Reset();
	const float GScale = Cloud.Gravity.Size() / 980.0f;
	const FVector3f Down = FVector3f(Cloud.Gravity.GetSafeNormal());
	const FOSSmokeParams K = GridParams();
	FOSSmokeSim& G = *Cloud.Grid;
	const int32 N = G.N;
	TArray<float> OldX, OldY, OldZ;
	for (int32 Step = 0; Step < Steps; ++Step)
	{
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
			G.CellCm = FMath::Clamp((Hi3 - Lo3).GetMax() / (VelocityCells - 2), VelocityCellCm, 40.0f);
		}
		const float GridCell = G.CellCm;
		{
			// The velocity grid round the gas (whole cells): the parcels' momentum, mass and heat onto it.
			float Mass = 0.0f;
			for (const FOSGasParcel& P : Parcels)
			{
				GridCentre += P.P * P.Mass;
				Mass += P.Mass;
			}
			GridCentre = SnapTo(GridCentre / FMath::Max(Mass, 1e-3f), GridCell);
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
				if (C.GetMin() < 0.0f || C.GetMax() > N - 1.0f)
				{
					continue;
				}
				const FVector3f New(G.Sample(G.Ux, C), G.Sample(G.Uy, C), G.Sample(G.Uz, C));
				const FVector3f Old(G.Sample(OldX, C), G.Sample(OldY, C), G.Sample(OldZ, C));
				// The ring keeps its spin its first 0.3 s: all FLIP, no drag.
				const bool bYoung = P.Age < 0.3f;
				const float Share = bYoung ? 1.0f : Flip;
				P.V = (P.V + New - Old) * Share + New * (1.0f - Share);
				// One drag in air: the grid's (tau 0.6 s); none of the parcel's own (it stopped the ring dead, «тормозит»).
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
				P.Cling -= Dt;
				P.V = P.Carrier + P.Throw;
			}
			const FVector3f Before = P.P;
			if (Cloud.bAir)
			{
				// The gun's velocity it inherited goes into the air in ~0.2 s (a shot on the walk stops with the walker);
				// in a vacuum it keeps it.
				if (P.Cling <= 0.0f)
				{
					const FVector3f Lost = P.Carrier * (1.0f - FMath::Exp(-Dt / CarrierSeconds));
					P.Carrier -= Lost;
					P.V -= Lost;
				}
				// Old gas relaxes to the still room's air (it moved forever: «уезжает хрен пойми куда»); the young
				// keep their ring.
				if (P.Age > 0.3f)
				{
					P.V *= FMath::Exp(-Dt / AirRestSeconds);
				}
			}
			const float Speed = P.V.Size();
			P.P += P.V * Dt;
			P.RadiusCm = Cloud.bAir ? FMath::Min(P.RadiusCm + AirGrowCmPerSecond * Dt, 14.0f) : P.RadiusCm + VacuumGrowPerCm * Speed * Dt;
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
						const FVector3f Out = Off.GetSafeNormal(UE_SMALL_NUMBER, FVector3f::UpVector);
						P.P += Out * (Reach - D) * 0.6f;
						const float Push = BodyShare * (M.Velocity | Out) - (P.V | Out);
						if (Push > 0.0f)
						{
							P.V += Out * Push;
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
						P.P += Out * Depth * 0.6f;
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
				}
			}
			if (P.Age >= P.Life || P.Mass < 0.01f)
			{
				Parcels.RemoveAtSwap(Index, EAllowShrinking::No);
			}
		}
	}
	// Vacuum: a parcel grows to at least half the way to its second nearest neighbour (25 cm at most) - the cone stays
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
			Need[I] = FMath::Min(1.2f * FMath::Sqrt(Second), 30.0f);
		});
		for (int32 I = 0; I < Parcels.Num(); ++I)
		{
			Parcels[I].RadiusCm = FMath::Max(Parcels[I].RadiusCm, Need[I]);
		}
	}
	(void)Down;
	Splat(Cloud);
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
	// (owner 23.10: «респавнится у оружия», «кубом обрезается»). Its cell: the size over 48, 2 to 8 cm.
	(void)Spread;
	FVector3f BoxLo(1e9f), BoxHi(-1e9f);
	for (const FOSGasParcel& P : Parcels)
	{
		const float Reach = FMath::Max(P.RadiusCm, P.V.Size() * 0.08f);
		BoxLo = BoxLo.ComponentMin(P.P - FVector3f(Reach));
		BoxHi = BoxHi.ComponentMax(P.P + FVector3f(Reach));
	}
	if (Parcels.IsEmpty())
	{
		BoxLo = BoxHi = Centre;
	}
	const float Cell = FMath::Clamp((BoxHi - BoxLo).GetMax() * 1.1f / N, 2.0f, 8.0f);
	const FVector3f MassCentre = Centre;
	Centre = SnapTo((BoxLo + BoxHi) * 0.5f, 0.5f);
	const float Half = 0.5f * N * Cell;
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
			if (Along <= 0.0f)
			{
				continue;
			}
			const float R = A.RadiusCm + B.RadiusCm;
			const float Across2 = D.SizeSquared() - Along * Along;
			if (Across2 < R * R)
			{
				Sum += B.Mass * (1.0f - Across2 / (R * R)) * B.RadiusCm / 4.0f;
			}
		}
		Light[I] = FMath::Exp(-KeyShadow * Sum);
	});
	// The noise flows with the gas: read at the point less the cloud's mean travel (its parcels' P - Birth).
	FVector3f Travel = FVector3f::ZeroVector;
	for (const FOSGasParcel& P : Parcels)
	{
		Travel += (P.P - P.Birth) * P.Mass;
	}
	Travel /= FMath::Max(Mass, 1e-3f);
	const float Cut = Cloud.bAir ? NoiseCut + 0.1f : 0.15f;
	const float Scale = Cloud.bAir ? NoiseCm : 12.0f;
	const uint32 CloudSeed = Cloud.Seed;
	// The splat, slice by slice: the parcels' smooth kernels only (the noise comes once per cell below).
	const int32 Cells = N * N * N;
	TArray<float> D, H, L, W;
	D.SetNumZeroed(Cells);
	H.SetNumZeroed(Cells);
	L.SetNumZeroed(Cells);
	W.SetNumZeroed(Cells);
	const float Fade = 1.0f;
	// How much a parcel's kernel fills its cell (the calibration of the cloud's opacity, os.Fx.SmokeCost's T).
	const float MassScale = Cloud.bAir ? AirMassScale : VacuumMassScale;
	// Only the cells round the gas (its parcels' reach, 3 cells more for the blur and the shadows): every pass below.
	FIntVector Lo(N - 1), Hi(0);
	for (const FOSGasParcel& P : Parcels)
	{
		const float Reach = FMath::Max(P.RadiusCm, P.V.Size() * 0.08f);
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
	const int32 Slices = FMath::Max(0, Hi.Z - Lo.Z + 1);
	GasFor(Slices, [&](int32 K)
	{
		const int32 Z = Lo.Z + K;
		const float CZ = Centre.Z + (Z + 0.5f - 0.5f * N) * Cell;
		for (int32 Index = 0; Index < Parcels.Num(); ++Index)
		{
			const FOSGasParcel& P = Parcels[Index];
			// An ellipsoid: semi-axis max(radius, |v| x 0.08 s) along the velocity, the radius across - a fast parcel
			// draws a streak of jet, not a ball (owner 23.10: «в невесомости дым вылетает как будто дробинками»).
			const float Speed = P.V.Size();
			const FVector3f Along = Speed > 1.0f ? P.V / Speed : FVector3f::ZeroVector;
			const float Long = FMath::Max(P.RadiusCm, Speed * 0.08f);
			const float R = Long;
			const float InvAcross2 = 1.0f / (P.RadiusCm * P.RadiusCm);
			const float InvLong2 = 1.0f / (Long * Long);
			if (FMath::Abs(P.P.Z - CZ) >= R || P.Mass < 0.05f)
			{
				continue;
			}
			const float LifeFade = 1.0f - FMath::SmoothStep(0.6f * P.Life, P.Life, P.Age);
			// The same matter in more room: a parcel thins as it grows (air: (3 cm / r)^2; vacuum: 5 cm / r, it spreads metres).
			const float Thin = Cloud.bAir ? FMath::Clamp(FMath::Square(3.0f / P.RadiusCm), 0.02f, 1.0f) : FMath::Clamp(5.0f / P.RadiusCm, 0.05f, 1.0f);
			const float M = P.Mass * LifeFade * Fade * Thin;
			const int32 X0 = FMath::Max(0, FMath::FloorToInt((P.P.X - R - Centre.X + Half) / Cell));
			const int32 X1 = FMath::Min(N - 1, FMath::FloorToInt((P.P.X + R - Centre.X + Half) / Cell));
			const int32 Y0 = FMath::Max(0, FMath::FloorToInt((P.P.Y - R - Centre.Y + Half) / Cell));
			const int32 Y1 = FMath::Min(N - 1, FMath::FloorToInt((P.P.Y + R - Centre.Y + Half) / Cell));

			for (int32 Y = Y0; Y <= Y1; ++Y)
			{
				const float CY = Centre.Y + (Y + 0.5f - 0.5f * N) * Cell;
				for (int32 X = X0; X <= X1; ++X)
				{
					const FVector3f At(Centre.X + (X + 0.5f - 0.5f * N) * Cell, CY, CZ);
					const FVector3f Off = At - P.P;
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
					H[I] += KM * P.Heat;
					L[I] += KM * Light[Index];
					W[I] += KM;
				}
			}
		}
	});
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
				const FVector3f Flow = (At - Travel) / Scale;
				const float Noise = Noise3(Flow, CloudSeed) * 0.67f + Noise3(Flow * 2.0f, CloudSeed ^ 0x51ED270Bu) * 0.33f;
				D[I] *= FMath::Clamp((Noise - Cut) * NoiseGain, 0.0f, 1.0f);
			}
		}
	});
	// A soft sharpen: the density less 0.4 of its blur (5 cells a side) - the low cushion goes, the lumps stay.
	{
		TArray<float> T0, T1;
		T0.SetNumZeroed(Cells);
		T1.SetNumZeroed(Cells);
		const auto Box = [N, Lo, Hi, Slices](const TArray<float>& From, TArray<float>& To, int32 AX, int32 AY, int32 AZ)
		{
			GasFor(Slices, [&](int32 KZ)
			{
				const int32 Z = Lo.Z + KZ;
				for (int32 Y = Lo.Y; Y <= Hi.Y; ++Y)
				{
					for (int32 X = Lo.X; X <= Hi.X; ++X)
					{
						float Sum = 0.0f;
						for (int32 K = -2; K <= 2; ++K)
						{
							const int32 QX = FMath::Clamp(X + AX * K, 0, N - 1), QY = FMath::Clamp(Y + AY * K, 0, N - 1), QZ = FMath::Clamp(Z + AZ * K, 0, N - 1);
							Sum += From[(QZ * N + QY) * N + QX];
						}
						To[(Z * N + Y) * N + X] = Sum * 0.2f;
					}
				}
			});
		};
		Box(D, T0, 1, 0, 0);
		Box(T0, T1, 0, 1, 0);
		Box(T1, T0, 0, 0, 1);
		for (int32 Z = Lo.Z; Z <= Hi.Z; ++Z)
		{
			for (int32 Y = Lo.Y; Y <= Hi.Y; ++Y)
			{
				for (int32 X = Lo.X; X <= Hi.X; ++X)
				{
					const int32 I = (Z * N + Y) * N + X;
					D[I] = FMath::Max(0.0f, D[I] - 0.4f * T0[I]) / 0.6f;
				}
			}
		}
	}
	// The atlas: R density, G heat, B the key light, A the ambient's shadow (the gas around, 2 cells each way).
	const FIntPoint Size = FOSSmokeSim::AtlasSize(N);
	Cloud.Atlas.SetNumZeroed(Size.X * Size.Y);
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
				const float Heat = W[I] > 1e-4f ? H[I] / W[I] : 0.0f;
				// The key light's shadow through the drawn gas itself (1-12 cells toward it, as the old grid's Pack): a
				// lit flank and a dark belly - the relief (owner 23.10: «недостаточно рельефным»). The parcels' own
				// light (L / W) shades it further.
				float Toward = 0.0f;
				for (int32 S = 0; S < KeySteps; ++S)
				{
					Toward += At(X + KeyCells[S].X, Y + KeyCells[S].Y, Z + KeyCells[S].Z) * KeyWeights[S];
				}
				const float Lit = FMath::Exp(-GridKeyShadow * Toward) * (W[I] > 1e-4f ? L[I] / W[I] : 1.0f);
				Cloud.Atlas[(OY + Y) * Size.X + OX + X] = FColor(Byte(Dens), Byte(Heat * FMath::Min(W[I] * 2.0f, 1.0f)), Byte(Lit), Byte(FMath::Exp(-AmbientOcclusion * FMath::Min(Around * DensityGain, 6.0f))));
			}
		}
	});
	for (float V : D)
	{
		if (V > 0.004f)
		{
			bAny = true;
			break;
		}
	}
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
	const FIntPoint Size = FOSSmokeSim::AtlasSize(RenderCells);
	UTexture2D* Texture = UTexture2D::CreateTransient(Size.X, Size.Y, PF_B8G8R8A8);
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
		if (!Body || Radius < 1.0f || Radius > 400.0f || Cloud.NextMovers.Num() >= 8)
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
		// 176 cm) swept the whole cloud along with the walking player (owner 23.10: «облако следует за пушкой»).
		if (const APawn* PawnOwner = Cast<APawn>(Body->GetOwner()); PawnOwner && Body == PawnOwner->GetRootComponent())
		{
			float CapsuleRadius = 0.0f, CapsuleHalf = 0.0f;
			PawnOwner->GetSimpleCollisionCylinder(CapsuleRadius, CapsuleHalf);
			FOSSmokeSim::FMover& M = Cloud.NextMovers.AddDefaulted_GetRef();
			M.Centre = FVector3f(At - Cloud.Origin);
			M.RadiusCm = FMath::Clamp(CapsuleRadius, 15.0f, 45.0f);
			M.Velocity = FVector3f(Velocity.GetClampedToMaxSize(1500.0f));
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

void UOSGasSubsystem::PickKey(const FVector& Where, FVector3f& OutDir, FLinearColor& OutColour)
{
	UWorld* World = GetWorld();
	KeyLightsAge += World->GetDeltaSeconds();
	if (KeyLightsAge > 1.0f)
	{
		KeyLightsAge = 0.0f;
		KeyLights.Reset();
		for (TObjectIterator<ULightComponent> It; It; ++It)
		{
			if (It->GetWorld() == World)
			{
				KeyLights.Add(*It);
			}
		}
	}
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
