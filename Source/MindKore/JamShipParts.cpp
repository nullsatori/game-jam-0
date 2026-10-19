#include "JamShipParts.h"

#include "Components/BoxComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/OverlapResult.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "JamShip.h"
#include "JamZeroGPawn.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "MKPhysicsProp.h"
#include "MindKore.h"
#include "UObject/ConstructorHelpers.h"

#define LOCTEXT_NAMESPACE "JamShipParts"

namespace
{
	void ShipToast(const UObject* WorldContext, const FText& Text)
	{
		UE_LOG(LogMindKore, Log, TEXT("Toast: %s"), *Text.ToString());
		if (AJamShipGameMode* Mode = WorldContext->GetWorld()->GetAuthGameMode<AJamShipGameMode>())
		{
			Mode->Toast(Text);
		}
	}
}

// --- clean zone ---------------------------------------------------------------------

AJamCleanZone::AJamCleanZone()
{
	RequiredTag = TEXT("Junk.Foreign");
	// Counts only: no capture, and "complete" never fires for it.
	CaptureDamping = 0.0f;
	CapturePullAccelCm = 0.0f;
	RequiredCount = 100000;
}

// --- plate socket (a breach) ---------------------------------------------------------

AJamPlateSocket::AJamPlateSocket()
{
	PrimaryActorTick.bCanEverTick = true;
	Catch = CreateDefaultSubobject<UBoxComponent>(TEXT("Catch"));
	SetRootComponent(Catch);
	Catch->SetBoxExtent(FVector(260.0f, 260.0f, 150.0f));
	Catch->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Catch->SetCanEverAffectNavigation(false);
}

void AJamPlateSocket::BeginPlay()
{
	Super::BeginPlay();
	// The contour: four sides of the hole, cut into pieces of about SegmentCm.
	const FVector2D Corners[4] = { { -HoleHalfCm.X, -HoleHalfCm.Y }, { HoleHalfCm.X, -HoleHalfCm.Y }, { HoleHalfCm.X, HoleHalfCm.Y }, { -HoleHalfCm.X, HoleHalfCm.Y } };
	for (int32 Side = 0; Side < 4; ++Side)
	{
		const FVector2D From = Corners[Side];
		const FVector2D To = Corners[(Side + 1) % 4];
		const FVector2D Along = (To - From).GetSafeNormal();
		const FVector2D Out(Along.Y, -Along.X); // the corners run anticlockwise: right of the walk is outward
		const int32 Count = FMath::Max(1, FMath::RoundToInt32(float((To - From).Size()) / SegmentCm));
		for (int32 Piece = 0; Piece < Count; ++Piece)
		{
			SegmentCentre.Add(From + (To - From) * ((Piece + 0.5) / Count));
			SegmentNormal.Add(Out);
		}
	}
	Seam.Init(0.0f, SegmentCentre.Num());
	LastWelded.Init(-100.0, SegmentCentre.Num());
	Gap.Init(1.0e6f, SegmentCentre.Num());
}

int32 AJamPlateSocket::JunkCount() const
{
	return CleanZone ? CleanZone->GetCount() : 0;
}

float AJamPlateSocket::GetSeamFraction() const
{
	float Sum = 0.0f;
	for (const float Piece : Seam)
	{
		Sum += FMath::Min(Piece, 1.0f);
	}
	return Seam.IsEmpty() ? 0.0f : Sum / Seam.Num();
}

AActor* AJamPlateSocket::ProbeUp(const FVector2D& Local, float& OutGapCm) const
{
	// From inside the ship up through the hole: the first thing over it.
	const FTransform& Frame = GetActorTransform();
	const FVector From = Frame.TransformPositionNoScale(FVector(Local.X, Local.Y, SurfaceZ - 40.0f));
	const FVector To = Frame.TransformPositionNoScale(FVector(Local.X, Local.Y, SurfaceZ + 80.0f));
	FCollisionQueryParams Params(SCENE_QUERY_STAT(JamBreachProbe), /*bTraceComplex=*/false, this);
	for (TActorIterator<AJamZeroGPawn> Pawn(GetWorld()); Pawn; ++Pawn)
	{
		Params.AddIgnoredActor(*Pawn);
	}
	FHitResult Hit;
	OutGapCm = 1.0e6f;
	if (!GetWorld()->LineTraceSingleByChannel(Hit, From, To, ECC_Visibility, Params))
	{
		return nullptr;
	}
	OutGapCm = float(Frame.InverseTransformPositionNoScale(Hit.ImpactPoint).Z) - SurfaceZ;
	return Hit.GetActor();
}

