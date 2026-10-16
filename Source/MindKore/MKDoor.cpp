#include "MKDoor.h"

#include "AIController.h"
#include "Components/BoxComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/OverlapResult.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "GameFramework/Pawn.h"
// Jam lift: MKDeviceHUD.h and MKEnemyCharacter.h includes cut - no device HUD, no MindKore enemy in the starter.
#include "MKKeyring.h"
#include "Navigation/NavLinkProxy.h"
#include "Navigation/PathFollowingComponent.h"
#include "NavigationPath.h"
#include "MKMechanismMotion.h"
#include "MindKore.h"

#define LOCTEXT_NAMESPACE "MKDoor"

AMKDoor::AMKDoor()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.bStartWithTickEnabled = true;

	USceneComponent* Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	SetRootComponent(Root);
	Root->SetMobility(EComponentMobility::Movable);

	Hinge = CreateDefaultSubobject<USceneComponent>(TEXT("Hinge"));
	Hinge->SetupAttachment(Root);
	Hinge->SetMobility(EComponentMobility::Movable);

	Leaf = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Leaf"));
	Leaf->SetupAttachment(Hinge);
	Leaf->SetMobility(EComponentMobility::Movable);
	Leaf->SetCollisionProfileName(TEXT("BlockAll"));
	Leaf->SetGenerateOverlapEvents(false);
	// The leaf is the thing the eye finds: the interaction trace runs on
	// Visibility and must stop on it, not on the frame behind.
	Leaf->SetCollisionResponseToChannel(ECC_Visibility, ECR_Block);

	// Jam lift: HandleAnchor creation cut - device HUD framing only (see MKDoor.h).

	AiOpenVolume = CreateDefaultSubobject<UBoxComponent>(TEXT("AiOpenVolume"));
	AiOpenVolume->SetupAttachment(Root);
	AiOpenVolume->SetMobility(EComponentMobility::Movable);
	// Centred on the closed leaf's plane, 140 cm to either side (a walking man
	// covers 80 cm before an eased leaf has moved), 120 cm along the wall.
	AiOpenVolume->SetRelativeLocation(FVector(0.0f, 50.0f, 100.0f));
	AiOpenVolume->SetBoxExtent(FVector(140.0f, 60.0f, 100.0f));
	// Explicit, not a profile: the volume is large enough for the player to
	// stand inside, and anything but "overlap pawns, ignore the rest" lets it
	// catch the interaction trace (seen in PIE: the eye found this door
	// instead of the switch in front of it).
	AiOpenVolume->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	AiOpenVolume->SetCollisionObjectType(ECC_WorldDynamic);
	AiOpenVolume->SetCollisionResponseToAllChannels(ECR_Ignore);
	AiOpenVolume->SetCollisionResponseToChannel(ECC_Pawn, ECR_Overlap);
	AiOpenVolume->SetGenerateOverlapEvents(true);
	AiOpenVolume->SetCanEverAffectNavigation(false);

	DisplayName = LOCTEXT("DefaultName", "Дверь");
	LockedReason = LOCTEXT("Locked", "Служебный режим");
	KeyRequiredReason = LOCTEXT("KeyRequired", "Заперто на ключ");
}

void AMKDoor::BeginPlay()
{
	Super::BeginPlay();
	CurrentYaw = 0.0f;
	TargetYaw = 0.0f;
	State = EMKDoorState::Closed;
	// The volume follows the leaf: the constructor sized it for a 100 cm door.
	if (const float Width = GetLeafWidthCm(); Width > 0.0f)
	{
		AiOpenVolume->SetRelativeLocation(FVector(0.0f, Width * 0.5f, 100.0f));
		AiOpenVolume->SetBoxExtent(FVector(140.0f, Width * 0.5f + 10.0f, 100.0f));
		// Jam lift: HandleAnchor placement cut - device HUD framing only.
	}
	ApplyNavigationFootprint();
}

#if WITH_EDITOR
void AMKDoor::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);
	ApplyNavigationFootprint();
}
#endif

