#include "JamFlyer.h"

#include "Camera/CameraComponent.h"
#include "Components/SphereComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/PlayerStart.h"
#include "HAL/IConsoleManager.h"
#include "AudioDevice.h"
#include "AudioMixerBlueprintLibrary.h"
#include "AudioThread.h"
#include "Containers/Ticker.h"
#include "Misc/FileHelper.h"
#include "Algo/Reverse.h"
#include "JamClipRecorder.h"
#include "JamLivingWall.h"
#include "JamShip.h"
#include "JamVisorBlood.h"
#include "JamWeapons.h"
#include "JamZeroGPawn.h"
#include "Kismet/GameplayStatics.h"
#include "MKDoor.h"
#include "MKImpactDelivery.h"
#include "MKVitals.h"
#include "MindKore.h"
#include "PhysicalMaterials/PhysicalMaterial.h"
#include "Components/AudioComponent.h"
#include "JamOxygen.h"
#include "JamPickups.h"
#include "Sound/SoundMix.h"
#include "Sound/SoundSubmix.h"
#include "Sound/SoundBase.h"
#include "TimerManager.h"
#include "UObject/ConstructorHelpers.h"

namespace JamFlyerParts
{
	// The joints of the Blender parts (Scripts/blender/jam_flyer.py, metres there), cm.
	const FVector ShoulderCm(18.0f, 9.0f, 7.0f);
	const FVector TailRootCm(-42.0f, 0.0f, 2.0f);
	const FVector TailSplitCm(-155.0f, 0.0f, 0.0f);
	const FVector HeadCm(105.0f, 0.0f, 18.0f);

	/** A wing's turn: the flap about the body's long axis, then the sweep back. Side +1 right, -1 left (mirrored mesh). */
	FQuat WingTurn(float Side, float FlapDeg, float SweepDeg)
	{
		return FQuat(FVector::UpVector, Side * FMath::DegreesToRadians(SweepDeg)) * FQuat(FVector::ForwardVector, Side * FMath::DegreesToRadians(FlapDeg));
	}

	UStaticMeshComponent* MakePart(AActor* Owner, const TCHAR* Name, UStaticMesh* Mesh, USceneComponent* Parent, const FVector& At)
	{
		UStaticMeshComponent* Part = Owner->CreateDefaultSubobject<UStaticMeshComponent>(Name);
		Part->SetupAttachment(Parent);
		Part->SetRelativeLocation(At);
		Part->SetStaticMesh(Mesh);
		// Only rounds (the Shot channel, complex) hit the parts; the sphere is the physical body.
		Part->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
		Part->SetCollisionResponseToAllChannels(ECR_Ignore);
		Part->SetCollisionResponseToChannel(ECC_GameTraceChannel2, ECR_Block);
		Part->SetGenerateOverlapEvents(false);
		Part->SetCanEverAffectNavigation(false);
		return Part;
	}

	const TCHAR* MoodName(EJamFlockMood Mood)
	{
		switch (Mood)
		{
		case EJamFlockMood::Curious: return TEXT("curious");
		case EJamFlockMood::Stalking: return TEXT("stalking");
		default: return TEXT("hunting");
		}
	}

	/** Where the player looks from: the jam pawn's camera (its view location is the body's centre). */
	FVector EyeOf(const APawn* Pawn)
	{
		const AJamZeroGPawn* Floater = Cast<AJamZeroGPawn>(Pawn);
		return Floater && Floater->Camera ? Floater->Camera->GetComponentLocation() : Pawn->GetPawnViewLocation();
	}

	/** The player has a gun (any unlocked): the first-EVA rule (bSpareUnarmed) is over. */
	bool PlayerArmed(const APawn* Pawn)
	{
		const AJamZeroGPawn* Floater = Cast<AJamZeroGPawn>(Pawn);
		if (!Floater || !Floater->Guns)
		{
			return true; // not the jam pawn: no first-EVA rule
		}
		for (int32 Index = 0; Index < 8; ++Index)
		{
			if (Floater->Guns->IsWeaponUnlocked(Index))
			{
				return true;
			}
		}
		return false;
	}
}

using namespace JamFlyerParts;

// --- habitat -------------------------------------------------------------------------------

AJamFlyerHabitat::AJamFlyerHabitat()
{
	Zone = CreateDefaultSubobject<USphereComponent>(TEXT("Zone"));
	SetRootComponent(Zone);
	Zone->InitSphereRadius(1500.0f);
	Zone->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Zone->ShapeColor = FColor(200, 80, 40);
	FlyerClass = AJamFlyer::StaticClass();
}

float AJamFlyerHabitat::GetOuterRadius() const
{
	return Zone->GetScaledSphereRadius();
}

float AJamFlyerHabitat::GetInnerRadius() const
{
	return FMath::Min(InnerRadiusCm, GetOuterRadius() * 0.9f);
}

bool AJamFlyerHabitat::Contains(const FVector& Location, float ExtraCm) const
{
	return FVector::DistSquared(Location, GetActorLocation()) <= FMath::Square(GetOuterRadius() + ExtraCm);
}

FVector AJamFlyerHabitat::UpAt(const FVector& Location) const
{
	return GetInnerRadius() > 0.0f ? (Location - GetActorLocation()).GetSafeNormal(UE_SMALL_NUMBER, FVector::UpVector) : FVector::UpVector;
}

FVector AJamFlyerHabitat::PickPoint(const FVector& From, const FVector& Forward, bool bSwoop, float ReachCm, float ClearanceCm) const
{
	if (IsRoost())
	{
		// A sentry's lazy loop: off the roost surface, to a spot 5-15 m along it from here (or any).
		const FJamRoostSpot* Pick = &Spots[FMath::RandRange(0, Spots.Num() - 1)];
		for (int32 Try = 0; Try < 8; ++Try)
		{
			const FJamRoostSpot& Each = Spots[FMath::RandRange(0, Spots.Num() - 1)];
			const float Distance = FVector::Dist(Each.Point, From);
			if (Distance > 500.0f && Distance < 1500.0f)
			{
				Pick = &Each;
				break;
			}
		}
		return Pick->Point + Pick->Normal * FMath::FRandRange(SentryStandoffCm.X, SentryStandoffCm.Y) + FMath::VRand() * 150.0f;
	}
	// Ahead and to a side, so it wheels instead of zigzagging; then into the shell.
	const FVector Centre = GetActorLocation();
	const float Inner = GetInnerRadius();
	const float Outer = GetOuterRadius();
	const FVector Direction = (Forward + FMath::VRand() * 0.9f).GetSafeNormal(UE_SMALL_NUMBER, Forward);
	const FVector Ahead = From + Direction * ReachCm;
	const FVector FromCentre = Ahead - Centre;
	const float Low = Inner + ClearanceCm;
	const float High = FMath::Max(Low, Outer - ClearanceCm);
	// A swoop: down to just over the inner surface (a planetoid) or low through the ball.
	const float Distance = bSwoop ? (Inner > 0.0f ? Low : Outer * 0.5f) : FMath::Clamp(float(FromCentre.Size()), Low * (Inner > 0.0f ? 1.0f : 0.0f), High);
	const FVector Out = bSwoop && Inner <= 0.0f ? (FromCentre.GetSafeNormal() - FVector::UpVector).GetSafeNormal() : FromCentre.GetSafeNormal();
	return Centre + Out * Distance;
}

FVector AJamFlyerHabitat::ClampInside(const FVector& Location) const
{
	const FVector Centre = GetActorLocation();
	const float Margin = FMath::Min(300.0f, GetOuterRadius() * 0.2f);
	const FVector Offset = Location - Centre;
	const float Distance = FMath::Clamp(float(Offset.Size()), GetInnerRadius() + Margin, FMath::Max(GetInnerRadius() + Margin, GetOuterRadius() - Margin));
	return Centre + Offset.GetSafeNormal(UE_SMALL_NUMBER, FVector::UpVector) * Distance;
}

void AJamFlyerHabitat::BeginPlay()
{
	Super::BeginPlay();
	SampleSpots();
	const float Outer = GetOuterRadius();
	const float Inner = GetInnerRadius();
	UClass* Class = FlyerClass ? FlyerClass.Get() : AJamFlyer::StaticClass();
	FActorSpawnParameters Params;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	const int32 Clinging = IsRoost() ? FMath::RoundToInt(FlockSize * RoostShare) : 0;
	TArray<int32> Order;
	FRandomStream Shuffle(RoostSeed != 0 ? RoostSeed + 1 : FMath::Rand());
	for (int32 Index = 0; Index < Spots.Num(); ++Index)
	{
		Order.Insert(Index, Shuffle.RandRange(0, Order.Num()));
	}
	for (int32 Index = 0; Index < FlockSize; ++Index)
	{
		FVector Where = GetActorLocation() + FMath::VRand() * FMath::FRandRange(Inner + 0.25f * (Outer - Inner), Inner + 0.85f * (Outer - Inner));
		if (IsRoost())
		{
			Where = PickPoint(Where, FVector::ForwardVector, false, 0.0f, 0.0f);
		}
		AJamFlyer* Flyer = GetWorld()->SpawnActor<AJamFlyer>(Class, Where, FRotator::ZeroRotator, Params);
		if (!Flyer)
		{
			continue;
		}
		Flyer->SetHome(this);
		Flock.Add(Flyer);
		if (Index < Clinging && Order.IsValidIndex(Index))
		{
			// The spots in the seed's order: the spots themselves are densest at the focus.
			Spots[Order[Index]].Holder = Flyer;
			Flyer->RoostAt(Order[Index]);
		}
	}
	NextSwapSeconds = GetWorld()->GetTimeSeconds() + FMath::FRandRange(RoostSwapSeconds.X, RoostSwapSeconds.Y);
	GetWorldTimerManager().SetTimer(PulseTimer, this, &AJamFlyerHabitat::Pulse, 0.5f, true);
	UE_LOG(LogMindKore, Log, TEXT("Flyer habitat %s: %d flyers at %s, radius %.0f m (inner %.0f m), leash %.0f m; roost: %d spots, %d clinging; %s"),
		*GetName(), Flock.Num(), *GetActorLocation().ToCompactString(), Outer / 100.0f, Inner / 100.0f, LeashCm / 100.0f, Spots.Num(),
		CountIn(EJamFlyerState::Roost), bSpareUnarmed ? TEXT("spares the unarmed player") : TEXT("no first-EVA rule"));
}

bool AJamFlyerHabitat::IsRoostSurface(const AActor* Struck) const
{
	for (const TObjectPtr<AActor>& Surface : RoostOn)
	{
		if (Surface && Struck && (Struck == Surface || Struck->IsAttachedTo(Surface)))
		{
			return true;
		}
	}
	return false;
}

void AJamFlyerHabitat::SampleSpots()
{
	if (RoostOn.IsEmpty())
	{
		return;
	}
	UWorld* World = GetWorld();
	TArray<FVector> Doors;
	for (TActorIterator<AMKDoor> It(World); It; ++It)
	{
		Doors.Add(It->GetComponentsBoundingBox(true).GetCenter());
	}
	// A living wall is the encounter's door: no one clings by it, so the armed player who comes to tear it does not
	// wake the roost by coming close - the wall's pain call does.
	for (TActorIterator<AJamLivingWall> It(World); It; ++It)
	{
		Doors.Add(It->GetActorLocation());
	}
	const int32 Want = FMath::Max(1, FMath::CeilToInt(FlockSize * RoostShare * 1.6f));
	FRandomStream Stream(RoostSeed != 0 ? RoostSeed : FMath::Rand());
	FCollisionQueryParams Params(SCENE_QUERY_STAT(JamRoostSample), /*bTraceComplex=*/true);
	FCollisionObjectQueryParams Solid;
	Solid.AddObjectTypesToQuery(ECC_WorldStatic);
	Solid.AddObjectTypesToQuery(ECC_WorldDynamic);
	int32 Tries = 0;
	for (; Tries < Want * 120 && Spots.Num() < Want; ++Tries)
	{
		const AActor* Surface = RoostOn[Tries % RoostOn.Num()];
		if (!Surface)
		{
			continue;
		}
		FVector Origin, Extent;
		Surface->GetActorBounds(/*bOnlyCollidingComponents=*/true, Origin, Extent, /*bIncludeFromChildActors=*/true);
		// From a random direction, at a random point inside the bounds: an even spread over long hulls too.
		const FVector Inside = Origin + FVector(Stream.FRandRange(-0.8f, 0.8f) * Extent.X, Stream.FRandRange(-0.8f, 0.8f) * Extent.Y, Stream.FRandRange(-0.8f, 0.8f) * Extent.Z);
		const FVector From = Stream.VRand();
		FHitResult Hit;
		if (!World->LineTraceSingleByChannel(Hit, Inside + From * (Extent.Size() + 500.0f), Inside, ECC_Visibility, Params)
			|| !IsRoostSurface(Hit.GetActor()) || FVector::DotProduct(FVector(Hit.ImpactNormal), From) < 0.2f)
		{
			continue;
		}
		const FVector Point = Hit.ImpactPoint;
		const FVector Normal = Hit.ImpactNormal;
		if (RoostFocusFalloffCm > 0.0f && Stream.FRand() > FMath::Max(0.06f, FMath::Exp(-FVector::Dist(Point, RoostFocus) / RoostFocusFalloffCm)))
		{
			continue;
		}
		if (Doors.ContainsByPredicate([this, &Point](const FVector& Door) { return FVector::Dist(Door, Point) < RoostClearOfDoorsCm; }))
		{
			continue;
		}
		const float Spacing = RoostSpacingCm * Stream.FRandRange(0.85f, 1.3f);
		if (Spots.ContainsByPredicate([&Point, Spacing](const FJamRoostSpot& Spot) { return FVector::Dist(Spot.Point, Point) < Spacing; }))
		{
			continue;
		}
		// Room for the spike: nothing within 3 m out of the surface.
		FHitResult Blocker;
		if (World->SweepSingleByObjectType(Blocker, Point + Normal * 60.0f, Point + Normal * 320.0f, FQuat::Identity, Solid, FCollisionShape::MakeSphere(45.0f), Params))
		{
			continue;
		}
		FJamRoostSpot& Spot = Spots.AddDefaulted_GetRef();
		Spot.Point = Point;
		Spot.Normal = Normal;
		Spot.Side = FVector::CrossProduct(Normal, Stream.VRand()).GetSafeNormal(UE_SMALL_NUMBER, FVector::CrossProduct(Normal, FVector::RightVector).GetSafeNormal());
	}
	UE_LOG(LogMindKore, Log, TEXT("Flyer habitat %s: roost - %d of %d spots sampled on %d surface(s) in %d tries%s"), *GetName(), Spots.Num(), Want,
		RoostOn.Num(), Tries, RoostFocusFalloffCm > 0.0f ? *FString::Printf(TEXT(", densest at %s (falloff %.0f m)"), *RoostFocus.ToCompactString(), RoostFocusFalloffCm / 100.0f) : TEXT(""));
}

int32 AJamFlyerHabitat::ClaimSpot(AJamFlyer* Flyer, const FVector& Near)
{
	// Never within the wake radius of the player: its own landing there would wake the roost as if he had come close
	// (a player hiding by the hull set off a new cascade with every flyer that landed beside him).
	const APawn* Player = UGameplayStatics::GetPlayerPawn(this, 0);
	TArray<int32, TInlineAllocator<3>> Nearest;
	for (int32 Index = 0; Index < Spots.Num(); ++Index)
	{
		if ((Spots[Index].Holder.IsValid() && Spots[Index].Holder.Get() != Flyer)
			|| (Player && FVector::Dist(Spots[Index].Point, Player->GetActorLocation()) < RoostWakeRadiusCm))
		{
			continue;
		}
		Nearest.Add(Index);
		Nearest.Sort([this, &Near](int32 A, int32 B) { return FVector::DistSquared(Spots[A].Point, Near) < FVector::DistSquared(Spots[B].Point, Near); });
		if (Nearest.Num() > 3)
		{
			Nearest.RemoveAt(3);
		}
	}
	if (Nearest.IsEmpty())
	{
		return INDEX_NONE;
	}
	const int32 Pick = Nearest[FMath::RandRange(0, Nearest.Num() - 1)];
	Spots[Pick].Holder = Flyer;
	return Pick;
}

void AJamFlyerHabitat::ReleaseSpot(const AJamFlyer* Flyer, int32 Index)
{
	if (Spots.IsValidIndex(Index) && Spots[Index].Holder.Get() == Flyer)
	{
		Spots[Index].Holder.Reset();
	}
}

bool AJamFlyerHabitat::WantsMoreRoosting() const
{
	if (!IsRoost())
	{
		return false;
	}
	int32 Alive = 0;
	for (const TWeakObjectPtr<AJamFlyer>& Weak : Flock)
	{
		Alive += Weak.IsValid() && Weak->GetState() != EJamFlyerState::Dead ? 1 : 0;
	}
	return CountIn(EJamFlyerState::Roost) + CountIn(EJamFlyerState::Land) < FMath::RoundToInt(Alive * RoostShare);
}

int32 AJamFlyerHabitat::CountIn(EJamFlyerState State) const
{
	int32 Count = 0;
	for (const TWeakObjectPtr<AJamFlyer>& Weak : Flock)
	{
		Count += Weak.IsValid() && Weak->GetState() == State ? 1 : 0;
	}
	return Count;
}

bool AJamFlyerHabitat::SparesPlayer() const
{
	return bSpareUnarmed && !PlayerArmed(UGameplayStatics::GetPlayerPawn(this, 0));
}

void AJamFlyerHabitat::Provoke(EJamFlockMood Level, const FString& Why, const FVector& Where, AJamFlyer* By)
{
	FString Reason = Why;
	if (Level > EJamFlockMood::Stalking && SparesPlayer())
	{
		Level = EJamFlockMood::Stalking;
		Reason += TEXT(" (the player is unarmed: stalking at most)");
	}
	LastProvokedSeconds = GetWorld()->GetTimeSeconds();
	if (const APawn* Player = UGameplayStatics::GetPlayerPawn(this, 0))
	{
		SawPlayer(Player->GetActorLocation()); // a provocation shows where he is
	}
	if (Level <= Mood)
	{
		// Already hunting: the provoked one itself joins (a clinging one by the player, a touched one) and calls on.
		if (Level == EJamFlockMood::Hunting && By && By->GetState() != EJamFlyerState::Dead && !By->IsHunting())
		{
			By->Aggro(*FString::Printf(TEXT("the flock hunts: %s"), *Reason));
			Alert(By);
			return;
		}
		UE_LOG(LogMindKore, Log, TEXT("Flock %s: provoked (%s), stays %s"), *GetName(), *Reason, MoodName(Mood));
		return;
	}
	UE_LOG(LogMindKore, Log, TEXT("Flock %s: %s -> %s (%s)"), *GetName(), MoodName(Mood), MoodName(Level), *Reason);
	Mood = Level;
	if (Level != EJamFlockMood::Hunting)
	{
		return; // the trackers climb at their own next step (AJamFlyer::Think)
	}
	// The hunt: the cascade passes on from the provoked one (already hunting: it calls), or from the nearest to it.
	if (By && By->IsHunting())
	{
		Alert(By);
		return;
	}
	AJamFlyer* First = By;
	for (const TWeakObjectPtr<AJamFlyer>& Weak : Flock)
	{
		AJamFlyer* Other = Weak.Get();
		if (!By && Other && Other->GetState() != EJamFlyerState::Dead && !Other->IsHunting()
			&& (!First || FVector::DistSquared(Other->GetActorLocation(), Where) < FVector::DistSquared(First->GetActorLocation(), Where)))
		{
			First = Other;
		}
	}
	if (First)
	{
		First->Aggro(*FString::Printf(TEXT("the flock hunts: %s"), *Reason));
		if (First->IsHunting())
		{
			Alert(First);
		}
	}
}

bool AJamFlyerHabitat::TakeDiveToken()
{
	const double Now = GetWorld()->GetTimeSeconds();
	if (Mood != EJamFlockMood::Hunting || Now < RallyUntil || Now < NextDiveSeconds || CountIn(EJamFlyerState::Dive) >= MaxDivers)
	{
		return false;
	}
	NextDiveSeconds = Now + DiveGapSeconds;
	return true;
}

bool AJamFlyerHabitat::MayFeint() const
{
	return CountIn(EJamFlyerState::Feint) < MaxFeints;
}

bool AJamFlyerHabitat::TakeDart()
{
	const double Now = GetWorld()->GetTimeSeconds();
	if (Now < NextDartSeconds || Flock.ContainsByPredicate([](const TWeakObjectPtr<AJamFlyer>& Weak) { return Weak.IsValid() && Weak->IsDarting(); }))
	{
		return false;
	}
	NextDartSeconds = Now + FMath::FRandRange(DartEverySeconds.X, DartEverySeconds.Y);
	return true;
}

void AJamFlyerHabitat::SawPlayer(const FVector& At)
{
	PlayerSeenSeconds = GetWorld()->GetTimeSeconds();
	PlayerSeenAt = At;
}

void AJamFlyerHabitat::LosePlayer(float LostSeconds)
{
	if (Mood != EJamFlockMood::Hunting || GetWorld()->GetTimeSeconds() < RallyUntil)
	{
		return;
	}
	UE_LOG(LogMindKore, Log, TEXT("Flock %s: hunting -> stalking (lost the player for %.1f s: searches where it saw him last, %.0f m from the centre)"),
		*GetName(), LostSeconds, FVector::Dist(PlayerSeenAt, GetActorLocation()) / 100.0f);
	Mood = EJamFlockMood::Stalking;
	LastProvokedSeconds = GetWorld()->GetTimeSeconds(); // the search lasts a calm-down step
}

void AJamFlyerHabitat::Alert(AJamFlyer* Caller)
{
	const double Now = GetWorld()->GetTimeSeconds();
	if (Callers.IsEmpty())
	{
		CascadeStartedSeconds = Now;
		CascadeLastSeconds = Now;
		CascadeWoken = 0;
		CascadeDone.Reset();
	}
	CascadeDone.Add(Caller);
	Callers.AddUnique(Caller);
	if (!GetWorldTimerManager().IsTimerActive(CascadeTimer))
	{
		GetWorldTimerManager().SetTimer(CascadeTimer, this, &AJamFlyerHabitat::CascadeStep, FMath::FRandRange(WakeStepSeconds.X, WakeStepSeconds.Y), false);
	}
}

void AJamFlyerHabitat::CascadeStep()
{
	// The timer counts as active while its own callback runs: cleared, so the woken one's Alert schedules the next step.
	GetWorldTimerManager().ClearTimer(CascadeTimer);
	const double Now = GetWorld()->GetTimeSeconds();
	Callers.RemoveAll([](const TWeakObjectPtr<AJamFlyer>& Weak) { return !Weak.IsValid() || Weak->GetState() == EJamFlyerState::Dead; });
	// The nearest one not yet hunting to any that called, within the alert radius.
	AJamFlyer* Next = nullptr;
	const AJamFlyer* From = nullptr;
	float Best = FMath::Square(AlertRadiusCm);
	for (const TWeakObjectPtr<AJamFlyer>& Weak : Flock)
	{
		AJamFlyer* Other = Weak.Get();
		if (!Other || Other->GetState() == EJamFlyerState::Dead || Other->IsHunting() || CascadeDone.Contains(Weak))
		{
			continue;
		}
		for (const TWeakObjectPtr<AJamFlyer>& Caller : Callers)
		{
			const float Distance = FVector::DistSquared(Caller->GetActorLocation(), Other->GetActorLocation());
			if (Distance <= Best)
			{
				Best = Distance;
				Next = Other;
				From = Caller.Get();
			}
		}
	}
	if (!Next || Mood != EJamFlockMood::Hunting)
	{
		UE_LOG(LogMindKore, Log, TEXT("Flock %s: the cascade is over - %d woken in %.2f s"), *GetName(), CascadeWoken, float(CascadeLastSeconds - CascadeStartedSeconds));
		Callers.Reset();
		return;
	}
	const bool bClinging = Next->GetState() == EJamFlyerState::Roost;
	const FVector At = Next->GetActorLocation();
	++CascadeWoken;
	CascadeLastSeconds = Now;
	CascadeDone.Add(Next);
	Next->Aggro(*FString::Printf(TEXT("woken by %s's call, step %d at +%.2f s"), *GetNameSafe(From), CascadeWoken, float(Now - CascadeStartedSeconds)));
	Alert(Next);
	// The row: the clinging ones next to it unfold with it, silently (the wing snap), and call on too.
	if (bClinging)
	{
		for (const TWeakObjectPtr<AJamFlyer>& Weak : Flock)
		{
			AJamFlyer* Other = Weak.Get();
			if (Other && Other->GetState() == EJamFlyerState::Roost && FVector::Dist(Other->GetActorLocation(), At) < RoostRowCm)
			{
				++CascadeWoken;
				CascadeDone.Add(Weak);
				Other->Aggro(*FString::Printf(TEXT("unfolds in %s's row"), *Next->GetName()), /*bScreech=*/false);
				Alert(Other);
			}
		}
	}
}

void AJamFlyerHabitat::Pulse()
{
	const double Now = GetWorld()->GetTimeSeconds();
	// Calm: a rung down after CalmDownSeconds without provocation (each rung its own wait).
	if (Mood > EJamFlockMood::Curious && Now - LastProvokedSeconds > CalmDownSeconds && Now >= RallyUntil)
	{
		const EJamFlockMood Lower = EJamFlockMood(uint8(Mood) - 1);
		UE_LOG(LogMindKore, Log, TEXT("Flock %s: %s -> %s (calm: no provocation for %.0f s)"), *GetName(), MoodName(Mood), MoodName(Lower), CalmDownSeconds);
		Mood = Lower;
		LastProvokedSeconds = Now;
	}
	// The roost swap (at peace): a patrolling sentry lands, a clinging one peels off.
	if (IsRoost() && Now >= NextSwapSeconds && Mood == EJamFlockMood::Curious)
	{
		NextSwapSeconds = Now + FMath::FRandRange(RoostSwapSeconds.X, RoostSwapSeconds.Y);
		AJamFlyer* Sentry = nullptr;
		AJamFlyer* Clinging = nullptr;
		for (const TWeakObjectPtr<AJamFlyer>& Weak : Flock)
		{
			AJamFlyer* Each = Weak.Get();
			Sentry = !Sentry && Each && Each->GetState() == EJamFlyerState::Wander ? Each : Sentry;
			Clinging = Each && Each->GetState() == EJamFlyerState::Roost && FMath::RandBool() ? Each : Clinging;
		}
		if (Sentry && Clinging)
		{
			UE_LOG(LogMindKore, Log, TEXT("Flock %s: swap - %s lands, %s peels off"), *GetName(), *Sentry->GetName(), *Clinging->GetName());
			Sentry->StartLanding(TEXT("swap: lands for a rest"));
			Clinging->PeelOff(TEXT("swap: takes the watch"));
		}
	}
}

void AJamFlyerHabitat::HearNoise(const FVector& Where, const TArray<AJamFlyer*>& Listeners, const FString& What)
{
	for (AJamFlyer* Listener : Listeners)
	{
		Listener->Investigate(Where);
	}
	const EJamFlockMood Level = IsRoost() ? EJamFlockMood::Hunting : EJamFlockMood(FMath::Min(int32(Mood) + 1, int32(EJamFlockMood::Hunting)));
	Provoke(Level, What, Where, nullptr);
}

void AJamFlyerHabitat::ReportNoise(const UObject* WorldContext, const FVector& Where, const AActor* Struck, const TCHAR* What)
{
	UWorld* World = WorldContext ? WorldContext->GetWorld() : nullptr;
	if (!World)
	{
		return;
	}
	// The medium: the breathable zone around the noise, or vacuum.
	const AJamGravityZone* Air = nullptr;
	for (TActorIterator<AJamGravityZone> It(World); It && !Air; ++It)
	{
		Air = It->bBreathable && It->Contains(Where, 0.0f) ? *It : nullptr;
	}
	for (TActorIterator<AJamFlyerHabitat> It(World); It; ++It)
	{
		AJamFlyerHabitat* Home = *It;
		if (!Home->Contains(Where, Home->HearingRangeCm))
		{
			continue;
		}
		const float Metres = FVector::Dist(Where, Home->GetActorLocation()) / 100.0f;
		// Contact: through the hull they cling to.
		if (Struck && Home->IsRoostSurface(Struck) && Home->CountIn(EJamFlyerState::Roost) > 0)
		{
			UE_LOG(LogMindKore, Log, TEXT("Flyer hearing: %s on %s, %.0f m from %s's centre - contact: the roost feels it through the hull"), What,
				*GetNameSafe(Struck), Metres, *Home->GetName());
			AJamFlyer* Nearest = nullptr;
			for (const TWeakObjectPtr<AJamFlyer>& Weak : Home->GetFlock())
			{
				AJamFlyer* Each = Weak.Get();
				if (Each && Each->GetState() == EJamFlyerState::Roost
					&& (!Nearest || FVector::DistSquared(Each->GetActorLocation(), Where) < FVector::DistSquared(Nearest->GetActorLocation(), Where)))
				{
					Nearest = Each;
				}
			}
			Home->Provoke(EJamFlockMood::Hunting, FString::Printf(TEXT("%s on the roost's hull (contact)"), What), Where, Nearest);
			continue;
		}
		TArray<AJamFlyer*> Listeners;
		if (Air)
		{
			for (const TWeakObjectPtr<AJamFlyer>& Weak : Home->GetFlock())
			{
				if (AJamFlyer* Each = Weak.Get(); Each && Each->GetState() != EJamFlyerState::Dead && Air->Contains(Each->GetActorLocation(), 0.0f))
				{
					Listeners.Add(Each);
				}
			}
		}
		if (Listeners.IsEmpty())
		{
			UE_LOG(LogMindKore, Log, TEXT("Flyer hearing: %s at %.0f m from %s's centre - %s: unheard"), What, Metres, *Home->GetName(),
				Air ? *FString::Printf(TEXT("air of %s, no flyer in it"), *Air->GetName()) : TEXT("vacuum"));
			continue;
		}
		UE_LOG(LogMindKore, Log, TEXT("Flyer hearing: %s at %.0f m from %s's centre - air (%s): %d flyer(s) hear it"), What, Metres, *Home->GetName(),
			*Air->GetName(), Listeners.Num());
		Home->HearNoise(Where, Listeners, FString::Printf(TEXT("heard %s %.0f m away through the air"), What, FVector::Dist(Where, Listeners[0]->GetActorLocation()) / 100.0f));
	}
}

void AJamFlyerHabitat::AnswerPainCall(const FVector& Where, float DelaySeconds, const FString& From)
{
	RallyPoint = Where + (Where - GetActorLocation()).GetSafeNormal() * RallyOutCm;
	RallyUntil = GetWorld()->GetTimeSeconds() + DelaySeconds;
	UE_LOG(LogMindKore, Log, TEXT("Flock %s: answers %s's pain call - hunts, gathers over the wound %.0f m away for %.1f s, then goes for the player"),
		*GetName(), *From, FVector::Dist(Where, GetActorLocation()) / 100.0f, DelaySeconds);
	Provoke(EJamFlockMood::Hunting, FString::Printf(TEXT("%s's pain call (flesh is their nerves)"), *From), Where, nullptr);
}

// --- flyer ---------------------------------------------------------------------------------------

double AJamFlyer::TickSecondsRoost = 0.0;
double AJamFlyer::TickSecondsOther = 0.0;
int32 AJamFlyer::TicksRoost = 0;
int32 AJamFlyer::TicksOther = 0;