void AJamPlateSocket::Measure()
{
	// The plate: the RequiredTag actor most traces through the hole meet.
	TMap<AActor*, int32> Votes;
	TArray<AActor*> SegmentHit;
	SegmentHit.Init(nullptr, SegmentCentre.Num());
	for (int32 Index = 0; Index < SegmentCentre.Num(); ++Index)
	{
		// 4 cm inside the edge, under where the plate lies on the hull.
		AActor* Hit = ProbeUp(SegmentCentre[Index] - SegmentNormal[Index] * 4.0f, Gap[Index]);
		SegmentHit[Index] = Hit;
		if (Hit && Hit->ActorHasTag(RequiredTag))
		{
			Votes.FindOrAdd(Hit)++;
		}
	}
	constexpr int32 Grid = 10;
	TArray<TPair<AActor*, float>> Cells;
	for (int32 X = 0; X < Grid; ++X)
	{
		for (int32 Y = 0; Y < Grid; ++Y)
		{
			const FVector2D Local((X + 0.5f) / Grid * 2.0f - 1.0f, (Y + 0.5f) / Grid * 2.0f - 1.0f);
			float Height = 0.0f;
			AActor* Hit = ProbeUp(Local * (HoleHalfCm - FVector2D(5.0f, 5.0f)), Height);
			Cells.Emplace(Hit, Height);
			if (Hit && Hit->ActorHasTag(RequiredTag))
			{
				Votes.FindOrAdd(Hit)++;
			}
		}
	}
	AActor* Best = nullptr;
	int32 BestVotes = 0;
	for (const TPair<AActor*, int32>& Vote : Votes)
	{
		if (Vote.Value > BestVotes)
		{
			BestVotes = Vote.Value;
			Best = Vote.Key;
		}
	}
	if (bTacked && Plate.IsValid())
	{
		Best = Plate.Get(); // a tacked plate stays this breach's plate
	}
	Plate = Best;
	int32 Covered = 0;
	for (const TPair<AActor*, float>& Cell : Cells)
	{
		Covered += Best && Cell.Key == Best && Cell.Value <= 40.0f ? 1 : 0;
	}
	Coverage = float(Covered) / (Grid * Grid);
	for (int32 Index = 0; Index < SegmentCentre.Num(); ++Index)
	{
		if (!Best || SegmentHit[Index] != Best)
		{
			Gap[Index] = 1.0e6f; // something else over the edge, or nothing
		}
	}
}

EJamWeldResult AJamPlateSocket::Weld(const FVector& WorldPoint, float DeltaSeconds, bool bCut)
{
	const FVector Local = GetActorTransform().InverseTransformPositionNoScale(WorldPoint);
	if (FMath::Abs(Local.Z - SurfaceZ) > 45.0f)
	{
		return EJamWeldResult::OffSeam;
	}
	const FVector2D Torch(Local.X, Local.Y);
	const double Now = GetWorld()->GetTimeSeconds();
	EJamWeldResult Result = EJamWeldResult::OffSeam;
	for (int32 Index = 0; Index < SegmentCentre.Num(); ++Index)
	{
		if (FVector2D::Distance(Torch, SegmentCentre[Index]) > TorchRadiusCm)
		{
			continue;
		}
		if (bCut)
		{
			Seam[Index] = FMath::Max(0.0f, Seam[Index] - DeltaSeconds / (SecondsPerSegment * 0.5f));
			Result = EJamWeldResult::Cut;
			continue;
		}
		if (JunkCount() > 0)
		{
			return EJamWeldResult::Junk;
		}
		if (FMath::Abs(Gap[Index]) > MaxGapCm)
		{
			Result = Result == EJamWeldResult::Welded ? Result : EJamWeldResult::Gap;
			continue;
		}
		Seam[Index] = FMath::Min(1.0f, Seam[Index] + DeltaSeconds / SecondsPerSegment);
		LastWelded[Index] = Now;
		Result = EJamWeldResult::Welded;
	}
	if (Result == EJamWeldResult::Welded || Result == EJamWeldResult::Cut)
	{
		UpdateState();
	}
	return Result;
}