bool AMKDoor::IsInside(const FVector& Location) const
{
	const FVector ToLocation = Location - GetActorLocation();
	return FVector::DotProduct(GetActorForwardVector(), ToLocation) < 0.0f;
}

void AMKDoor::ApplyNavigationFootprint()
{
	if (!Leaf)
	{
		return;
	}
	// Secured: the leaf is a wall for the navmesh. Unsecured: the enemy paths
	// through and swings it on contact, so the leaf must not carve a hole.
	const bool bBlocksNavigation = IsSecured();
	if (Leaf->CanEverAffectNavigation() != bBlocksNavigation)
	{
		Leaf->SetCanEverAffectNavigation(bBlocksNavigation);
	}
	if (NavLink && NavLink->IsSmartLinkEnabled() == bBlocksNavigation)
	{
		NavLink->SetSmartLinkEnabled(!bBlocksNavigation);
	}
}

void AMKDoor::SetState(EMKDoorState NewState)
{
	if (State == NewState)
	{
		return;
	}
	UE_LOG(LogMindKore, Log, TEXT("Door %s: %s -> %s (yaw %.0f)"), *GetName(),
		*UEnum::GetValueAsString(State), *UEnum::GetValueAsString(NewState), CurrentYaw);
	State = NewState;
}

void AMKDoor::OpenAwayFrom(const FVector& PusherLocation)
{
	if (IsSecured())
	{
		return;
	}
	if (State == EMKDoorState::Closed || State == EMKDoorState::Closing)
	{
		TargetYaw = OpenAngleDeg * MKDoorSwingSign(GetActorForwardVector(), PusherLocation - GetActorLocation());
	}
	else if (State == EMKDoorState::Ajar)
	{
		// Keep swinging the way it was already going; a half-open door does
		// not reverse under a hand on the other side, it just finishes.
		TargetYaw = OpenAngleDeg * (FMath::Abs(CurrentYaw) < 1.0f
			? MKDoorSwingSign(GetActorForwardVector(), PusherLocation - GetActorLocation())
			: (CurrentYaw < 0.0f ? -1.0f : 1.0f));
	}
	else
	{
		return;
	}
	SwingStartYaw = CurrentYaw;
	SwingAlpha = 0.0f;
	MKReportMechanismNoise(this, GetActorLocation(), SwingNoiseLoudness, TEXT("MK_Door"));
	SetState(EMKDoorState::Opening);
}

void AMKDoor::Close()
{
	if (State == EMKDoorState::Closed || State == EMKDoorState::Closing)
	{
		return;
	}
	TargetYaw = 0.0f;
	SwingStartYaw = CurrentYaw;
	SwingAlpha = 0.0f;
	MKReportMechanismNoise(this, GetActorLocation(), SwingNoiseLoudness, TEXT("MK_Door"));
	SetState(EMKDoorState::Closing);
}

void AMKDoor::Unlock()
{
	if (!bLocked)
	{
		return;
	}
	bLocked = false;
	ApplyNavigationFootprint();
	MKReportMechanismNoise(this, GetActorLocation(), LockNoiseLoudness, TEXT("MK_Lock"));
	UE_LOG(LogMindKore, Log, TEXT("Door %s: unlocked"), *GetName());
}

EMKDoorVerdict AMKDoor::VerdictFor(const AActor* Hand) const
{
	return MKResolveDoorVerdict(bBoltedInside, IsInside(Hand->GetActorLocation()), bLocked,
		IsKeyLockEngaged(), MKHolderHasKey(Hand, RequiredKeyId));
}

bool AMKDoor::RefusesForKey(const AActor* Hand) const
{
	return Hand && VerdictFor(Hand) == EMKDoorVerdict::RefuseNoKey;
}