AJamFlyer::AJamFlyer()
{
	PrimaryActorTick.bCanEverTick = true;

	Body = CreateDefaultSubobject<USphereComponent>(TEXT("Body"));
	SetRootComponent(Body);
	Body->InitSphereRadius(40.0f);
	Body->SetCollisionObjectType(ECC_PhysicsBody);
	Body->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
	Body->SetCollisionResponseToAllChannels(ECR_Block);
	// Alive it passes through the player (the bite is the contact); dead it is a body to push.
	Body->SetCollisionResponseToChannel(ECC_Pawn, ECR_Ignore);
	Body->SetCollisionResponseToChannel(ECC_Visibility, ECR_Ignore);
	Body->SetCollisionResponseToChannel(ECC_Camera, ECR_Ignore);
	Body->SetSimulatePhysics(true);
	Body->SetEnableGravity(false);
	Body->SetLinearDamping(0.0f);
	Body->SetAngularDamping(0.0f);
	Body->BodyInstance.bOverrideMass = true;
	Body->BodyInstance.SetMassOverride(MassKg);
	Body->BodyInstance.DOFMode = EDOFMode::SixDOF;
	Body->BodyInstance.bLockXRotation = true;
	Body->BodyInstance.bLockYRotation = true;
	Body->BodyInstance.bLockZRotation = true;
	Body->SetCanEverAffectNavigation(false);
	// A round in the body reads flesh, as one in the mesh parts does (their material's).
	static ConstructorHelpers::FObjectFinder<UPhysicalMaterial> Flesh(TEXT("/Game/MindKore/Materials/Physical/PM_MK_Flesh.PM_MK_Flesh"));
	FleshMaterial = Flesh.Object; // set on the body in BeginPlay (not in a constructor: the CDO has no engine yet)

	Visual = CreateDefaultSubobject<USceneComponent>(TEXT("Visual"));
	Visual->SetupAttachment(Body);

	static ConstructorHelpers::FObjectFinder<UStaticMesh> TorsoMesh(TEXT("/Game/JamArt/Meshes/Flyer/SM_Jam_Flyer_Body.SM_Jam_Flyer_Body"));
	static ConstructorHelpers::FObjectFinder<UStaticMesh> WingMesh(TEXT("/Game/JamArt/Meshes/Flyer/SM_Jam_Flyer_Wing.SM_Jam_Flyer_Wing"));
	static ConstructorHelpers::FObjectFinder<UStaticMesh> TailMesh(TEXT("/Game/JamArt/Meshes/Flyer/SM_Jam_Flyer_Tail.SM_Jam_Flyer_Tail"));
	static ConstructorHelpers::FObjectFinder<UStaticMesh> TailTipMesh(TEXT("/Game/JamArt/Meshes/Flyer/SM_Jam_Flyer_TailTip.SM_Jam_Flyer_TailTip"));
	Torso = MakePart(this, TEXT("Torso"), TorsoMesh.Object, Visual, FVector::ZeroVector);
	WingR = MakePart(this, TEXT("WingR"), WingMesh.Object, Torso, ShoulderCm);
	WingL = MakePart(this, TEXT("WingL"), WingMesh.Object, Torso, FVector(ShoulderCm.X, -ShoulderCm.Y, ShoulderCm.Z));
	WingL->SetRelativeScale3D(FVector(1.0f, -1.0f, 1.0f)); // the right wing mirrored
	Tail = MakePart(this, TEXT("Tail"), TailMesh.Object, Torso, TailRootCm);
	TailTip = MakePart(this, TEXT("TailTip"), TailTipMesh.Object, Tail, TailSplitCm);

	// The stems of Scripts/jam_flyer_sounds.py (recorded CC0 donors, CREDITS.md): per event Attack,
	// Body, Character, Sub and the suit's Tail (vacuum only); 0 dB plays the rendered mix.
	const auto Stem = [](const TCHAR* EventName, const TCHAR* StemRole, float JitterDb, bool bVacuumOnly = false)
	{
		FJamSoundLayer Layer;
		const FString Name = FString::Printf(TEXT("SW_Jam_Flyer_%s_%s"), EventName, StemRole);
		const FString Path = FString::Printf(TEXT("/Game/JamArt/Audio/Flyer/%s.%s"), *Name, *Name);
		ConstructorHelpers::FObjectFinder<USoundBase> Finder(*Path); // loads now (the optional finder keeps the pointer)
		Layer.Sound = Finder.Object;
		Layer.GainJitterDb = JitterDb;
		Layer.bVacuumOnly = bVacuumOnly;
		return Layer;
	};
	const auto Event = [&Stem](const TCHAR* Name, std::initializer_list<const TCHAR*> Roles, float Volume, FVector2D Pitch)
	{
		FJamSoundEvent Result;
		for (const TCHAR* StemRole : Roles)
		{
			const bool bTail = FCString::Strcmp(StemRole, TEXT("Tail")) == 0;
			const bool bAttack = FCString::Strcmp(StemRole, TEXT("Attack")) == 0;
			Result.Layers.Add(Stem(Name, StemRole, bAttack || FCString::Strcmp(StemRole, TEXT("Character")) == 0 ? 2.0f : 1.0f, bTail));
		}
		Result.Volume = Volume;
		Result.Pitch = Pitch;
		return Result;
	};
	// Levels (Docs/Flyers.md, "the mix"): the stems are normalised per event; these keep a screech
	// 5-7 dB under the player's own pistol at 10 m and every wing beat far under both.
	AggroSound = Event(TEXT("Aggro"), { TEXT("Attack"), TEXT("Body"), TEXT("Character"), TEXT("Sub"), TEXT("Tail") }, 1.0f, FVector2D(0.94f, 1.06f));
	DiveSound = Event(TEXT("Dive"), { TEXT("Attack"), TEXT("Body"), TEXT("Character"), TEXT("Sub"), TEXT("Tail") }, 1.0f, FVector2D(0.97f, 1.07f));
	DeathSound = Event(TEXT("Death"), { TEXT("Attack"), TEXT("Body"), TEXT("Character"), TEXT("Sub"), TEXT("Tail") }, 0.8f, FVector2D(0.95f, 1.03f));
	// The stagger's yelp: the death cry's head (broken click, the close cry, the throat), a little higher, cut short.
	PainSound = Event(TEXT("Death"), { TEXT("Attack"), TEXT("Body"), TEXT("Character"), TEXT("Sub"), TEXT("Tail") }, 0.75f, FVector2D(1.08f, 1.22f));
	// The scanner's chirr: the beak clicks of the hit and of the bite (two clicks), an octave up.
	ChirrSound = Event(TEXT("Bite"), { TEXT("Attack") }, 0.8f, FVector2D(1.75f, 2.05f));
	ChirrSound.Layers.Add(Stem(TEXT("Hit"), TEXT("Attack"), 2.0f));
	ChirrSound.Layers.Last().GainDb = -4.0f;
	ChirrSound.StartJitterSeconds = 0.0f;
	ChirrSound.AirReverbSend = 0.1f;
	FlapSound = Event(TEXT("Flap"), { TEXT("Attack"), TEXT("Body"), TEXT("Sub"), TEXT("Tail") }, 1.0f, FVector2D(0.94f, 1.06f));
	FlyBySound = Event(TEXT("FlyBy"), { TEXT("Body"), TEXT("Character"), TEXT("Sub"), TEXT("Tail") }, 0.9f, FVector2D(0.9f, 1.1f));
	// A round into it, at 10 m about 3 dB under the player's own shot (jam.Flyer.MixCheck, Docs/Flyers.md).
	HitSound = Event(TEXT("Hit"), { TEXT("Attack"), TEXT("Body"), TEXT("Character"), TEXT("Sub"), TEXT("Tail") }, 3.2f, FVector2D(0.88f, 1.12f));
	BiteSound = Event(TEXT("Bite"), { TEXT("Attack"), TEXT("Body"), TEXT("Character"), TEXT("Sub"), TEXT("Tail") }, 1.0f, FVector2D(0.95f, 1.05f));
	{
		// MindKore's gristle crunch under the bite (recorded donors, CREDITS.md).
		static ConstructorHelpers::FObjectFinder<USoundBase> Crunch(TEXT("/Game/MindKore/Audio/Impact/SC_MK_Hit_Limb.SC_MK_Hit_Limb"));
		FJamSoundLayer Layer;
		Layer.Sound = Crunch.Object;
		Layer.GainDb = -6.0f;
		Layer.GainJitterDb = 2.0f;
		BiteSound.Layers.Add(Layer);
	}
	DiveSound.AirReverbSend = 0.2f;
	FlapSound.AirReverbSend = 0.15f;
	FlapSound.StartJitterSeconds = 0.01f;
	static ConstructorHelpers::FObjectFinder<USoundBase> FlutterWave(TEXT("/Game/JamArt/Audio/Flyer/SW_Jam_Flyer_Flutter_Loop.SW_Jam_Flyer_Flutter_Loop"));
	FlutterLoop = FlutterWave.Object;
	static ConstructorHelpers::FObjectFinder<USoundMix> Duck(TEXT("/Game/JamArt/Audio/Mix/SMIX_Jam_FlyerDuck.SMIX_Jam_FlyerDuck"));
	AggroDuck = Duck.Object;
	static ConstructorHelpers::FObjectFinder<USoundSubmixBase> AirReverbSubmix(TEXT("/Game/JamArt/Audio/Mix/SM_Jam_AirReverb.SM_Jam_AirReverb"));
	AirReverb = AirReverbSubmix.Object;
	VoiceAttenuation = JamFlyerAttenuation(5000.0f, 800.0f);
	// A call carries: full volume inside 6 m, -40 dB at 50 m (jam.Flyer.Clip takes, max momentary loudness
	// over 0.6 s: with 2 m and -60 dB a dive shriek at 10-14 m sat 11-13 dB under the pistol shot, quieter
	// than a bite; the aim is 5-7 dB under the player's own shot).
	VoiceAttenuation.AttenuationShapeExtents = FVector(600.0f, 0.0f, 0.0f);
	VoiceAttenuation.dBAttenuationAtMax = -40.0f;
	WingsAttenuation = JamFlyerAttenuation(2500.0f, 400.0f);
	ImpactAttenuation = JamFlyerAttenuation(3000.0f, 600.0f);
	// A hit is heard where the round lands, usually 5-15 m out: full volume inside 8 m, then linear to 30 m
	// (jam.Flyer.MixCheck: with the natural falloff a hit at 10 m sat 15 dB under its unattenuated level).
	ImpactAttenuation.AttenuationShapeExtents = FVector(800.0f, 0.0f, 0.0f);
	ImpactAttenuation.DistanceAlgorithm = EAttenuationDistanceModel::Linear; // natural falloff cost 15 dB at 10 m
}

void AJamFlyer::BeginPlay()
{
	Super::BeginPlay();
	Health = MaxHealth;
	Seed = FMath::FRandRange(0.0f, 100.0f);
	Curiosity = FMath::FRand();
	NextTwitchSeconds = GetWorld()->GetTimeSeconds() + FMath::FRandRange(RoostTwitchSeconds.X, RoostTwitchSeconds.Y);
	Body->SetMassOverrideInKg(NAME_None, MassKg, true);
	Body->SetPhysMaterialOverride(FleshMaterial);
	Body->SetPhysicsLinearVelocity(FMath::VRand() * CruiseSpeedCm);
	AllFlyers.Add(this);
	if (FlutterLoop)
	{
		Flutter = NewObject<UAudioComponent>(this);
		Flutter->bAutoActivate = false;
		Flutter->bAllowSpatialization = true;
		Flutter->bOverrideAttenuation = true;
		Flutter->AttenuationOverrides = WingsAttenuation;
		Flutter->SetSound(FlutterLoop);
		Flutter->SetupAttachment(Torso);
		Flutter->RegisterComponent();
	}
}

void AJamFlyer::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	AllFlyers.Remove(this);
	Super::EndPlay(EndPlayReason);
}

void AJamFlyer::SetHome(AJamFlyerHabitat* InHabitat)
{
	Habitat = InHabitat;
}

const TCHAR* AJamFlyer::StateName(EJamFlyerState InState)
{
	switch (InState)
	{
	case EJamFlyerState::Wander: return TEXT("wander");
	case EJamFlyerState::Circle: return TEXT("circle");
	case EJamFlyerState::Dive: return TEXT("dive");
	case EJamFlyerState::Retreat: return TEXT("retreat");
	case EJamFlyerState::Roost: return TEXT("roost");
	case EJamFlyerState::Land: return TEXT("land");
	case EJamFlyerState::Curious: return TEXT("curious");
	case EJamFlyerState::Stalk: return TEXT("stalk");
	case EJamFlyerState::Feint: return TEXT("feint");
	case EJamFlyerState::Stagger: return TEXT("stagger");
	case EJamFlyerState::Flee: return TEXT("flee");
	default: return TEXT("dead");
	}
}

bool AJamFlyer::IsHunting() const
{
	switch (State)
	{
	case EJamFlyerState::Circle:
	case EJamFlyerState::Dive:
	case EJamFlyerState::Retreat:
	case EJamFlyerState::Feint:
	case EJamFlyerState::Stagger:
	case EJamFlyerState::Flee:
		return true;
	default:
		// A mid-fight scanner dart is still the hunt.
		return State == EJamFlyerState::Curious && bDartFromHunt;
	}
}

APawn* AJamFlyer::Player() const
{
	return UGameplayStatics::GetPlayerPawn(this, 0);
}

FVector AJamFlyer::HeadLocation() const
{
	return Visual->GetComponentTransform().TransformPosition(HeadCm);
}

FVector AJamFlyer::Up(const FVector& Location) const
{
	const AJamFlyerHabitat* Home = Habitat.Get();
	return Home ? Home->UpAt(Location) : FVector::UpVector;
}

void AJamFlyer::PlayerView(const APawn* Target, FVector& OutEye, FVector& OutForward) const
{
	OutEye = EyeOf(Target);
	const FVector View = Target->GetViewRotation().Vector();
	const FVector Axis = Up(OutEye);
	OutForward = (View - Axis * FVector::DotProduct(View, Axis)).GetSafeNormal(UE_SMALL_NUMBER, View);
}

FVector AJamFlyer::RoostDirection(const FJamRoostSpot& Spot) const
{
	// Its own lean: a tilt off the normal toward its own side (from the seed), no two alike.
	const float Lean = FMath::Lerp(RoostTiltDeg.X, RoostTiltDeg.Y, FMath::Frac(Seed * 0.37f));
	const FVector Toward = FQuat(Spot.Normal, Seed * 2.3f).RotateVector(Spot.Side);
	const FVector Axis = FVector::CrossProduct(Spot.Normal, Toward).GetSafeNormal(UE_SMALL_NUMBER, Spot.Side);
	return FQuat(Axis, FMath::DegreesToRadians(Lean)).RotateVector(Spot.Normal);
}

FVector AJamFlyer::RoostCentre(const FJamRoostSpot& Spot) const
{
	return Spot.Point + RoostDirection(Spot) * RoostStandoffCm;
}

namespace JamFlyerParts
{
	TAutoConsoleVariable<bool> CVarLogFlaps(TEXT("jam.Flyer.LogFlaps"), false, TEXT("Log every audible wing beat of every flyer (jam.Flyer.Probe turns it on)."));
	/** The screeches playing now (their flyers), for Mix.MaxScreeches. */
	TArray<TWeakObjectPtr<AJamFlyer>> Screamers;
}

TArray<TWeakObjectPtr<AJamFlyer>> AJamFlyer::AllFlyers;

bool AJamFlyer::ListenerInAir() const
{
	const AJamZeroGPawn* Pawn = Cast<AJamZeroGPawn>(Player());
	return Pawn && Pawn->Oxygen && Pawn->Oxygen->IsBreathable();
}

bool AJamFlyer::WingsHeard() const
{
	const APawn* Listener = Player();
	if (!Listener || State == EJamFlyerState::Dead)
	{
		return false;
	}
	const float Mine = FVector::DistSquared(Listener->GetActorLocation(), GetActorLocation());
	if (Mine > FMath::Square(WingsAttenuation.FalloffDistance + 200.0f))
	{
		return false;
	}
	int32 Nearer = 0;
	for (const TWeakObjectPtr<AJamFlyer>& Weak : AllFlyers)
	{
		const AJamFlyer* Other = Weak.Get();
		if (Other && Other != this && Other->GetWorld() == GetWorld() && Other->State != EJamFlyerState::Dead
			&& FVector::DistSquared(Listener->GetActorLocation(), Other->GetActorLocation()) < Mine)
		{
			++Nearer;
		}
	}
	return Nearer < Mix.MaxWingSets;
}

bool AJamFlyer::TakeScreechSlot(const TCHAR* Why)
{
	const APawn* Listener = Player();
	const FVector Ear = Listener ? Listener->GetActorLocation() : GetActorLocation();
	const double Now = GetWorld()->GetTimeSeconds();
	Screamers.RemoveAll([this](const TWeakObjectPtr<AJamFlyer>& Weak)
	{
		return !Weak.IsValid() || Weak.Get() == this || Weak->GetWorld() != GetWorld() || !Weak->VoicePlay.IsPlaying();
	});
	if (Screamers.Num() >= Mix.MaxScreeches)
	{
		// First the oldest, when only its tail is left (a call answered: the cascade); else the farthest, if this one is nearer.
		AJamFlyer* Oldest = nullptr;
		AJamFlyer* Farthest = nullptr;
		for (const TWeakObjectPtr<AJamFlyer>& Weak : Screamers)
		{
			if (!Oldest || Weak->VoiceStartedSeconds < Oldest->VoiceStartedSeconds)
			{
				Oldest = Weak.Get();
			}
			if (!Farthest || FVector::DistSquared(Weak->GetActorLocation(), Ear) > FVector::DistSquared(Farthest->GetActorLocation(), Ear))
			{
				Farthest = Weak.Get();
			}
		}
		AJamFlyer* Gives = Now - Oldest->VoiceStartedSeconds >= ScreechBodySeconds ? Oldest : Farthest;
		if (Gives == Farthest && FVector::DistSquared(GetActorLocation(), Ear) >= FVector::DistSquared(Farthest->GetActorLocation(), Ear))
		{
			UE_LOG(LogMindKore, Log, TEXT("Flyer %s: screech (%s) dropped - %d nearer screeches playing"), *GetName(), Why, Screamers.Num());
			return false;
		}
		Gives->VoicePlay.Stop(Gives == Oldest ? 0.25f : 0.1f);
		Screamers.Remove(Gives);
		UE_LOG(LogMindKore, Log, TEXT("Flyer %s: screech (%s) takes the place of %s's (%s)"), *GetName(), Why, *Gives->GetName(),
			Gives == Oldest ? *FString::Printf(TEXT("%.2f s old: only its tail left"), float(Now - Gives->VoiceStartedSeconds))
			: *FString::Printf(TEXT("%.1f m away, the farthest"), FVector::Dist(Gives->GetActorLocation(), Ear) / 100.0f));
	}
	Screamers.Add(this);
	return true;
}

void AJamFlyer::Screech(const TCHAR* Why, bool bDive)
{
	++(bDive ? DiveScreeches : CallScreeches);
	if (!TakeScreechSlot(Why))
	{
		return;
	}
	VoicePlay.Stop(0.05f);
	const bool bAir = ListenerInAir();
	VoicePlay = JamPlaySoundEvent(this, bDive ? DiveSound : AggroSound, FVector::ZeroVector, Torso, VoiceAttenuation, bAir, AirReverb);
	VoiceStartedSeconds = GetWorld()->GetTimeSeconds();
	bVoiceDoppler = bDive;
	if (!bDive && AggroDuck)
	{
		UGameplayStatics::PushSoundMixModifier(this, AggroDuck);
	}
	UE_LOG(LogMindKore, Log, TEXT("Flyer %s: screech (%s) - %s, %s%s; %.1f m from the player"), *GetName(), Why, bDive ? TEXT("Dive") : TEXT("Aggro"),
		*VoicePlay.Describe(), bDive ? TEXT(", Doppler") : TEXT(", ambience ducked"),
		Player() ? FVector::Dist(Player()->GetActorLocation(), GetActorLocation()) / 100.0f : -1.0f);
}

void AJamFlyer::PlayPain()
{
	if (!TakeScreechSlot(TEXT("pain")))
	{
		return;
	}
	VoicePlay.Stop(0.05f);
	VoicePlay = JamPlaySoundEvent(this, PainSound, FVector::ZeroVector, Torso, VoiceAttenuation, ListenerInAir(), AirReverb);
	const double Started = GetWorld()->GetTimeSeconds();
	VoiceStartedSeconds = Started;
	bVoiceDoppler = false;
	UE_LOG(LogMindKore, Log, TEXT("Flyer %s: pain cry - %s, cut at %.2f s"), *GetName(), *VoicePlay.Describe(), PainCrySeconds);
	// Cut short: the yelp, not the death cry. Not if another voice (or the death cry) took over since.
	FTimerHandle Handle;
	GetWorldTimerManager().SetTimer(Handle, FTimerDelegate::CreateWeakLambda(this, [this, Started]()
	{
		if (State != EJamFlyerState::Dead && VoiceStartedSeconds == Started)
		{
			VoicePlay.Stop(0.12f);
		}
	}), PainCrySeconds, false);
}

void AJamFlyer::PlayChirr()
{
	// Three clicks in a row: the "photo".
	for (int32 Click = 0; Click < 3; ++Click)
	{
		FTimerHandle Handle;
		GetWorldTimerManager().SetTimer(Handle, FTimerDelegate::CreateWeakLambda(this, [this, Click]()
		{
			const FJamSoundPlay Play = JamPlaySoundEvent(this, ChirrSound, FVector::ZeroVector, Torso, WingsAttenuation, ListenerInAir(), AirReverb,
				Click == 1 ? 0.8f : 1.0f);
			if (Click == 0)
			{
				UE_LOG(LogMindKore, Log, TEXT("Flyer %s: chirr - %s"), *GetName(), *Play.Describe());
			}
		}), 0.01f + Click * 0.075f, false);
	}
}

float AJamFlyer::Doppler() const
{
	const APawn* Listener = Player();
	if (!Listener || Mix.DopplerScale <= 0.0f)
	{
		return 1.0f;
	}
	// f' = f (c + v_listener) / (c - v_source), along the line between them; scaled in log pitch.
	constexpr float SpeedOfSound = 34300.0f;
	const FVector ToListener = (Listener->GetActorLocation() - GetActorLocation()).GetSafeNormal();
	const float Source = FVector::DotProduct(GetVelocity(), ToListener);
	const float Receiver = FVector::DotProduct(Listener->GetVelocity(), -ToListener);
	const float Ratio = (SpeedOfSound + Receiver) / FMath::Max(SpeedOfSound - Source, 1000.0f);
	return FMath::Clamp(FMath::Pow(Ratio, Mix.DopplerScale), 0.5f, 2.0f);
}

void AJamFlyer::PlayFlap(float Strength, float Rate)
{
	if (Strength < 0.35f || !WingsHeard())
	{
		return;
	}
	// A big beat is louder and opens up; a fast one is higher; a half-glide beat is soft and dark.
	const float Size = FMath::Clamp(Strength, 0.0f, 1.2f);
	const float Speed = FMath::Clamp((Rate - 0.8f) / 0.8f, 0.0f, 1.0f);
	const FJamSoundPlay Play = JamPlaySoundEvent(this, FlapSound, FVector::ZeroVector, Torso, WingsAttenuation, ListenerInAir(), AirReverb,
		FlapVolume * Size, FMath::Lerp(0.92f, 1.12f, Speed), FMath::Lerp(0.45f, 1.0f, FMath::Min(Size, 1.0f)));
	if (CVarLogFlaps.GetValueOnGameThread())
	{
		UE_LOG(LogMindKore, Log, TEXT("Flyer %s: flap - size %.2f, rate %.1f: %s, %s, %.1f m from the player"), *GetName(), Size, Rate,
			*Play.Describe(), StateName(State), Player() ? FVector::Dist(Player()->GetActorLocation(), GetActorLocation()) / 100.0f : -1.0f);
	}
}

void AJamFlyer::UpdateSounds(float DeltaSeconds)
{
	const APawn* Listener = Player();
	const bool bClinging = State == EJamFlyerState::Roost;
	// Doppler on the dive shriek and the fly-by while they play (Pitch is the drawn pitch, without it).
	if (bVoiceDoppler && VoicePlay.IsPlaying())
	{
		VoicePlay.SetPitch(VoicePlay.Pitch * Doppler());
	}
	if (FlyByPlay.IsPlaying())
	{
		FlyByPlay.SetPitch(FlyByPlay.Pitch * Doppler());
	}
	// Moving sounds keep checking the line to the player (rock and hull muffle them).
	if (!bClinging && GetWorld()->GetTimeSeconds() >= NextOcclusionSeconds)
	{
		NextOcclusionSeconds = GetWorld()->GetTimeSeconds() + 0.15;
		VoicePlay.UpdateOcclusion(this);
		FlyByPlay.UpdateOcclusion(this);
	}
	if (!Listener)
	{
		return;
	}
	const double Now = GetWorld()->GetTimeSeconds();
	// A close pass: the whoosh starts a quarter second before the closest approach.
	const FVector Offset = Listener->GetActorLocation() - GetActorLocation();
	const FVector Relative = GetVelocity() - Listener->GetVelocity();
	const float RelativeSpeed = Relative.Size();
	if (!bClinging && RelativeSpeed > FlyByMinSpeedCm && Now - LastFlyBySeconds > 1.5)
	{
		const float ToClosest = FVector::DotProduct(Offset, Relative) / FMath::Square(RelativeSpeed);
		const float Miss = (Offset - Relative * ToClosest).Size();
		if (ToClosest > 0.0f && ToClosest < 0.25f && Miss < FlyByDistanceCm)
		{
			LastFlyBySeconds = Now;
			FlyByPlay = JamPlaySoundEvent(this, FlyBySound, FVector::ZeroVector, Torso, WingsAttenuation, ListenerInAir(), AirReverb,
				FMath::Clamp(RelativeSpeed / 1800.0f, 0.6f, 1.2f));
			UE_LOG(LogMindKore, Log, TEXT("Flyer %s: fly-by - %.1f m miss at %.0f cm/s, closest in %.2f s (%s): %s"), *GetName(), Miss / 100.0f,
				RelativeSpeed, ToClosest, StateName(State), *FlyByPlay.Describe());
		}
	}
	// The cloth flutter between beats, with the speed; only for the wings that are heard. Clinging, the
	// nearest ones are the roost's bed: a low dark rustle, so the hull sounds alive before it moves.
	if (Flutter)
	{
		const float Target = !WingsHeard() ? 0.0f : bClinging ? RoostFlutterVolume : FlutterVolume * FMath::Clamp(GetVelocity().Size() / 1200.0f, 0.3f, 1.2f);
		const float LowPass = bClinging ? (ListenerInAir() ? 2500.0f : 1400.0f) : (ListenerInAir() ? 9000.0f : 3500.0f);
		if (Target > 0.0f && !Flutter->IsPlaying())
		{
			Flutter->SetLowPassFilterEnabled(true);
			Flutter->SetLowPassFilterFrequency(LowPass);
			Flutter->SetVolumeMultiplier(Target);
			Flutter->SetPitchMultiplier(bClinging ? 0.8f : 1.0f);
			Flutter->Play(FMath::FRandRange(0.0f, 1.9f));
		}
		else if (Target <= 0.0f && Flutter->IsPlaying())
		{
			Flutter->FadeOut(0.3f, 0.0f);
		}
		else if (Target > 0.0f)
		{
			Flutter->SetVolumeMultiplier(Target);
			Flutter->SetLowPassFilterFrequency(LowPass);
			Flutter->SetPitchMultiplier(bClinging ? 0.8f : 1.0f);
		}
	}
}

// --- states --------------------------------------------------------------------------------------

void AJamFlyer::SetState(EJamFlyerState NewState, const FString& Why)
{
	const double Now = GetWorld()->GetTimeSeconds();
	const EJamFlyerState Old = State;
	AJamFlyerHabitat* Home = Habitat.Get();
	UE_LOG(LogMindKore, Log, TEXT("Flyer %s: %s -> %s (%s)"), *GetName(), StateName(Old), StateName(NewState), *Why);
	State = NewState;
	StateSeconds = Now;
	if (Old == EJamFlyerState::Dive && NewState != EJamFlyerState::Dive && Home)
	{
		UE_LOG(LogMindKore, Log, TEXT("Flyer %s: returns its dive token (%d/%d diving now)"), *GetName(), Home->CountIn(EJamFlyerState::Dive), Home->MaxDivers);
	}
	if ((Old == EJamFlyerState::Roost || Old == EJamFlyerState::Land) && NewState != EJamFlyerState::Roost && NewState != EJamFlyerState::Land)
	{
		if (Home)
		{
			Home->ReleaseSpot(this, RoostSpot);
		}
		RoostSpot = INDEX_NONE;
	}
	if (Old == EJamFlyerState::Roost && NewState != EJamFlyerState::Roost)
	{
		SetActorTickInterval(0.0f);
	}
	if (NewState != EJamFlyerState::Curious)
	{
		DartPhase = 0;
		bDartFromHunt = false;
	}
	switch (NewState)
	{
	case EJamFlyerState::Circle:
		StateUntil = Now + FMath::FRandRange(CircleSeconds.X, CircleSeconds.Y);
		break;
	case EJamFlyerState::Retreat:
		StateUntil = Now + RetreatSeconds;
		break;
	case EJamFlyerState::Wander:
		WanderPickedSeconds = -100.0; // a fresh target
		break;
	case EJamFlyerState::Stalk:
		StalkLeg = 0;
		StateUntil = Now + StalkLegSeconds;
		break;
	case EJamFlyerState::Feint:
		StateUntil = Now + MaxDiveSeconds;
		break;
	case EJamFlyerState::Flee:
		StateUntil = Now + FleeSeconds;
		break;
	case EJamFlyerState::Land:
		StateUntil = Now + LandSeconds;
		break;
	default:
		break;
	}
}

void AJamFlyer::Unfold()
{
	const AJamFlyerHabitat* Home = Habitat.Get();
	const FJamRoostSpot* Spot = Home ? Home->GetSpot(RoostSpot) : nullptr;
	const FVector Out = Spot ? RoostDirection(*Spot) : Visual->GetForwardVector();
	// Off the hull with a push of the legs and a hard first beat (the wing snap).
	Body->SetPhysicsLinearVelocity(Out * 650.0f + FMath::VRand() * 150.0f);
	FlapPhase = UE_PI * 0.9f;
	PlayFlap(1.2f, 1.8f);
}

void AJamFlyer::RoostAt(int32 SpotIndex)
{
	const AJamFlyerHabitat* Home = Habitat.Get();
	const FJamRoostSpot* Spot = Home ? Home->GetSpot(SpotIndex) : nullptr;
	if (!Spot)
	{
		return;
	}
	RoostSpot = SpotIndex;
	Body->SetWorldLocation(RoostCentre(*Spot), false, nullptr, ETeleportType::TeleportPhysics);
	Body->SetPhysicsLinearVelocity(FVector::ZeroVector);
	RoostBlend = 1.0f;
	StareDirection = RoostDirection(*Spot);
	SetState(EJamFlyerState::Roost, FString::Printf(TEXT("clings to spot %d"), SpotIndex));
	SetActorTickInterval(RoostTickSeconds);
}

void AJamFlyer::StartLanding(const TCHAR* Why)
{
	AJamFlyerHabitat* Home = Habitat.Get();
	if (!Home || !Home->IsRoost() || State == EJamFlyerState::Dead)
	{
		return;
	}
	if (State == EJamFlyerState::Land)
	{
		Home->ReleaseSpot(this, RoostSpot);
	}
	const int32 Spot = Home->ClaimSpot(this, GetActorLocation());
	if (Spot == INDEX_NONE)
	{
		SetState(EJamFlyerState::Wander, FString::Printf(TEXT("%s - no free spot, patrols"), Why));
		return;
	}
	RoostSpot = Spot;
	SetState(EJamFlyerState::Land, FString::Printf(TEXT("%s - to spot %d, %.1f m off"), Why, Spot, FVector::Dist(Home->GetSpot(Spot)->Point, GetActorLocation()) / 100.0f));
}

void AJamFlyer::PeelOff(const TCHAR* Why)
{
	if (State != EJamFlyerState::Roost)
	{
		return;
	}
	Unfold();
	SetState(EJamFlyerState::Wander, Why);
}

void AJamFlyer::Investigate(const FVector& Where)
{
	const AJamFlyerHabitat* Home = Habitat.Get();
	if (State != EJamFlyerState::Wander || !Home)
	{
		return;
	}
	NoiseCentre = Home->ClampInside(Where);
	NoisePoint = NoiseCentre + FMath::VRand() * InvestigateSpreadCm;
	NoiseUntil = GetWorld()->GetTimeSeconds() + InvestigateSeconds;
	WanderPickedSeconds = GetWorld()->GetTimeSeconds();
	UE_LOG(LogMindKore, Log, TEXT("Flyer %s: investigates the noise %.1f m away (for %.0f s)"), *GetName(), FVector::Dist(Where, GetActorLocation()) / 100.0f, InvestigateSeconds);
}

void AJamFlyer::Notice(const TCHAR* Why)
{
	if (State == EJamFlyerState::Wander || State == EJamFlyerState::Roost || State == EJamFlyerState::Land)
	{
		OnNoticed(Why);
	}
}

void AJamFlyer::OnNoticed(const FString& Why)
{
	const AJamFlyerHabitat* Home = Habitat.Get();
	const EJamFlockMood Mood = Home ? Home->GetMood() : EJamFlockMood::Hunting;
	LastSeenSeconds = GetWorld()->GetTimeSeconds();
	NoiseUntil = -100.0;
	if (State == EJamFlyerState::Roost)
	{
		Unfold();
	}
	// The first rung of the ladder is curiosity; a flock already past it brings the newcomer in by a stalking pass.
	if (Mood == EJamFlockMood::Curious)
	{
		SetState(EJamFlyerState::Curious, FString::Printf(TEXT("curious: %s"), *Why));
	}
	else
	{
		SetState(EJamFlyerState::Stalk, FString::Printf(TEXT("stalks (the flock is %s): %s"), MoodName(Mood), *Why));
	}
}