void AJamPlateSocket::UpdateState()
{
	AActor* PlateActor = Plate.Get();
	UPrimitiveComponent* Root = PlateActor ? Cast<UPrimitiveComponent>(PlateActor->GetRootComponent()) : nullptr;
	const float Fraction = GetSeamFraction();
	if (!bTacked && Root && Fraction >= TackFraction)
	{
		// Held by the seam where it lies; a pawn holding it lets go itself (AJamZeroGPawn::Tick).
		Root->SetSimulatePhysics(false);
		bTacked = true;
		UE_LOG(LogMindKore, Log, TEXT("Breach %s: %s tacked (seam %.0f%%)"), *GetName(), *PlateActor->GetName(), Fraction * 100.0f);
		ShipToast(this, LOCTEXT("Tacked", "Лист прихвачен: стяжку можно снять"));
	}
	else if (bTacked && Fraction < TackFraction)
	{
		if (Root)
		{
			Root->SetSimulatePhysics(true);
			Root->WakeAllRigidBodies();
		}
		bTacked = false;
		UE_LOG(LogMindKore, Log, TEXT("Breach %s: the tack cut, %s loose"), *GetName(), *GetNameSafe(PlateActor));
		ShipToast(this, LOCTEXT("Untacked", "Прихватка срезана: лист свободен"));
	}
	const bool bSealed = PlateActor && Fraction >= SealFraction && Coverage >= SealCoverage && JunkCount() == 0;
	if (bSealed == bFilled)
	{
		return;
	}
	bFilled = bSealed;
	if (bSealed)
	{
		PlateActor->Tags.AddUnique(TEXT("Hull.Fitted"));
	}
	else if (PlateActor)
	{
		PlateActor->Tags.Remove(TEXT("Hull.Fitted"));
	}
	int32 Filled = 0;
	int32 Total = 0;
	for (TActorIterator<AJamPlateSocket> Socket(GetWorld()); Socket; ++Socket)
	{
		++Total;
		Filled += Socket->bFilled ? 1 : 0;
	}
	UE_LOG(LogMindKore, Log, TEXT("Breach %s: %s (seam %.0f%%, coverage %.0f%%), %d/%d sealed"), *GetName(), bSealed ? TEXT("sealed") : TEXT("open again"),
		Fraction * 100.0f, Coverage * 100.0f, Filled, Total);
	ShipToast(this, bSealed ? FText::Format(LOCTEXT("Sealed", "Пробоина заварена {0}/{1}"), Filled, Total)
		: LOCTEXT("Unsealed", "Шов вскрыт: пробоина снова открыта"));
}

void AJamPlateSocket::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	if (SegmentCentre.IsEmpty())
	{
		return;
	}
	// Active while a plate is in the catch box or the player is near: measured ten times a second.
	bool bActive = bTacked;
	for (TActorIterator<AJamZeroGPawn> Pawn(GetWorld()); Pawn && !bActive; ++Pawn)
	{
		bActive = FVector::Dist(Pawn->GetActorLocation(), GetActorLocation()) <= ShowContourCm;
	}
	if (!bActive)
	{
		TArray<FOverlapResult> Overlaps;
		FCollisionObjectQueryParams Objects(ECC_PhysicsBody);
		Objects.AddObjectTypesToQuery(ECC_WorldDynamic);
		GetWorld()->OverlapMultiByObjectType(Overlaps, Catch->GetComponentLocation(), Catch->GetComponentQuat(), Objects,
			FCollisionShape::MakeBox(Catch->GetScaledBoxExtent()));
		bActive = Overlaps.ContainsByPredicate([this](const FOverlapResult& Overlap) { return Overlap.GetActor() && Overlap.GetActor()->ActorHasTag(RequiredTag); });
	}
	if (!bActive)
	{
		return;
	}
	const double Now = GetWorld()->GetTimeSeconds();
	if (Now >= NextMeasure)
	{
		NextMeasure = Now + 0.1;
		Measure();
		UpdateState();
	}
}

