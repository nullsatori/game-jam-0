#include "OSGas.h"

#include "Async/ParallelFor.h"
#include "Camera/PlayerCameraManager.h"
#include "Components/DirectionalLightComponent.h"
#include "Components/HeterogeneousVolumeComponent.h"
#include "Components/PointLightComponent.h"
#include "Components/LocalLightComponent.h"
#include "Engine/OverlapResult.h"
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
	constexpr float DensityGain = 6.0f;
	constexpr float KeyShadow = 0.04f;
	constexpr float AmbientOcclusion = 0.15f;
	constexpr float KeyStrength = 0.014f;
	constexpr float KeyMax = 3.0f;
	constexpr float EyeClearCm = 30.0f;
	constexpr float EvictSeconds = 0.5f;

	TAutoConsoleVariable<int32> CVarGasCards(
		TEXT("os.Gas.Cards"), 0,
		TEXT("1: the old card wisps (shroud, muzzle stream) are drawn too; 0 (v11): the gas is parcels only."));

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
		K.DragSeconds = 0.8f;
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
		if (Cloud.Volume)
		{
			Cloud.Volume->SetVisibility(false);
		}
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
		Line += FString::Printf(TEXT(" [gas age %.2f %s, %d parcels, mass %.1f, mean |V| %.0f cm/s, heat %.2f, centre %s, cell %.0f cm]"),
			Cloud.Age, Cloud.bAir ? TEXT("air") : TEXT("vacuum"), Cloud.Parcels.Num(), Mass, Speed / FMath::Max(Mass, 0.01f), Heat,
			*(Cloud.Origin + FVector(Cloud.DrawnCentre)).ToCompactString(), Cloud.PackedCellCm);
	}
	return Line;
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