void AJamFlyer::Aggro(const TCHAR* Why, bool bScreech)
{
	if (State == EJamFlyerState::Dead || IsHunting())
	{
		return;
	}
	if (State == EJamFlyerState::Roost)
	{
		Unfold();
	}
	const double Now = GetWorld()->GetTimeSeconds();
	LastSeenSeconds = Now; // a woken flockmate gets LoseSightSeconds to find the player
	// Answered a call a moment ago: back into the same fight without a second screech.
	const bool bQuiet = Now - AnsweredSeconds < RejoinQuietSeconds;
	SetState(EJamFlyerState::Circle, bQuiet ? FString::Printf(TEXT("aggro: %s - rejoins silently, answered a call %.1f s ago"), Why, float(Now - AnsweredSeconds))
		: FString::Printf(TEXT("aggro: %s"), Why));
	if (bQuiet)
	{
		return;
	}
	AnsweredSeconds = Now;
	if (bScreech)
	{
		Screech(TEXT("aggro"), false);
	}
}

void AJamFlyer::GoCalm(const FString& Why)
{
	CalmUntil = GetWorld()->GetTimeSeconds() + CalmSeconds;
	NoiseUntil = -100.0;
	if (Habitat.IsValid() && Habitat->WantsMoreRoosting())
	{
		StartLanding(*FString::Printf(TEXT("%s; back to the roost"), *Why));
		return;
	}
	SetState(EJamFlyerState::Wander, Why);
}

bool AJamFlyer::SearchFrame(FVector& OutCentre, FVector& OutForward) const
{
	const AJamFlyerHabitat* Home = Habitat.Get();
	if (!Home || bSeesPlayer || GetWorld()->GetTimeSeconds() - Home->GetPlayerSeenSeconds() <= LoseSightSeconds)
	{
		return false;
	}
	OutCentre = Home->GetPlayerSeenAt();
	const FVector Axis = Up(OutCentre);
	const FVector Base = FVector::CrossProduct(Axis, FVector::RightVector).GetSafeNormal(UE_SMALL_NUMBER, FVector::ForwardVector);
	OutForward = FQuat(Axis, Seed * 2.39996f).RotateVector(Base); // the golden angle per seed: bearings spread round
	return true;
}

void AJamFlyer::Stagger(const FVector& Direction)
{
	const double Now = GetWorld()->GetTimeSeconds();
	if (State == EJamFlyerState::Stagger)
	{
		StateUntil = FMath::Max(StateUntil, Now + StaggerSeconds.X); // another pellet, another round: longer, no new cry
		return;
	}
	StaggerFrom = State;
	if (State == EJamFlyerState::Roost)
	{
		Unfold();
	}
	// Knocked off its line: the flight half lost, the round's shove on top; nothing steers it now.
	const FVector Before = Body->GetPhysicsLinearVelocity();
	Body->SetPhysicsLinearVelocity(Before * StaggerKeepSpeed + Direction.GetSafeNormal() * StaggerKnockCm);
	TumbleSign = FMath::RandBool() ? 1.0f : -1.0f;
	TumbleDeg = 0.0f;
	SetState(EJamFlyerState::Stagger, FString::Printf(TEXT("hit out of its %s: tumbles, %.0f -> %.0f cm/s, knocked along the round"), StateName(StaggerFrom),
		Before.Size(), Body->GetPhysicsLinearVelocity().Size()));
	StateUntil = Now + FMath::FRandRange(StaggerSeconds.X, StaggerSeconds.Y);
	PlayPain();
}

void AJamFlyer::EndStagger()
{
	const AJamFlyerHabitat* Home = Habitat.Get();
	const EJamFlockMood Mood = Home ? Home->GetMood() : EJamFlockMood::Hunting;
	LastSeenSeconds = GetWorld()->GetTimeSeconds();
	if (Health <= MaxHealth * FleeHealthFraction)
	{
		SetState(EJamFlyerState::Flee, FString::Printf(TEXT("hurt (%.0f/%.0f): flees, back in %.0f s"), Health, MaxHealth, FleeSeconds));
		return;
	}
	if (Mood != EJamFlockMood::Hunting)
	{
		SetState(Mood == EJamFlockMood::Stalking ? EJamFlyerState::Stalk : EJamFlyerState::Curious,
			FString::Printf(TEXT("recovers; the flock is only %s"), MoodName(Mood)));
		return;
	}
	const bool bWasCalm = StaggerFrom == EJamFlyerState::Wander || StaggerFrom == EJamFlyerState::Roost || StaggerFrom == EJamFlyerState::Land
		|| StaggerFrom == EJamFlyerState::Curious || StaggerFrom == EJamFlyerState::Stalk;
	SetState(EJamFlyerState::Circle, TEXT("recovers from the stagger, enraged: dives first chance"));
	StateUntil = GetWorld()->GetTimeSeconds();
	if (bWasCalm)
	{
		Screech(TEXT("aggro"), false);
	}
}

// --- senses and thought ------------------------------------------------------------------------------

void AJamFlyer::Sense()
{
	bSeesPlayer = false;
	const APawn* Target = Player();
	if (!Target)
	{
		return;
	}
	const FVector Eye = HeadLocation();
	const FVector To = Target->GetActorLocation() - Eye;
	const float Distance = To.Size();
	if (Distance > SightRangeCm)
	{
		return;
	}
	// Noticing needs the cone; tracking keeps track all around.
	if (!IsTracking() && FVector::DotProduct(To / FMath::Max(Distance, 1.0f), Visual->GetForwardVector()) < FMath::Cos(FMath::DegreesToRadians(SightHalfAngleDeg)))
	{
		return;
	}
	FCollisionQueryParams Params(SCENE_QUERY_STAT(JamFlyerSight), /*bTraceComplex=*/false, this);
	Params.AddIgnoredActor(Target);
	bSeesPlayer = !GetWorld()->LineTraceTestByChannel(Eye, Target->GetActorLocation(), ECC_Visibility, Params);
	if (bSeesPlayer)
	{
		LastSeenSeconds = GetWorld()->GetTimeSeconds();
		if (AJamFlyerHabitat* Home = Habitat.Get())
		{
			Home->SawPlayer(Target->GetActorLocation());
		}
	}
}

void AJamFlyer::CheckAim(float DeltaSeconds)
{
	const AJamZeroGPawn* Pawn = Cast<AJamZeroGPawn>(Player());
	AJamFlyerHabitat* Home = Habitat.Get();
	if (!Pawn || !Home || !Pawn->Guns || !Pawn->Guns->GetWeapon() || AimProvokeSeconds <= 0.0f)
	{
		AimedSeconds = 0.0f;
		return;
	}
	const FVector Eye = EyeOf(Pawn);
	const FVector To = (GetActorLocation() - Eye).GetSafeNormal();
	if (FVector::DotProduct(To, Pawn->GetViewRotation().Vector()) < FMath::Cos(FMath::DegreesToRadians(AimConeDeg)))
	{
		AimedSeconds = 0.0f;
		return;
	}
	AimedSeconds += DeltaSeconds;
	if (AimedSeconds >= AimProvokeSeconds)
	{
		AimedSeconds = 0.0f;
		// Only a gun it can see: the view through the hull (the player inside the ship) points at nothing.
		FCollisionQueryParams Params(SCENE_QUERY_STAT(JamFlyerAim), /*bTraceComplex=*/false, this);
		Params.AddIgnoredActor(Pawn);
		if (GetWorld()->LineTraceTestByChannel(Eye, GetActorLocation(), ECC_Visibility, Params))
		{
			return;
		}
		const EJamFlockMood Up = EJamFlockMood(FMath::Min(int32(Home->GetMood()) + 1, int32(EJamFlockMood::Hunting)));
		Home->Provoke(Up, FString::Printf(TEXT("a gun held on %s for %.1f s"), *GetName(), AimProvokeSeconds), GetActorLocation(), this);
		Notice(TEXT("a gun held on it"));
	}
}

void AJamFlyer::ThinkRoost(float DeltaSeconds)
{
	AJamFlyerHabitat* Home = Habitat.Get();
	const FJamRoostSpot* Spot = Home ? Home->GetSpot(RoostSpot) : nullptr;
	if (!Spot || Spot->Holder.Get() != this)
	{
		SetState(EJamFlyerState::Wander, TEXT("lost its roost spot"));
		return;
	}
	const double Now = GetWorld()->GetTimeSeconds();
	// Held on its spot (a bump or a stray push does not carry it off).
	const FVector Centre = RoostCentre(*Spot);
	if (FVector::DistSquared(Body->GetComponentLocation(), Centre) > 25.0f)
	{
		Body->SetWorldLocation(Centre, false, nullptr, ETeleportType::TeleportPhysics);
		Body->SetPhysicsLinearVelocity(FVector::ZeroVector);
	}
	const APawn* Target = Player();
	const float Distance = Target ? FVector::Dist(Target->GetActorLocation(), Centre) : UE_BIG_NUMBER;
	// Close by, but the other side of the hull (the player inside the ship): nobody came near this one.
	FCollisionQueryParams Params(SCENE_QUERY_STAT(JamFlyerRoostNear), /*bTraceComplex=*/false, this);
	Params.AddIgnoredActor(Target);
	const bool bNear = Target && Distance < FMath::Max(Home->RoostTouchCm, Home->RoostWakeRadiusCm)
		&& !GetWorld()->LineTraceTestByChannel(Centre, Target->GetActorLocation(), ECC_Visibility, Params);
	if (bNear && Distance < Home->RoostTouchCm)
	{
		// Bumped: off with a warning screech and a feint at the one who touched it - not a bite.
		LastSeenSeconds = Now;
		Unfold();
		const FVector Side = FVector::CrossProduct(Target->GetActorLocation() - Centre, Up(Centre)).GetSafeNormal(UE_SMALL_NUMBER, Spot->Side);
		FeintAim = Side * FeintMissCm + Up(Centre) * 100.0f;
		SetState(EJamFlyerState::Feint, FString::Printf(TEXT("bumped %.1f m away: peels off with a warning screech and a feint"), Distance / 100.0f));
		Screech(TEXT("warning"), false);
		Home->Provoke(EJamFlockMood::Hunting, FString::Printf(TEXT("the player touched %s on the roost"), *GetName()), Centre, this);
		return;
	}
	if (bNear && Distance < Home->RoostWakeRadiusCm && !Home->SparesPlayer())
	{
		Home->Provoke(EJamFlockMood::Hunting, FString::Printf(TEXT("the armed player came within %.1f m of the roost"), Distance / 100.0f), Centre, this);
		return;
	}
	CheckAim(DeltaSeconds);
	if (State != EJamFlyerState::Roost)
	{
		return;
	}
	// The hull watches: heads turn toward a player near, a little.
	const FVector Rest = RoostDirection(*Spot);
	FVector Stare = Rest;
	if (Target && Distance < RoostStareCm)
	{
		const FVector ToEye = (EyeOf(Target) - Centre).GetSafeNormal(UE_SMALL_NUMBER, Rest);
		const float Angle = FMath::Acos(FMath::Clamp(float(FVector::DotProduct(Rest, ToEye)), -1.0f, 1.0f));
		const float Allowed = FMath::DegreesToRadians(RoostStareDeg);
		Stare = Angle <= Allowed ? ToEye : FQuat::Slerp(FQuat::Identity, FQuat::FindBetweenNormals(Rest, ToEye), Allowed / Angle).RotateVector(Rest);
	}
	StareDirection = FMath::VInterpNormalRotationTo(StareDirection.IsNearlyZero() ? Rest : StareDirection, Stare, DeltaSeconds, 30.0f);
	// One idle twitch at a time, each on its own clock (never in sync).
	if (Now >= NextTwitchSeconds)
	{
		TwitchKind = FMath::RandRange(0, 2);
		TwitchStart = Now;
		TwitchLength = TwitchKind == 0 ? 0.7f : TwitchKind == 1 ? 1.6f : 2.6f;
		TwitchAxis = FVector::CrossProduct(Rest, FMath::VRand()).GetSafeNormal(UE_SMALL_NUMBER, Spot->Side);
		NextTwitchSeconds = Now + TwitchLength + FMath::FRandRange(RoostTwitchSeconds.X, RoostTwitchSeconds.Y);
		if (TwitchKind == 0)
		{
			PlayFlap(0.4f, 2.6f); // the wing shiver, soft
		}
	}
	// Slow far off and at rest; every frame while something moves or the player is near.
	const bool bBusy = Now - TwitchStart < TwitchLength || Distance < RoostStareCm || RoostBlend < 1.0f;
	const float Interval = bBusy ? 0.0f : RoostTickSeconds;
	if (!FMath::IsNearlyEqual(GetActorTickInterval(), Interval))
	{
		SetActorTickInterval(Interval);
	}
	UpdateSounds(DeltaSeconds);
}

void AJamFlyer::Think(const FVector& Location, const FVector& Velocity)
{
	const double Now = GetWorld()->GetTimeSeconds();
	APawn* Target = Player();
	AJamFlyerHabitat* Home = Habitat.Get();
	if (!Home)
	{
		return;
	}
	EJamFlockMood Mood = Home->GetMood();
	switch (State)
	{
	case EJamFlyerState::Wander:
		CheckAim(GetWorld()->GetDeltaSeconds());
		if (State == EJamFlyerState::Wander && bSeesPlayer && Now >= CalmUntil && Target && Home->Contains(Target->GetActorLocation(), Now < NoiseUntil ? Home->LeashCm : Home->AggroMarginCm))
		{
			const FVector To = Target->GetActorLocation() - HeadLocation();
			OnNoticed(FString::Printf(TEXT("saw the player %.1f m away, %.0f deg off the nose%s"), To.Size() / 100.0f,
				FMath::RadiansToDegrees(FMath::Acos(FVector::DotProduct(To.GetSafeNormal(), Visual->GetForwardVector()))),
				Now < NoiseUntil ? TEXT(", investigating a noise") : TEXT("")));
		}
		return;
	case EJamFlyerState::Land:
	{
		const FJamRoostSpot* Spot = Home->GetSpot(RoostSpot);
		if (!Spot || Spot->Holder.Get() != this || Now >= StateUntil)
		{
			StartLanding(Now >= StateUntil ? TEXT("gave up on its spot") : TEXT("its spot was taken"));
		}
		else if (FVector::Dist(Location, RoostCentre(*Spot)) < 60.0f)
		{
			LandedSeconds = Now;
			Body->SetPhysicsLinearVelocity(FVector::ZeroVector);
			PlayFlap(0.55f, 0.9f); // the grab
			const int32 Index = RoostSpot;
			RoostSpot = INDEX_NONE;
			RoostAt(Index);
			RoostBlend = 0.0f; // the settle: the wings fold, the spike rights itself
			SetActorTickInterval(0.0f);
		}
		return;
	}
	case EJamFlyerState::Stagger:
		if (Now >= StateUntil)
		{
			EndStagger();
		}
		return;
	default:
		break;
	}
	if (!Target)
	{
		GoCalm(TEXT("no player"));
		return;
	}
	const FVector PlayerAt = Target->GetActorLocation();
	const bool bRally = Now < Home->RallyUntil;
	if (bRally)
	{
		LastSeenSeconds = Now; // the pain call told them where the player is
		Home->SawPlayer(PlayerAt);
	}
	if (!bRally && !Home->Contains(PlayerAt, Home->LeashCm))
	{
		GoCalm(FString::Printf(TEXT("de-aggro: the player left the habitat, %.0f m from its centre, leash at %.0f m"),
			FVector::Dist(PlayerAt, Home->GetActorLocation()) / 100.0f, (Home->GetOuterRadius() + Home->LeashCm) / 100.0f));
		return;
	}
	if (State == EJamFlyerState::Flee)
	{
		if (Now >= StateUntil)
		{
			LastSeenSeconds = Now;
			SetState(Mood == EJamFlockMood::Hunting ? EJamFlyerState::Circle : EJamFlyerState::Stalk, TEXT("comes back from fleeing"));
		}
		return;
	}
	if (Now - LastSeenSeconds > LoseSightSeconds)
	{
		// Lost: only a calm flock lets it go (land or patrol). A hunting flock that lost the player as a whole
		// searches (stalking: passes over where it saw him last) and calms down from there, a rung per calm-down.
		if (Mood == EJamFlockMood::Curious)
		{
			GoCalm(FString::Printf(TEXT("de-aggro: lost sight of the player for %.1f s, the flock is calm"), float(Now - LastSeenSeconds)));
			return;
		}
		if (Mood == EJamFlockMood::Hunting && Now - Home->GetPlayerSeenSeconds() > LoseSightSeconds)
		{
			Home->LosePlayer(float(Now - Home->GetPlayerSeenSeconds()));
			Mood = Home->GetMood();
		}
	}
	// Down the ladder: the flock calmed below hunting, lost the player (the search), or the player is unarmed.
	if (Mood != EJamFlockMood::Hunting && (State == EJamFlyerState::Circle || State == EJamFlyerState::Retreat))
	{
		FVector Centre, Forward;
		SetState(EJamFlyerState::Stalk, FString::Printf(TEXT("the flock is %s now%s"), MoodName(Mood),
			SearchFrame(Centre, Forward) ? TEXT(": searches where it saw the player last") : TEXT("")));
		return;
	}
	CheckAim(GetWorld()->GetDeltaSeconds());
	if (State == EJamFlyerState::Dead)
	{
		return;
	}
	FVector Eye, ViewForward;
	PlayerView(Target, Eye, ViewForward);
	const float Distance = FVector::Dist(PlayerAt, Location);
	switch (State)
	{
	case EJamFlyerState::Curious:
		if (DartPhase == 0 && !bDartFromHunt && Mood != EJamFlockMood::Curious)
		{
			SetState(EJamFlyerState::Stalk, FString::Printf(TEXT("up the ladder: the flock is %s"), MoodName(Mood)));
		}
		else if (DartPhase == 0 && bSeesPlayer && Home->TakeDart())
		{
			DartPhase = 1;
			DartPhaseUntil = Now + 2.0;
			UE_LOG(LogMindKore, Log, TEXT("Flyer %s: darts in at the face from %.1f m (curiosity %.2f)"), *GetName(), FVector::Dist(Location, Eye) / 100.0f, Curiosity);
		}
		else if (DartPhase == 1 && (FVector::Dist(Location, Eye + Target->GetViewRotation().Vector() * DartStopCm) < 70.0f || Now >= DartPhaseUntil))
		{
			DartPhase = 2;
			DartPhaseUntil = Now + DartHoldSeconds;
			Body->SetPhysicsLinearVelocity(Target->GetVelocity());
			PlayFlap(1.2f, 1.9f); // the snap of the wings that stops it dead
			PlayChirr();
			UE_LOG(LogMindKore, Log, TEXT("Flyer %s: stops dead %.1f m from the face and chirrs (the photo)"), *GetName(), FVector::Dist(Location, Eye) / 100.0f);
		}
		else if (DartPhase == 2 && Now >= DartPhaseUntil)
		{
			DartPhase = 3;
			DartPhaseUntil = Now + 1.0;
		}
		else if (DartPhase == 3 && Now >= DartPhaseUntil)
		{
			DartPhase = 0;
			if (bDartFromHunt)
			{
				bDartFromHunt = false;
				SetState(Mood == EJamFlockMood::Hunting ? EJamFlyerState::Circle : EJamFlyerState::Stalk, TEXT("backs off from its dart into the fight"));
			}
		}
		break;
	case EJamFlyerState::Stalk:
	{
		// Over the player, or (the search) over where the flock saw him last.
		FVector Centre = PlayerAt, Forward = ViewForward;
		const bool bSearch = SearchFrame(Centre, Forward);
		const FVector Behind = Centre + Up(Centre) * StalkHeightCm - Forward * StalkBehindCm;
		if (StalkLeg == 0 && (FVector::Dist(Location, Behind) < 300.0f || Now >= StateUntil))
		{
			StalkLeg = 1;
			StateUntil = Now + StalkLegSeconds;
			UE_LOG(LogMindKore, Log, TEXT("Flyer %s: stalk - from above and behind, passes over %s (%.1f m off)"), *GetName(),
				bSearch ? TEXT("where the flock saw the player last") : TEXT("the head"), FVector::Dist(Location, Centre) / 100.0f);
		}
		else if (StalkLeg == 1 && (FVector::DotProduct(Location - (bSearch ? Centre : Eye), Forward) > 600.0f || Now >= StateUntil))
		{
			if (Mood == EJamFlockMood::Hunting)
			{
				// Aggro needs a calm state: Stalk is one.
				Aggro(TEXT("announced: the shadow pass is over"));
			}
			else if (Mood == EJamFlockMood::Curious)
			{
				SetState(EJamFlyerState::Curious, TEXT("down the ladder: curious again"));
			}
			else
			{
				StalkLeg = 0;
				StateUntil = Now + StalkLegSeconds;
			}
		}
		break;
	}
	case EJamFlyerState::Circle:
		if (Now >= StateUntil && bSeesPlayer && !bRally)
		{
			if (Home->TakeDiveToken())
			{
				SetState(EJamFlyerState::Dive, FString::Printf(TEXT("dive from %.1f m with a token (%d/%d diving)"), Distance / 100.0f,
					Home->CountIn(EJamFlyerState::Dive) + 1, Home->MaxDivers));
				Screech(TEXT("dive"), true);
			}
			else if (Curiosity >= CuriousDartThreshold && FMath::FRand() < Curiosity * 0.5f && Home->TakeDart())
			{
				SetState(EJamFlyerState::Curious, FString::Printf(TEXT("curious (%.2f) mid-fight: darts in between the dives"), Curiosity));
				bDartFromHunt = true;
				DartPhase = 1;
				DartPhaseUntil = Now + 2.0;
			}
			else if (Home->MayFeint() && FMath::FRand() < FeintChance)
			{
				const FVector Side = FVector::CrossProduct(PlayerAt - Location, Up(PlayerAt)).GetSafeNormal(UE_SMALL_NUMBER, FVector::RightVector);
				FeintAim = Side * FeintMissCm * (FMath::RandBool() ? 1.0f : -1.0f) + Up(PlayerAt) * 100.0f;
				SetState(EJamFlyerState::Feint, FString::Printf(TEXT("no dive token (%d/%d diving): a feint pass from %.1f m"),
					Home->CountIn(EJamFlyerState::Dive), Home->MaxDivers, Distance / 100.0f));
			}
			else
			{
				StateUntil = Now + FMath::FRandRange(CircleAgainSeconds.X, CircleAgainSeconds.Y);
			}
		}
		break;
	case EJamFlyerState::Dive:
		if (Distance <= BiteReachCm)
		{
			Bite(Target);
			SetState(EJamFlyerState::Retreat, TEXT("bit, pulling away"));
		}
		else if (Now - StateSeconds > MaxDiveSeconds)
		{
			SetState(EJamFlyerState::Retreat, TEXT("missed: the dive timed out"));
		}
		// Overshot: it came close and now flies away (a dive that starts flying away still has the time-out).
		else if (Distance < OvershootCm && FVector::DotProduct(Velocity, PlayerAt - Location) < 0.0f)
		{
			SetState(EJamFlyerState::Retreat, FString::Printf(TEXT("missed: overshot by %.1f m"), Distance / 100.0f));
		}
		break;
	case EJamFlyerState::Feint:
		if (Distance < OvershootCm && FVector::DotProduct(Velocity, PlayerAt - Location) < 0.0f)
		{
			SetState(EJamFlyerState::Circle, FString::Printf(TEXT("the feint passed %.1f m off"), Distance / 100.0f));
		}
		else if (Now >= StateUntil)
		{
			SetState(EJamFlyerState::Circle, TEXT("the feint timed out"));
		}
		break;
	case EJamFlyerState::Retreat:
		if (Now >= StateUntil)
		{
			SetState(EJamFlyerState::Circle, TEXT("wheeling for another dive"));
		}
		break;
	default:
		break;
	}
}

FVector AJamFlyer::DesiredVelocity(const FVector& Location, const FVector& Velocity, float& OutAccel)
{
	const double Now = GetWorld()->GetTimeSeconds();
	const APawn* Target = Player();
	const AJamFlyerHabitat* Home = Habitat.Get();
	const FVector Forward = Velocity.GetSafeNormal(UE_SMALL_NUMBER, Visual->GetForwardVector());
	FVector Desired = FVector::ZeroVector;
	OutAccel = HuntAccelCm;
	FVector Eye = Location, ViewForward = Forward;
	if (Target)
	{
		PlayerView(Target, Eye, ViewForward);
	}
	switch (State)
	{
	case EJamFlyerState::Wander:
	{
		const bool bNoise = Now < NoiseUntil;
		if (Home && (Now - WanderPickedSeconds > WanderRetargetSeconds || FVector::Dist(Location, bNoise ? NoisePoint : WanderPoint) < WanderArriveCm))
		{
			const bool bSwoop = !bNoise && FMath::FRand() < SwoopChance;
			if (bNoise)
			{
				NoisePoint = NoiseCentre + FMath::VRand() * InvestigateSpreadCm; // searching around the noise
			}
			else
			{
				WanderPoint = Home->PickPoint(Location, Forward, bSwoop, FMath::FRandRange(WanderReachCm.X, WanderReachCm.Y), ShellClearanceCm);
			}
			WanderSpeedFactor = bSwoop ? SwoopSpeedScale : FMath::FRandRange(WanderSpeedScale.X, WanderSpeedScale.Y);
			WanderPickedSeconds = Now;
		}
		// Never static: always at cruise, turning at most MaxWanderTurnDeg off the flight at a time
		// (a target behind is reached in a wheeling arc, not by braking), bobbing along the up.
		const FVector ToPoint = ((bNoise ? NoisePoint : WanderPoint) - Location).GetSafeNormal(UE_SMALL_NUMBER, Forward);
		const float Angle = FMath::Acos(FMath::Clamp(float(FVector::DotProduct(Forward, ToPoint)), -1.0f, 1.0f));
		const float MaxTurn = FMath::DegreesToRadians(MaxWanderTurnDeg);
		const FVector Heading = Angle <= MaxTurn ? ToPoint : FQuat::Slerp(FQuat::Identity, FQuat::FindBetweenNormals(Forward, ToPoint), MaxTurn / Angle).RotateVector(Forward);
		const float Scale = bNoise ? InvestigateSpeedScale : WanderSpeedFactor * (Home && Home->IsRoost() ? SentrySpeedScale : 1.0f);
		Desired = Heading * CruiseSpeedCm * Scale + Up(Location) * WanderBobCm * FMath::Sin(float(Now) * 0.8f + Seed);
		OutAccel = WanderAccelCm;
		break;
	}
	case EJamFlyerState::Land:
	{
		const FJamRoostSpot* Spot = Home ? Home->GetSpot(RoostSpot) : nullptr;
		if (!Spot)
		{
			break;
		}
		// In from outside along its spike, then a slow last metre and the grab.
		const FVector Goal = RoostCentre(*Spot);
		const FVector Approach = Goal + RoostDirection(*Spot) * 500.0f;
		const float ToGoal = FVector::Dist(Location, Goal);
		Desired = ToGoal > 650.0f ? (Approach - Location).GetSafeNormal() * CruiseSpeedCm : (Goal - Location) * 2.0f;
		Desired = Desired.GetClampedToMaxSize(ToGoal > 650.0f ? CruiseSpeedCm : 500.0f);
		break;
	}
	case EJamFlyerState::Curious:
	{
		if (!Target)
		{
			break;
		}
		// The scanner: its own place at the side of the view, about face height, keeping pace.
		const float Side = FMath::Frac(Seed * 0.71f) < 0.5f ? -1.0f : 1.0f;
		const float Angle = Side * FMath::Lerp(CuriousAngleDeg.X, CuriousAngleDeg.Y, FMath::Frac(Seed * 0.53f));
		const float Reach = FMath::Lerp(CuriousDistanceCm.X, CuriousDistanceCm.Y, FMath::Frac(Seed * 0.29f));
		const FVector Axis = Up(Eye);
		const FVector Hover = Eye + FQuat(Axis, FMath::DegreesToRadians(Angle)).RotateVector(ViewForward) * Reach
			+ Axis * 40.0f * FMath::Sin(float(Now) * 0.7f + Seed);
		const FVector Pace = Target->GetVelocity();
		if (DartPhase == 1)
		{
			const FVector Stop = Eye + Target->GetViewRotation().Vector() * DartStopCm;
			Desired = (Stop - Location).GetSafeNormal() * DartSpeedCm * FMath::Clamp(float(FVector::Dist(Stop, Location)) / 250.0f, 0.25f, 1.0f) + Pace;
			OutAccel = HuntAccelCm * 1.5f;
		}
		else if (DartPhase == 2)
		{
			Desired = Pace; // dead still in front of the face
			OutAccel = HuntAccelCm * 3.0f;
		}
		else
		{
			Desired = ((Hover - Location) * 1.8f).GetClampedToMaxSize(CircleSpeedCm) + Pace;
		}
		break;
	}
	case EJamFlyerState::Stalk:
	{
		if (!Target)
		{
			break;
		}
		// Over the player (keeping his pace), or the search: over where the flock saw him last, each from its own side.
		FVector Centre = Target->GetActorLocation(), Pass = ViewForward;
		const bool bSearch = SearchFrame(Centre, Pass);
		const FVector Axis = Up(Centre);
		if (StalkLeg == 0)
		{
			const FVector Behind = Centre + Axis * StalkHeightCm - Pass * StalkBehindCm;
			Desired = (Behind - Location).GetSafeNormal() * CircleSpeedCm * FMath::Clamp(float(FVector::Dist(Behind, Location)) / 400.0f, 0.4f, 1.0f);
		}
		else
		{
			// Straight over the head to well ahead: the shadow pass.
			const FVector Ahead = Centre + Axis * StalkHeightCm * 0.6f + Pass * 1500.0f;
			Desired = (Ahead - Location).GetSafeNormal() * StalkPassSpeedCm;
		}
		Desired += bSearch ? FVector::ZeroVector : Target->GetVelocity();
		break;
	}
	case EJamFlyerState::Circle:
	{
		if (!Target)
		{
			break;
		}
		// Round the player, or round the wound while the flock gathers on a pain call.
		const FVector Centre = Home && Now < Home->RallyUntil ? Home->RallyPoint : Target->GetActorLocation();
		const FVector Axis = Up(Centre);
		const FVector Rel = Location - (Centre + Axis * CircleHeightCm);
		const float Height = FVector::DotProduct(Rel, Axis);
		FVector Radial = Rel - Axis * Height;
		const float RadialCm = Radial.Size();
		Radial = RadialCm > 1.0f ? Radial / RadialCm : FVector::CrossProduct(Axis, FVector::RightVector).GetSafeNormal(UE_SMALL_NUMBER, FVector::ForwardVector);
		const float Turn = FMath::Fmod(Seed, 2.0f) < 1.0f ? 1.0f : -1.0f; // each one wheels its own way round
		// The noose tightens the longer it wheels; each flyer on its own ring.
		const float Tighten = FMath::Clamp(float(Now - StateSeconds) / CircleTightenSeconds, 0.0f, 1.0f);
		const float Radius = FMath::Lerp(CircleRadiusCm, CircleTightRadiusCm, Tighten) * (1.0f + CircleRadiusJitter * (FMath::Frac(Seed * 0.61f) * 2.0f - 1.0f));
		Desired = FVector::CrossProduct(Axis, Radial) * Turn * CircleSpeedCm + Radial * (Radius - RadialCm) * CircleGain
			- Axis * Height * CircleGain + Axis * CircleBobCm * FMath::Sin(float(Now) * 2.0f + Seed);
		Desired = Desired.GetClampedToMaxSize(FMath::Max(CircleSpeedCm, CruiseSpeedCm * 1.5f));
		break;
	}
	case EJamFlyerState::Dive:
	{
		// At where the player will be when it arrives (at most DiveLeadSeconds ahead).
		const FVector To = Target->GetActorLocation() - Location;
		const float Lead = FMath::Min(To.Size() / FMath::Max(DiveSpeedCm, 1.0f), DiveLeadSeconds);
		DiveDirection = (To + Target->GetVelocity() * Lead).GetSafeNormal(UE_SMALL_NUMBER, Forward);
		Desired = DiveDirection * DiveSpeedCm;
		OutAccel = HuntAccelCm * DiveAccelScale;
		break;
	}
	case EJamFlyerState::Feint:
	{
		// Like a dive, but at a point beside the player: it passes close and whooshes by.
		const FVector Aim = Target->GetActorLocation() + FeintAim;
		Desired = (Aim - Location).GetSafeNormal(UE_SMALL_NUMBER, Forward) * FeintSpeedCm;
		OutAccel = HuntAccelCm * DiveAccelScale;
		break;
	}
	case EJamFlyerState::Retreat:
		Desired = ((Location - Target->GetActorLocation()).GetSafeNormal() + Up(Target->GetActorLocation()) * RetreatClimb).GetSafeNormal() * CruiseSpeedCm * RetreatSpeedScale;
		break;
	case EJamFlyerState::Flee:
		Desired = ((Location - Target->GetActorLocation()).GetSafeNormal() + Up(Location) * 0.6f).GetSafeNormal() * CruiseSpeedCm * FleeSpeedScale;
		break;
	case EJamFlyerState::Stagger:
		OutAccel = 0.0f; // nothing steers a tumble: the shove carries it
		return Velocity;
	default:
		break;
	}
	// Keep out of each other's wings (the dive alone ignores the flock).
	if (Home && State != EJamFlyerState::Dive && State != EJamFlyerState::Land)
	{
		for (const TWeakObjectPtr<AJamFlyer>& Weak : Home->GetFlock())
		{
			const AJamFlyer* Other = Weak.Get();
			if (Other && Other != this && Other->State != EJamFlyerState::Dead)
			{
				const FVector Away = Location - Other->GetActorLocation();
				const float Distance = Away.Size();
				if (Distance < SeparationCm && Distance > 1.0f)
				{
					Desired += Away / Distance * (SeparationCm - Distance) / SeparationCm * SeparationPushCm;
				}
			}
		}
	}
	// Never ask for more than a dive (the scanner keeps the player's pace on top).
	return Desired.GetClampedToMaxSize(DiveSpeedCm + (Target ? Target->GetVelocity().Size() : 0.0f));
}