// --- panel ------------------------------------------------------------------------------

AJamShipPanel::AJamShipPanel()
{
	PrimaryActorTick.bCanEverTick = true;
	static ConstructorHelpers::FObjectFinder<UStaticMesh> Cube(TEXT("/Engine/BasicShapes/Cube.Cube"));
	static ConstructorHelpers::FObjectFinder<UStaticMesh> Sphere(TEXT("/Engine/BasicShapes/Sphere.Sphere"));

	// Faces +X: the desk is 30 cm deep, 150 wide, 100 tall, origin at its centre.
	Desk = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Desk"));
	SetRootComponent(Desk);
	Desk->SetStaticMesh(Cube.Object);
	Desk->SetRelativeScale3D(FVector(0.3f, 1.5f, 1.0f));
	Desk->SetCollisionProfileName(TEXT("BlockAll"));

	Lever = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Lever"));
	Lever->SetupAttachment(Desk);
	Lever->SetStaticMesh(Cube.Object);
	// Children live in the desk's unscaled mesh space (the cube spans +-50) and
	// inherit its scale: locations in those units, scales divided by the desk's.
	Lever->SetRelativeLocation(FVector(60.0f, 35.0f, 20.0f));
	Lever->SetRelativeScale3D(FVector(0.1f / 0.3f, 0.1f / 1.5f, 0.4f / 1.0f));
	Lever->SetCollisionEnabled(ECollisionEnabled::NoCollision);

	for (int32 Index = 0; Index < 3; ++Index)
	{
		UStaticMeshComponent* Lamp = CreateDefaultSubobject<UStaticMeshComponent>(*FString::Printf(TEXT("Lamp%d"), Index));
		Lamp->SetupAttachment(Desk);
		Lamp->SetStaticMesh(Sphere.Object);
		Lamp->SetRelativeLocation(FVector(55.0f, -20.0f + Index * 20.0f, 35.0f));
		Lamp->SetRelativeScale3D(FVector(0.15f / 0.3f, 0.15f / 1.5f, 0.15f / 1.0f));
		Lamp->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		Lamp->SetCastShadow(false);
		Lamps.Add(Lamp);
	}
}

void AJamShipPanel::BeginPlay()
{
	Super::BeginPlay();
	for (UStaticMeshComponent* Lamp : Lamps)
	{
		LampMaterials.Add(Lamp->CreateDynamicMaterialInstance(0));
	}
}

int32 AJamShipPanel::HullDone() const
{
	int32 Done = 0;
	for (const AJamPlateSocket* Socket : HullSockets)
	{
		Done += Socket && Socket->bFilled ? 1 : 0;
	}
	return Done;
}

int32 AJamShipPanel::FuelDone() const { return FuelDock ? FuelDock->GetCount() : 0; }
int32 AJamShipPanel::NavDone() const { return NavDock ? NavDock->GetCount() : 0; }
int32 AJamShipPanel::FuelNeeded() const { return FuelDock ? FuelDock->RequiredCount : 0; }
int32 AJamShipPanel::NavNeeded() const { return NavDock ? NavDock->RequiredCount : 0; }

void AJamShipPanel::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	const int32 State = (IsHullDone() ? 1 : 0) | (IsFuelDone() ? 2 : 0) | (IsNavDone() ? 4 : 0);
	if (State == LampState)
	{
		return;
	}
	LampState = State;
	for (int32 Index = 0; Index < LampMaterials.Num(); ++Index)
	{
		if (LampMaterials[Index])
		{
			const bool bDone = (State & (1 << Index)) != 0;
			LampMaterials[Index]->SetVectorParameterValue(TEXT("Color"), bDone ? FLinearColor(0.05f, 1.0f, 0.1f) : FLinearColor(1.0f, 0.02f, 0.01f));
		}
	}
	UE_LOG(LogMindKore, Log, TEXT("Panel %s: hull %s (%d/%d), fuel %s, navigation %s"), *GetName(),
		IsHullDone() ? TEXT("green") : TEXT("red"), HullDone(), HullSockets.Num(),
		IsFuelDone() ? TEXT("green") : TEXT("red"), IsNavDone() ? TEXT("green") : TEXT("red"));
}