FMKInteractionOffer AMKDoor::QueryInteraction(const AActor* Hand) const
{
	if (!Hand)
	{
		return FMKInteractionOffer::None();
	}
	switch (VerdictFor(Hand))
	{
	case EMKDoorVerdict::Unbolt:
		return FMKInteractionOffer::Available(LOCTEXT("Unbolt", "Снять засов"));
	case EMKDoorVerdict::RefuseBolted:
		return FMKInteractionOffer::Refused(LOCTEXT("Open", "Открыть"), LOCTEXT("BoltedInside", "Заперто изнутри"));
	case EMKDoorVerdict::RefuseLocked:
		return FMKInteractionOffer::Refused(LOCTEXT("Open", "Открыть"), LockedReason);
	case EMKDoorVerdict::TurnKey:
		return FMKInteractionOffer::Available(LOCTEXT("TurnKey", "Отпереть"));
	case EMKDoorVerdict::RefuseNoKey:
		return FMKInteractionOffer::Refused(LOCTEXT("Open", "Открыть"), KeyRequiredReason);
	case EMKDoorVerdict::Operate:
		break;
	}
	switch (State)
	{
	case EMKDoorState::Closed:
	case EMKDoorState::Closing:
	case EMKDoorState::Ajar:
		return FMKInteractionOffer::Available(LOCTEXT("Open", "Открыть"));
	case EMKDoorState::Open:
	case EMKDoorState::Opening:
		return FMKInteractionOffer::Available(LOCTEXT("Close", "Закрыть"));
	}
	return FMKInteractionOffer::None();
}

void AMKDoor::CompleteInteraction(AActor* Hand)
{
	if (!Hand)
	{
		return;
	}
	switch (VerdictFor(Hand))
	{
	case EMKDoorVerdict::Unbolt:
		bBoltedInside = false;
		ApplyNavigationFootprint();
		MKReportMechanismNoise(this, GetActorLocation(), LockNoiseLoudness, TEXT("MK_Lock"));
		UE_LOG(LogMindKore, Log, TEXT("Door %s: bolt lifted by %s"), *GetName(), *Hand->GetName());
		return;
	case EMKDoorVerdict::TurnKey:
		// Turning the key is its own action, like lifting the bolt: this use
		// unlocks, the next one opens. bLocked is not touched, so a door that
		// has locked itself behind the player stays locked for the key too.
		bKeyTurned = true;
		ApplyNavigationFootprint();
		MKReportMechanismNoise(this, GetActorLocation(), LockNoiseLoudness, TEXT("MK_Lock"));
		UE_LOG(LogMindKore, Log, TEXT("Door %s: key '%s' turned by %s"), *GetName(), *RequiredKeyId.ToString(), *Hand->GetName());
		return;
	case EMKDoorVerdict::RefuseBolted:
	case EMKDoorVerdict::RefuseLocked:
	case EMKDoorVerdict::RefuseNoKey:
		return;
	case EMKDoorVerdict::Operate:
		break;
	}
	if (State == EMKDoorState::Open || State == EMKDoorState::Opening)
	{
		Close();
	}
	else
	{
		OpenAwayFrom(Hand->GetActorLocation());
		if (bCloseAndLockBehind && !bLockBehindArmed)
		{
			LockBehindOpener = Hand;
			bOpenerWasInside = IsInside(Hand->GetActorLocation());
		}
	}
}

void AMKDoor::TickLockBehind()
{
	if (!bCloseAndLockBehind || bLocked)
	{
		return;
	}
	if (bLockBehindArmed)
	{
		// Armed and shut: the lock drops. Armed and stopped by a body: it
		// tries again; the leaf never pushes anyone, so it just waits.
		if (State == EMKDoorState::Closed)
		{
			bLocked = true;
			ApplyNavigationFootprint();
			MKReportMechanismNoise(this, GetActorLocation(), LockNoiseLoudness, TEXT("MK_Lock"));
			UE_LOG(LogMindKore, Log, TEXT("Door %s: locked behind %s"), *GetName(),
				LockBehindOpener.IsValid() ? *LockBehindOpener->GetName() : TEXT("?"));
		}
		else if (State == EMKDoorState::Ajar || State == EMKDoorState::Open)
		{
			// Not every tick: a body in the arc would turn this into a noise
			// event and a log line per frame.
			const float Now = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.0f;
			if (Now - LastLockBehindRetryTime >= 0.7f)
			{
				LastLockBehindRetryTime = Now;
				Close();
			}
		}
		return;
	}
	const AActor* Opener = LockBehindOpener.Get();
	if (!Opener || (State != EMKDoorState::Open && State != EMKDoorState::Ajar))
	{
		return;
	}
	const FVector OpenerLocation = Opener->GetActorLocation();
	const bool bCrossed = IsInside(OpenerLocation) != bOpenerWasInside;
	const float PlaneDistance = FMath::Abs(FVector::DotProduct(GetActorForwardVector(), OpenerLocation - GetActorLocation()));
	if (bCrossed && PlaneDistance >= LockBehindDistanceCm)
	{
		bLockBehindArmed = true;
		Close();
	}
}

