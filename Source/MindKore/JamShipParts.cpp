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

// --- plate socket -------------------------------------------------------------------

AJamPlateSocket::AJamPlateSocket()
{
	PrimaryActorTick.bCanEverTick = true;
	Catch = CreateDefaultSubobject<UBoxComponent>(TEXT("Catch"));
	SetRootComponent(Catch);
	Catch->SetBoxExtent(FVector(200.0f, 200.0f, 150.0f));
	Catch->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Catch->SetCanEverAffectNavigation(false);
}

void AJamPlateSocket::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	if (bFilled)
	{
		return;
	}
	TArray<FOverlapResult> Overlaps;
	FCollisionObjectQueryParams Objects(ECC_PhysicsBody);
	Objects.AddObjectTypesToQuery(ECC_WorldDynamic);
	GetWorld()->OverlapMultiByObjectType(Overlaps, Catch->GetComponentLocation(), Catch->GetComponentQuat(), Objects,
		FCollisionShape::MakeBox(Catch->GetScaledBoxExtent()));
	for (const FOverlapResult& Overlap : Overlaps)
	{
		AActor* Plate = Overlap.GetActor();
		UPrimitiveComponent* Root = Plate ? Cast<UPrimitiveComponent>(Plate->GetRootComponent()) : nullptr;
		if (!Root || !Root->IsSimulatingPhysics() || !Plate->ActorHasTag(RequiredTag))
		{
			continue;
		}
		const int32 Junk = CleanZone ? CleanZone->GetCount() : 0;
		if (Junk > 0)
		{
			const double Now = GetWorld()->GetTimeSeconds();
			if (Now - LastRefuseSeconds >= RefuseRepeatSeconds)
			{
				LastRefuseSeconds = Now;
				UE_LOG(LogMindKore, Log, TEXT("Socket %s: refuses %s, junk in the hull %d"), *GetName(), *Plate->GetName(), Junk);
				ShipToast(this, FText::Format(LOCTEXT("JunkInside", "В корпусе мусор: {0}"), Junk));
			}
			return;
		}
		// Whoever holds it lets go: a joint to a body that stops simulating would pin the pawn.
		for (TActorIterator<AJamZeroGPawn> Pawn(GetWorld()); Pawn; ++Pawn)
		{
			if (Pawn->GetGrabbed() == Root)
			{
				Pawn->Release();
			}
			if (Pawn->GetTethered() == Root)
			{
				Pawn->DetachTether();
			}
		}
		Root->SetSimulatePhysics(false);
		Root->SetWorldLocationAndRotation(GetActorLocation(), GetActorQuat(), false, nullptr, ETeleportType::TeleportPhysics);
		Plate->Tags.Remove(RequiredTag);
		Plate->Tags.AddUnique(TEXT("Hull.Fitted"));
		bFilled = true;
		int32 Filled = 0;
		int32 Total = 0;
		for (TActorIterator<AJamPlateSocket> Socket(GetWorld()); Socket; ++Socket)
		{
			++Total;
			Filled += Socket->bFilled ? 1 : 0;
		}
		UE_LOG(LogMindKore, Log, TEXT("Socket %s: %s fitted (%d/%d)"), *GetName(), *Plate->GetName(), Filled, Total);
		ShipToast(this, FText::Format(LOCTEXT("PlateFitted", "Лист закреплён {0}/{1}"), Filled, Total));
		return;
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