FVector AJamFlyer::Avoid(const FVector& Location, const FVector& Desired) const
{
	// The last metres of a landing go to the hull on purpose.
	if (State == EJamFlyerState::Land)
	{
		const AJamFlyerHabitat* Home = Habitat.Get();
		const FJamRoostSpot* Spot = Home ? Home->GetSpot(RoostSpot) : nullptr;
		if (Spot && FVector::Dist(Location, RoostCentre(*Spot)) < 700.0f)
		{
			return Desired;
		}
	}
	const float Speed = FMath::Max(float(Desired.Size()), 100.0f);
	const FVector Direction = Desired.GetSafeNormal(UE_SMALL_NUMBER, Visual->GetForwardVector());
	FCollisionQueryParams Params(SCENE_QUERY_STAT(JamFlyerAvoid), /*bTraceComplex=*/false, this);
	FCollisionObjectQueryParams Objects;
	Objects.AddObjectTypesToQuery(ECC_WorldStatic);
	Objects.AddObjectTypesToQuery(ECC_WorldDynamic);
	Objects.AddObjectTypesToQuery(ECC_PhysicsBody);
	FHitResult Hit;
	if (!GetWorld()->SweepSingleByObjectType(Hit, Location, Location + Direction * (Speed * AvoidSeconds + AvoidExtraCm), FQuat::Identity,
		Objects, FCollisionShape::MakeSphere(AvoidRadiusCm), Params))
	{
		return Desired;
	}
	// A dive goes for the player even on the ground: only what is in front of the player is avoided.
	if (State == EJamFlyerState::Dive)
	{
		if (const APawn* Target = Player(); Target && Hit.Distance > FVector::Dist(Location, Target->GetActorLocation()) - 200.0f)
		{
			return Desired;
		}
	}
	const float Urgency = Hit.bStartPenetrating ? 1.0f : 1.0f - Hit.Time;
	return (Direction + FVector(Hit.ImpactNormal) * 2.0f * Urgency).GetSafeNormal(UE_SMALL_NUMBER, Direction) * Speed;
}

void AJamFlyer::Bite(APawn* Target)
{
	UPrimitiveComponent* TargetBody = Cast<UPrimitiveComponent>(Target->GetRootComponent());
	const FVector Location = GetActorLocation();
	const FVector Direction = (Target->GetActorLocation() - Location).GetSafeNormal(UE_SMALL_NUMBER, DiveDirection);
	// The blow goes through the impact system like a round: damage, and the shove as its physics impulse.
	FHitResult Hit(Target, TargetBody, Target->GetActorLocation() - Direction * 40.0f, -Direction);
	Hit.bBlockingHit = true;
	FMKImpactResolveParams Params;
	FMKImpactFeedback Feedback;
	FMKResolvedImpact Resolved = MKResolveImpactFromHit(Hit, Direction, Params, Feedback);
	Resolved.ShotId = FGuid::NewGuid();
	Resolved.Kind = EMKImpactKind::Blunt;
	Resolved.SourceActor = this;
	Resolved.ImpactTimeSeconds = GetWorld()->GetTimeSeconds();
	Resolved.RangeCm = FVector::Dist(Location, Target->GetActorLocation());
	Resolved.DamageAmount = BiteDamage;
	Resolved.ImpulseNewtonSeconds = Direction * BiteImpulse / 100.0f;
	Resolved.bHasPhysicsImpulse = true;
	const float SpeedBefore = Target->GetVelocity().Size();
	const float OwnSpeed = Body->GetPhysicsLinearVelocity().Size();
	const bool bDelivered = MKDeliverResolvedImpact(this, this, Resolved);
	++BiteCount;
	const FJamSoundPlay BitePlay = JamPlaySoundEvent(this, BiteSound, Hit.ImpactPoint, nullptr, ImpactAttenuation, ListenerInAir(), AirReverb);
	UE_LOG(LogMindKore, Log, TEXT("Flyer %s: bite sound - %s"), *GetName(), *BitePlay.Describe());
	// On the player's side: the camera jolts and blood hits the visor.
	if (AJamZeroGPawn* Pawn = Cast<AJamZeroGPawn>(Target))
	{
		Pawn->KickCamera(BiteShakeCm);
		FVector2D Blot(-1.0, -1.0);
		if (BiteVisorBlood > 0.0f && Target->IsPlayerControlled())
		{
			Blot = UJamVisorBloodComponent::FindOrAdd(Pawn)->Splash(BiteVisorBlood);
		}
		UE_LOG(LogMindKore, Log, TEXT("Flyer %s: bite feedback - camera shake %.0f cm, visor blood %.2f at (%.2f, %.2f)"), *GetName(), BiteShakeCm,
			Blot.X >= 0.0 ? BiteVisorBlood : 0.0f, Blot.X, Blot.Y);
	}
	// It bounces off what it bit and climbs away.
	Body->SetPhysicsLinearVelocity(-Direction * BiteBounceBackCm + Up(Location) * BiteBounceUpCm);
	UE_LOG(LogMindKore, Log, TEXT("Flyer %s: bite %d on %s at %.0f cm/s - damage %.0f, shove %.0f kg cm/s, %s; the player moved %.0f cm/s before"),
		*GetName(), BiteCount, *GetNameSafe(Target), OwnSpeed, BiteDamage, BiteImpulse, bDelivered ? TEXT("delivered") : TEXT("no receiver"), SpeedBefore);
}

void AJamFlyer::ReceiveResolvedImpact_Implementation(const FMKResolvedImpact& ResolvedImpact)
{
	const FVector Push = ResolvedImpact.bHasPhysicsImpulse ? ResolvedImpact.ImpulseNewtonSeconds * 100.0 : FVector::ZeroVector;
	// The gun already pushes a simulated body it hits (UJamWeaponsComponent::TraceShot): only a hit on a mesh part needs it here.
	const UPrimitiveComponent* HitComponent = ResolvedImpact.Hit.GetComponent();
	const bool bPushHere = !Push.IsNearlyZero() && !(HitComponent && HitComponent->IsSimulatingPhysics());
	// The thwack: one per source and HitWindowSeconds (a blast's pellets, before and after the death), at the first round.
	const TWeakObjectPtr<AActor> Source = ResolvedImpact.SourceActor;
	if (FPendingHit* Pending = PendingHits.FindByPredicate([&Source](const FPendingHit& Each) { return Each.Source == Source; }))
	{
		Pending->Damage += ResolvedImpact.DamageAmount;
		++Pending->Rounds;
	}
	else
	{
		PendingHits.Add({ Source, FVector(ResolvedImpact.Hit.ImpactPoint), ResolvedImpact.DamageAmount, 1 });
		if (HitWindowSeconds > 0.0f)
		{
			FTimerHandle Handle;
			GetWorldTimerManager().SetTimer(Handle, FTimerDelegate::CreateUObject(this, &AJamFlyer::PlayHit, Source), HitWindowSeconds, false);
		}
		else
		{
			PlayHit(Source);
		}
	}
	if (State == EJamFlyerState::Dead)
	{
		if (bPushHere)
		{
			Body->AddImpulseAtLocation(Push, ResolvedImpact.Hit.ImpactPoint);
		}
		return;
	}
	if (bPushHere)
	{
		Body->AddImpulse(Push);
	}
	Health -= ResolvedImpact.DamageAmount;
	UE_LOG(LogMindKore, Log, TEXT("Flyer %s: hit in %s (%s) by %s - damage %.1f, health %.0f/%.0f"), *GetName(), *GetNameSafe(HitComponent),
		*UEnum::GetValueAsString(ResolvedImpact.SurfaceKind), *GetNameSafe(ResolvedImpact.SourceActor.Get()), ResolvedImpact.DamageAmount,
		FMath::Max(Health, 0.0f), MaxHealth);
	if (Health <= 0.0f)
	{
		Die(Push);
		// A flockmate killed: the flock hunts (the cascade starts from the nearest one alive).
		if (AJamFlyerHabitat* Home = Habitat.Get())
		{
			Home->Provoke(EJamFlockMood::Hunting, FString::Printf(TEXT("%s was killed"), *GetName()), GetActorLocation(), nullptr);
		}
		return;
	}
	// A hit is the strongest provocation (the cascade starts from it), and it knocks it out of its flight.
	const FVector Along = !ResolvedImpact.ShotDirection.IsNearlyZero() ? FVector(ResolvedImpact.ShotDirection)
		: !Push.IsNearlyZero() ? Push.GetSafeNormal() : -FVector(ResolvedImpact.Hit.ImpactNormal);
	Stagger(Along);
	if (AJamFlyerHabitat* Home = Habitat.Get())
	{
		Home->Provoke(EJamFlockMood::Hunting, FString::Printf(TEXT("%s was shot"), *GetName()), GetActorLocation(), this);
	}
}

void AJamFlyer::PlayHit(TWeakObjectPtr<AActor> Source)
{
	const int32 Index = PendingHits.IndexOfByPredicate([&Source](const FPendingHit& Each) { return Each.Source == Source; });
	if (Index == INDEX_NONE)
	{
		return;
	}
	const FPendingHit Hit = PendingHits[Index];
	PendingHits.RemoveAt(Index);
	// Louder with the damage: a pistol round (25) at 1, a blast's 8 pellets (96) at the cap.
	const FJamSoundPlay HitPlay = JamPlaySoundEvent(this, HitSound, Hit.Point, nullptr, ImpactAttenuation, ListenerInAir(), AirReverb,
		FMath::Clamp(Hit.Damage / 25.0f, 0.6f, 1.6f));
	UE_LOG(LogMindKore, Log, TEXT("Flyer %s: hit sound - %d round(s), %.0f damage from %s within %.0f ms: %s"), *GetName(), Hit.Rounds, Hit.Damage,
		*GetNameSafe(Source.Get()), HitWindowSeconds * 1000.0f, *HitPlay.Describe());
}

void AJamFlyer::Die(const FVector& Push)
{
	SetState(EJamFlyerState::Dead, FString::Printf(TEXT("killed, drifting at %.0f cm/s"), Body->GetPhysicsLinearVelocity().Size()));
	DiedSeconds = GetWorld()->GetTimeSeconds();
	// The death cry plays in VoicePlay but is no screech: it leaves its slot (TakeScreechSlot counts
	// a playing voice as a screech and would keep it, or cut the death cry for a farther screech).
	Screamers.RemoveAll([this](const TWeakObjectPtr<AJamFlyer>& Weak) { return Weak.Get() == this; });
	VoicePlay.Stop(0.05f);
	if (Flutter)
	{
		Flutter->FadeOut(0.2f, 0.0f);
	}
	VoicePlay = JamPlaySoundEvent(this, DeathSound, FVector::ZeroVector, Torso, VoiceAttenuation, ListenerInAir(), AirReverb);
	bVoiceDoppler = false;
	UE_LOG(LogMindKore, Log, TEXT("Flyer %s: death cry - %s"), *GetName(), *VoicePlay.Describe());
	// A corpse: free to tumble, solid to the player, limp.
	Body->BodyInstance.SetDOFLock(EDOFMode::None);
	Body->SetCollisionResponseToChannel(ECC_Pawn, ECR_Block);
	Body->SetPhysicsAngularVelocityInDegrees(FMath::VRand() * FMath::FRandRange(60.0f, 180.0f) + Push.GetSafeNormal() * 90.0f);
	WingR->SetRelativeRotation(WingTurn(1.0f, -30.0f, 25.0f));
	WingL->SetRelativeRotation(WingTurn(-1.0f, -20.0f, 35.0f));
	Tail->SetRelativeRotation(FRotator(-10.0f, 25.0f, 0.0f));
	TailTip->SetRelativeRotation(FRotator(-15.0f, 40.0f, 0.0f));
	Torso->SetRelativeLocation(FVector::ZeroVector);
}

void AJamFlyer::Animate(float DeltaSeconds, const FVector& Velocity, const FVector& Accel)
{
	const float Now = float(GetWorld()->GetTimeSeconds());
	const FVector Location = Body->GetComponentLocation();
	const AJamFlyerHabitat* Home = Habitat.Get();
	// Clinging: the folded pose; the blend eases in on a landing (the settle) and out on the unfold.
	const bool bClinging = State == EJamFlyerState::Roost;
	RoostBlend = FMath::FInterpConstantTo(RoostBlend, bClinging ? 1.0f : 0.0f, DeltaSeconds, bClinging ? 1.0f / 0.4f : 1.0f / 0.25f);
	const FJamRoostSpot* Spot = bClinging && Home ? Home->GetSpot(RoostSpot) : nullptr;

	// Turn to the flight and bank into the turn: "lift" is the habitat's up plus the sideways pull.
	const float Speed = Velocity.Size();
	FVector Forward = Speed > 50.0f ? Velocity / Speed : Visual->GetForwardVector();
	// The scanner and the stalker look at the player, not where they fly.
	if ((State == EJamFlyerState::Curious || (State == EJamFlyerState::Stalk && StalkLeg == 0)) && Player())
	{
		Forward = (EyeOf(Player()) - Location).GetSafeNormal(UE_SMALL_NUMBER, Forward);
	}
	SmoothedAccel = FMath::VInterpTo(SmoothedAccel, Accel, DeltaSeconds, 4.0f);
	const FVector Lateral = SmoothedAccel - Forward * FVector::DotProduct(SmoothedAccel, Forward);
	const FVector Lift = Up(Location) * 980.0f + Lateral.GetClampedToMaxSize(2500.0f);
	FQuat Target = FRotationMatrix::MakeFromXZ(Forward, Lift).ToQuat();
	if (State == EJamFlyerState::Stagger)
	{
		// The tumble: rolling over and over, the nose wobbling.
		TumbleDeg += TumbleSign * StaggerSpinDeg * DeltaSeconds;
		Target = Target * FQuat(FVector::ForwardVector, FMath::DegreesToRadians(TumbleDeg)) * FQuat(FVector::RightVector, FMath::DegreesToRadians(25.0f * FMath::Sin(Now * 23.0f)));
	}

	// Wings: glide and flap while wandering, hard beats when hunting, tucked back in a dive, flailing in a tumble.
	float Rate = 1.0f, Amplitude = FlapAmplitudeDeg, Sweep = 0.0f, Dihedral = 8.0f, LeftLag = 0.0f;
	switch (State)
	{
	case EJamFlyerState::Wander:
		Amplitude *= 0.25f + 0.75f * FMath::Clamp(0.6f + 0.8f * FMath::Sin(Now * 0.45f + Seed), 0.0f, 1.0f);
		break;
	case EJamFlyerState::Curious:
		// Hovering: quick shallow beats; holding still in front of a face, quicker still.
		Rate = DartPhase == 2 ? 2.4f : 1.7f;
		Amplitude *= DartPhase == 2 ? 0.8f : 0.55f;
		Dihedral = 14.0f;
		break;
	case EJamFlyerState::Stalk:
		// A silent glide.
		Rate = 0.8f;
		Amplitude *= 0.3f;
		Sweep = 15.0f;
		break;
	case EJamFlyerState::Circle:
		Rate = 1.2f;
		break;
	case EJamFlyerState::Dive:
		Rate = 0.8f;
		Amplitude = 6.0f;
		Sweep = 45.0f;
		Dihedral = -4.0f;
		break;
	case EJamFlyerState::Feint:
		Rate = 0.9f;
		Amplitude = 10.0f;
		Sweep = 30.0f;
		Dihedral = 0.0f;
		break;
	case EJamFlyerState::Retreat:
		Rate = 1.6f;
		Amplitude *= 1.15f;
		break;
	case EJamFlyerState::Stagger:
		Rate = 3.2f;
		Amplitude *= 1.5f;
		LeftLag = 2.2f; // out of step: flailing, not flying
		break;
	case EJamFlyerState::Flee:
		Rate = 1.9f;
		Amplitude *= 1.2f;
		break;
	case EJamFlyerState::Land:
		Rate = 1.4f;
		Amplitude *= 0.8f;
		break;
	default:
		break;
	}
	if (RoostBlend < 1.0f)
	{
		const float LastPhase = FlapPhase;
		FlapPhase = FMath::Fmod(FlapPhase + DeltaSeconds * UE_TWO_PI * FlapHz * Rate, UE_TWO_PI);
		// The beat is heard mid-downstroke (the wing passes level going down: phase pi).
		if (LastPhase < UE_PI && FlapPhase >= UE_PI)
		{
			PlayFlap(Amplitude / FMath::Max(FlapAmplitudeDeg, 1.0f) * (1.0f - RoostBlend), Rate);
		}
	}
	const float FlapR = Dihedral + Amplitude * FMath::Sin(FlapPhase);
	const float FlapL = Dihedral + Amplitude * FMath::Sin(FlapPhase - LeftLag);
	FQuat WingRQuat = WingTurn(1.0f, FlapR, Sweep);
	FQuat WingLQuat = WingTurn(-1.0f, FlapL, Sweep);
	// The body heaves against the beat.
	FVector TorsoAt(0.0f, 0.0f, -0.12f * Amplitude * FMath::Cos(FlapPhase));
	// The tail swings, the tip a beat later, and trails out of a turn.
	const float Turn = FMath::Clamp(FVector::DotProduct(Lateral, Visual->GetRightVector()) / 1500.0f, -1.0f, 1.0f);
	FQuat TailQuat = FRotator(5.0f * FMath::Sin(Now * 1.1f + Seed), 10.0f * FMath::Sin(Now * 1.7f + Seed) - 15.0f * Turn, 0.0f).Quaternion();
	FQuat TipQuat = FRotator(8.0f * FMath::Sin(Now * 1.1f - 0.8f + Seed), 18.0f * FMath::Sin(Now * 1.7f - 1.1f + Seed) - 10.0f * Turn, 0.0f).Quaternion();

	if (Spot && RoostBlend > 0.0f)
	{
		// The spike: head out along its lean (or turned to the player near), its back to its side; the idle twitch.
		FVector Out = StareDirection.IsNearlyZero() ? RoostDirection(*Spot) : StareDirection;
		const float Twitch = TwitchLength > 0.0f && Now - TwitchStart < TwitchLength ? FMath::Sin(UE_PI * float(Now - TwitchStart) / TwitchLength) : 0.0f;
		float Shiver = 0.0f, Breath = 0.0f;
		if (TwitchKind == 1)
		{
			Out = FQuat(TwitchAxis, FMath::DegreesToRadians(18.0f) * Twitch).RotateVector(Out); // a head turn
		}
		else if (TwitchKind == 0)
		{
			Shiver = 7.0f * Twitch * FMath::Sin(Now * UE_TWO_PI * 9.0f); // a wing shiver
		}
		else
		{
			Breath = Twitch; // a slow breath
		}
		const FQuat RoostQuat = FRotationMatrix::MakeFromXZ(Out, Spot->Side).ToQuat();
		const float Fold = RoostWingFlapDeg + 4.0f * Breath;
		const float Side = TwitchAxis.X >= 0.0f ? 1.0f : 0.0f; // the shiver takes one wing
		WingRQuat = FQuat::Slerp(WingRQuat, WingTurn(1.0f, Fold + Shiver * Side, RoostWingSweepDeg), RoostBlend);
		WingLQuat = FQuat::Slerp(WingLQuat, WingTurn(-1.0f, Fold + Shiver * (1.0f - Side), RoostWingSweepDeg), RoostBlend);
		TailQuat = FQuat::Slerp(TailQuat, FRotator(RoostTailCurlDeg, 6.0f * FMath::Sin(Seed), 0.0f).Quaternion(), RoostBlend);
		TipQuat = FQuat::Slerp(TipQuat, FRotator(RoostTailCurlDeg * 0.5f, 10.0f * FMath::Sin(Seed * 1.7f), 0.0f).Quaternion(), RoostBlend);
		TorsoAt = FMath::Lerp(TorsoAt, FVector(2.5f * Breath, 0.0f, 0.0f), RoostBlend);
		Target = FQuat::Slerp(Target, RoostQuat, RoostBlend);
	}
	Visual->SetWorldRotation(RoostBlend >= 1.0f ? Target : FQuat::Slerp(Visual->GetComponentQuat(), Target, 1.0f - FMath::Exp(-(State == EJamFlyerState::Stagger ? 20.0f : 6.0f) * DeltaSeconds)));
	WingR->SetRelativeRotation(WingRQuat);
	WingL->SetRelativeRotation(WingLQuat);
	Torso->SetRelativeLocation(TorsoAt);
	Tail->SetRelativeRotation(TailQuat);
	TailTip->SetRelativeRotation(TipQuat);
}

void AJamFlyer::FallAsCorpse(float DeltaSeconds)
{
	// The zones' pull (JamPullBodies) skips a body with gravity off - the flyer's, which flies
	// itself while alive. A corpse is a loose body like any other: the zones pull it, and once
	// it rests on the ground they put it to sleep until a hit or a grab moves it.
	if (!Body->IsGravityEnabled())
	{
		Body->SetEnableGravity(true);
	}
	const FVector Location = Body->GetComponentLocation();
	const FJamGravity Gravity = JamGravityAt(GetWorld(), Location);
	const AJamGravityZone* Zone = Gravity.Zone;
	if (!Zone)
	{
		return; // zero-G: it drifts
	}
	const FVector Down = Gravity.Acceleration.GetSafeNormal();
	// The body is a sphere: without losses it rolls down the planetoid's slopes for ever. A carcass drags.
	if (Body->GetAngularDamping() < CorpseAngularDamping)
	{
		Body->SetLinearDamping(CorpseLinearDamping);
		Body->SetAngularDamping(CorpseAngularDamping);
	}
	// Once: on the ground (rolling or not).
	if (!bLanded && GetWorld()->GetTimeSeconds() - DiedSeconds > 0.5)
	{
		FHitResult Ground;
		FCollisionQueryParams Params(SCENE_QUERY_STAT(JamFlyerGround), false, this);
		if (GetWorld()->LineTraceSingleByChannel(Ground, Location, Location + Down * (Body->GetScaledSphereRadius() + 10.0f), ECC_Visibility, Params))
		{
			bLanded = true;
			// The body is a sphere: free to turn it rolls down any slope for ever. Down, it stops turning
			// and slides to a stop on friction - then the zones put it to sleep.
			Body->SetPhysicsAngularVelocityInDegrees(FVector::ZeroVector);
			Body->BodyInstance.SetDOFLock(EDOFMode::SixDOF);
			UE_LOG(LogMindKore, Log, TEXT("Flyer %s: corpse landed on %s in %s, %.1f s after death, %.0f cm above the ground"), *GetName(),
				*GetNameSafe(Ground.GetActor()), *GetNameSafe(Zone), float(GetWorld()->GetTimeSeconds() - DiedSeconds), Ground.Distance);
		}
	}
}

void AJamFlyer::Tick(float DeltaSeconds)
{
	const double Started = FPlatformTime::Seconds();
	Super::Tick(DeltaSeconds);
	if (State == EJamFlyerState::Dead)
	{
		FallAsCorpse(DeltaSeconds);
		return;
	}
	if (State == EJamFlyerState::Roost)
	{
		ThinkRoost(DeltaSeconds);
		if (State == EJamFlyerState::Roost)
		{
			Animate(DeltaSeconds, FVector::ZeroVector, FVector::ZeroVector);
		}
		TickSecondsRoost += FPlatformTime::Seconds() - Started;
		++TicksRoost;
		return;
	}
	const double Now = GetWorld()->GetTimeSeconds();
	if (Now >= NextSenseSeconds)
	{
		Sense();
		NextSenseSeconds = Now + SenseSeconds;
	}
	const FVector Location = Body->GetComponentLocation();
	const FVector Velocity = Body->GetPhysicsLinearVelocity();
	Think(Location, Velocity);
	if (State == EJamFlyerState::Dead || State == EJamFlyerState::Roost)
	{
		return;
	}
	float MaxAccel = 0.0f;
	FVector Desired = DesiredVelocity(Location, Velocity, MaxAccel);
	if (State != EJamFlyerState::Stagger)
	{
		Desired = Avoid(Location, Desired);
	}
	const FVector Accel = ((Desired - Velocity) / SteerSeconds).GetClampedToMaxSize(MaxAccel);
	Body->AddForce(Accel, NAME_None, /*bAccelChange=*/true);
	Animate(DeltaSeconds, Velocity, Accel);
	UpdateSounds(DeltaSeconds);
	TickSecondsOther += FPlatformTime::Seconds() - Started;
	++TicksOther;
}

// --- console probes ------------------------------------------------------------------------------

namespace JamFlyerProbe
{
	void After(UWorld* World, float Seconds, TFunction<void()> Step)
	{
		FTimerHandle Handle;
		World->GetTimerManager().SetTimer(Handle, FTimerDelegate::CreateLambda(MoveTemp(Step)), Seconds, false);
	}

	/** Every Interval until Done() is true (then Next) or Timeout passes (then Next too, logged). */
	void Poll(UWorld* World, float Interval, float Timeout, TFunction<bool()> Done, TFunction<void(bool)> Next)
	{
		if (Done())
		{
			Next(true);
			return;
		}
		if (Timeout <= 0.0f)
		{
			Next(false);
			return;
		}
		After(World, Interval, [World, Interval, Timeout, Done, Next]() { Poll(World, Interval, Timeout - Interval, Done, Next); });
	}

	void Teleport(AActor* Actor, const FVector& Location)
	{
		if (UPrimitiveComponent* Root = Cast<UPrimitiveComponent>(Actor->GetRootComponent()); Root && Root->IsSimulatingPhysics())
		{
			Root->SetWorldLocation(Location, false, nullptr, ETeleportType::TeleportPhysics);
			Root->SetPhysicsLinearVelocity(FVector::ZeroVector);
			return;
		}
		Actor->SetActorLocation(Location, false, nullptr, ETeleportType::TeleportPhysics);
	}

	AJamZeroGPawn* ProbePawn(UWorld* World)
	{
		const APlayerController* Controller = World->GetFirstPlayerController();
		return Controller ? Cast<AJamZeroGPawn>(Controller->GetPawn()) : nullptr;
	}

	void LogFlyer(const TCHAR* Stage, const AJamFlyer* Flyer, const AJamFlyerHabitat* Home)
	{
		const AJamZeroGPawn* Pawn = Flyer ? ProbePawn(Flyer->GetWorld()) : nullptr;
		if (!Flyer || !Pawn)
		{
			UE_LOG(LogMindKore, Warning, TEXT("Flyer probe [%s]: the flyer or the pawn is gone"), Stage);
			return;
		}
		UE_LOG(LogMindKore, Log, TEXT("Flyer probe [%s]: %s %s, %.0f cm/s, spin %.0f deg/s, %.1f m from home, %.1f m from the player; player %.0f cm/s, HP %.0f, %.1f m from home"),
			Stage, *Flyer->GetName(), AJamFlyer::StateName(Flyer->GetState()), Flyer->GetVelocity().Size(),
			Flyer->Body->GetPhysicsAngularVelocityInDegrees().Size(), FVector::Dist(Flyer->GetActorLocation(), Home->GetActorLocation()) / 100.0f,
			FVector::Dist(Flyer->GetActorLocation(), Pawn->GetActorLocation()) / 100.0f, Pawn->GetBodyVelocity().Size(), Pawn->Vitals->Health,
			FVector::Dist(Pawn->GetActorLocation(), Home->GetActorLocation()) / 100.0f);
	}

	/** A point 10-12 m ahead of the flyer, inside its habitat and clear of a planetoid; false while it flies outward. */
	bool InFront(const AJamFlyer* Flyer, const AJamFlyerHabitat* Home, float AheadCm, FVector& Out)
	{
		const FVector Forward = Flyer->Visual->GetForwardVector();
		Out = Flyer->GetActorLocation() + Forward * AheadCm;
		const float FromCentre = FVector::Dist(Out, Home->GetActorLocation());
		return FromCentre < Home->GetOuterRadius() && FromCentre > Home->InnerRadiusCm + 300.0f;
	}

	struct FProbe
	{
		TWeakObjectPtr<AJamFlyer> Flyer;
		TWeakObjectPtr<AJamFlyerHabitat> Home;
		TArray<TWeakObjectPtr<AJamFlyer>> Frozen;
		FVector Far = FVector::ZeroVector;
	};

	void ProbeEnd(UWorld* World, TSharedRef<FProbe> P)
	{
		for (const TWeakObjectPtr<AJamFlyer>& Weak : P->Frozen)
		{
			if (AJamFlyer* Other = Weak.Get())
			{
				Other->SetActorTickEnabled(true);
			}
		}
		JamFlyerParts::CVarLogFlaps->Set(false, ECVF_SetByCode);
		UE_LOG(LogMindKore, Log, TEXT("Flyer probe: done"));
	}

	/** Pistol shots every 0.3 s at where the flyer is now, until it dies (20 at most); then the corpse. */
	void ProbeShoot(UWorld* World, TSharedRef<FProbe> P)
	{
		for (int32 Shot = 0; Shot < 20; ++Shot)
		{
			After(World, 0.01f + Shot * 0.3f, [World, P, Shot]() // a timer of 0 s never fires
			{
				AJamZeroGPawn* Now = ProbePawn(World);
				AJamFlyer* Target = P->Flyer.Get();
				if (!Now || !Target || Target->GetState() == EJamFlyerState::Dead)
				{
					return;
				}
				Now->GetController()->SetControlRotation((Target->GetActorLocation() - Now->Camera->GetComponentLocation()).Rotation());
				const bool bFired = Now->Fire();
				UE_LOG(LogMindKore, Log, TEXT("Flyer probe [shoot]: shot %d %s at %.1f m, flyer %s, health %.0f"), Shot + 1, bFired ? TEXT("fired") : TEXT("not fired"),
					FVector::Dist(Target->GetActorLocation(), Now->GetActorLocation()) / 100.0f, AJamFlyer::StateName(Target->GetState()), Target->GetHealth());
			});
		}
		Poll(World, 0.3f, 8.0f, [P]() { return !P->Flyer.IsValid() || P->Flyer->GetState() == EJamFlyerState::Dead; }, [World, P](bool bDead)
		{
			if (!bDead)
			{
				UE_LOG(LogMindKore, Warning, TEXT("Flyer probe [shoot]: still alive after the shots"));
				ProbeEnd(World, P);
				return;
			}
			// Around a planetoid the corpse falls to it (its "landed" line is the flyer's own); elsewhere it drifts.
			const bool bShell = P->Home.IsValid() && P->Home->InnerRadiusCm > 0.0f;
			const int32 Seconds = bShell ? 15 : 4;
			for (int32 Step = 0; Step <= Seconds; ++Step)
			{
				After(World, Step * 1.0f, [P, Step]() { LogFlyer(*FString::Printf(TEXT("corpse +%d s"), Step), P->Flyer.Get(), P->Home.Get()); });
			}
			After(World, Seconds + 0.5f, [World, P]() { ProbeEnd(World, P); });
		});
	}