float AMKDoor::GetLeafWidthCm() const
{
	// A pack leaf is authored along its own X and turned 90 degrees onto the
	// door's +Y, the wood leaf likewise, so "width" is not a fixed mesh axis:
	// it is the longer horizontal side. Reading mesh Y gave the THICKNESS
	// (18 cm); the route test then only passed for a man who happened to cross
	// near the hinge, and failed outright on the 120 cm leaf.
	const UStaticMesh* Mesh = Leaf ? Leaf->GetStaticMesh() : nullptr;
	if (!Mesh)
	{
		return 0.0f;
	}
	const FVector Size = Mesh->GetBoundingBox().GetSize() * Leaf->GetRelativeScale3D().GetAbs();
	return FMath::Max(Size.X, Size.Y);
}

bool AMKDoor::IsOnRouteOf(const AActor* Walker) const
{
	const APawn* Pawn = Cast<APawn>(Walker);
	const AAIController* Controller = Pawn ? Cast<AAIController>(Pawn->GetController()) : nullptr;
	const UPathFollowingComponent* Following = Controller ? Controller->GetPathFollowingComponent() : nullptr;
	const FNavPathSharedPtr Path = Following ? Following->GetPath() : nullptr;
	const UStaticMesh* Mesh = Leaf ? Leaf->GetStaticMesh() : nullptr;
	if (!Path.IsValid() || !Mesh || Following->GetStatus() != EPathFollowingStatus::Moving)
	{
		return false;
	}
	const FTransform DoorSpace = GetActorTransform();
	TArray<FVector, TInlineAllocator<16>> Points;
	Points.Add(DoorSpace.InverseTransformPosition(Pawn->GetActorLocation()));
	const TArray<FNavPathPoint>& PathPoints = Path->GetPathPoints();
	for (int32 Index = FMath::Max(0, (int32)Following->GetNextPathIndex()); Index < PathPoints.Num(); ++Index)
	{
		Points.Add(DoorSpace.InverseTransformPosition(PathPoints[Index].Location));
	}
	const float LeafWidthCm = GetLeafWidthCm();
	return MKRouteCrossesDoorway(Points, LeafWidthCm, 40.0f);
}

AActor* AMKDoor::FindPawnInLeafArc(float CandidateYaw) const
{
	UWorld* World = GetWorld();
	const UStaticMesh* Mesh = Leaf ? Leaf->GetStaticMesh() : nullptr;
	if (!World || !Mesh)
	{
		return nullptr;
	}
	// Leaf transform if the hinge stood at CandidateYaw.
	const FTransform HingeCandidate(FRotator(0.0f, CandidateYaw, 0.0f), Hinge->GetRelativeLocation(), Hinge->GetRelativeScale3D());
	const FTransform LeafWorld = Leaf->GetRelativeTransform() * HingeCandidate * GetActorTransform();
	const FBox Local = Mesh->GetBoundingBox();
	const FVector Center = LeafWorld.TransformPosition(Local.GetCenter());
	const FVector Extent = Local.GetExtent() * LeafWorld.GetScale3D().GetAbs();

	TArray<FOverlapResult> Overlaps;
	FCollisionObjectQueryParams ObjectParams;
	ObjectParams.AddObjectTypesToQuery(ECC_Pawn);
	FCollisionQueryParams Params(SCENE_QUERY_STAT(MKDoorArc), false, this);
	World->OverlapMultiByObjectType(Overlaps, Center, LeafWorld.GetRotation(), ObjectParams, FCollisionShape::MakeBox(Extent), Params);
	for (const FOverlapResult& Overlap : Overlaps)
	{
		if (AActor* Actor = Overlap.GetActor())
		{
			if (!Actor->IsA<APawn>())
			{
				continue;
			}
			// A leaf is never stopped by whoever it is moving AWAY from. +yaw
			// carries the leaf toward local -X, so a body on +X is behind it
			// (and the reverse). Without this the man who pushes a door while
			// walking reaches the leaf before it clears, blocks it at a few
			// degrees and is then shut in by it - seen in PIE on E1-E2.
			const float LocalX = GetActorTransform().InverseTransformPosition(Actor->GetActorLocation()).X;
			const float Direction = CandidateYaw - CurrentYaw;
			const bool bBehindLeaf = Direction > 0.0f ? LocalX > 0.0f : LocalX < 0.0f;
			if (bBehindLeaf)
			{
				continue;
			}
			return Actor;
		}
	}
	return nullptr;
}