int32 UOSGasSubsystem::CloudFor(const FOSGasLook& Look, const FVector& Where)
{
	UWorld* World = GetWorld();
	// One of this look with room, its gas close.
	for (int32 Index = 0; Index < Clouds.Num(); ++Index)
	{
		const FCloud& Cloud = Clouds[Index];
		if (Cloud.IsAlive() && Cloud.Evict <= 0.0f && Cloud.Look == Look && Cloud.Parcels.Num() + Cloud.Incoming.Num() < MaxParcels
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
	if (!Clouds.ContainsByPredicate([&Cloud](const FCloud& Each) { return !Each.IsAlive() && &Each != &Cloud; }) && Clouds.Num() >= MaxClouds)
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
	FCollisionQueryParams Query(SCENE_QUERY_STAT(OSGasPlanes), false);
	for (const FVector& Direction : { Cloud.Gravity.GetSafeNormal(), FVector::ForwardVector, -FVector::ForwardVector, FVector::RightVector, -FVector::RightVector, FVector::UpVector })
	{
		FHitResult Hit;
		if (!Direction.IsNearlyZero() && World->LineTraceSingleByChannel(Hit, Where, Where + Direction * 150.0f, ECC_Visibility, Query)
			&& !Cloud.Planes.ContainsByPredicate([&Hit](const FPlane& Each) { return (FVector(Each) | Hit.ImpactNormal) > 0.95f; }))
		{
			Cloud.Planes.Emplace(Hit.ImpactPoint, Hit.ImpactNormal);
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
	Source.Look = Look;
	Source.Where = Where;
	Source.Direction = Direction;
	Source.Carrier = CarrierVelocity;
	Source.Attach = Attach;
	Source.bAttached = Attach != nullptr;
	Source.BurstOwed = float(Recipe.BurstCount);
	Source.BurstLeft = Recipe.BurstCount > 0 ? FMath::Max(Recipe.BurstSeconds, 1e-3f) : 0.0f;
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
	if (Count > 0)
	{
		Emit(Clouds[CloudFor(Look, Where)], Recipe, Where, Direction, CarrierVelocity, Count);
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
				S.Cloud = CloudFor(S.Look, Where);
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
			Cloud.Commands.Add({ FCommand::EKind::Carve, Start, End, RadiusCm, 0.0f });
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
			Cloud.Commands.Add({ FCommand::EKind::Burst, Location, Normal, RadiusCm, Speed });
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
				Cloud.Commands.Add({ FCommand::EKind::Burst, Flow.Location, -Flow.Direction, Flow.ReachCm, -Flow.SpeedCm * DeltaTime * 4.0f });
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
		if (Cloud.Volume)
		{
			Cloud.Volume->SetVisibility(false);
		}
		return;
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
		Cloud.Volume->SetVisibility(Cloud.bHasGas);
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
		const float Dt = Cloud.Age - Cloud.StepCarry < FastSeconds ? FastStep : SlowStep;
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
		const float Ease = 1.0f - FMath::Exp(-DeltaTime / 0.25f);
		Cloud.KeyDir = (Cloud.KeyDir + (Dir - Cloud.KeyDir) * Ease).GetSafeNormal(UE_SMALL_NUMBER, Dir);
		Cloud.KeyColour = Cloud.KeyColour + (Colour - Cloud.KeyColour) * Ease;
	}
	const float Half = 0.5f * RenderCells * Cloud.RenderCellCm;
	FVector Eye = Centre, Body = Centre + FVector(1e5f);
	float BodyRadius = 0.0f;
	if (const APlayerController* Controller = World->GetFirstPlayerController())
	{
		if (Controller->PlayerCameraManager)
		{
			Eye = Controller->PlayerCameraManager->GetCameraLocation();
		}
		if (const APawn* Pawn = Controller->GetPawn())
		{
			Body = Pawn->GetActorLocation();
			float HalfHeight;
			Pawn->GetSimpleCollisionCylinder(BodyRadius, HalfHeight);
			BodyRadius = FMath::Clamp(BodyRadius, 15.0f, 45.0f);
		}
	}
	const FVector EyeLocal = Eye - Centre, BodyLocal = Body - Centre;
	const float LifeFade = 1.0f - FMath::SmoothStep(0.6f * Cloud.Look.LifeSeconds, Cloud.Look.LifeSeconds, Cloud.Age);
	const float EvictFade = Cloud.Evict > 0.0f ? Cloud.Evict / EvictSeconds : 1.0f;
	UMaterialInstanceDynamic* M = Cloud.Material;
	M->SetVectorParameterValue(TEXT("CloudCentre"), FLinearColor(FVector3f(Centre)));
	M->SetVectorParameterValue(TEXT("CloudX"), FLinearColor(1, 0, 0, 0));
	M->SetVectorParameterValue(TEXT("CloudY"), FLinearColor(0, 1, 0, 0));
	M->SetVectorParameterValue(TEXT("CloudZ"), FLinearColor(0, 0, 1, 0));
	M->SetScalarParameterValue(TEXT("HalfCm"), Half);
	M->SetScalarParameterValue(TEXT("Cells"), float(RenderCells));
	M->SetScalarParameterValue(TEXT("Age"), Cloud.Age);
	M->SetScalarParameterValue(TEXT("Life"), Cloud.Look.LifeSeconds);
	M->SetScalarParameterValue(TEXT("Thin"), 1.0f);
	// The rags come from the splat's noise now: no erosion in the material.
	M->SetScalarParameterValue(TEXT("Erode"), 0.0f);
	M->SetScalarParameterValue(TEXT("Gain"), LifeFade * EvictFade);
	M->SetScalarParameterValue(TEXT("PixelCm"), 3.0f);
	M->SetScalarParameterValue(TEXT("AmbientShare"), Cloud.bAir ? 1.0f : 0.0f);
	M->SetScalarParameterValue(TEXT("Seed"), float(Cloud.Seed % 1000) / 1000.0f);
	M->SetScalarParameterValue(TEXT("Extinction"), Cloud.Look.Extinction);
	M->SetScalarParameterValue(TEXT("UnitCm"), Cloud.RenderCellCm);
	M->SetVectorParameterValue(TEXT("Colour"), Cloud.Look.Colour);
	M->SetVectorParameterValue(TEXT("WarmColour"), Cloud.Look.WarmColour);
	M->SetVectorParameterValue(TEXT("KeyColour"), Cloud.KeyColour);
	M->SetVectorParameterValue(TEXT("Eye"), FLinearColor(EyeLocal.X, EyeLocal.Y, EyeLocal.Z, EyeClearCm));
	M->SetVectorParameterValue(TEXT("Body"), FLinearColor(BodyLocal.X, BodyLocal.Y, BodyLocal.Z, BodyRadius));
	Cloud.Volume->SetWorldTransform(FTransform(FQuat::Identity, Centre, FVector(Cloud.RenderCellCm)));
	UpdateTunnels(Cloud);
}

void UOSGasSubsystem::LaunchStep(FCloud& Cloud, int32 Steps, float Dt)
{
	// What came while the last step ran joins now (the game thread holds the cloud: no step in flight).
	Cloud.Parcels.Append(MoveTemp(Cloud.Incoming));
	Cloud.Incoming.Reset();
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
					P.V += Off.GetSafeNormal(UE_SMALL_NUMBER, FVector3f::UpVector) * (120.0f * Overlap) + Seg.GetSafeNormal() * (80.0f * Overlap);
				}
			}
			else
			{
				const FVector3f Off = P.P - A;
				const float D = Off.Size();
				if (D < C.RadiusCm)
				{
					const float Fall = 1.0f - D / C.RadiusCm;
					const FVector3f Out = Off.GetSafeNormal(UE_SMALL_NUMBER, B.GetSafeNormal());
					P.V += (Out + B.GetSafeNormal() * 0.5f).GetSafeNormal() * (C.SpeedCm * Fall);
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
		if (Cloud.bAir)
		{
			// The velocity grid round the gas (whole cells): the parcels' momentum, mass and heat onto it.
			float Mass = 0.0f;
			for (const FOSGasParcel& P : Parcels)
			{
				GridCentre += P.P * P.Mass;
				Mass += P.Mass;
			}
			GridCentre = SnapTo(GridCentre / FMath::Max(Mass, 1e-3f), VelocityCellCm);
			const FVector3f GridWorld = Origin + GridCentre;
			G.Planes.Reset();
			for (const FPlane& Plane : Cloud.Planes)
			{
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
				const FVector3f C = (P.P - GridCentre) / VelocityCellCm + FVector3f(0.5f * N - 0.5f);
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
			G.StepGrid(Dt, K, GScale);
			// The grid back onto the parcels: FLIP (the change) mostly, PIC (the grid's own) the rest; and a drag toward
			// the flow by the parcel's size.
			for (FOSGasParcel& P : Parcels)
			{
				const FVector3f C = (P.P - GridCentre) / VelocityCellCm + FVector3f(0.5f * N - 0.5f);
				const FVector3f New(G.Sample(G.Ux, C), G.Sample(G.Uy, C), G.Sample(G.Uz, C));
				const FVector3f Old(G.Sample(OldX, C), G.Sample(OldY, C), G.Sample(OldZ, C));
				P.V = (P.V + New - Old) * Flip + New * (1.0f - Flip);
				const float Hold = 1.0f - FMath::Exp(-Dt * FMath::Min(DragPerCm2 * P.RadiusCm * P.RadiusCm, MaxDrag));
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
			const float Speed = P.V.Size();
			P.P += P.V * Dt;
			P.RadiusCm += Cloud.bAir ? AirGrowCmPerSecond * Dt : VacuumGrowPerCm * Speed * Dt;
			P.Heat *= FMath::Exp(-Dt / HeatSeconds);
			// The floor and the walls: it slides (no speed into them).
			for (const FPlane& Plane : Cloud.Planes)
			{
				const FVector3f Normal = FVector3f(FVector(Plane));
				const float Dist = (FVector3f(Origin + P.P) | Normal) - float(Plane.W);
				const float Keep = P.RadiusCm * 0.5f;
				if (Dist < Keep)
				{
					P.P += Normal * (Keep - Dist);
					const float In = P.V | Normal;
					if (In < 0.0f)
					{
						P.V -= Normal * In;
					}
				}
			}
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
						const float Push = (M.Velocity | Out) - (P.V | Out);
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
						const float Push = (M.Velocity | Out) - (P.V | Out);
						if (Push > 0.0f)
						{
							P.V += Out * Push;
						}
					}
				}
			}
			if (P.Age >= P.Life || P.Mass < 0.01f)
			{
				Parcels.RemoveAtSwap(Index, EAllowShrinking::No);
			}
		}
	}
	// No buoyancy in a vacuum, and none in air without gravity either (the grid's lift scales with |g|).
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
	// Deviation from the contract (3 cm at most): a vacuum cone runs metres in a second; the cell steps 2 -> 3 -> 4.5 -> 6 cm
	// as it spreads (the drawn pixel stays the material's 3 cm), or the gas leaves the box within half a second.
	float Cell = Cloud.PackedCellCm;
	while (!Cloud.bAir && Cell < 6.0f && Spread > 0.3f * N * Cell)
	{
		Cell = FMath::Min(Cell * 1.5f, 6.0f);
	}
	Centre = SnapTo(Centre, Cell);
	const float Half = 0.5f * N * Cell;
	// Each parcel's light: the others in front of it toward the key light (a cylinder of their summed radii).
	const FVector3f Key = Cloud.KeyDir;
	TArray<float> Light;
	Light.SetNumUninitialized(Parcels.Num());
	ParallelFor(Parcels.Num(), [&](int32 I)
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
	// The splat, slice by slice: a smooth kernel times a noise read where the parcel was born (it flows with it).
	const int32 Cells = N * N * N;
	TArray<float> D, H, L, W;
	D.SetNumZeroed(Cells);
	H.SetNumZeroed(Cells);
	L.SetNumZeroed(Cells);
	W.SetNumZeroed(Cells);
	const float Fade = 1.0f;
	ParallelFor(N, [&](int32 Z)
	{
		const float CZ = Centre.Z + (Z + 0.5f - 0.5f * N) * Cell;
		for (int32 Index = 0; Index < Parcels.Num(); ++Index)
		{
			const FOSGasParcel& P = Parcels[Index];
			const float R = P.RadiusCm;
			if (FMath::Abs(P.P.Z - CZ) >= R)
			{
				continue;
			}
			const float LifeFade = 1.0f - FMath::SmoothStep(0.6f * P.Life, P.Life, P.Age);
			const float M = P.Mass * LifeFade * Fade;
			const int32 X0 = FMath::Max(0, FMath::FloorToInt((P.P.X - R - Centre.X + Half) / Cell));
			const int32 X1 = FMath::Min(N - 1, FMath::FloorToInt((P.P.X + R - Centre.X + Half) / Cell));
			const int32 Y0 = FMath::Max(0, FMath::FloorToInt((P.P.Y - R - Centre.Y + Half) / Cell));
			const int32 Y1 = FMath::Min(N - 1, FMath::FloorToInt((P.P.Y + R - Centre.Y + Half) / Cell));
			const float InvR2 = 1.0f / (R * R);
			const uint32 Seed = P.Seed;
			const FVector3f Offset(float(Seed & 0xFF), float((Seed >> 8) & 0xFF), float((Seed >> 16) & 0xFF));
			for (int32 Y = Y0; Y <= Y1; ++Y)
			{
				const float CY = Centre.Y + (Y + 0.5f - 0.5f * N) * Cell;
				for (int32 X = X0; X <= X1; ++X)
				{
					const FVector3f At(Centre.X + (X + 0.5f - 0.5f * N) * Cell, CY, CZ);
					const float Q = FVector3f::DistSquared(At, P.P) * InvR2;
					if (Q >= 1.0f)
					{
						continue;
					}
					const float Kernel = FMath::Cube(1.0f - Q);
					const FVector3f NP = (At - P.P + P.Birth - P.Birth) / NoiseCm;
					// Read where the parcel's own matter sits: the point less the parcel's travel (flows with it).
					const FVector3f Flow = (At - (P.P - P.Birth)) / NoiseCm + Offset;
					const float Noise = Noise3(Flow, Seed) * 0.67f + Noise3(Flow * 2.0f, Seed ^ 0x51ED270Bu) * 0.33f;
					(void)NP;
					const float Rag = FMath::Clamp((Noise - NoiseCut) * NoiseGain, 0.0f, 1.0f);
					const int32 I = (Z * N + Y) * N + X;
					const float KM = Kernel * M;
					D[I] += KM * Rag;
					H[I] += KM * P.Heat;
					L[I] += KM * Light[Index];
					W[I] += KM;
				}
			}
		}
	});
	// The atlas: R density, G heat, B the key light, A the ambient's shadow (the gas around, 2 cells each way).
	const FIntPoint Size = FOSSmokeSim::AtlasSize(N);
	Cloud.Atlas.SetNumZeroed(Size.X * Size.Y);
	const auto Byte = [](float V) { return uint8(FMath::Clamp(FMath::RoundToInt(V * 255.0f), 0, 255)); };
	const auto At = [&D, N](int32 X, int32 Y, int32 Z) { return D[(FMath::Clamp(Z, 0, N - 1) * N + FMath::Clamp(Y, 0, N - 1)) * N + FMath::Clamp(X, 0, N - 1)]; };
	bool bAny = false;
	ParallelFor(N, [&](int32 Z)
	{
		const int32 OX = (Z % 8) * N, OY = (Z / 8) * N;
		for (int32 Y = 0; Y < N; ++Y)
		{
			for (int32 X = 0; X < N; ++X)
			{
				const int32 I = (Z * N + Y) * N + X;
				const float Dens = FMath::Min(D[I] * DensityGain, 1.0f);
				const float Around = At(X + 2, Y, Z) + At(X - 2, Y, Z) + At(X, Y + 2, Z) + At(X, Y - 2, Z) + At(X, Y, Z + 2) + At(X, Y, Z - 2);
				const float Heat = W[I] > 1e-4f ? H[I] / W[I] : 0.0f;
				const float Lit = W[I] > 1e-4f ? L[I] / W[I] : 1.0f;
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
	Cloud.PackedCentre = Centre;
	Cloud.PackedCellCm = Cell;
	Cloud.bHasGas = bAny;
	Cloud.bPendingAtlas = true;
}

// --- rendering helpers ----------------------------------------------------------------------

void UOSGasSubsystem::EnsureRender(FCloud& Cloud)
{
	if (Cloud.Volume)
	{
		return;
	}
	UWorld* World = GetWorld();
	if (!Host)
	{
		FActorSpawnParameters Params;
		Params.ObjectFlags |= RF_Transient;
		Host = World->SpawnActor<AActor>(AActor::StaticClass(), FTransform::Identity, Params);
		USceneComponent* Root = NewObject<USceneComponent>(Host, TEXT("GasRoot"));
		Host->SetRootComponent(Root);
		Root->RegisterComponent();
	}
	UHeterogeneousVolumeComponent* Volume = NewObject<UHeterogeneousVolumeComponent>(Host);
	Volume->SetUsingAbsoluteLocation(true);
	Volume->SetUsingAbsoluteRotation(true);
	Volume->SetUsingAbsoluteScale(true);
	Volume->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Volume->bPivotAtCentroid = true;
	Volume->LightingDownsampleFactor = 1.0f;
	Volume->StepFactor = 1.0f;
	Volume->ShadowStepFactor = 1.0f;
	Volume->RegisterComponent();
	Volume->SetVolumeResolution(FIntVector(RenderCells));
	Volume->SetVisibility(false);
	UMaterialInstanceDynamic* Material = UMaterialInstanceDynamic::Create(CloudMaterial.LoadSynchronous(), Host);
	Volume->SetMaterial(0, Material);
	const FIntPoint Size = FOSSmokeSim::AtlasSize(RenderCells);
	UTexture2D* Texture = UTexture2D::CreateTransient(Size.X, Size.Y, PF_B8G8R8A8);
	Texture->SRGB = false;
	Texture->Filter = TF_Bilinear;
	Texture->NeverStream = true;
	Texture->UpdateResource();
	Material->SetTextureParameterValue(TEXT("Grid"), Texture);
	Cloud.Volume = Volume;
	Cloud.Material = Material;
	Cloud.Texture = Texture;
	Keep.Add(Volume);
	Keep.Add(Material);
	Keep.Add(Texture);
}

void UOSGasSubsystem::UpdateTunnels(FCloud& Cloud)
{
	const FVector Centre = Cloud.Origin + FVector(Cloud.DrawnCentre);
	const float Half = 0.5f * RenderCells * Cloud.RenderCellCm;
	TArray<FLinearColor, TInlineAllocator<MaxTunnels * 2>> Rows;
	Rows.SetNumZeroed(MaxTunnels * 2);
	int32 Count = 0;
	for (const FTunnel& T : Tunnels)
	{
		if (Count >= MaxTunnels || Cloud.Age < T.Age + 0.15f)
		{
			continue;
		}
		const FVector Closest = FMath::ClosestPointOnSegment(Centre, T.Start, T.End);
		if (FVector::Dist(Closest, Centre) > Half * 1.74f + T.RadiusCm)
		{
			continue;
		}
		const float Slow = T.bVacuum ? 2.0f : 1.0f;
		const float Heal = FMath::SmoothStep(0.8f * Slow, 3.3f * Slow, T.Age);
		const float Ragged = FMath::SmoothStep(0.8f * Slow, (0.8f + 1.25f) * Slow, T.Age) * 0.75f;
		if (T.RadiusCm * (1.0f - Heal) < 0.3f)
		{
			continue;
		}
		const FVector A = T.Start - Centre, B = T.End - Centre;
		Rows[Count] = FLinearColor(A.X, A.Y, A.Z, T.RadiusCm * (1.0f - Heal));
		Rows[MaxTunnels + Count] = FLinearColor(B.X, B.Y, B.Z, Ragged);
		++Count;
	}
	for (const FOSSmokeSim::FMover& M : Cloud.Movers)
	{
		if (Count >= MaxTunnels || !M.Extent.IsZero())
		{
			continue;
		}
		const FVector C = Cloud.Origin + FVector(M.Centre) - Centre;
		Rows[Count] = FLinearColor(C.X, C.Y, C.Z, -M.RadiusCm);
		Rows[MaxTunnels + Count] = FLinearColor(C.X, C.Y, C.Z, 0.0f);
		++Count;
	}
	if (Count == 0 && Cloud.TunnelCount == 0)
	{
		return;
	}
	if (!Cloud.TunnelTexture)
	{
		UTexture2D* Texture = UTexture2D::CreateTransient(MaxTunnels, 2, PF_A32B32G32R32F);
		Texture->SRGB = false;
		Texture->Filter = TF_Nearest;
		Texture->NeverStream = true;
		Texture->UpdateResource();
		Cloud.TunnelTexture = Texture;
		Keep.Add(Texture);
		Cloud.Material->SetTextureParameterValue(TEXT("Tunnels"), Texture);
	}
	Cloud.TunnelCount = Count;
	Cloud.Material->SetScalarParameterValue(TEXT("TunnelCount"), float(Count));
	if (Count > 0)
	{
		uint8* Data = static_cast<uint8*>(FMemory::Malloc(Rows.Num() * sizeof(FLinearColor)));
		FMemory::Memcpy(Data, Rows.GetData(), Rows.Num() * sizeof(FLinearColor));
		FUpdateTextureRegion2D* Region = new FUpdateTextureRegion2D(0, 0, 0, 0, MaxTunnels, 2);
		Cloud.TunnelTexture->UpdateTextureRegions(0, 1, Region, MaxTunnels * sizeof(FLinearColor), sizeof(FLinearColor), Data,
			[](uint8* Src, const FUpdateTextureRegion2D* Regions) { FMemory::Free(Src); delete Regions; });
	}
}

void UOSGasSubsystem::FindMovers(FCloud& Cloud, float DeltaTime, TSet<TObjectKey<UPrimitiveComponent>>& Seen)
{
	Cloud.Movers.Reset();
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
		if (!Body || Radius < 1.0f || Radius > 400.0f || Cloud.Movers.Num() >= 8)
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
		const bool bBig = Radius > 90.0f;
		if (bBig && Velocity.SizeSquared() < 25.0f && !Body->IsSimulatingPhysics())
		{
			continue;
		}
		FOSSmokeSim::FMover& M = Cloud.Movers.AddDefaulted_GetRef();
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