	void ProbeKill(UWorld* World, TSharedRef<FProbe> P)
	{
		AJamZeroGPawn* Pawn = ProbePawn(World);
		if (!Pawn || !P->Flyer.IsValid() || !P->Home.IsValid())
		{
			return;
		}
		if (!Pawn->Guns->IsWeaponUnlocked(0))
		{
			Pawn->Guns->UnlockWeapon(0);
		}
		Pawn->Guns->SelectWeapon(0);
		const AJamFlyerHabitat* Home = P->Home.Get();
		if (Home->InnerRadiusCm > 0.0f)
		{
			// Around a planetoid: the player on the ground under it, so it dies diving low, inside the planetoid's gravity.
			const FVector Centre = Home->GetActorLocation();
			const FVector Spot = Centre + (P->Flyer->GetActorLocation() - Centre).GetSafeNormal() * (Home->InnerRadiusCm - 200.0f);
			Teleport(Pawn, Spot);
			P->Home->Provoke(EJamFlockMood::Hunting, TEXT("probe: the player on the ground below"), P->Flyer->GetActorLocation(), P->Flyer.Get());
			UE_LOG(LogMindKore, Log, TEXT("Flyer probe [shoot]: the player dropped on the planetoid under %s, %.1f m from the centre, %s"),
				*P->Flyer->GetName(), FVector::Dist(Spot, Centre) / 100.0f, *Pawn->Guns->Describe());
			Poll(World, 0.05f, 15.0f, [World, P]()
			{
				const AJamZeroGPawn* Now = ProbePawn(World);
				return !P->Flyer.IsValid() || !Now
					|| (P->Flyer->GetState() == EJamFlyerState::Dive && FVector::Dist(P->Flyer->GetActorLocation(), Now->GetActorLocation()) < 900.0f);
			}, [World, P](bool bDiving)
			{
				LogFlyer(bDiving ? TEXT("shoot: it dives at the player") : TEXT("shoot: no dive in 15 s, shooting anyway"), P->Flyer.Get(), P->Home.Get());
				ProbeShoot(World, P);
			});
			return;
		}
		Poll(World, 0.2f, 10.0f, [P]()
		{
			FVector Spot;
			return P->Flyer.IsValid() && P->Home.IsValid() && InFront(P->Flyer.Get(), P->Home.Get(), 1200.0f, Spot);
		}, [World, P](bool bOk)
		{
			AJamZeroGPawn* Now = ProbePawn(World);
			AJamFlyer* Flyer = P->Flyer.Get();
			if (!Now || !Flyer)
			{
				return;
			}
			FVector Spot;
			InFront(Flyer, P->Home.Get(), 1200.0f, Spot);
			Teleport(Now, Spot);
			UE_LOG(LogMindKore, Log, TEXT("Flyer probe [shoot]: the player 12 m ahead of %s (%s), the pistol out: %s"), *Flyer->GetName(),
				bOk ? TEXT("it flew inward") : TEXT("placed anyway after 10 s"), *Now->Guns->Describe());
			After(World, 0.3f, [World, P]() { ProbeShoot(World, P); });
		});
	}

	void ProbeLeave(UWorld* World, TSharedRef<FProbe> P)
	{
		AJamZeroGPawn* Pawn = ProbePawn(World);
		if (!Pawn)
		{
			return;
		}
		Teleport(Pawn, P->Far);
		UE_LOG(LogMindKore, Log, TEXT("Flyer probe [leave]: the player moved %.1f m from the habitat's centre (radius %.0f m + leash %.0f m)"),
			FVector::Dist(P->Far, P->Home->GetActorLocation()) / 100.0f, P->Home->GetOuterRadius() / 100.0f, P->Home->LeashCm / 100.0f);
		Poll(World, 0.1f, 3.0f, [P]() { return !P->Flyer.IsValid() || P->Flyer->GetState() == EJamFlyerState::Wander; }, [World, P](bool bCalm)
		{
			LogFlyer(bCalm ? TEXT("leave: back to wandering") : TEXT("leave: STILL HUNTING after 3 s"), P->Flyer.Get(), P->Home.Get());
			After(World, 2.0f, [World, P]() { LogFlyer(TEXT("leave +2 s"), P->Flyer.Get(), P->Home.Get()); ProbeKill(World, P); });
		});
	}

	/** Behind the planetoid (a shell habitat only): the far side's surface, inside the leash, out of sight. */
	void ProbeHide(UWorld* World, TSharedRef<FProbe> P)
	{
		AJamZeroGPawn* Pawn = ProbePawn(World);
		const AJamFlyer* Flyer = P->Flyer.Get();
		const AJamFlyerHabitat* Home = P->Home.Get();
		if (!Pawn || !Flyer || !Home || Home->InnerRadiusCm <= 0.0f)
		{
			UE_LOG(LogMindKore, Log, TEXT("Flyer probe [hide]: skipped - no planetoid in this habitat to hide behind (lost sight is not probed here)"));
			ProbeLeave(World, P);
			return;
		}
		const FVector Centre = Home->GetActorLocation();
		const FVector Spot = Centre - (Flyer->GetActorLocation() - Centre).GetSafeNormal() * (Home->InnerRadiusCm - 200.0f);
		Teleport(Pawn, Spot);
		UE_LOG(LogMindKore, Log, TEXT("Flyer probe [hide]: the player on the far side of the planetoid, %.1f m from the centre (inside the leash)"), FVector::Dist(Spot, Centre) / 100.0f);
		Poll(World, 0.25f, 12.0f, [P]() { return !P->Flyer.IsValid() || P->Flyer->GetState() == EJamFlyerState::Wander; }, [World, P](bool bCalm)
		{
			LogFlyer(bCalm ? TEXT("hide: back to wandering") : TEXT("hide: STILL HUNTING after 12 s"), P->Flyer.Get(), P->Home.Get());
			// Find it again before the leash step: in sight of it, so it hunts once more.
			After(World, 3.5f, [World, P]()
			{
				Poll(World, 0.2f, 10.0f, [P]()
				{
					FVector Spot;
					return P->Flyer.IsValid() && InFront(P->Flyer.Get(), P->Home.Get(), 1000.0f, Spot);
				}, [World, P](bool)
				{
					FVector Spot;
					if (AJamZeroGPawn* Now = ProbePawn(World); Now && P->Flyer.IsValid())
					{
						InFront(P->Flyer.Get(), P->Home.Get(), 1000.0f, Spot);
						Teleport(Now, Spot);
					}
					Poll(World, 0.1f, 3.0f, [P]() { return !P->Flyer.IsValid() || P->Flyer->IsTracking(); }, [World, P](bool bHunting)
					{
						LogFlyer(bHunting ? TEXT("hide: noticed the player again") : TEXT("hide: did not notice the player again"), P->Flyer.Get(), P->Home.Get());
						After(World, 1.0f, [World, P]() { ProbeLeave(World, P); });
					});
				});
			});
		});
	}

	/** The flock over a stretch: the most diving and feinting at once, the dives, feints and darts begun, who joined the hunt when. */
	struct FFlockWatch
	{
		TWeakObjectPtr<AJamFlyerHabitat> Home;
		double Started = 0.0;
		int32 MaxDiving = 0;
		int32 MaxFeinting = 0;
		int32 Dives = 0;
		int32 Feints = 0;
		int32 Darts = 0;
		int32 Samples = 0;
		TMap<TWeakObjectPtr<AJamFlyer>, EJamFlyerState> Last;
		TMap<TWeakObjectPtr<AJamFlyer>, bool> Darting;
		TArray<TPair<FString, double>> Joined;
	};

	/** Samples the flock every frame for Seconds (or until Stop), then logs it under Tag and calls Next. */
	void WatchFlock(UWorld* World, AJamFlyerHabitat* Home, float Seconds, const FString& Tag, TFunction<bool()> Stop, TFunction<void()> Next)
	{
		TSharedRef<FFlockWatch> W = MakeShared<FFlockWatch>();
		W->Home = Home;
		W->Started = World->GetTimeSeconds();
		for (const TWeakObjectPtr<AJamFlyer>& Weak : Home->GetFlock())
		{
			if (Weak.IsValid())
			{
				W->Last.Add(Weak, Weak->GetState());
				W->Darting.Add(Weak, Weak->IsDarting());
				if (Weak->IsHunting())
				{
					W->Joined.Add({ Weak->GetName(), -1.0 });
				}
			}
		}
		Poll(World, 0.01f, Seconds, [World, W, Stop]()
		{
			const AJamFlyerHabitat* H = W->Home.Get();
			if (!H)
			{
				return true;
			}
			++W->Samples;
			W->MaxDiving = FMath::Max(W->MaxDiving, H->CountIn(EJamFlyerState::Dive));
			W->MaxFeinting = FMath::Max(W->MaxFeinting, H->CountIn(EJamFlyerState::Feint));
			for (const TWeakObjectPtr<AJamFlyer>& Weak : H->GetFlock())
			{
				if (!Weak.IsValid())
				{
					continue;
				}
				EJamFlyerState& Was = W->Last.FindOrAdd(Weak, Weak->GetState());
				bool& WasDarting = W->Darting.FindOrAdd(Weak, false);
				const EJamFlyerState Now = Weak->GetState();
				W->Dives += Now == EJamFlyerState::Dive && Was != EJamFlyerState::Dive ? 1 : 0;
				W->Feints += Now == EJamFlyerState::Feint && Was != EJamFlyerState::Feint ? 1 : 0;
				W->Darts += Weak->IsDarting() && !WasDarting ? 1 : 0;
				if (Weak->IsHunting() && !W->Joined.ContainsByPredicate([&Weak](const TPair<FString, double>& Each) { return Each.Key == Weak->GetName(); }))
				{
					W->Joined.Add({ Weak->GetName(), World->GetTimeSeconds() - W->Started });
				}
				Was = Now;
				WasDarting = Weak->IsDarting();
			}
			return Stop && Stop();
		}, [World, W, Tag, Next](bool)
		{
			FString Joined;
			for (const TPair<FString, double>& Each : W->Joined)
			{
				Joined += Each.Value < 0.0 ? FString::Printf(TEXT(" %s (already)"), *Each.Key) : FString::Printf(TEXT(" %s +%.2f s"), *Each.Key, Each.Value);
			}
			const AJamFlyerHabitat* H = W->Home.Get();
			UE_LOG(LogMindKore, Log, TEXT("Flyer probe [%s]: %.1f s, %d samples - at most %d diving at once (tokens %d), at most %d feinting (cap %d); %d dives, %d feints, %d darts begun; mood %s; joined the hunt:%s"),
				*Tag, float(World->GetTimeSeconds() - W->Started), W->Samples, W->MaxDiving, H ? H->MaxDivers : 0, W->MaxFeinting, H ? H->MaxFeints : 0,
				W->Dives, W->Feints, W->Darts, H ? JamFlyerParts::MoodName(H->GetMood()) : TEXT("-"), Joined.IsEmpty() ? TEXT(" nobody") : *Joined);
			if (H && W->MaxDiving > H->MaxDivers)
			{
				UE_LOG(LogMindKore, Warning, TEXT("Flyer probe [%s]: FAIL - %d diving at once over %d tokens"), *Tag, W->MaxDiving, H->MaxDivers);
			}
			if (H && H->CascadeWoken > 0)
			{
				UE_LOG(LogMindKore, Log, TEXT("Flyer probe [%s]: the last cascade woke %d over %.2f s (%.2f s a step on average)"), *Tag, H->CascadeWoken,
					float(H->CascadeLastSeconds - H->CascadeStartedSeconds), float(H->CascadeLastSeconds - H->CascadeStartedSeconds) / FMath::Max(1, H->CascadeWoken));
			}
			Next();
		});
	}

	/** The view held on the flyer (the gun drawn) for Seconds: the aim provocation. */
	void HoldAim(UWorld* World, TWeakObjectPtr<AJamFlyer> Target, float Seconds, TFunction<void()> Next)
	{
		Poll(World, 0.01f, Seconds, [World, Target]()
		{
			AJamZeroGPawn* Pawn = ProbePawn(World);
			if (Pawn && Target.IsValid())
			{
				Pawn->GetController()->SetControlRotation((Target->GetActorLocation() - Pawn->Camera->GetComponentLocation()).Rotation());
			}
			return !Target.IsValid();
		}, [Next](bool) { Next(); });
	}

	/** A round delivered into Target along Along (a probe's stand-in for a shot that lands). */
	void ProbeRound(UWorld* World, AJamFlyer* Target, float Damage, const FVector& Along)
	{
		FMKResolvedImpact Hit;
		Hit.ShotId = FGuid::NewGuid();
		Hit.Kind = EMKImpactKind::Round;
		Hit.DamageAmount = Damage;
		Hit.ShotDirection = Along;
		Hit.Hit = FHitResult(Target, Target->Body, Target->GetActorLocation(), -Along);
		Hit.ImpulseNewtonSeconds = Along * 30.0f;
		Hit.bHasPhysicsImpulse = true;
		Hit.SourceActor = ProbePawn(World);
		MKDeliverResolvedImpact(World, ProbePawn(World), Hit);
	}

	void ProbeCalm(UWorld* World, TSharedRef<FProbe> P)
	{
		AJamFlyerHabitat* Home = P->Home.Get();
		if (!Home)
		{
			return;
		}
		// No provocation now: the mood slides down a rung per CalmDownSeconds (the probe set it short).
		const double Started = World->GetTimeSeconds();
		Poll(World, 0.25f, Home->CalmDownSeconds * 2.0f + 6.0f, [P]() { return !P->Home.IsValid() || P->Home->GetMood() == EJamFlockMood::Curious; }, [World, P, Started](bool bCalm)
		{
			UE_LOG(LogMindKore, Log, TEXT("Flyer probe [calm]: %s after %.1f s without provocation; the subject %s"), bCalm ? TEXT("curious again") : TEXT("STILL NOT CURIOUS"),
				float(World->GetTimeSeconds() - Started), P->Flyer.IsValid() ? AJamFlyer::StateName(P->Flyer->GetState()) : TEXT("-"));
			ProbeHide(World, P);
		});
	}

	/** Two rounds while it dives or pulls away: a stagger (the tumble, the shove carrying it), then at low health it flees and comes back. */
	void ProbeStagger(UWorld* World, TSharedRef<FProbe> P)
	{
		Poll(World, 0.01f, 12.0f, [P]() { return !P->Flyer.IsValid() || P->Flyer->GetState() == EJamFlyerState::Dive || P->Flyer->GetState() == EJamFlyerState::Retreat; }, [World, P](bool bOk)
		{
			AJamFlyer* Flyer = P->Flyer.Get();
			if (!Flyer)
			{
				return;
			}
			// Across its flight: the shove reads against it.
			const FVector Velocity = Flyer->GetVelocity();
			const FVector Along = FVector::CrossProduct(Velocity.GetSafeNormal(UE_SMALL_NUMBER, FVector::ForwardVector), FVector::UpVector).GetSafeNormal(UE_SMALL_NUMBER, FVector::RightVector);
			const EJamFlyerState Before = Flyer->GetState();
			const FVector At = Flyer->GetActorLocation();
			ProbeRound(World, Flyer, 10.0f, Along);
			UE_LOG(LogMindKore, Log, TEXT("Flyer probe [stagger]: a 10-damage round across its %s at %.0f cm/s (%s); now %s, HP %.0f"), AJamFlyer::StateName(Before), Velocity.Size(),
				bOk ? TEXT("caught it") : TEXT("waited 12 s"), AJamFlyer::StateName(Flyer->GetState()), Flyer->GetHealth());
			const double Hit = World->GetTimeSeconds();
			After(World, 0.25f, [P, At, Along, Velocity]()
			{
				if (const AJamFlyer* Now = P->Flyer.Get())
				{
					// The shove carries: velocity along the round against before, and how far it drifted sideways.
					const FVector Moved = Now->GetActorLocation() - At - Velocity * 0.25f;
					UE_LOG(LogMindKore, Log, TEXT("Flyer probe [stagger +0.25 s]: %s, %.0f cm/s along the round (was %.0f), %.0f cm off its line"),
						AJamFlyer::StateName(Now->GetState()), FVector::DotProduct(Now->GetVelocity(), Along), FVector::DotProduct(Velocity, Along),
						FVector::DotProduct(Moved, Along));
				}
			});
			Poll(World, 0.01f, 2.0f, [P]() { return !P->Flyer.IsValid() || P->Flyer->GetState() != EJamFlyerState::Stagger; }, [World, P, Hit](bool)
			{
				UE_LOG(LogMindKore, Log, TEXT("Flyer probe [stagger]: tumbled %.2f s, then %s"), float(World->GetTimeSeconds() - Hit),
					P->Flyer.IsValid() ? AJamFlyer::StateName(P->Flyer->GetState()) : TEXT("-"));
				After(World, 1.5f, [World, P]()
				{
					AJamFlyer* Again = P->Flyer.Get();
					if (!Again)
					{
						return;
					}
					// Down to a third of its health: it flees, and comes back.
					ProbeRound(World, Again, Again->GetHealth() - 15.0f, FVector::UpVector);
					UE_LOG(LogMindKore, Log, TEXT("Flyer probe [flee]: a round to HP %.0f; now %s"), Again->GetHealth(), AJamFlyer::StateName(Again->GetState()));
					const double Shot = World->GetTimeSeconds();
					Poll(World, 0.05f, 3.0f, [P]() { return !P->Flyer.IsValid() || P->Flyer->GetState() == EJamFlyerState::Flee; }, [World, P, Shot](bool bFled)
					{
						UE_LOG(LogMindKore, Log, TEXT("Flyer probe [flee]: %s at +%.2f s"), bFled ? TEXT("flees") : TEXT("DID NOT FLEE"), float(World->GetTimeSeconds() - Shot));
						LogFlyer(TEXT("flee"), P->Flyer.Get(), P->Home.Get());
						Poll(World, 0.1f, 8.0f, [P]() { return !P->Flyer.IsValid() || P->Flyer->GetState() != EJamFlyerState::Flee; }, [World, P, Shot](bool bBack)
						{
							LogFlyer(bBack ? *FString::Printf(TEXT("back after %.1f s"), float(World->GetTimeSeconds() - Shot)) : TEXT("flee: NOT BACK in 8 s"), P->Flyer.Get(), P->Home.Get());
							ProbeCalm(World, P);
						});
					});
				});
			});
		});
	}