void AMKDoor::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	TickLockBehind();
	if ((State == EMKDoorState::Ajar || State == EMKDoorState::Closed) && !IsSecured())
	{
		// A begin-overlap fires once, and it fires for a man who is only walking
		// past. So the volume is polled instead: whoever stands in it is asked
		// where he is going, and a half-open door is re-pushed for a man who is
		// still waiting at it.
		AiOpenRetryAccumulator += DeltaSeconds;
		if (AiOpenRetryAccumulator >= 0.25f)
		{
			AiOpenRetryAccumulator = 0.0f;
			TArray<AActor*> Near;
			// Jam lift: AMKEnemyCharacter filter replaced by APawn - any AI pawn; IsOnRouteOf already refuses a pawn without an AAIController, so the player never pushes it.
			AiOpenVolume->GetOverlappingActors(Near, APawn::StaticClass());
			for (AActor* Walker : Near)
			{
				if (IsOnRouteOf(Walker))
				{
					UE_LOG(LogMindKore, Log, TEXT("Door %s: pushed open by %s"), *GetName(), *Walker->GetName());
					OpenAwayFrom(Walker->GetActorLocation());
					break;
				}
			}
		}
	}
	if (State != EMKDoorState::Opening && State != EMKDoorState::Closing)
	{
		return;
	}

	// Eased, not linear: a hand accelerates a leaf and the latch side coasts
	// in. SwingAlpha runs 0..1 over SwingSeconds for a full swing (a shorter
	// swing from Ajar takes proportionally less), and the yaw follows a
	// smoothstep between where this swing started and its target.
	const float FullSwing = FMath::Max(FMath::Abs(TargetYaw - SwingStartYaw), 1.0f);
	const float Duration = FMath::Max(SwingSeconds, 0.05f) * FMath::Clamp(FullSwing / FMath::Max(OpenAngleDeg, 1.0f), 0.25f, 1.0f);
	SwingAlpha = MKAdvanceAlpha(SwingAlpha, /*bTowardOne=*/true, Duration, DeltaSeconds);
	const float NewYaw = FMath::Lerp(SwingStartYaw, TargetYaw, FMath::SmoothStep(0.0f, 1.0f, SwingAlpha));

	// A body in the arc stops the leaf where it is, and the door stays Ajar
	// until a hand moves it again. It never pushes anyone. This is an explicit
	// overlap test, not a sweep: the engine does not sweep rotation-only
	// moves, so a swept SetRelativeRotation would report nothing.
	if (AActor* Blocker = FindPawnInLeafArc(NewYaw))
	{
		UE_LOG(LogMindKore, Log, TEXT("Door %s: blocked by %s at yaw %.0f"), *GetName(), *Blocker->GetName(), CurrentYaw);
		SetState(EMKDoorState::Ajar);
		return;
	}
	Hinge->SetRelativeRotation(FRotator(0.0f, NewYaw, 0.0f));
	CurrentYaw = NewYaw;

	if (SwingAlpha >= 1.0f)
	{
		SetState(FMath::IsNearlyZero(TargetYaw) ? EMKDoorState::Closed : EMKDoorState::Open);
	}
}

#undef LOCTEXT_NAMESPACE