FMKInteractionOffer AJamShipPanel::QueryInteraction(const AActor* Hand) const
{
	const FText Action = LOCTEXT("Launch", "Потянуть рычаг");
	TArray<FString> Missing;
	if (!IsHullDone()) { Missing.Add(TEXT("корпус")); }
	if (!IsFuelDone()) { Missing.Add(TEXT("топливо")); }
	if (!IsNavDone()) { Missing.Add(TEXT("навигация")); }
	return Missing.IsEmpty()
		? FMKInteractionOffer::Available(Action)
		: FMKInteractionOffer::Refused(Action, FText::Format(LOCTEXT("Missing", "не готово: {0}"), FText::FromString(FString::Join(Missing, TEXT(", ")))));
}

void AJamShipPanel::CompleteInteraction(AActor* Hand)
{
	if (!IsHullDone() || !IsFuelDone() || !IsNavDone())
	{
		return;
	}
	UE_LOG(LogMindKore, Log, TEXT("Panel %s: launch lever pulled by %s"), *GetName(), *GetNameSafe(Hand));
	if (AJamShipGameMode* Mode = GetWorld()->GetAuthGameMode<AJamShipGameMode>())
	{
		Mode->EndGame();
	}
}

// --- breakable rock ------------------------------------------------------------------------

AJamBreakableRock::AJamBreakableRock()
{
	PrimaryActorTick.bCanEverTick = false;
	static ConstructorHelpers::FObjectFinder<UStaticMesh> Sphere(TEXT("/Engine/BasicShapes/Sphere.Sphere"));
	Mesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Mesh"));
	SetRootComponent(Mesh);
	Mesh->SetStaticMesh(Sphere.Object);
	Mesh->SetCollisionProfileName(TEXT("BlockAll"));
	Mesh->SetCanEverAffectNavigation(false);
	Tags.Add(TEXT("Rock"));
}

void AJamBreakableRock::ReceiveResolvedImpact_Implementation(const FMKResolvedImpact& ResolvedImpact)
{
	if (bBroken || ResolvedImpact.Kind != EMKImpactKind::Round || !ResolvedImpact.bHasPhysicsImpulse)
	{
		return;
	}
	const float Damage = FMath::Max(0.0f, float(ResolvedImpact.ImpulseNewtonSeconds.Size()) - ImpulseThreshold);
	Health -= Damage;
	UE_LOG(LogMindKore, Verbose, TEXT("Rock %s: hit for %.0f, health %.0f"), *GetName(), Damage, Health);
	if (Health <= 0.0f)
	{
		Break(ResolvedImpact.ShotDirection);
	}
}

void AJamBreakableRock::Break(const FVector& Direction)
{
	bBroken = true;
	UWorld* World = GetWorld();
	UStaticMesh* Shape = Mesh->GetStaticMesh();
	const FVector Scale = GetActorScale3D() * 0.35f;
	for (int32 Index = 0; Index < SplitCount; ++Index)
	{
		const FVector Out = (Direction.GetSafeNormal() + FMath::VRand() * 0.8f).GetSafeNormal();
		const FTransform Where(FRotator::ZeroRotator, GetActorLocation() + Out * GetActorScale3D().X * 30.0f, Scale);
		AMKPhysicsProp* Piece = World->SpawnActorDeferred<AMKPhysicsProp>(AMKPhysicsProp::StaticClass(), Where, nullptr, nullptr,
			ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
		if (!Piece)
		{
			continue;
		}
		Piece->Body->SetStaticMesh(Shape);
		Piece->MassKg = PieceMassKg;
		Piece->FinishSpawning(Where);
		Piece->Body->SetLinearDamping(0.0f);
		Piece->Body->SetAngularDamping(0.0f);
		Piece->Body->SetPhysicsLinearVelocity(Out * PieceSpeedCm);
	}
	UE_LOG(LogMindKore, Log, TEXT("Rock %s: broken into %d pieces"), *GetName(), SplitCount);
	Destroy();
}

#undef LOCTEXT_NAMESPACE