	/** The ladder: curious (a dart), a gun held on it (stalking: the shadow pass), held again (hunting: the cascade), the tokens, two bites. */
	void ProbeLadder(UWorld* World, TSharedRef<FProbe> P)
	{
		Poll(World, 0.2f, 10.0f, [P]()
		{
			FVector Spot;
			return P->Flyer.IsValid() && InFront(P->Flyer.Get(), P->Home.Get(), 1000.0f, Spot);
		}, [World, P](bool bOk)
		{
			AJamZeroGPawn* Now = ProbePawn(World);
			if (!Now || !P->Flyer.IsValid())
			{
				return;
			}
			FVector Spot;
			InFront(P->Flyer.Get(), P->Home.Get(), 1000.0f, Spot);
			Teleport(Now, Spot);
			// Looking at it, as a player would (not aiming yet: the gun points at it only while HoldAim runs).
			Now->GetController()->SetControlRotation((P->Flyer->GetActorLocation() + FVector(0, 0, 300) - Now->Camera->GetComponentLocation()).Rotation());
			UE_LOG(LogMindKore, Log, TEXT("Flyer probe [sight]: the player 10 m ahead of %s%s"), *P->Flyer->GetName(), bOk ? TEXT("") : TEXT(" (it was flying outward; placed anyway)"));
			Poll(World, 0.05f, 3.0f, [P]() { return !P->Flyer.IsValid() || P->Flyer->IsTracking(); }, [World, P](bool bSeen)
			{
				LogFlyer(bSeen ? TEXT("noticed") : TEXT("NOT NOTICED in 3 s"), P->Flyer.Get(), P->Home.Get());
				// Curious: wait for a scanner dart.
				Poll(World, 0.05f, 12.0f, [P]() { return !P->Flyer.IsValid() || P->Flyer->IsDarting(); }, [World, P](bool bDart)
				{
					UE_LOG(LogMindKore, Log, TEXT("Flyer probe [curious]: %s"), bDart ? TEXT("a dart at the face") : TEXT("NO DART in 12 s"));
					After(World, 2.5f, [World, P]()
					{
						UE_LOG(LogMindKore, Log, TEXT("Flyer probe [aim]: the gun held on it for 1.3 s"));
						HoldAim(World, P->Flyer, 1.3f, [World, P]()
						{
							LogFlyer(*FString::Printf(TEXT("aimed once, the flock %s"), P->Home.IsValid() ? JamFlyerParts::MoodName(P->Home->GetMood()) : TEXT("-")), P->Flyer.Get(), P->Home.Get());
							// Two stalking passes, then the second aim.
							WatchFlock(World, P->Home.Get(), 5.0f, TEXT("stalking"), nullptr, [World, P]()
							{
								HoldAim(World, P->Flyer, 1.3f, [World, P]()
								{
									LogFlyer(*FString::Printf(TEXT("aimed twice, the flock %s"), P->Home.IsValid() ? JamFlyerParts::MoodName(P->Home->GetMood()) : TEXT("-")), P->Flyer.Get(), P->Home.Get());
									const int32 BitesBefore = P->Flyer.IsValid() ? P->Flyer->GetBiteCount() : 0;
									// The hunt: tokens and the cascade, until two bites (25 s at most).
									WatchFlock(World, P->Home.Get(), 25.0f, TEXT("hunt"), [P, BitesBefore]() { return !P->Flyer.IsValid() || P->Flyer->GetBiteCount() >= BitesBefore + 2; }, [World, P, BitesBefore]()
									{
										UE_LOG(LogMindKore, Log, TEXT("Flyer probe [hunt]: %d bite(s)"), P->Flyer.IsValid() ? P->Flyer->GetBiteCount() - BitesBefore : -1);
										ProbeStagger(World, P);
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
	 * Hearing: the player beyond sight (and the leash) fires into empty space - in vacuum nobody hears it;
	 * then the same inside a breathable zone made for the step around the habitat and the player - the flyers in it
	 * hear it and drift to the noise.
	 */
	void ProbeHear(UWorld* World, TSharedRef<FProbe> P)
	{
		AJamZeroGPawn* Pawn = ProbePawn(World);
		AJamFlyerHabitat* Home = P->Home.Get();
		if (!Pawn || !Home)
		{
			return;
		}
		const FVector Centre = Home->GetActorLocation();
		// Beyond every flyer's sight: past the outer radius by the sight range, inside the hearing range.
		const float SightCm = P->Flyer.IsValid() ? P->Flyer->SightRangeCm : 4500.0f;
		const float Out = FMath::Min(Home->GetOuterRadius() + SightCm + 300.0f, Home->GetOuterRadius() + Home->HearingRangeCm - 200.0f);
		const FVector Dir = (P->Far - Centre).GetSafeNormal(UE_SMALL_NUMBER, FVector::ForwardVector);
		const FVector Spot = Centre + Dir * Out;
		Teleport(Pawn, Spot);
		Pawn->GetController()->SetControlRotation(Dir.Rotation()); // away from the flock
		const auto Fire = [World](const TCHAR* Tag)
		{
			AJamZeroGPawn* Shooter = ProbePawn(World);
			const bool bFired = Shooter && Shooter->Fire();
			UE_LOG(LogMindKore, Log, TEXT("Flyer probe [hear]: %s - %s"), Tag, bFired ? TEXT("fired") : TEXT("NOT FIRED"));
		};
		float Nearest = UE_BIG_NUMBER;
		for (const TWeakObjectPtr<AJamFlyer>& Weak : Home->GetFlock())
		{
			Nearest = Weak.IsValid() ? FMath::Min(Nearest, float(FVector::Dist(Weak->GetActorLocation(), Spot))) : Nearest;
		}
		UE_LOG(LogMindKore, Log, TEXT("Flyer probe [hear]: the player %.1f m from the centre, the nearest flyer %.1f m off (sight %.0f m), facing away"),
			Out / 100.0f, Nearest / 100.0f, SightCm / 100.0f);
		After(World, 1.6f, [Fire]() { Fire(TEXT("a shot in vacuum")); });
		After(World, 3.0f, [World, P, Spot, Fire]()
		{
			AJamFlyerHabitat* H = P->Home.Get();
			if (!H)
			{
				return;
			}
			FActorSpawnParameters Params;
			Params.ObjectFlags |= RF_Transient;
			AJamGravityZone* Air = World->SpawnActor<AJamGravityZone>(H->GetActorLocation(), FRotator::ZeroRotator, Params);
			Air->SphereRadiusCm = FVector::Dist(Spot, H->GetActorLocation()) + 1500.0f;
			Air->bWalk = false;
			Air->bBreathable = true;
			Air->RefillPerSecond = 0.0f;
			UE_LOG(LogMindKore, Log, TEXT("Flyer probe [hear]: an air zone for the step, %.0f m round the habitat"), Air->SphereRadiusCm / 100.0f);
			TSharedRef<TMap<FString, float>> Before = MakeShared<TMap<FString, float>>();
			for (const TWeakObjectPtr<AJamFlyer>& Weak : H->GetFlock())
			{
				if (Weak.IsValid() && Weak->IsActorTickEnabled())
				{
					Before->Add(Weak->GetName(), FVector::Dist(Weak->GetActorLocation(), Spot));
				}
			}
			Fire(TEXT("a shot in air"));
			for (const float Delay : { 2.0f, 4.0f, 6.0f })
			{
				After(World, Delay, [P, Spot, Before, Delay]()
				{
					FString Line;
					float Sum = 0.0f, SumBefore = 0.0f;
					int32 Count = 0;
					for (const TWeakObjectPtr<AJamFlyer>& Weak : P->Home->GetFlock())
					{
						if (const float* Was = Weak.IsValid() ? Before->Find(Weak->GetName()) : nullptr)
						{
							const float Now = FVector::Dist(Weak->GetActorLocation(), Spot);
							Line += FString::Printf(TEXT(" %s %.1f->%.1f m (%s)"), *Weak->GetName(), *Was / 100.0f, Now / 100.0f, AJamFlyer::StateName(Weak->GetState()));
							Sum += Now;
							SumBefore += *Was;
							++Count;
						}
					}
					UE_LOG(LogMindKore, Log, TEXT("Flyer probe [hear +%.0f s]: mean distance to the shot %.1f -> %.1f m:%s"), Delay,
						SumBefore / FMath::Max(Count, 1) / 100.0f, Sum / FMath::Max(Count, 1) / 100.0f, *Line);
				});
			}
			TWeakObjectPtr<AJamGravityZone> WeakAir = Air;
			After(World, 6.5f, [World, P, WeakAir]()
			{
				if (WeakAir.IsValid())
				{
					WeakAir->Destroy();
				}
				ProbeLadder(World, P);
			});
		});
	}

	/** jam.Flyer.Probe [all]: wander, hearing (vacuum and air), the ladder (curious, stalking, hunting), tokens, bites, a stagger, flight, calming, hiding, the leash, shot dead, the corpse. */
	void RunFlyerProbe(const TArray<FString>& Args, UWorld* World)
	{
		AJamZeroGPawn* Pawn = ProbePawn(World);
		// Args: "all", and a part of a habitat's name or label (the first habitat otherwise).
		const bool bAll = Args.Contains(TEXT("all"));
		const FString* Filter = Args.FindByPredicate([](const FString& Arg) { return Arg != TEXT("all"); });
		AJamFlyerHabitat* Home = nullptr;
		for (TActorIterator<AJamFlyerHabitat> It(World); It && !Home; ++It)
		{
#if WITH_EDITOR
			const FString Label = It->GetActorLabel();
#else
			const FString Label;
#endif
			if (!Filter || It->GetName().Contains(*Filter) || Label.Contains(*Filter))
			{
				Home = *It;
			}
		}
		if (!Pawn || !Home || Home->GetFlock().IsEmpty() || !Home->GetFlock()[0].IsValid())
		{
			UE_LOG(LogMindKore, Warning, TEXT("Flyer probe: needs the zero-G pawn and a habitat with flyers"));
			return;
		}
		JamFlyerParts::CVarLogFlaps->Set(true, ECVF_SetByCode); // every audible beat in the log while the probe runs
		TSharedRef<FProbe> P = MakeShared<FProbe>();
		P->Home = Home;
		P->Flyer = Home->GetFlock()[0];
		// One subject; its flockmates hold still unless "all" (the cascade still wakes them: they hunt once the probe ends).
		for (int32 Index = 1; Index < Home->GetFlock().Num() && !bAll; ++Index)
		{
			if (AJamFlyer* Other = Home->GetFlock()[Index].Get())
			{
				Other->SetActorTickEnabled(false);
				Other->Body->SetPhysicsLinearVelocity(FVector::ZeroVector);
				P->Frozen.Add(Other);
			}
		}
		// The calming step: a rung every 9 s here (12 in play), so the stalking stretch (5 s) stays between the two aims.
		Home->CalmDownSeconds = 9.0f;
		// The pistol in hand from the start (the hearing step fires it; the aim steps hold it).
		Pawn->Guns->UnlockWeapon(0);
		Pawn->Guns->SelectWeapon(0);
		const FVector Centre = Home->GetActorLocation();
		P->Far = Centre + (Pawn->GetActorLocation() - Centre).GetSafeNormal(UE_SMALL_NUMBER, FVector::ForwardVector) * (Home->GetOuterRadius() + Home->LeashCm + 800.0f);
		Teleport(Pawn, P->Far);
		UE_LOG(LogMindKore, Log, TEXT("Flyer probe: habitat %s, subject %s (curiosity %.2f), %d flockmates %s; calm-down %.0f s a rung; the player waits %.1f m from the centre"),
			*Home->GetName(), *P->Flyer->GetName(), P->Flyer->GetCuriosity(), Home->GetFlock().Num() - 1, bAll ? TEXT("free") : TEXT("held still"),
			Home->CalmDownSeconds, FVector::Dist(P->Far, Centre) / 100.0f);
		for (int32 Step = 0; Step < 4; ++Step)
		{
			After(World, 0.5f + Step * 1.0f, [P, Step]() { LogFlyer(*FString::Printf(TEXT("wander %d"), Step), P->Flyer.Get(), P->Home.Get()); });
		}
		After(World, 4.0f, [World, P]() { ProbeHear(World, P); });
	}

	FAutoConsoleCommandWithWorldAndArgs GFlyerProbe(
		TEXT("jam.Flyer.Probe"),
		TEXT("jam.Flyer.Probe [all] [Habitat]: one flyer of the first (or the named) habitat - wander, hearing (vacuum, air), the ladder (curious dart, aim: stalking, aim: hunting and the cascade), dive tokens, bites, a stagger, flight, calming down, the player hides and leaves (de-aggro), shot dead, the corpse. 'all' lets its flockmates fly too."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&RunFlyerProbe));

	/**
	 * jam.Flyer.BiteShot: rendered stills of a bite (needs a real RHI). The first
	 * habitat's first flyer gets the player 10 m ahead (the rest stop noticing);
	 * the view follows it until it bites, then "Shot" at +0.2, +1.2 and +2.5 s:
	 * the visor blood landing, running and fading.
	 */
	void RunFlyerBiteShot(UWorld* World)
	{
		AJamFlyerHabitat* Home = nullptr;
		for (TActorIterator<AJamFlyerHabitat> It(World); It && !Home; ++It)
		{
			Home = *It;
		}
		AJamZeroGPawn* Pawn = ProbePawn(World);
		if (!Home || Home->GetFlock().IsEmpty() || !Home->GetFlock()[0].IsValid() || !Pawn)
		{
			UE_LOG(LogMindKore, Warning, TEXT("Flyer bite shot: needs the zero-G pawn and a habitat with flyers"));
			return;
		}
		TSharedRef<FProbe> P = MakeShared<FProbe>();
		P->Home = Home;
		P->Flyer = Home->GetFlock()[0];
		for (int32 Index = 1; Index < Home->GetFlock().Num(); ++Index)
		{
			if (AJamFlyer* Other = Home->GetFlock()[Index].Get())
			{
				Other->SightRangeCm = 0.0f;
			}
		}
		Poll(World, 0.2f, 10.0f, [P]()
		{
			FVector Spot;
			return P->Flyer.IsValid() && InFront(P->Flyer.Get(), P->Home.Get(), 1000.0f, Spot);
		}, [World, P](bool)
		{
			AJamZeroGPawn* Now = ProbePawn(World);
			if (!Now || !P->Flyer.IsValid())
			{
				return;
			}
			FVector Spot;
			InFront(P->Flyer.Get(), P->Home.Get(), 1000.0f, Spot);
			Teleport(Now, Spot);
			const int32 Before = P->Flyer->GetBiteCount();
			Poll(World, 0.01f, 15.0f, [World, P, Before]()
			{
				AJamZeroGPawn* Watcher = ProbePawn(World);
				if (!Watcher || !P->Flyer.IsValid())
				{
					return true;
				}
				Watcher->GetController()->SetControlRotation((P->Flyer->GetActorLocation() - Watcher->Camera->GetComponentLocation()).Rotation());
				return P->Flyer->GetBiteCount() > Before;
			}, [World](bool bBitten)
			{
				UE_LOG(LogMindKore, Log, TEXT("Flyer bite shot: %s"), bBitten ? TEXT("bitten") : TEXT("no bite in 15 s"));
				for (const float Delay : { 0.2f, 1.2f, 2.5f })
				{
					After(World, Delay, [World, Delay]()
					{
						const AJamZeroGPawn* Watcher = ProbePawn(World);
						const UJamVisorBloodComponent* Blood = Watcher ? Watcher->FindComponentByClass<UJamVisorBloodComponent>() : nullptr;
						UE_LOG(LogMindKore, Log, TEXT("Flyer bite shot: +%.1f s, %d blot(s) on the visor"), Delay, Blood ? Blood->GetBlotCount() : 0);
						World->GetFirstPlayerController()->ConsoleCommand(TEXT("Shot"));
					});
				}
			});
		});
	}

	FAutoConsoleCommandWithWorld GFlyerBiteShot(
		TEXT("jam.Flyer.BiteShot"),
		TEXT("Rendered stills of a flyer bite: the visor blood at +0.2, +1.2, +2.5 s (Shot; needs a real RHI)."),
		FConsoleCommandWithWorldDelegate::CreateStatic(&RunFlyerBiteShot));

	/**
	 * jam.Flyer.CorpseShot: a still of a dead flyer on the planetoid (needs a real
	 * RHI and a shell habitat). The first shell habitat's first flyer is put 6 m
	 * over the surface under where it flies and killed by a round there; 7 s later
	 * the player stands 4 m from the corpse, looks at it and "Shot".
	 */
	void RunFlyerCorpseShot(UWorld* World)
	{
		AJamFlyerHabitat* Home = nullptr;
		for (TActorIterator<AJamFlyerHabitat> It(World); It && !Home; ++It)
		{
			Home = It->InnerRadiusCm > 0.0f ? *It : nullptr;
		}
		if (!Home || Home->GetFlock().IsEmpty() || !Home->GetFlock()[0].IsValid() || !ProbePawn(World))
		{
			UE_LOG(LogMindKore, Warning, TEXT("Flyer corpse shot: needs the zero-G pawn and a habitat round a planetoid"));
			return;
		}
		AJamFlyer* Flyer = Home->GetFlock()[0].Get();
		const FVector Centre = Home->GetActorLocation();
		const FVector Out = (Flyer->GetActorLocation() - Centre).GetSafeNormal();
		// The ground under it: a line down from the habitat's inner radius.
		FHitResult Ground;
		FCollisionQueryParams Params(SCENE_QUERY_STAT(JamFlyerCorpseShot), false, Flyer);
		const FVector Top = Centre + Out * Home->InnerRadiusCm;
		const FVector Surface = World->LineTraceSingleByChannel(Ground, Top, Centre, ECC_Visibility, Params) ? FVector(Ground.ImpactPoint) : Top;
		Teleport(Flyer, Surface + Out * 600.0f);
		// A round from above: the flyer's own receiver (damage, push, death) as a gun delivers it.
		FMKResolvedImpact Kill;
		Kill.ShotId = FGuid::NewGuid();
		Kill.Kind = EMKImpactKind::Round;
		Kill.DamageAmount = 1000.0f;
		Kill.Hit = FHitResult(Flyer, Flyer->Body, Flyer->GetActorLocation(), Out);
		Kill.ImpulseNewtonSeconds = -Out * 30.0f;
		Kill.bHasPhysicsImpulse = true;
		Kill.SourceActor = ProbePawn(World);
		MKDeliverResolvedImpact(World, ProbePawn(World), Kill);
		UE_LOG(LogMindKore, Log, TEXT("Flyer corpse shot: %s killed 6 m over the surface, %.1f m from the centre"), *Flyer->GetName(),
			FVector::Dist(Flyer->GetActorLocation(), Centre) / 100.0f);
		TWeakObjectPtr<AJamFlyer> Weak = Flyer;
		After(World, 7.0f, [World, Weak, Centre]()
		{
			AJamZeroGPawn* Pawn = ProbePawn(World);
			const AJamFlyer* Corpse = Weak.Get();
			if (!Pawn || !Corpse)
			{
				return;
			}
			const FVector Up = (Corpse->GetActorLocation() - Centre).GetSafeNormal();
			const FVector Side = FVector::CrossProduct(Up, FVector::UpVector).GetSafeNormal(UE_SMALL_NUMBER, FVector::ForwardVector);
			Teleport(Pawn, Corpse->GetActorLocation() + Side * 400.0f + Up * 150.0f);
		});
		for (const float Delay : { 7.6f, 8.4f })
		{
			After(World, Delay, [World, Weak, Centre, Delay]()
			{
				AJamZeroGPawn* Pawn = ProbePawn(World);
				const AJamFlyer* Corpse = Weak.Get();
				if (!Pawn || !Corpse)
				{
					return;
				}
				Pawn->GetController()->SetControlRotation((Corpse->GetActorLocation() - Pawn->Camera->GetComponentLocation()).Rotation());
				if (Delay > 8.0f)
				{
					UE_LOG(LogMindKore, Log, TEXT("Flyer corpse shot: corpse %.1f m from the centre, %.0f cm/s; the player %.1f m from it"),
						FVector::Dist(Corpse->GetActorLocation(), Centre) / 100.0f, Corpse->GetVelocity().Size(),
						FVector::Dist(Corpse->GetActorLocation(), Pawn->GetActorLocation()) / 100.0f);
					World->GetFirstPlayerController()->ConsoleCommand(TEXT("Shot"));
				}
			});
		}
	}

	FAutoConsoleCommandWithWorld GFlyerCorpseShot(
		TEXT("jam.Flyer.CorpseShot"),
		TEXT("A rendered still of a flyer killed over the planetoid, lying where it fell (Shot; needs a real RHI)."),
		FConsoleCommandWithWorldDelegate::CreateStatic(&RunFlyerCorpseShot));

	/**
	 * jam.Flyer.Clip [Seconds] [Ffmpeg] [shotgun] [stat]: a recorded take of a fly-by attack with its
	 * sound (needs a real RHI and a window): the player 14 m ahead of the first habitat's first flyer,
	 * the view following it, the flock free; after 60 % of the take the pistol (or the shotgun, inside
	 * 12 m) until it dies. The player's health is topped up (the take is about the flyers). Video:
	 * Saved/Clips/FlyerClip.mp4 (60 fps nominal), sound: Saved/Clips/FlyerClip.wav (the master
	 * submix); the log gives the real seconds and the frame count for the mux (Docs/Flyers.md), and
	 * the voices every 0.5 s ("Flyer clip voices").
	 */
	AJamFlyerHabitat* FindRoost(UWorld* World, const FString& Filter);
	AJamLivingWall* FindLinkedWall(UWorld* World);
	void PlaceEye(AJamZeroGPawn* Pawn, const FVector& Eye, const FVector& Look);
	bool AirlockExit(UWorld* World, FVector& OutPoint, FVector& OutOut);
	FVector WallFront(const AJamLivingWall* Wall, const FVector& From, float Distance);
	FString DescribeFlock(const AJamFlyerHabitat* Home);

	struct FClip
	{
		/** A scene (roost, wall) directs the take itself: the view, the moves, the shots; T is seconds into it. */
		TFunction<void(UWorld*, float, double)> Director;
		/** The flyer a scene shoots at until it dies. */
		TWeakObjectPtr<AJamFlyer> Locked;
		TUniquePtr<FJamClipRecorder> Recorder;
		TWeakObjectPtr<AJamFlyer> Subject;
		FTSTicker::FDelegateHandle Ticker;
		float Seconds = 26.0f;
		double Started = 0.0;
		bool bShooting = false;
		bool bShotgun = false;
		bool bStopped = false;
		double NextShot = 0.0;
		FRotator View = FRotator::ZeroRotator;
		double NextVoiceLog = 0.0;
	};

	/**
	 * The voices during a take, sampled on the audio thread once per game frame: the wave
	 * instances the device sorted in its last update (one per playing layer) against its
	 * channel limit; the ones over the limit, the lowest priorities, are culled (stat audio:
	 * "Audible Wave Instances Dropped"). Peaks per log interval and for the whole take.
	 */
	struct FVoiceStats
	{
		std::atomic<int32> Channels{ 0 };
		std::atomic<int32> PeakWaves{ 0 };
		std::atomic<int32> PeakSounds{ 0 };
		std::atomic<int32> TakePeakWaves{ 0 };
		std::atomic<int32> Samples{ 0 };
		std::atomic<int32> OverSamples{ 0 };
		std::atomic<int64> OverWaves{ 0 };
	};

	void SampleVoices(UWorld* World, TSharedRef<FVoiceStats, ESPMode::ThreadSafe> Stats)
	{
		FAudioDevice* Device = World->GetAudioDeviceRaw();
		if (!Device)
		{
			return;
		}
		FAudioThread::RunCommandOnAudioThread([Device, Stats]()
		{
			const int32 Waves = Device->GetActiveWaveInstances().Num();
			const int32 Channels = Device->GetMaxChannels();
			Stats->Channels = Channels;
			Stats->PeakWaves = FMath::Max(Stats->PeakWaves.load(), Waves);
			Stats->PeakSounds = FMath::Max(Stats->PeakSounds.load(), Device->GetActiveSounds().Num());
			Stats->TakePeakWaves = FMath::Max(Stats->TakePeakWaves.load(), Waves);
			++Stats->Samples;
			if (Waves > Channels)
			{
				++Stats->OverSamples;
				Stats->OverWaves += Waves - Channels;
			}
		});
	}

	TSharedRef<FVoiceStats, ESPMode::ThreadSafe> GVoices = MakeShared<FVoiceStats, ESPMode::ThreadSafe>();

	TSharedPtr<FClip> GClip;

	FString FindFfmpeg()
	{
		FString Path = FPlatformMisc::GetEnvironmentVariable(TEXT("PATH"));
		TArray<FString> Dirs;
		Path.ParseIntoArray(Dirs, TEXT(";"));
		for (const FString& Dir : Dirs)
		{
			const FString Candidate = FPaths::Combine(Dir, TEXT("ffmpeg.exe"));
			if (FPaths::FileExists(Candidate))
			{
				return Candidate;
			}
		}
		return TEXT("ffmpeg.exe");
	}

	bool ClipTick(UWorld* World, float DeltaSeconds)
	{
		TSharedPtr<FClip> Clip = GClip;
		if (!Clip.IsValid() || !Clip->Recorder)
		{
			return false;
		}
		Clip->Recorder->Tick();
		const double Now = World->GetTimeSeconds();
		if (!Clip->bStopped)
		{
			SampleVoices(World, GVoices);
			if (Now >= Clip->NextVoiceLog)
			{
				Clip->NextVoiceLog = Now + 0.5;
				UE_LOG(LogMindKore, Log, TEXT("Flyer clip voices: +%.1f s, up to %d wave instances (%d active sounds) of %d channels"),
					Now - Clip->Started, GVoices->PeakWaves.load(), GVoices->PeakSounds.load(), GVoices->Channels.load());
				GVoices->PeakWaves = 0;
				GVoices->PeakSounds = 0;
			}
		}
		AJamZeroGPawn* Pawn = ProbePawn(World);
		AJamFlyer* Flyer = Clip->Subject.Get();
		if (Pawn && Clip->Director && !Clip->bStopped)
		{
			Pawn->ProbeThrust(FVector::ZeroVector, /*bStabilise=*/true);
			if (Pawn->Vitals->Health < 60.0f)
			{
				Pawn->Vitals->Restore(100.0f, 0.0f);
			}
			Clip->Director(World, DeltaSeconds, Now - Clip->Started);
		}
		else if (Pawn && Flyer && !Clip->bStopped)
		{
			// The view follows the flyer (eased, as a person turning to it).
			const FRotator Want = (Flyer->GetActorLocation() - Pawn->Camera->GetComponentLocation()).Rotation();
			Clip->View = FMath::RInterpTo(Clip->View, Want, DeltaSeconds, 5.0f);
			Pawn->GetController()->SetControlRotation(Clip->View);
			// The player brakes after every shove (the stabiliser, X), so the fight stays in the habitat.
			Pawn->ProbeThrust(FVector::ZeroVector, /*bStabilise=*/true);
			if (Pawn->Vitals->Health < 60.0f)
			{
				Pawn->Vitals->Restore(100.0f, 0.0f);
			}
			if (!Clip->bShooting && Now - Clip->Started > Clip->Seconds * 0.6f)
			{
				Clip->bShooting = true;
				UE_LOG(LogMindKore, Log, TEXT("Flyer clip: +%.1f s, the %s"), Now - Clip->Started, Clip->bShotgun ? TEXT("shotgun") : TEXT("pistol"));
			}
			// The shotgun's pellets spread 6 degrees: it waits for the flyer inside 12 m.
			if (Clip->bShooting && Flyer->GetState() != EJamFlyerState::Dead && Now >= Clip->NextShot
				&& FVector::Dist(Flyer->GetActorLocation(), Pawn->GetActorLocation()) < (Clip->bShotgun ? 1200.0f : 4000.0f))
			{
				Clip->NextShot = Now + (Clip->bShotgun ? 0.6 : 0.35);
				// Rounds fly (ProjectileSpeed): lead it by their travel time.
				const FJamWeapon* Gun = Pawn->Guns->GetWeapon();
				const float Travel = FVector::Dist(Flyer->GetActorLocation(), Pawn->Camera->GetComponentLocation()) / FMath::Max(Gun ? Gun->ProjectileSpeed : 9000.0f, 100.0f);
				const FVector Aim = Flyer->GetActorLocation() + (Flyer->GetVelocity() - Pawn->GetBodyVelocity()) * Travel;
				Clip->View = (Aim - Pawn->Camera->GetComponentLocation()).Rotation();
				Pawn->GetController()->SetControlRotation(Clip->View);
				Pawn->Fire();
			}
		}
		if (!Clip->bStopped && Now - Clip->Started >= Clip->Seconds)
		{
			Clip->bStopped = true;
			const FString Dir = FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir() / TEXT("Clips"));
			Clip->Recorder->Stop(World, TEXT("FlyerClip"), Dir);
			UE_LOG(LogMindKore, Log, TEXT("Flyer clip: stopped after %.2f s real, %d frames so far; sound %s/FlyerClip.wav"),
				Clip->Recorder->GetSeconds(), Clip->Recorder->GetFrames(), *Dir);
			UE_LOG(LogMindKore, Log, TEXT("Flyer clip voices: the take peaked at %d wave instances of %d channels; over the limit in %d of %d samples, %lld culled wave-frames"),
				GVoices->TakePeakWaves.load(), GVoices->Channels.load(), GVoices->OverSamples.load(), GVoices->Samples.load(), GVoices->OverWaves.load());
		}
		if (Clip->bStopped && Clip->Recorder->IsFinished())
		{
			UE_LOG(LogMindKore, Log, TEXT("Flyer clip: done - %d frames over %.3f s real (%.2f fps); video Saved/Clips/FlyerClip.mp4 at a nominal 60 fps"),
				Clip->Recorder->GetFrames(), Clip->Recorder->GetSeconds(), Clip->Recorder->GetFrames() / FMath::Max(Clip->Recorder->GetSeconds(), 0.001));
			GClip.Reset();
			return false;
		}
		return true;
	}

	/** The view eases toward Want (a person turning), at Rate. */
	void ClipLook(FClip& Clip, AJamZeroGPawn* Pawn, const FVector& Want, float DeltaSeconds, float Rate)
	{
		Clip.View = FMath::RInterpTo(Clip.View, (Want - Pawn->Camera->GetComponentLocation()).Rotation(), DeltaSeconds, Rate);
		Pawn->GetController()->SetControlRotation(Clip.View);
	}

	/** The nearest flyer of Home in the hunt (a diving or feinting one first), within MaxCm; null when none. */
	AJamFlyer* ClipQuarry(const AJamFlyerHabitat* Home, const FVector& From, float MaxCm)
	{
		AJamFlyer* Best = nullptr;
		float Score = UE_BIG_NUMBER;
		for (const TWeakObjectPtr<AJamFlyer>& Weak : Home->GetFlock())
		{
			AJamFlyer* Each = Weak.Get();
			if (!Each || !Each->IsTracking())
			{
				continue;
			}
			const float Distance = FVector::Dist(Each->GetActorLocation(), From);
			const float Rank = Distance * (Each->GetState() == EJamFlyerState::Dive || Each->GetState() == EJamFlyerState::Feint ? 0.5f : 1.0f);
			if (Distance < MaxCm && Rank < Score)
			{
				Score = Rank;
				Best = Each;
			}
		}
		return Best;
	}

	/** The flyer the take shoots at: kept until it dies or gets away (MaxCm), so the take ends in a kill. */
	AJamFlyer* ClipTarget(TWeakObjectPtr<AJamFlyer>& Locked, const AJamFlyerHabitat* Home, const FVector& From, float MaxCm)
	{
		if (!Locked.IsValid() || Locked->GetState() == EJamFlyerState::Dead || FVector::Dist(Locked->GetActorLocation(), From) > MaxCm)
		{
			Locked = ClipQuarry(Home, From, MaxCm);
		}
		return Locked.Get();
	}

	/** One pistol round at Target, led by the round's flight. */
	void ClipShoot(FClip& Clip, AJamZeroGPawn* Pawn, const AJamFlyer* Target, double Now)
	{
		if (Now < Clip.NextShot)
		{
			return;
		}
		Clip.NextShot = Now + 0.35;
		const FJamWeapon* Gun = Pawn->Guns->GetWeapon();
		const float Travel = FVector::Dist(Target->GetActorLocation(), Pawn->Camera->GetComponentLocation()) / FMath::Max(Gun ? Gun->ProjectileSpeed : 9000.0f, 100.0f);
		const FVector Aim = Target->GetActorLocation() + (Target->GetVelocity() - Pawn->GetBodyVelocity()) * Travel;
		Clip.View = (Aim - Pawn->Camera->GetComponentLocation()).Rotation();
		Pawn->GetController()->SetControlRotation(Clip.View);
		Pawn->Fire();
	}

	/**
	 * The roost scene (Lvl_Jam_Ship): the reveal from the airlock, unarmed - the hull bristling, the roost's
	 * rustle, a sentry turning curious and darting in; the pistol; a round into the hull by the densest
	 * clinging (contact): the cascade over the hull; the hunt with its tokens and feints, the pistol on the
	 * nearest hunter - a stagger, a kill. Staged for the roost on the player's ship; since 19.10 the roost is on
	 * the wreck, 65 m from the airlock, and the scene was not reworked for it (Docs/Flyers.md, limits).
	 */
	bool MakeRoostScene(UWorld* World, FClip& Clip)
	{
		AJamFlyerHabitat* Home = FindRoost(World, TEXT(""));
		AJamZeroGPawn* Pawn = ProbePawn(World);
		FVector Exit, Out;
		if (!Home || !Pawn || !AirlockExit(World, Exit, Out))
		{
			return false;
		}
		// The reveal (stills 19.10): just out of the door, looking aft down the flank that bristles, the Milky Way behind.
		const FVector Eye = Exit + Out * 100.0f + FVector(0, 0, 60);
		const FVector Reveal = Home->RoostFocus + Out * 250.0f - FVector(0, 0, 290);
		PlaceEye(Pawn, Eye, Reveal);
		Clip.View = (Reveal - Eye).Rotation();
		TWeakObjectPtr<AJamFlyerHabitat> WeakHome = Home;
		TSharedRef<int32> Stage = MakeShared<int32>(0);
		Clip.Director = [WeakHome, Stage, Eye, Reveal](UWorld* InWorld, float Dt, double T)
		{
			AJamZeroGPawn* P = ProbePawn(InWorld);
			AJamFlyerHabitat* H = WeakHome.Get();
			TSharedPtr<FClip> C = GClip;
			if (!P || !H || !C)
			{
				return;
			}
			const double Now = InWorld->GetTimeSeconds();
			if (T < 2.5)
			{
				ClipLook(*C, P, Reveal + FVector(0, 0, 80.0f * FMath::Sin(float(T) * 0.6f)), Dt, 2.0f);
				return;
			}
			if (*Stage == 0)
			{
				*Stage = 1;
				// The nearest sentry notices the figure in the airlock.
				AJamFlyer* Sentry = nullptr;
				for (const TWeakObjectPtr<AJamFlyer>& Weak : H->GetFlock())
				{
					if (Weak.IsValid() && Weak->GetState() == EJamFlyerState::Wander
						&& (!Sentry || FVector::DistSquared(Weak->GetActorLocation(), Eye) < FVector::DistSquared(Sentry->GetActorLocation(), Eye)))
					{
						Sentry = Weak.Get();
					}
				}
				if (Sentry)
				{
					Sentry->Notice(TEXT("clip: the figure in the airlock"));
				}
				UE_LOG(LogMindKore, Log, TEXT("Flyer clip: +%.1f s, a sentry notices - %s"), T, *DescribeFlock(H));
			}
			if (T < 12.0)
			{
				// The scanner at work: the view follows the curious one, back to the hull when none.
				const AJamFlyer* Curious = ClipQuarry(H, P->GetActorLocation(), 3000.0f);
				ClipLook(*C, P, Curious ? Curious->GetActorLocation() : Reveal, Dt, 3.0f);
				return;
			}
			if (*Stage == 1)
			{
				*Stage = 2;
				P->Guns->UnlockWeapon(0);
				P->Guns->SelectWeapon(0);
				UE_LOG(LogMindKore, Log, TEXT("Flyer clip: +%.1f s, the pistol"), T);
			}
			// The round into the hull: a free spot by the densest clinging.
			const FJamRoostSpot* Spot = nullptr;
			for (int32 Index = 0; Index < H->GetSpotCount(); ++Index)
			{
				const FJamRoostSpot* Each = H->GetSpot(Index);
				if (!Each->Holder.IsValid() && (!Spot || FVector::DistSquared(Each->Point, H->RoostFocus) < FVector::DistSquared(Spot->Point, H->RoostFocus)))
				{
					Spot = Each;
				}
			}
			const FVector HullAim = Spot ? Spot->Point : H->RoostFocus;
			if (T < 13.6)
			{
				ClipLook(*C, P, HullAim, Dt, 4.0f);
				return;
			}
			if (*Stage == 2)
			{
				*Stage = 3;
				C->View = (HullAim - P->Camera->GetComponentLocation()).Rotation();
				P->GetController()->SetControlRotation(C->View);
				UE_LOG(LogMindKore, Log, TEXT("Flyer clip: +%.1f s, a round into the hull - %s"), T, P->Fire() ? TEXT("fired") : TEXT("NOT FIRED"));
				return;
			}
			if (T < 18.5)
			{
				ClipLook(*C, P, Reveal + FVector(0, 0, 200.0f), Dt, 1.5f); // the wave over the hull
				return;
			}
			if (*Stage == 3)
			{
				*Stage = 4;
				UE_LOG(LogMindKore, Log, TEXT("Flyer clip: +%.1f s, the hunt - %s"), T, *DescribeFlock(H));
			}
			if (AJamFlyer* Quarry = T > 21.0 ? ClipTarget(C->Locked, H, P->GetActorLocation(), 2500.0f) : ClipQuarry(H, P->GetActorLocation(), 2500.0f))
			{
				ClipLook(*C, P, Quarry->GetActorLocation(), Dt, 4.0f);
				if (T > 21.0 && FVector::Dist(Quarry->GetActorLocation(), P->GetActorLocation()) < 2000.0f)
				{
					ClipShoot(*C, P, Quarry, Now);
				}
			}
			else
			{
				ClipLook(*C, P, H->RoostFocus, Dt, 2.0f);
			}
		};
		return true;
	}

	/**
	 * The wall scene (Lvl_Jam_Ship): before the linked flesh wall with the shotgun; the blast - the scream
	 * (and in the radio), the blood and spores venting; the turn to the wreck: the roost waking unheard; the
	 * flight home looking back: the flock coming, the pistol on the first to close in.
	 */
	bool MakeWallScene(UWorld* World, FClip& Clip)
	{
		AJamLivingWall* Wall = FindLinkedWall(World);
		AJamZeroGPawn* Pawn = ProbePawn(World);
		FVector Exit, Out;
		if (!Wall || !Pawn || !AirlockExit(World, Exit, Out) || !Wall->LinkedHabitats[0].Habitat)
		{
			return false;
		}
		const FVector Front = WallFront(Wall, Exit, 450.0f);
		PlaceEye(Pawn, Front, Wall->GetActorLocation());
		const FVector Bow = WallFront(Wall, Exit, 900.0f) + Wall->GetActorUpVector() * 1000.0f; // out of the bow, over the belt
		Clip.View = (Wall->GetActorLocation() - Front).Rotation();
		Pawn->Guns->UnlockWeapon(0);
		Pawn->Guns->UnlockWeapon(1);
		Pawn->Guns->SelectWeapon(1);
		TWeakObjectPtr<AJamFlyerHabitat> WeakHome = Wall->LinkedHabitats[0].Habitat.Get();
		TWeakObjectPtr<AJamLivingWall> WeakWall = Wall;
		TSharedRef<int32> Stage = MakeShared<int32>(0);
		TSharedRef<FVector> At = MakeShared<FVector>(Front);
		Clip.Director = [WeakHome, WeakWall, Stage, At, Exit, Bow](UWorld* InWorld, float Dt, double T)
		{
			AJamZeroGPawn* P = ProbePawn(InWorld);
			AJamFlyerHabitat* H = WeakHome.Get();
			TSharedPtr<FClip> C = GClip;
			if (!P || !H || !C || !WeakWall.IsValid())
			{
				return;
			}
			const double Now = InWorld->GetTimeSeconds();
			const FVector Wound = WeakWall->GetActorLocation();
			if (T < 2.5)
			{
				ClipLook(*C, P, Wound, Dt, 4.0f);
				return;
			}
			if (*Stage == 0)
			{
				*Stage = 1;
				C->View = (Wound - P->Camera->GetComponentLocation()).Rotation();
				P->GetController()->SetControlRotation(C->View);
				UE_LOG(LogMindKore, Log, TEXT("Flyer clip: +%.1f s, the blast - %s"), T, P->Fire() ? TEXT("fired") : TEXT("NOT FIRED"));
				return;
			}
			if (T < 6.5)
			{
				ClipLook(*C, P, Wound, Dt, 2.0f); // the wound screaming and venting
				return;
			}
			if (*Stage == 1)
			{
				*Stage = 2;
				P->Guns->SelectWeapon(0);
				UE_LOG(LogMindKore, Log, TEXT("Flyer clip: +%.1f s, the turn to the wreck - %s"), T, *DescribeFlock(H));
			}
			// Home, looking back: a step along the way each frame (thrusters, 5 m/s), the view on the nearest coming.
			if (T > 9.0)
			{
				// Out of the bow first, then home.
				const FVector Goal = FVector::DotProduct(*At - WeakWall->GetActorLocation(), Bow - WeakWall->GetActorLocation()) < (Bow - WeakWall->GetActorLocation()).SizeSquared() * 0.95f ? Bow : Exit;
				*At += (Goal - *At).GetSafeNormal() * FMath::Min(500.0f * Dt, float(FVector::Dist(Goal, *At)));
				Teleport(P, *At);
			}
			AJamFlyer* Quarry = Now >= H->RallyUntil ? ClipTarget(C->Locked, H, P->GetActorLocation(), 4000.0f) : ClipQuarry(H, P->GetActorLocation(), 4000.0f);
			ClipLook(*C, P, Quarry ? Quarry->GetActorLocation() : H->GetActorLocation(), Dt, Quarry ? 3.0f : 1.5f);
			if (Quarry && Now >= H->RallyUntil && FVector::Dist(Quarry->GetActorLocation(), P->GetActorLocation()) < 1800.0f)
			{
				ClipShoot(*C, P, Quarry, Now);
			}
		};
		return true;
	}

	void RunFlyerClip(const TArray<FString>& Args, UWorld* World)
	{
		AJamFlyerHabitat* Home = nullptr;
		for (TActorIterator<AJamFlyerHabitat> It(World); It && !Home; ++It)
		{
			Home = *It;
		}
		AJamZeroGPawn* Pawn = ProbePawn(World);
		if (!Home || Home->GetFlock().IsEmpty() || !Home->GetFlock()[0].IsValid() || !Pawn || GClip.IsValid())
		{
			UE_LOG(LogMindKore, Warning, TEXT("Flyer clip: needs the zero-G pawn and a habitat with flyers (and no clip running)"));
			return;
		}
		// A silent master submix switches off and the recording skips it (MindKore, 12.10).
		if (IConsoleVariable* Never = IConsoleManager::Get().FindConsoleVariable(TEXT("au.NeverDisableSubmixes")))
		{
			Never->Set(1, ECVF_SetByCode);
		}
		JamFlyerParts::CVarLogFlaps->Set(true, ECVF_SetByCode);
		TSharedPtr<FClip> Clip = MakeShared<FClip>();
		// "shotgun" anywhere: the shotgun instead of the pistol; "stat": the engine's voice counters
		// on screen (stat audio: Max Channels, Wave Instances, ... Dropped - in the video too);
		// the rest are Seconds and Ffmpeg.
		TArray<FString> Rest = Args;
		Clip->bShotgun = Rest.Remove(TEXT("shotgun")) > 0;
		const bool bStat = Rest.Remove(TEXT("stat")) > 0;
		const bool bRoost = Rest.Remove(TEXT("roost")) > 0;
		const bool bWall = Rest.Remove(TEXT("wall")) > 0;
		Clip->Seconds = Rest.Num() > 0 ? FCString::Atof(*Rest[0]) : 26.0f;
		Clip->Subject = Home->GetFlock()[0];
		GVoices = MakeShared<FVoiceStats, ESPMode::ThreadSafe>();
		if (bStat)
		{
			World->GetFirstPlayerController()->ConsoleCommand(TEXT("stat audio"));
		}
		if (bRoost || bWall)
		{
			World->GetFirstPlayerController()->ConsoleCommand(TEXT("DisableAllScreenMessages"));
			if (!(bRoost ? MakeRoostScene(World, *Clip) : MakeWallScene(World, *Clip)))
			{
				UE_LOG(LogMindKore, Warning, TEXT("Flyer clip: the %s scene needs Lvl_Jam_Ship's roost (or linked wall) and airlock"), bRoost ? TEXT("roost") : TEXT("wall"));
				return;
			}
			Clip->Subject = nullptr;
		}
		else
		{
			// The gun drawn from the start (the pawn begins with none in hand).
			Pawn->Guns->UnlockWeapon(Clip->bShotgun ? 1 : 0);
			Pawn->Guns->SelectWeapon(Clip->bShotgun ? 1 : 0);
			FVector Spot;
			InFront(Clip->Subject.Get(), Home, 1400.0f, Spot);
			Teleport(Pawn, Spot);
			Clip->View = (Clip->Subject->GetActorLocation() - Spot).Rotation();
		}
		Clip->Recorder = MakeUnique<FJamClipRecorder>();
		FString Error;
		const FString Video = FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir() / TEXT("Clips/FlyerClip.mp4"));
		if (!Clip->Recorder->Start(World, Rest.Num() > 1 ? Rest[1] : FindFfmpeg(), Video, 1280, Error))
		{
			UE_LOG(LogMindKore, Warning, TEXT("Flyer clip: %s"), *Error);
			return;
		}
		Clip->Started = World->GetTimeSeconds();
		GClip = Clip;
		TWeakObjectPtr<UWorld> WeakWorld = World;
		Clip->Ticker = FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([WeakWorld](float DeltaSeconds)
		{
			return WeakWorld.IsValid() && ClipTick(WeakWorld.Get(), DeltaSeconds);
		}));
		if (Clip->Director)
		{
			UE_LOG(LogMindKore, Log, TEXT("Flyer clip: %.0f s take, the %s scene"), Clip->Seconds, bRoost ? TEXT("roost") : TEXT("wall"));
			return;
		}
		// A second of it flying, then the flock hunts (the take is about the attack, not the search).
		TWeakObjectPtr<AJamFlyer> Subject = Clip->Subject;
		TWeakObjectPtr<AJamFlyerHabitat> WeakHome = Home;
		After(World, 1.0f, [Subject, WeakHome]()
		{
			if (AJamFlyer* Flyer = Subject.Get(); Flyer && WeakHome.IsValid())
			{
				WeakHome->Provoke(EJamFlockMood::Hunting, TEXT("clip: the player in front"), Flyer->GetActorLocation(), Flyer);
			}
		});
		UE_LOG(LogMindKore, Log, TEXT("Flyer clip: %.0f s take, the player 14 m ahead of %s"), Clip->Seconds, *Clip->Subject->GetName());
	}

	FAutoConsoleCommandWithWorldAndArgs GFlyerClip(
		TEXT("jam.Flyer.Clip"),
		TEXT("jam.Flyer.Clip [Seconds] [Ffmpeg] [shotgun] [stat] [roost|wall]: a recorded take (video and sound) of a fly-by attack - or the roost waking and hunting, or the flesh wall's pain call and the flock coming - the voices in the log; Saved/Clips (needs a real RHI and a window)."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&RunFlyerClip));

	/**
	 * jam.Flyer.MixCheck [DistanceCm] [noatt] [shotgun]: the hit against the player's own shot, apart
	 * (needs sound). The first habitat's first flyer is held still DistanceCm (1000) ahead of the
	 * player; the master submix is recorded for 6.5 s: the pistol is drawn, at 2.0 s it fires into
	 * empty space, at 4.0 s a 25-damage round is delivered into the flyer (its hit sound alone, no
	 * gun). "shotgun": the shotgun's shot, then one blast's 8 pellets of 12 over three frames.
	 * Sound: Saved/Clips/FlyerMixCheck.wav; the log gives the moments.
	 */
	void RunFlyerMixCheck(const TArray<FString>& Args, UWorld* World)
	{
		AJamFlyerHabitat* Home = nullptr;
		for (TActorIterator<AJamFlyerHabitat> It(World); It && !Home; ++It)
		{
			Home = *It;
		}
		AJamZeroGPawn* Pawn = ProbePawn(World);
		if (!Home || Home->GetFlock().IsEmpty() || !Home->GetFlock()[0].IsValid() || !Pawn)
		{
			UE_LOG(LogMindKore, Warning, TEXT("Flyer mix check: needs the zero-G pawn and a habitat with flyers"));
			return;
		}
		const float Distance = Args.Num() > 0 && FCString::Atof(*Args[0]) > 0.0f ? FCString::Atof(*Args[0]) : 1000.0f;
		// "shotgun": its shot into empty space, then one blast's pellets into the flyer. The pawn starts
		// with no gun in hand: the draw takes the first second, so everything runs 1.5 s later.
		const bool bShotgun = Args.Contains(TEXT("shotgun"));
		const float Later = 1.5f;
		Pawn->Guns->UnlockWeapon(bShotgun ? 1 : 0);
		Pawn->Guns->SelectWeapon(bShotgun ? 1 : 0);
		for (const TWeakObjectPtr<AJamFlyer>& Weak : Home->GetFlock())
		{
			if (AJamFlyer* Other = Weak.Get())
			{
				Other->SetActorTickEnabled(false); // no wings, no voices: the hit alone
				Other->Body->SetPhysicsLinearVelocity(FVector::ZeroVector);
			}
		}
		AJamFlyer* Flyer = Home->GetFlock()[0].Get();
		// The round makes it angry: its screeches would sound with the hit, so they are silenced here.
		Flyer->AggroSound.Layers.Reset();
		Flyer->DiveSound.Layers.Reset();
		Flyer->DeathSound.Layers.Reset(); // a blast kills it
		// Diagnostics: "noatt" turns the attenuation off (the raw level, panned).
		if (Args.Contains(TEXT("noatt")))
		{
			Flyer->ImpactAttenuation.bAttenuate = false;
			Flyer->ImpactAttenuation.bAttenuateWithLPF = false;
		}
		// The same place every run, the line between them checked: occlusion would muffle the hit.
		const FVector FlyerAt(1000.0f, 600.0f, 1800.0f);
		Teleport(Flyer, FlyerAt);
		Teleport(Pawn, FlyerAt - FVector::ForwardVector * Distance);
		FCollisionQueryParams Line(SCENE_QUERY_STAT(JamFlyerMixCheck), false, Flyer);
		Line.AddIgnoredActor(Pawn);
		FHitResult Blocker;
		const bool bOccluded = World->LineTraceSingleByChannel(Blocker, Pawn->GetActorLocation(), FlyerAt, ECC_Visibility, Line);
		UE_LOG(LogMindKore, Log, TEXT("Flyer mix check: the line to the flyer is %s%s"), bOccluded ? TEXT("blocked by ") : TEXT("clear"),
			bOccluded ? *GetNameSafe(Blocker.GetActor()) : TEXT(""));
		if (IConsoleVariable* Never = IConsoleManager::Get().FindConsoleVariable(TEXT("au.NeverDisableSubmixes")))
		{
			Never->Set(1, ECVF_SetByCode);
		}
		UAudioMixerBlueprintLibrary::StartRecordingOutput(World, 10.0f);
		UE_LOG(LogMindKore, Log, TEXT("Flyer mix check: recording; %s held %.0f m ahead"), *Flyer->GetName(), Distance / 100.0f);
		After(World, 0.5f + Later, [World, Later]()
		{
			if (AJamZeroGPawn* Now = ProbePawn(World))
			{
				Now->GetController()->SetControlRotation(FRotator(0.0f, 180.0f, 0.0f)); // away from the flyer: nothing hit
				UE_LOG(LogMindKore, Log, TEXT("Flyer mix check: +%.1f s the %s, %s"), 0.5f + Later, *Now->Guns->Describe(), Now->Fire() ? TEXT("fired") : TEXT("NOT fired"));
			}
		});
		TWeakObjectPtr<AJamFlyer> Weak = Flyer;
		// The pistol: one 25-damage round. The shotgun: its 8 pellets of 12, landing over three frames as flying pellets do.
		const int32 Rounds = bShotgun ? 8 : 1;
		for (int32 Index = 0; Index < Rounds; ++Index)
		{
			After(World, 2.5f + Later + (Index % 3) * 0.017f, [World, Weak, bShotgun, Index]()
			{
				AJamFlyer* Target = Weak.Get();
				if (!Target)
				{
					return;
				}
				FMKResolvedImpact Round;
				Round.ShotId = FGuid::NewGuid();
				Round.Kind = EMKImpactKind::Round;
				Round.DamageAmount = bShotgun ? 12.0f : 25.0f;
				Round.Hit = FHitResult(Target, Target->Body, Target->GetActorLocation(), FVector::BackwardVector);
				Round.SourceActor = ProbePawn(World);
				MKDeliverResolvedImpact(World, ProbePawn(World), Round);
				UE_LOG(LogMindKore, Log, TEXT("Flyer mix check: round %d (%.0f damage) into %s"), Index + 1, Round.DamageAmount, *Target->GetName());
			});
		}
		After(World, 5.0f + Later, [World]()
		{
			const FString Dir = FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir() / TEXT("Clips"));
			UAudioMixerBlueprintLibrary::StopRecordingOutput(World, EAudioRecordingExportType::WavFile, TEXT("FlyerMixCheck"), Dir);
			UE_LOG(LogMindKore, Log, TEXT("Flyer mix check: done - %s/FlyerMixCheck.wav"), *Dir);
		});
	}

	FAutoConsoleCommandWithWorldAndArgs GFlyerMixCheck(
		TEXT("jam.Flyer.MixCheck"),
		TEXT("jam.Flyer.MixCheck [DistanceCm] [noatt] [shotgun]: record the player's shot and, apart, a round (or a blast's pellets) into a flyer held that far ahead (Saved/Clips/FlyerMixCheck.wav; needs sound)."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&RunFlyerMixCheck));

	/**
	 * jam.Flyer.Shots: rendered stills (needs a real RHI). The flyers stop noticing
	 * the player; the camera rides along the first habitat's first flyer - ahead
	 * above, below, behind above, wider - then looks over the flock.
	 * "Shot" every 3 s; files in Saved/Screenshots.
	 */
	void RunFlyerShots(UWorld* World)
	{
		AJamFlyerHabitat* Home = nullptr;
		for (TActorIterator<AJamFlyerHabitat> It(World); It && !Home; ++It)
		{
			Home = *It;
		}
		if (!Home || Home->GetFlock().IsEmpty() || !ProbePawn(World))
		{
			UE_LOG(LogMindKore, Warning, TEXT("Flyer shots: needs the zero-G pawn and a habitat with flyers"));
			return;
		}
		for (const TWeakObjectPtr<AJamFlyer>& Weak : Home->GetFlock())
		{
			if (AJamFlyer* Flyer = Weak.Get())
			{
				Flyer->SightRangeCm = 0.0f;
			}
		}
		TWeakObjectPtr<AJamFlyer> Subject = Home->GetFlock()[0];
		TWeakObjectPtr<AJamFlyerHabitat> WeakHome = Home;
		struct FView { const TCHAR* Name; FVector Offset; }; // flyer frame: X forward, Y right, Z up
		const TArray<FView> Views = {
			{ TEXT("front_above"), FVector(320.0f, 220.0f, 130.0f) },
			{ TEXT("below"), FVector(40.0f, 90.0f, -320.0f) },
			{ TEXT("behind_above"), FVector(-380.0f, 140.0f, 220.0f) },
			{ TEXT("wide"), FVector(900.0f, -300.0f, 350.0f) },
			{ TEXT("flock"), FVector::ZeroVector },
		};
		for (int32 Index = 0; Index < Views.Num(); ++Index)
		{
			const FView View = Views[Index];
			// Ride along every 10 ms for 2.5 s, then the shot.
			for (int32 Tick = 0; Tick < 250; ++Tick)
			{
				After(World, Index * 3.0f + Tick * 0.01f, [World, Subject, WeakHome, View]()
				{
					AJamZeroGPawn* Pawn = ProbePawn(World);
					const AJamFlyer* Flyer = Subject.Get();
					const AJamFlyerHabitat* Home = WeakHome.Get();
					if (!Pawn || !Flyer || !Home)
					{
						return;
					}
					FVector Eye, Look;
					if (View.Offset.IsZero())
					{
						const FVector Up = Home->InnerRadiusCm > 0.0f ? (Flyer->GetActorLocation() - Home->GetActorLocation()).GetSafeNormal() : FVector::UpVector;
						Eye = Flyer->GetActorLocation() + Up * 1500.0f + (Flyer->GetActorLocation() - Home->GetActorLocation()).GetSafeNormal() * 500.0f;
						Look = Home->InnerRadiusCm > 0.0f ? Flyer->GetActorLocation() : Home->GetActorLocation();
					}
					else
					{
						Eye = Flyer->Visual->GetComponentTransform().TransformPosition(View.Offset);
						Look = Flyer->GetActorLocation();
					}
					Teleport(Pawn, Eye - (Pawn->Camera->GetComponentLocation() - Pawn->GetActorLocation()));
					Pawn->GetController()->SetControlRotation((Look - Eye).Rotation());
				});
			}
			After(World, Index * 3.0f + 2.6f, [World, Subject, View, Index]()
			{
				const AJamFlyer* Flyer = Subject.Get();
				UE_LOG(LogMindKore, Log, TEXT("Flyer shots: %d %s, %s %s at %.0f cm/s"), Index, View.Name, *GetNameSafe(Flyer),
					Flyer ? AJamFlyer::StateName(Flyer->GetState()) : TEXT("-"), Flyer ? Flyer->GetVelocity().Size() : 0.0f);
				World->GetFirstPlayerController()->ConsoleCommand(TEXT("Shot"));
			});
		}
	}

	FAutoConsoleCommandWithWorld GFlyerShots(
		TEXT("jam.Flyer.Shots"),
		TEXT("Rendered stills riding along a flyer (ahead above, below, behind above, wider) and over its flock (Shot; needs a real RHI)."),
		FConsoleCommandWithWorldDelegate::CreateStatic(&RunFlyerShots));

	// --- the roost, the first EVA, the encounter (Lvl_Jam_Ship) ------------------------------------------

	/** No engine warnings printed over the frame (the VSM light-overflow line) in stills and takes. */
	void QuietScreen(UWorld* World)
	{
		if (APlayerController* Controller = World->GetFirstPlayerController())
		{
			Controller->ConsoleCommand(TEXT("DisableAllScreenMessages"));
		}
	}

	AJamFlyerHabitat* FindHabitat(UWorld* World, const FString& Filter)
	{
		for (TActorIterator<AJamFlyerHabitat> It(World); It; ++It)
		{
#if WITH_EDITOR
			const FString Label = It->GetActorLabel();
#else
			const FString Label;
#endif
			if (Filter.IsEmpty() || It->GetName().Contains(Filter) || Label.Contains(Filter))
			{
				return *It;
			}
		}
		return nullptr;
	}

	/** The first roost habitat (or the named one). */
	AJamFlyerHabitat* FindRoost(UWorld* World, const FString& Filter)
	{
		for (TActorIterator<AJamFlyerHabitat> It(World); It; ++It)
		{
#if WITH_EDITOR
			const FString Label = It->GetActorLabel();
#else
			const FString Label;
#endif
			if (It->IsRoost() && (Filter.IsEmpty() || It->GetName().Contains(Filter) || Label.Contains(Filter)))
			{
				return *It;
			}
		}
		return nullptr;
	}

	/** The first living wall that calls a flock. */
	AJamLivingWall* FindLinkedWall(UWorld* World)
	{
		for (TActorIterator<AJamLivingWall> It(World); It; ++It)
		{
			if (!It->LinkedHabitats.IsEmpty())
			{
				return *It;
			}
		}
		return nullptr;
	}

	/** The camera at Eye looking at Look (the pawn moved so its camera is there). */
	void PlaceEye(AJamZeroGPawn* Pawn, const FVector& Eye, const FVector& Look)
	{
		Teleport(Pawn, Eye - (Pawn->Camera->GetComponentLocation() - Pawn->GetActorLocation()));
		Pawn->GetController()->SetControlRotation((Look - Eye).Rotation());
	}

	/**
	 * Just outside the airlock's outer door, and the way out: the door whose label holds "Airlock" and
	 * lies farthest from the PlayerStart (in the cabin) along its own thin axis.
	 */
	bool AirlockExit(UWorld* World, FVector& OutPoint, FVector& OutOut)
	{
		TActorIterator<APlayerStart> Start(World);
		const FVector ShipCentre = Start ? Start->GetActorLocation() : FVector::ZeroVector;
		float Best = -1.0f;
		for (TActorIterator<AMKDoor> It(World); It; ++It)
		{
#if WITH_EDITOR
			if (!It->GetActorLabel().Contains(TEXT("Airlock")))
			{
				continue;
			}
#endif
			// Through the door: its own forward axis (the leaf's normal), outward from the ship's centre.
			const FBox Box = It->GetComponentsBoundingBox(true);
			const FVector Thin = It->GetActorForwardVector().GetSafeNormal2D();
			const float Along = FVector::DotProduct(Box.GetCenter() - ShipCentre, Thin);
			if (FMath::Abs(Along) > Best)
			{
				Best = FMath::Abs(Along);
				OutOut = Thin * FMath::Sign(Along);
				OutPoint = Box.GetCenter() + OutOut * 200.0f;
			}
		}
		return Best >= 0.0f;
	}

	/** The player's ship: what a line down onto the PlayerStart (in the cabin) hits first - its roof (not its attach root: the USD import's root is an empty actor). */
	const AActor* ShipHull(UWorld* World)
	{
		TActorIterator<APlayerStart> Start(World);
		if (!Start)
		{
			return nullptr;
		}
		FCollisionQueryParams Params(SCENE_QUERY_STAT(JamShipHull), true);
		if (const APawn* Pawn = ProbePawn(World))
		{
			Params.AddIgnoredActor(Pawn);
		}
		FHitResult Hit;
		if (!World->LineTraceSingleByChannel(Hit, Start->GetActorLocation() + FVector(0, 0, 5000), Start->GetActorLocation(), ECC_Visibility, Params))
		{
			return nullptr;
		}
		return Hit.GetActor();
	}

	/** Flies the pawn along Points at Speed (teleported a step at a time, its velocity kept), then Done. */
	void FlyPath(UWorld* World, TArray<FVector> Points, float Speed, TFunction<void(const FVector&, const FVector&)> OnStep, TFunction<void()> Done)
	{
		struct FWalk
		{
			TArray<FVector> Points;
			int32 Leg = 0;
			FVector At = FVector::ZeroVector;
		};
		TSharedRef<FWalk> Walk = MakeShared<FWalk>();
		Walk->Points = MoveTemp(Points);
		Walk->At = Walk->Points[0];
		if (AJamZeroGPawn* Start = ProbePawn(World))
		{
			Teleport(Start, Walk->At); // the start as it is (out of the airlock: through the closed door)
		}
		Poll(World, 0.02f, 600.0f, [World, Walk, Speed, OnStep]()
		{
			AJamZeroGPawn* Pawn = ProbePawn(World);
			if (!Pawn || Walk->Leg + 1 >= Walk->Points.Num())
			{
				return true;
			}
			float Left = Speed * 0.02f;
			FVector Dir = FVector::ForwardVector;
			while (Left > 0.0f && Walk->Leg + 1 < Walk->Points.Num())
			{
				const FVector To = Walk->Points[Walk->Leg + 1] - Walk->At;
				Dir = To.GetSafeNormal(UE_SMALL_NUMBER, Dir);
				if (To.Size() <= Left)
				{
					Left -= To.Size();
					Walk->At = Walk->Points[++Walk->Leg];
				}
				else
				{
					Walk->At += Dir * Left;
					Left = 0.0f;
				}
			}
			// Swept: a rock or a hull on the way stops the body instead of throwing it out of a penetration.
			Pawn->SetActorLocation(Walk->At, /*bSweep=*/true, nullptr, ETeleportType::TeleportPhysics);
			if (UPrimitiveComponent* Root = Cast<UPrimitiveComponent>(Pawn->GetRootComponent()); Root && Root->IsSimulatingPhysics())
			{
				Root->SetPhysicsLinearVelocity(Dir * Speed);
			}
			if (Pawn->Vitals->Health < 50.0f)
			{
				Pawn->Vitals->Restore(100.0f, 0.0f);
			}
			if (OnStep)
			{
				OnStep(Walk->At, Dir);
			}
			return false;
		}, [Done](bool) { Done(); });
	}

	int32 FlockBites(const AJamFlyerHabitat* Home)
	{
		int32 Bites = 0;
		for (const TWeakObjectPtr<AJamFlyer>& Weak : Home->GetFlock())
		{
			Bites += Weak.IsValid() ? Weak->GetBiteCount() : 0;
		}
		return Bites;
	}

	/** The flock's screeches begun so far: calls (bDive false) or dives. */
	int32 FlockScreeches(const AJamFlyerHabitat* Home, bool bDive)
	{
		int32 Count = 0;
		for (const TWeakObjectPtr<AJamFlyer>& Weak : Home->GetFlock())
		{
			Count += Weak.IsValid() ? (bDive ? Weak->GetDiveScreeches() : Weak->GetCallScreeches()) : 0;
		}
		return Count;
	}

	FString DescribeFlock(const AJamFlyerHabitat* Home)
	{
		TMap<FString, int32> Count;
		for (const TWeakObjectPtr<AJamFlyer>& Weak : Home->GetFlock())
		{
			if (Weak.IsValid())
			{
				++Count.FindOrAdd(AJamFlyer::StateName(Weak->GetState()));
			}
		}
		FString Line = FString::Printf(TEXT("%s mood %s:"), *Home->GetName(), JamFlyerParts::MoodName(Home->GetMood()));
		for (const TPair<FString, int32>& Each : Count)
		{
			Line += FString::Printf(TEXT(" %d %s"), Each.Value, *Each.Key);
		}
		return Line;
	}

	/** jam.Flyer.Cost [Seconds]: the flyers' tick on the game thread over Seconds (5): clinging and the rest, per frame and per tick. */
	void RunFlyerCost(const TArray<FString>& Args, UWorld* World)
	{
		const float Seconds = Args.Num() > 0 ? FMath::Max(1.0f, FCString::Atof(*Args[0])) : 5.0f;
		AJamFlyer::TickSecondsRoost = AJamFlyer::TickSecondsOther = 0.0;
		AJamFlyer::TicksRoost = AJamFlyer::TicksOther = 0;
		const uint64 Frame = GFrameCounter;
		const double Real = FPlatformTime::Seconds();
		After(World, Seconds, [World, Frame, Real]()
		{
			const int32 Frames = int32(GFrameCounter - Frame);
			int32 Clinging = 0, Flying = 0;
			for (TActorIterator<AJamFlyer> It(World); It; ++It)
			{
				Clinging += It->GetState() == EJamFlyerState::Roost ? 1 : 0;
				Flying += It->GetState() != EJamFlyerState::Roost && It->GetState() != EJamFlyerState::Dead ? 1 : 0;
			}
			UE_LOG(LogMindKore, Log, TEXT("Flyer cost: %d frames in %.1f s real - %d clinging: %.3f ms a frame (%d ticks, %.1f us each); %d flying: %.3f ms a frame (%d ticks, %.1f us each)"),
				Frames, float(FPlatformTime::Seconds() - Real), Clinging, AJamFlyer::TickSecondsRoost * 1000.0 / FMath::Max(Frames, 1), AJamFlyer::TicksRoost,
				AJamFlyer::TickSecondsRoost * 1e6 / FMath::Max(AJamFlyer::TicksRoost, 1), Flying, AJamFlyer::TickSecondsOther * 1000.0 / FMath::Max(Frames, 1),
				AJamFlyer::TicksOther, AJamFlyer::TickSecondsOther * 1e6 / FMath::Max(AJamFlyer::TicksOther, 1));
		});
	}

	FAutoConsoleCommandWithWorldAndArgs GFlyerCost(
		TEXT("jam.Flyer.Cost"),
		TEXT("jam.Flyer.Cost [Seconds]: the flyers' tick cost on the game thread (clinging and the rest), per frame and per tick."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&RunFlyerCost));

	/**
	 * jam.Flyer.Views <file>: stills from the views listed in the file (Saved/... or absolute), one per
	 * line "name x y z tx ty tz [seconds]" (cm; the camera at x y z looking at tx ty tz; # comments):
	 * the camera holds each view for the seconds given (2.5) and "Shot" at its end (needs a real RHI).
	 */
	void RunFlyerViews(const TArray<FString>& Args, UWorld* World)
	{
		QuietScreen(World);
		TArray<FString> Lines;
		if (Args.IsEmpty() || !FFileHelper::LoadFileToStringArray(Lines, *Args[0]))
		{
			UE_LOG(LogMindKore, Warning, TEXT("Flyer views: cannot read %s"), Args.IsEmpty() ? TEXT("(no file)") : *Args[0]);
			return;
		}
		// "blind": nobody notices the player (a still of the flock at rest, not of a scanner in the lens).
		if (Args.Contains(TEXT("blind")))
		{
			for (TActorIterator<AJamFlyer> It(World); It; ++It)
			{
				It->SightRangeCm = 0.0f;
			}
		}
		float At = 1.0f;
		for (const FString& Line : Lines)
		{
			TArray<FString> F;
			Line.TrimStartAndEnd().ParseIntoArrayWS(F);
			if (F.Num() < 7 || F[0].StartsWith(TEXT("#")))
			{
				continue;
			}
			const FString Name = F[0];
			const FVector Eye(FCString::Atof(*F[1]), FCString::Atof(*F[2]), FCString::Atof(*F[3]));
			const FVector Look(FCString::Atof(*F[4]), FCString::Atof(*F[5]), FCString::Atof(*F[6]));
			const float Hold = F.Num() > 7 ? FCString::Atof(*F[7]) : 2.5f;
			const int32 Ticks = FMath::Max(1, FMath::RoundToInt(Hold / 0.05f));
			for (int32 Tick = 0; Tick < Ticks; ++Tick)
			{
				After(World, At + Tick * 0.05f, [World, Eye, Look]()
				{
					if (AJamZeroGPawn* Pawn = ProbePawn(World))
					{
						PlaceEye(Pawn, Eye, Look);
					}
				});
			}
			After(World, At + Hold, [World, Name]()
			{
				FString Flock;
				for (TActorIterator<AJamFlyerHabitat> It(World); It; ++It)
				{
					Flock += TEXT(" ") + DescribeFlock(*It) + TEXT(";");
				}
				UE_LOG(LogMindKore, Log, TEXT("Flyer views: %s -%s"), *Name, *Flock);
				World->GetFirstPlayerController()->ConsoleCommand(TEXT("Shot"));
			});
			At += Hold + 0.6f;
		}
		After(World, At + 1.0f, []() { UE_LOG(LogMindKore, Log, TEXT("Flyer views: done")); });
	}

	FAutoConsoleCommandWithWorldAndArgs GFlyerViews(
		TEXT("jam.Flyer.Views"),
		TEXT("jam.Flyer.Views <file> [blind]: stills from listed views (name x y z tx ty tz [seconds]; Shot; needs a real RHI); blind: no flyer notices the player."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&RunFlyerViews));

	/** jam.Flyer.Rays x y z: from that point, 98 rays (Visibility, complex) and a 40 cm sphere sweep each: what is hit how far; the open ways out (for placing an encounter). */
	void RunFlyerRays(const TArray<FString>& Args, UWorld* World)
	{
		if (Args.Num() < 3)
		{
			return;
		}
		const FVector From(FCString::Atof(*Args[0]), FCString::Atof(*Args[1]), FCString::Atof(*Args[2]));
		FCollisionQueryParams Params(SCENE_QUERY_STAT(JamFlyerRays), true);
		if (const APawn* Pawn = ProbePawn(World))
		{
			Params.AddIgnoredActor(Pawn);
		}
		for (int32 Index = 0; Index < 98; ++Index)
		{
			// A Fibonacci sphere: even directions.
			const float Y = 1.0f - 2.0f * (Index + 0.5f) / 98.0f;
			const float R = FMath::Sqrt(1.0f - Y * Y);
			const float Phi = Index * 2.39996323f;
			const FVector Dir(FMath::Cos(Phi) * R, FMath::Sin(Phi) * R, Y);
			FHitResult Hit, Swept;
			const bool bHit = World->LineTraceSingleByChannel(Hit, From, From + Dir * 5000.0f, ECC_Visibility, Params);
			const bool bSwept = World->SweepSingleByChannel(Swept, From, From + Dir * 5000.0f, FQuat::Identity, ECC_Pawn, FCollisionShape::MakeSphere(40.0f), Params);
			UE_LOG(LogMindKore, Log, TEXT("Flyer rays: dir %s - line %s %.0f cm (%s at %s, normal %s), sweep %s %.0f cm (%s)"), *Dir.ToCompactString(), bHit ? TEXT("hit") : TEXT("open"),
				bHit ? Hit.Distance : 5000.0f, bHit ? *GetNameSafe(Hit.GetComponent()) : TEXT("-"), *FVector(Hit.ImpactPoint).ToCompactString(), *FVector(Hit.ImpactNormal).ToCompactString(),
				bSwept ? TEXT("hit") : TEXT("open"), bSwept ? Swept.Distance : 5000.0f, bSwept ? *GetNameSafe(Swept.GetComponent()) : TEXT("-"));
		}
		UE_LOG(LogMindKore, Log, TEXT("Flyer rays: done"));
	}

	FAutoConsoleCommandWithWorldAndArgs GFlyerRays(
		TEXT("jam.Flyer.Rays"),
		TEXT("jam.Flyer.Rays x y z: 98 rays and 40 cm sweeps out of a point - what they hit how far (finding a way in)."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&RunFlyerRays));

	/** jam.Flyer.Portraits [Count] [Habitat]: stills of clinging flyers from the side, 4 m off, one each (the pose; needs a real RHI). */
	void RunFlyerPortraits(const TArray<FString>& Args, UWorld* World)
	{
		QuietScreen(World);
		AJamFlyerHabitat* Home = FindRoost(World, Args.Num() > 1 ? Args[1] : TEXT(""));
		const int32 Count = Args.Num() > 0 ? FCString::Atoi(*Args[0]) : 4;
		if (!Home || !ProbePawn(World))
		{
			UE_LOG(LogMindKore, Warning, TEXT("Flyer portraits: needs a roost habitat and the pawn"));
			return;
		}
		for (TActorIterator<AJamFlyer> It(World); It; ++It)
		{
			It->SightRangeCm = 0.0f;
		}
		TArray<TWeakObjectPtr<AJamFlyer>> Clinging;
		for (const TWeakObjectPtr<AJamFlyer>& Weak : Home->GetFlock())
		{
			if (Weak.IsValid() && Weak->GetState() == EJamFlyerState::Roost && Clinging.Num() < Count)
			{
				Clinging.Add(Weak);
			}
		}
		for (int32 Index = 0; Index < Clinging.Num(); ++Index)
		{
			const TWeakObjectPtr<AJamFlyer> Subject = Clinging[Index];
			for (int32 Tick = 0; Tick < 50; ++Tick)
			{
				After(World, 1.0f + Index * 3.0f + Tick * 0.05f, [World, Subject, Index]()
				{
					AJamZeroGPawn* Pawn = ProbePawn(World);
					if (!Pawn || !Subject.IsValid())
					{
						return;
					}
					// From the side of the spike, a little out from the hull, looking at its middle.
					const FVector Spike = Subject->Visual->GetForwardVector();
					const FVector Side = Subject->Visual->GetRightVector() * (Index % 2 ? -1.0f : 1.0f);
					const FVector Mid = Subject->GetActorLocation() + Spike * 60.0f;
					PlaceEye(Pawn, Mid + Side * 400.0f + Spike * 120.0f, Mid);
				});
			}
			After(World, 1.0f + Index * 3.0f + 2.6f, [World, Subject, Index]()
			{
				UE_LOG(LogMindKore, Log, TEXT("Flyer portraits: %d %s %s spot %d"), Index, *GetNameSafe(Subject.Get()), Subject.IsValid() ? AJamFlyer::StateName(Subject->GetState()) : TEXT("-"),
					Subject.IsValid() ? Subject->GetRoostSpot() : -1);
				World->GetFirstPlayerController()->ConsoleCommand(TEXT("Shot"));
			});
		}
		After(World, 1.0f + Clinging.Num() * 3.0f + 1.0f, []() { UE_LOG(LogMindKore, Log, TEXT("Flyer portraits: done")); });
	}

	FAutoConsoleCommandWithWorldAndArgs GFlyerPortraits(
		TEXT("jam.Flyer.Portraits"),
		TEXT("jam.Flyer.Portraits [Count] [Habitat]: side stills of clinging flyers (the roost pose; Shot; needs a real RHI)."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&RunFlyerPortraits));

	/**
	 * jam.Flyer.WakeStrip [x y z tx ty tz]: a frame strip of the roost waking (needs a real RHI): the camera
	 * at the view given (the airlock's exit, looking along the hull, when none), 2 s of the roost at rest,
	 * then a round into the hull by the densest clinging (contact: the cascade) and "Shot" every 0.25 s for 5 s.
	 */
	void RunFlyerWakeStrip(const TArray<FString>& Args, UWorld* World)
	{
		QuietScreen(World);
		AJamFlyerHabitat* Home = FindRoost(World, TEXT(""));
		AJamZeroGPawn* Pawn = ProbePawn(World);
		FVector Exit, Out;
		if (!Home || !Pawn || !AirlockExit(World, Exit, Out))
		{
			UE_LOG(LogMindKore, Warning, TEXT("Flyer wake strip: needs a roost habitat, the pawn and an airlock door"));
			return;
		}
		// The reveal (stills 19.10): just out of the door, looking aft down the flank that bristles, the Milky Way behind.
		FVector Eye = Exit + Out * 100.0f + FVector(0, 0, 60), Look = Home->RoostFocus + Out * 250.0f - FVector(0, 0, 290);
		if (Args.Num() >= 6)
		{
			Eye = FVector(FCString::Atof(*Args[0]), FCString::Atof(*Args[1]), FCString::Atof(*Args[2]));
			Look = FVector(FCString::Atof(*Args[3]), FCString::Atof(*Args[4]), FCString::Atof(*Args[5]));
		}
		Pawn->Guns->UnlockWeapon(0);
		Pawn->Guns->SelectWeapon(0);
		TWeakObjectPtr<AJamFlyerHabitat> WeakHome = Home;
		Poll(World, 0.02f, 8.5f, [World, Eye, Look]()
		{
			if (AJamZeroGPawn* Now = ProbePawn(World))
			{
				PlaceEye(Now, Eye, Look);
			}
			return false;
		}, [](bool) {});
		After(World, 2.0f, [World, WeakHome]()
		{
			AJamFlyerHabitat* H = WeakHome.Get();
			if (!H)
			{
				return;
			}
			// The round the player would put into the hull: the noise by contact (UJamWeaponsComponent reports it so).
			AJamFlyerHabitat::ReportNoise(World, H->RoostFocus, H->RoostOn.IsEmpty() ? nullptr : H->RoostOn[0].Get(), TEXT("a round landing (strip)"));
			for (int32 Frame = 0; Frame < 20; ++Frame)
			{
				After(World, 0.05f + Frame * 0.25f, [World, WeakHome, Frame]()
				{
					UE_LOG(LogMindKore, Log, TEXT("Flyer wake strip: frame %d at +%.2f s - %s"), Frame, Frame * 0.25f, WeakHome.IsValid() ? *DescribeFlock(WeakHome.Get()) : TEXT("-"));
					World->GetFirstPlayerController()->ConsoleCommand(TEXT("Shot"));
				});
			}
			After(World, 6.0f, []() { UE_LOG(LogMindKore, Log, TEXT("Flyer wake strip: done")); });
		});
	}

	FAutoConsoleCommandWithWorldAndArgs GFlyerWakeStrip(
		TEXT("jam.Flyer.WakeStrip"),
		TEXT("jam.Flyer.WakeStrip [x y z tx ty tz]: frames every 0.25 s of the roost waking in its cascade after a round into its hull (Shot; needs a real RHI)."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&RunFlyerWakeStrip));

	/**
	 * jam.Flyer.RoostProbe [Habitat]: the first EVA (logic; NullRHI is fine). The roost at rest (spots,
	 * clinging, sentries, the tick cost), then the unarmed player out of the airlock, over to the roost (on the
	 * wreck) and back, round the ship's bow to the pistol pickup: no dive, no bite, sentries curious; at the roost
	 * a bump into a clinging one (a warning screech and a feint); at the pickup the pistol; a test shot into space (vacuum: unheard);
	 * back toward the roost and a round into its hull (contact): the cascade, the hunt with its tokens; then the
	 * player only hides for 120 s (RoostHide: where the fight left him, "airlock" or "cabin") - no new cascade.
	 */
	/** The roost spot nearest Where, cm. */
	float NearestSpotCm(const AJamFlyerHabitat* Home, const FVector& Where)
	{
		float Nearest = UE_BIG_NUMBER;
		for (int32 Index = 0; Index < Home->GetSpotCount(); ++Index)
		{
			Nearest = FMath::Min(Nearest, float(FVector::Dist(Home->GetSpot(Index)->Point, Where)));
		}
		return Nearest;
	}

	/** Roost spots within RadiusCm of Where. */
	int32 SpotsWithin(const AJamFlyerHabitat* Home, const FVector& Where, float RadiusCm)
	{
		int32 Count = 0;
		for (int32 Index = 0; Index < Home->GetSpotCount(); ++Index)
		{
			Count += FVector::Dist(Home->GetSpot(Index)->Point, Where) < RadiusCm ? 1 : 0;
		}
		return Count;
	}

	/**
	 * Inside the ship, as close under the roost as it gets: on a 1 m grid inside the gravity zones labelled "Cabin" or
	 * "Airlock" (not the boundary), clear of geometry for the pawn, the point with the most roost spots within the
	 * wake radius (then the nearest spot); false when there is none.
	 */
	bool CabinHide(UWorld* World, const AJamFlyerHabitat* Home, FVector& Out)
	{
		int32 Most = -1;
		float Nearest = UE_BIG_NUMBER;
		for (TActorIterator<AJamGravityZone> It(World); It; ++It)
		{
#if WITH_EDITOR
			const FString Label = It->GetActorLabel();
#else
			const FString Label;
#endif
			if (!(Label.Contains(TEXT("Cabin")) || Label.Contains(TEXT("Airlock"))) || Label.Contains(TEXT("Boundary")))
			{
				continue;
			}
			FVector Origin, Extent;
			It->GetActorBounds(false, Origin, Extent);
			for (float X = -Extent.X; X <= Extent.X; X += 100.0f)
			{
				for (float Y = -Extent.Y; Y <= Extent.Y; Y += 100.0f)
				{
					for (float Z = -Extent.Z; Z <= Extent.Z; Z += 100.0f)
					{
						const FVector P = Origin + FVector(X, Y, Z);
						if (!It->Contains(P, 50.0f))
						{
							continue;
						}
						const int32 Count = SpotsWithin(Home, P, Home->RoostWakeRadiusCm);
						const float Near = NearestSpotCm(Home, P);
						if ((Count > Most || (Count == Most && Near < Nearest))
							&& !World->OverlapAnyTestByChannel(P, FQuat::Identity, ECC_Pawn, FCollisionShape::MakeSphere(60.0f)))
						{
							Most = Count;
							Nearest = Near;
							Out = P;
						}
					}
				}
			}
		}
		return Most >= 0;
	}

	/**
	 * After the fight the player only hides for Seconds - where the fight left him; "airlock": tucked inside the open
	 * airlock looking in; "cabin": inside the ship under the roost (CabinHide), ducking in 4 s after the shot while
	 * the roost still wakes. Nothing he does provokes. Counts what the flock does - cascades begun, flyers into the
	 * hunt, call and dive screeches, bites, landings, the mood's steps - at the half and at the end. PASS: no cascade.
	 */
	void RoostHide(UWorld* World, TWeakObjectPtr<AJamFlyerHabitat> WeakHome, const FString& Hide, float Seconds)
	{
		AJamZeroGPawn* Pawn = ProbePawn(World);
		AJamFlyerHabitat* H = WeakHome.Get();
		FVector Exit, Out, Cabin;
		if (!Pawn || !H)
		{
			return;
		}
		if (Hide == TEXT("airlock") && AirlockExit(World, Exit, Out))
		{
			PlaceEye(Pawn, Exit - Out * 150.0f, Exit - Out * 1000.0f);
		}
		else if (Hide == TEXT("cabin") && CabinHide(World, H, Cabin))
		{
			Teleport(Pawn, Cabin);
			Pawn->GetController()->SetControlRotation(FRotator(-30.0f, 0.0f, 0.0f)); // at the floor
		}
		const float NearestSpot = NearestSpotCm(H, Pawn->GetActorLocation());
		UE_LOG(LogMindKore, Log, TEXT("Flyer roost probe [hide]: the player hides %s at %s, the nearest roost spot %.1f m off, %d spot(s) within the wake radius %.1f m, %.1f m from the centre; %s"),
			Hide == TEXT("airlock") ? TEXT("inside the airlock, looking in") : Hide == TEXT("cabin") ? TEXT("in the ship, under the roost") : TEXT("where the fight left him"), *Pawn->GetActorLocation().ToCompactString(), NearestSpot / 100.0f,
			SpotsWithin(H, Pawn->GetActorLocation(), H->RoostWakeRadiusCm), H->RoostWakeRadiusCm / 100.0f, FVector::Dist(Pawn->GetActorLocation(), H->GetActorLocation()) / 100.0f, *DescribeFlock(H));
		struct FHide
		{
			double Started = 0.0;
			double LastCascade = 0.0;
			int32 Cascades = 0;
			int32 Joined = 0;
			int32 Landed = 0;
			int32 Calls = 0;
			int32 Dives = 0;
			int32 Bites = 0;
			EJamFlockMood Mood = EJamFlockMood::Curious;
			FString Moods;
			TMap<TWeakObjectPtr<AJamFlyer>, EJamFlyerState> Last;
		};
		TSharedRef<FHide> C = MakeShared<FHide>();
		C->Started = World->GetTimeSeconds();
		C->LastCascade = H->CascadeStartedSeconds;
		C->Calls = FlockScreeches(H, false);
		C->Dives = FlockScreeches(H, true);
		C->Bites = FlockBites(H);
		C->Mood = H->GetMood();
		C->Moods = JamFlyerParts::MoodName(C->Mood);
		for (const TWeakObjectPtr<AJamFlyer>& Weak : H->GetFlock())
		{
			if (Weak.IsValid())
			{
				C->Last.Add(Weak, Weak->GetState());
			}
		}
		// The tally so far, at the half and at the end (the probe's timers run a frame late: the window ends by game time).
		const auto Report = [World, WeakHome, C](float T)
		{
			const AJamFlyerHabitat* Hab = WeakHome.Get();
			const AJamZeroGPawn* Now = ProbePawn(World);
			if (Hab)
			{
				UE_LOG(LogMindKore, Log, TEXT("Flyer roost probe [hide %.0f s]: %s - %d cascade(s) begun, %d into the hunt, %d call screech(es), %d dive screech(es), %d bite(s), %d landed; mood %s; %s; the player %.1f m from the nearest spot"),
					T, C->Cascades == 0 ? TEXT("PASS") : TEXT("FAIL"), C->Cascades, C->Joined, FlockScreeches(Hab, false) - C->Calls, FlockScreeches(Hab, true) - C->Dives,
					FlockBites(Hab) - C->Bites, C->Landed, *C->Moods, *DescribeFlock(Hab), Now ? NearestSpotCm(Hab, Now->GetActorLocation()) / 100.0f : -1.0f);
			}
		};
		TSharedRef<bool> bHalf = MakeShared<bool>(false);
		Poll(World, 0.01f, Seconds * 10.0f, [World, WeakHome, C, Seconds, bHalf, Report]()
		{
			const AJamFlyerHabitat* Hab = WeakHome.Get();
			if (!Hab)
			{
				return true;
			}
			const float T = float(World->GetTimeSeconds() - C->Started);
			if (Hab->CascadeStartedSeconds != C->LastCascade)
			{
				C->LastCascade = Hab->CascadeStartedSeconds;
				++C->Cascades;
				UE_LOG(LogMindKore, Log, TEXT("Flyer roost probe [hide +%.1f s]: a cascade begins"), T);
			}
			if (Hab->GetMood() != C->Mood)
			{
				C->Mood = Hab->GetMood();
				C->Moods += FString::Printf(TEXT(" -> %s (+%.1f s)"), JamFlyerParts::MoodName(C->Mood), T);
			}
			for (const TWeakObjectPtr<AJamFlyer>& Weak : Hab->GetFlock())
			{
				if (!Weak.IsValid())
				{
					continue;
				}
				EJamFlyerState& Was = C->Last.FindOrAdd(Weak, Weak->GetState());
				const EJamFlyerState Now = Weak->GetState();
				const bool bWasHunting = Was == EJamFlyerState::Circle || Was == EJamFlyerState::Dive || Was == EJamFlyerState::Retreat || Was == EJamFlyerState::Feint
					|| Was == EJamFlyerState::Stagger || Was == EJamFlyerState::Flee;
				C->Joined += Weak->IsHunting() && !bWasHunting && Now != EJamFlyerState::Curious ? 1 : 0;
				C->Landed += Now == EJamFlyerState::Roost && Was != EJamFlyerState::Roost ? 1 : 0;
				Was = Now;
			}
			if (!*bHalf && T >= Seconds * 0.5f)
			{
				*bHalf = true;
				Report(Seconds * 0.5f);
			}
			return T >= Seconds;
		}, [Seconds, Report](bool)
		{
			Report(Seconds);
			UE_LOG(LogMindKore, Log, TEXT("Flyer roost probe: done"));
		});
	}

	void RunFlyerRoostProbe(const TArray<FString>& Args, UWorld* World)
	{
		// Args: a part of the roost habitat's name (the first roost by default); "airlock" or "cabin": where the player hides after the fight.
		const FString* HideArg = Args.FindByPredicate([](const FString& Arg) { return Arg == TEXT("airlock") || Arg == TEXT("cabin"); });
		const FString Hide = HideArg ? *HideArg : FString();
		const FString* Filter = Args.FindByPredicate([](const FString& Arg) { return Arg != TEXT("airlock") && Arg != TEXT("cabin"); });
		AJamFlyerHabitat* Home = FindRoost(World, Filter ? *Filter : TEXT(""));
		AJamZeroGPawn* Pawn = ProbePawn(World);
		FVector Exit, Out;
		AJamWeaponPickup* Pistol = nullptr;
		for (TActorIterator<AJamWeaponPickup> It(World); It && !Pistol; ++It)
		{
			Pistol = It->WeaponIndex == 0 ? *It : nullptr;
		}
		if (!Home || !Pawn || !AirlockExit(World, Exit, Out) || !Pistol)
		{
			UE_LOG(LogMindKore, Warning, TEXT("Flyer roost probe: needs a roost habitat, the pawn, the airlock and the pistol pickup"));
			return;
		}
		JamFlyerParts::CVarLogFlaps->Set(false, ECVF_SetByCode);
		TWeakObjectPtr<AJamFlyerHabitat> WeakHome = Home;
		UE_LOG(LogMindKore, Log, TEXT("Flyer roost probe: %s - %d spots, %s; the player %s; the airlock exit %s, out %s; the pistol at %s; hide %s"), *Home->GetName(),
			Home->GetSpotCount(), *DescribeFlock(Home), JamFlyerParts::PlayerArmed(Pawn) ? TEXT("ARMED (the first-EVA rule is off)") : TEXT("unarmed"),
			*Exit.ToCompactString(), *Out.ToCompactString(), *Pistol->GetActorLocation().ToCompactString(), Hide.IsEmpty() ? TEXT("where the fight left him") : *Hide);
		// At rest: the roost seen from inside the open airlock, the cost of its tick.
		PlaceEye(Pawn, Exit - Out * 150.0f, Home->RoostFocus);
		TArray<FString> CostArgs = { TEXT("4") };
		After(World, 1.0f, [World, CostArgs]() { RunFlyerCost(CostArgs, World); });
		After(World, 6.0f, [World, WeakHome, Exit, Out, Hide, Pistol = TWeakObjectPtr<AJamWeaponPickup>(Pistol)]()
		{
			AJamFlyerHabitat* H = WeakHome.Get();
			if (!H || !Pistol.IsValid())
			{
				return;
			}
			// The way: out of the door, clear of the ship, over to the roost (6 m off the clinging one nearest the
			// airlock: the unarmed visit), back past the airlock, round the ship's bow (its -X end) to the pickup.
			const AActor* Ship = ShipHull(World);
			if (!Ship)
			{
				UE_LOG(LogMindKore, Warning, TEXT("Flyer roost probe: no ship hull over the PlayerStart"));
				return;
			}
			FVector Origin, Extent;
			Ship->GetActorBounds(true, Origin, Extent, true);
			const FVector Clear = Exit + Out * 700.0f;
			const FVector Bow(Origin.X - Extent.X - 800.0f, Clear.Y, Clear.Z + 300.0f);
			const FVector Target = Pistol->GetActorLocation() + FVector(0, -150, 0);
			const AJamFlyer* Near = nullptr;
			for (const TWeakObjectPtr<AJamFlyer>& Weak : H->GetFlock())
			{
				if (Weak.IsValid() && Weak->GetState() == EJamFlyerState::Roost
					&& (!Near || FVector::DistSquared(Weak->GetActorLocation(), Exit) < FVector::DistSquared(Near->GetActorLocation(), Exit)))
				{
					Near = Weak.Get();
				}
			}
			const FJamRoostSpot* NearSpot = Near ? H->GetSpot(Near->GetRoostSpot()) : nullptr;
			const FVector Visit = NearSpot ? NearSpot->Point + NearSpot->Normal * 600.0f : Clear;
			const TArray<FVector> Path = { Exit, Clear, Visit, Clear, Bow, Target };
			float Length = 0.0f;
			for (int32 Leg = 1; Leg < Path.Num(); ++Leg)
			{
				Length += FVector::Dist(Path[Leg - 1], Path[Leg]);
			}
			const int32 BitesBefore = FlockBites(H);
			UE_LOG(LogMindKore, Log, TEXT("Flyer roost probe [walk]: unarmed, %.0f m at 4 m/s - out to %s, the roost visit %s (%.0f m from the airlock exit), back, the bow %s of %s (x %.0f..%.0f), the pickup; %s"),
				Length / 100.0f, *Clear.ToCompactString(), *Visit.ToCompactString(), FVector::Dist(Visit, Exit) / 100.0f, *Bow.ToCompactString(), *Ship->GetName(),
				Origin.X - Extent.X, Origin.X + Extent.X, *DescribeFlock(H));
			// The bump: at the roost, brush past the clinging one nearest.
			TSharedRef<bool> bBumped = MakeShared<bool>(false);
			TSharedRef<int32> Steps = MakeShared<int32>(0);
			WatchFlock(World, H, 120.0f, TEXT("first EVA, unarmed"), [WeakHome, bBumped]() { return false; }, []() {});
			FlyPath(World, Path, 400.0f, [World, WeakHome, bBumped, Steps, Visit](const FVector& At, const FVector& Dir)
			{
				AJamZeroGPawn* Now = ProbePawn(World);
				AJamFlyerHabitat* Hab = WeakHome.Get();
				if (!Now || !Hab)
				{
					return;
				}
				// Looks where it goes, glancing at the hull now and then.
				const float Glance = FMath::Sin(++(*Steps) * 0.02f);
				Now->GetController()->SetControlRotation((Dir + (Hab->RoostFocus - At).GetSafeNormal() * FMath::Max(0.0f, Glance)).Rotation());
				if (!*bBumped && FVector::Dist(At, Visit) < 50.0f)
				{
					AJamFlyer* Nearest = nullptr;
					for (const TWeakObjectPtr<AJamFlyer>& Weak : Hab->GetFlock())
					{
						if (Weak.IsValid() && Weak->GetState() == EJamFlyerState::Roost
							&& (!Nearest || FVector::DistSquared(Weak->GetActorLocation(), At) < FVector::DistSquared(Nearest->GetActorLocation(), At)))
						{
							Nearest = Weak.Get();
						}
					}
					if (Nearest)
					{
						*bBumped = true;
						UE_LOG(LogMindKore, Log, TEXT("Flyer roost probe [bump]: the player brushes past %s (%.1f m from the path)"), *Nearest->GetName(), FVector::Dist(Nearest->GetActorLocation(), At) / 100.0f);
						Teleport(Now, Nearest->GetActorLocation() + (At - Nearest->GetActorLocation()).GetSafeNormal() * 120.0f);
					}
				}
			}, [World, WeakHome, BitesBefore, Hide]()
			{
				AJamZeroGPawn* Now = ProbePawn(World);
				AJamFlyerHabitat* Hab = WeakHome.Get();
				if (!Now || !Hab)
				{
					return;
				}
				const int32 Bites = FlockBites(Hab) - BitesBefore;
				UE_LOG(LogMindKore, Log, TEXT("Flyer roost probe [pickup]: %s - %d bite(s) on the way (%s); %s"), Bites == 0 ? TEXT("PASS") : TEXT("FAIL"), Bites,
					Bites == 0 ? TEXT("menace, no attack") : TEXT("an unarmed player was bitten"), *DescribeFlock(Hab));
				Now->Guns->UnlockWeapon(0);
				Now->Guns->SelectWeapon(0);
				// A test shot into space at the pickup: vacuum, nobody hears it.
				After(World, 1.6f, [World]()
				{
					if (AJamZeroGPawn* Shooter = ProbePawn(World))
					{
						Shooter->GetController()->SetControlRotation(FRotator(30.0f, 90.0f, 0.0f));
						UE_LOG(LogMindKore, Log, TEXT("Flyer roost probe [test shot]: %s"), Shooter->Fire() ? TEXT("fired into space") : TEXT("NOT FIRED"));
					}
				});
				// Back to the roost, 12 m off its densest part, and a round into the hull there.
				After(World, 3.0f, [World, WeakHome, Hide]()
				{
					AJamZeroGPawn* Shooter = ProbePawn(World);
					AJamFlyerHabitat* H2 = WeakHome.Get();
					if (!Shooter || !H2)
					{
						return;
					}
					const FJamRoostSpot* Spot = nullptr;
					for (int32 Index = 0; Index < H2->GetSpotCount(); ++Index)
					{
						const FJamRoostSpot* Each = H2->GetSpot(Index);
						if (!Each->Holder.IsValid() && (!Spot || FVector::DistSquared(Each->Point, H2->RoostFocus) < FVector::DistSquared(Spot->Point, H2->RoostFocus)))
						{
							Spot = Each; // a free spot near the focus: the round lands on the hull, not in a flyer
						}
					}
					const FVector Aim = Spot ? Spot->Point : H2->RoostFocus;
					const FVector Eye = Aim + (Spot ? Spot->Normal : FVector::UpVector) * 1200.0f;
					PlaceEye(Shooter, Eye, Aim);
					After(World, 0.3f, [World, Aim]()
					{
						AJamZeroGPawn* S = ProbePawn(World);
						if (S)
						{
							S->GetController()->SetControlRotation((Aim - S->Camera->GetComponentLocation()).Rotation());
							UE_LOG(LogMindKore, Log, TEXT("Flyer roost probe [first shot at the hull]: %s"), S->Fire() ? TEXT("fired") : TEXT("NOT FIRED"));
						}
					});
					// The cabin: he ducks into the ship while the roost is still waking (4 s); otherwise after the fight (14 s).
					WatchFlock(World, H2, Hide == TEXT("cabin") ? 4.0f : 14.0f, TEXT("roost awake"), nullptr, [World, WeakHome, Hide]()
					{
						UE_LOG(LogMindKore, Log, TEXT("Flyer roost probe: the fight is over - %s"), WeakHome.IsValid() ? *DescribeFlock(WeakHome.Get()) : TEXT("-"));
						RoostHide(World, WeakHome, Hide, 120.0f);
					});
				});
			});
		});
	}

	FAutoConsoleCommandWithWorldAndArgs GFlyerRoostProbe(
		TEXT("jam.Flyer.RoostProbe"),
		TEXT("jam.Flyer.RoostProbe [Habitat] [airlock|cabin]: the first EVA - the roost at rest and its cost, the unarmed way from the airlock to the roost and on to the pistol (no bite; a bump at the roost), the pistol, a shot into space, a round into the roost's hull: the cascade; then the player hides 120 s (where the fight left him, inside the airlock, or in the cabin nearest the roost): no new cascade."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&RunFlyerRoostProbe));

	/** In front of the wall on the side the airlock is on, Distance off it. */
	FVector WallFront(const AJamLivingWall* Wall, const FVector& From, float Distance)
	{
		const FVector Normal = Wall->GetFrontNormal();
		const float Side = FVector::DotProduct(From - Wall->GetActorLocation(), Normal) >= 0.0f ? 1.0f : -1.0f;
		return Wall->GetActorLocation() + Normal * Side * Distance;
	}

	/**
	 * jam.Flyer.EncounterProbe: the encounter beat (logic). From the airlock to the linked flesh wall with
	 * the pistol firing into space every 2 s (vacuum: unheard, the flock stays curious); the shotgun into
	 * the wall (the pain call: the scream, the flock hunts and gathers over the wound); back toward the
	 * airlock at 6 m/s (where the flock meets the player); then one shot inside the planetoid's air (heard).
	 */
	void RunFlyerEncounterProbe(const TArray<FString>& Args, UWorld* World)
	{
		AJamLivingWall* Wall = FindLinkedWall(World);
		AJamZeroGPawn* Pawn = ProbePawn(World);
		FVector Exit, Out;
		if (!Wall || !Pawn || !AirlockExit(World, Exit, Out) || !Wall->LinkedHabitats[0].Habitat)
		{
			UE_LOG(LogMindKore, Warning, TEXT("Flyer encounter probe: needs a living wall with a linked habitat, the pawn and the airlock"));
			return;
		}
		JamFlyerParts::CVarLogFlaps->Set(false, ECVF_SetByCode);
		AJamFlyerHabitat* Home = Wall->LinkedHabitats[0].Habitat;
		TWeakObjectPtr<AJamFlyerHabitat> WeakHome = Home;
		TWeakObjectPtr<AJamLivingWall> WeakWall = Wall;
		Pawn->Guns->UnlockWeapon(1);
		Pawn->Guns->UnlockWeapon(0);
		Pawn->Guns->SelectWeapon(0);
		// In by the wreck's open bow (the wall's side the airlock is on): over the debris belt the bow nests in,
		// down to 9 m before the wall, in to 4.5 m.
		const FVector Front = WallFront(Wall, Exit, 450.0f);
		const FVector Bow = WallFront(Wall, Exit, 900.0f);
		const FVector Over = Bow + Wall->GetActorUpVector() * 1000.0f;
		const FVector Clear = Exit + Out * 700.0f;
		const FVector High = (Clear + Over) * 0.5f + FVector(0, 0, 1500.0f);
		const TArray<FVector> In = { Exit, Clear, High, Over, Bow, Front };
		float Route = 0.0f;
		for (int32 Leg = 1; Leg < In.Num(); ++Leg)
		{
			Route += FVector::Dist(In[Leg - 1], In[Leg]);
		}
		UE_LOG(LogMindKore, Log, TEXT("Flyer encounter probe: wall %s at %s, linked %s (arrival delay %.1f s); route %.0f m; %s"), *Wall->GetName(), *Wall->GetActorLocation().ToCompactString(),
			*Home->GetName(), Wall->LinkedHabitats[0].ArrivalDelaySeconds, Route / 100.0f, *DescribeFlock(Home));
		TSharedRef<int32> Steps = MakeShared<int32>(0);
		WatchFlock(World, Home, 1000.0f, TEXT("the way there"), [WeakWall]() { return !WeakWall.IsValid() || WeakWall->GetPainCalls() > 0; }, []() {});
		FlyPath(World, In, 800.0f, [World, Steps, WeakWall](const FVector& At, const FVector& Dir)
		{
			AJamZeroGPawn* Now = ProbePawn(World);
			if (!Now || !WeakWall.IsValid())
			{
				return;
			}
			// Looking up and ahead (not along a flyer: a gun held on one provokes), a shot into space every 2 s while
			// the wreck is more than 30 m off (closer, a round could land on its hull: contact, not the test).
			const bool bShot = ++(*Steps) % 100 == 0 && FVector::Dist(At, WeakWall->GetActorLocation()) > 3000.0f;
			Now->GetController()->SetControlRotation((Dir + FVector(0, 0, 1.2f)).Rotation());
			if (bShot)
			{
				UE_LOG(LogMindKore, Log, TEXT("Flyer encounter probe [there]: a shot into space - %s"), Now->Fire() ? TEXT("fired") : TEXT("not fired"));
			}
		}, [World, WeakHome, WeakWall, Exit, Front, Route, In]()
		{
			AJamZeroGPawn* Now = ProbePawn(World);
			if (!Now || !WeakHome.IsValid() || !WeakWall.IsValid())
			{
				return;
			}
			UE_LOG(LogMindKore, Log, TEXT("Flyer encounter probe [at the wall]: %s; the shotgun out"), *DescribeFlock(WeakHome.Get()));
			Now->Guns->SelectWeapon(1);
			After(World, 1.8f, [World, WeakHome, WeakWall, Exit, Front, Route, In]()
			{
				AJamZeroGPawn* Shooter = ProbePawn(World);
				if (!Shooter || !WeakWall.IsValid())
				{
					return;
				}
				Shooter->GetController()->SetControlRotation((WeakWall->GetActorLocation() - Shooter->Camera->GetComponentLocation()).Rotation());
				UE_LOG(LogMindKore, Log, TEXT("Flyer encounter probe [blast]: %s"), Shooter->Fire() ? TEXT("the shotgun fired into the wall") : TEXT("NOT FIRED"));
				const double Called = World->GetTimeSeconds();
				After(World, 0.6f, [World, WeakHome, WeakWall, Exit, Front, Route, Called, In]()
				{
					UE_LOG(LogMindKore, Log, TEXT("Flyer encounter probe [after the blast]: %d pain call(s); %s"), WeakWall.IsValid() ? WeakWall->GetPainCalls() : -1,
						WeakHome.IsValid() ? *DescribeFlock(WeakHome.Get()) : TEXT("-"));
					// The cost of the woken flock while it gathers and comes (it all flies now).
					After(World, 0.5f, [World]() { RunFlyerCost({ TEXT("5") }, World); });
					// Back to the airlock: where does the flock catch up?
					TSharedRef<bool> bMet = MakeShared<bool>(false);
					TSharedRef<float> Covered = MakeShared<float>(0.0f);
					TArray<FVector> Back = In;
					Algo::Reverse(Back);
					WatchFlock(World, WeakHome.Get(), 60.0f, TEXT("the way back"), [WeakHome]() { return !WeakHome.IsValid(); }, []() {});
					FlyPath(World, Back, 600.0f, [World, WeakHome, bMet, Covered, Front, Route, Called](const FVector& At, const FVector& Dir)
					{
						AJamZeroGPawn* Now = ProbePawn(World);
						AJamFlyerHabitat* H = WeakHome.Get();
						if (!Now || !H)
						{
							return;
						}
						Now->GetController()->SetControlRotation((-Dir).Rotation()); // looking back at the wreck
						*Covered = FVector::Dist(At, Front);
						for (const TWeakObjectPtr<AJamFlyer>& Weak : H->GetFlock())
						{
							if (!*bMet && Weak.IsValid() && Weak->IsHunting() && FVector::Dist(Weak->GetActorLocation(), At) < 1500.0f && World->GetTimeSeconds() >= H->RallyUntil)
							{
								*bMet = true;
								UE_LOG(LogMindKore, Log, TEXT("Flyer encounter probe [met]: %s within 15 m of the player %.1f s after the call, %.0f m into the way back (%.0f%% of %.0f m)"),
									*Weak->GetName(), float(World->GetTimeSeconds() - Called), *Covered / 100.0f, 100.0f * *Covered / FMath::Max(Route, 1.0f), Route / 100.0f);
							}
						}
					}, [World, WeakHome, bMet]()
					{
						UE_LOG(LogMindKore, Log, TEXT("Flyer encounter probe [home]: %s; %s"), *bMet ? TEXT("met on the way") : TEXT("NOT MET on the way back"),
							WeakHome.IsValid() ? *DescribeFlock(WeakHome.Get()) : TEXT("-"));
						// The contrast: one shot inside the planetoid's air, where its flock flies.
						AJamFlyerHabitat* Planet = nullptr;
						for (TActorIterator<AJamFlyerHabitat> It(World); It && !Planet; ++It)
						{
							Planet = It->GetInnerRadius() > 0.0f ? *It : nullptr;
						}
						AJamZeroGPawn* Now = ProbePawn(World);
						if (!Planet || !Now)
						{
							return;
						}
						const AJamFlyer* Near = nullptr;
						for (const TWeakObjectPtr<AJamFlyer>& Weak : Planet->GetFlock())
						{
							Near = !Near && Weak.IsValid() ? Weak.Get() : Near;
						}
						const FVector Centre = Planet->GetActorLocation();
						const FVector Up = Near ? (Near->GetActorLocation() - Centre).GetSafeNormal() : FVector::UpVector;
						Teleport(Now, Centre + Up * (Planet->GetInnerRadius() + 300.0f));
						Now->Guns->SelectWeapon(0);
						After(World, 1.8f, [World, Up]()
						{
							if (AJamZeroGPawn* S = ProbePawn(World))
							{
								S->GetController()->SetControlRotation(Up.Rotation());
								UE_LOG(LogMindKore, Log, TEXT("Flyer encounter probe [air]: a shot inside the planetoid's air - %s"), S->Fire() ? TEXT("fired") : TEXT("NOT FIRED"));
							}
						});
						After(World, 6.0f, [World, Planet = TWeakObjectPtr<AJamFlyerHabitat>(Planet)]()
						{
							UE_LOG(LogMindKore, Log, TEXT("Flyer encounter probe: done - %s"), Planet.IsValid() ? *DescribeFlock(Planet.Get()) : TEXT("-"));
						});
					});
				});
			});
		});
	}

	FAutoConsoleCommandWithWorldAndArgs GFlyerEncounterProbe(
		TEXT("jam.Flyer.EncounterProbe"),
		TEXT("jam.Flyer.EncounterProbe: the airlock to the linked flesh wall with shots into space (unheard), the shotgun into the wall (the pain call), back to the airlock (where the flock meets the player), a shot in the planetoid's air (heard)."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&RunFlyerEncounterProbe));

	/**
	 * jam.Flyer.WallShots: stills of the encounter (needs a real RHI): the camera 6 m before the linked wall,
	 * the shotgun blast, "Shot" at +0.4, +1.2, +2.5, +4 s (the wound screaming and venting); then from 20 m
	 * back along the way home toward the wreck at +7 and +10 s, and after the arrival delay (the flock coming).
	 */
	void RunFlyerWallShots(UWorld* World)
	{
		QuietScreen(World);
		AJamLivingWall* Wall = FindLinkedWall(World);
		AJamZeroGPawn* Pawn = ProbePawn(World);
		FVector Exit, Out;
		if (!Wall || !Pawn || !AirlockExit(World, Exit, Out))
		{
			UE_LOG(LogMindKore, Warning, TEXT("Flyer wall shots: needs a linked living wall, the pawn and the airlock"));
			return;
		}
		Pawn->Guns->UnlockWeapon(1);
		Pawn->Guns->SelectWeapon(1);
		const FVector Front = WallFront(Wall, Exit, 450.0f);
		const FVector Target = Wall->GetActorLocation();
		const FVector Back = WallFront(Wall, Exit, 2600.0f); // out of the bow, on the way home
		const float Delay = Wall->LinkedHabitats.IsEmpty() ? 8.0f : Wall->LinkedHabitats[0].ArrivalDelaySeconds;
		Poll(World, 0.02f, 6.5f, [World, Front, Target]()
		{
			if (AJamZeroGPawn* Now = ProbePawn(World))
			{
				PlaceEye(Now, Front, Target);
			}
			return false;
		}, [](bool) {});
		After(World, 2.0f, [World]()
		{
			AJamZeroGPawn* Shooter = ProbePawn(World);
			UE_LOG(LogMindKore, Log, TEXT("Flyer wall shots: the blast - %s"), Shooter && Shooter->Fire() ? TEXT("fired") : TEXT("NOT FIRED"));
		});
		for (const float At : { 2.4f, 3.2f, 4.5f, 6.0f })
		{
			After(World, At, [World, At]()
			{
				UE_LOG(LogMindKore, Log, TEXT("Flyer wall shots: the wound at +%.1f s"), At - 2.0f);
				World->GetFirstPlayerController()->ConsoleCommand(TEXT("Shot"));
			});
		}
		Poll(World, 0.02f, Delay + 6.0f, [World, Back, Target]()
		{
			if (AJamZeroGPawn* Now = ProbePawn(World); Now && World->GetTimeSeconds() > 0.0)
			{
				PlaceEye(Now, Back, Target);
			}
			return false;
		}, [](bool) {});
		for (const float At : { 9.0f, 12.0f, 2.0f + Delay + 2.0f, 2.0f + Delay + 4.0f })
		{
			After(World, At, [World, At]()
			{
				FString Flock;
				for (TActorIterator<AJamFlyerHabitat> It(World); It; ++It)
				{
					Flock += TEXT(" ") + DescribeFlock(*It) + TEXT(";");
				}
				UE_LOG(LogMindKore, Log, TEXT("Flyer wall shots: 20 m back toward the wreck at +%.1f s -%s"), At - 2.0f, *Flock);
				World->GetFirstPlayerController()->ConsoleCommand(TEXT("Shot"));
			});
		}
		After(World, 2.0f + Delay + 5.0f, []() { UE_LOG(LogMindKore, Log, TEXT("Flyer wall shots: done")); });
	}

	FAutoConsoleCommandWithWorld GFlyerWallShots(
		TEXT("jam.Flyer.WallShots"),
		TEXT("Stills of the encounter: the linked flesh wall blasted (the scream and the vent), then the flock gathering and coming (Shot; needs a real RHI)."),
		FConsoleCommandWithWorldDelegate::CreateStatic(&RunFlyerWallShots));
}
