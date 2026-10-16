#include "JamPickups.h"

#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "JamShip.h"
#include "JamZeroGPawn.h"
#include "MKKeyring.h"
#include "MindKore.h"
#include "UObject/ConstructorHelpers.h"

#define LOCTEXT_NAMESPACE "JamPickups"

namespace
{
	void JamToast(const UObject* WorldContext, const FText& Text)
	{
		UE_LOG(LogMindKore, Log, TEXT("Toast: %s"), *Text.ToString());
		if (AJamShipGameMode* Mode = WorldContext->GetWorld()->GetAuthGameMode<AJamShipGameMode>())
		{
			Mode->Toast(Text);
		}
	}
}

AJamPickupBase::AJamPickupBase()
{
	PrimaryActorTick.bCanEverTick = false;
	Mesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Mesh"));
	SetRootComponent(Mesh);
	static ConstructorHelpers::FObjectFinder<UStaticMesh> Cube(TEXT("/Engine/BasicShapes/Cube.Cube"));
	Mesh->SetStaticMesh(Cube.Object);
	Mesh->SetCollisionProfileName(TEXT("BlockAll"));
	Mesh->SetCanEverAffectNavigation(false);
}

FMKInteractionOffer AJamPickupBase::QueryInteraction(const AActor* Hand) const
{
	if (bTaken || !Hand)
	{
		return FMKInteractionOffer::None();
	}
	const FText Refusal = RefusalFor(Hand);
	return Refusal.IsEmpty() ? FMKInteractionOffer::Available(ActionLabel) : FMKInteractionOffer::Refused(ActionLabel, Refusal);
}

void AJamPickupBase::CompleteInteraction(AActor* Hand)
{
	if (bTaken || !Hand || !RefusalFor(Hand).IsEmpty() || !Give(Hand))
	{
		return;
	}
	bTaken = true;
	if (bHideWhenTaken)
	{
		SetActorHiddenInGame(true);
		SetActorEnableCollision(false);
	}
}

AJamWeaponPickup::AJamWeaponPickup()
{
	ActionLabel = LOCTEXT("TakeGun", "Взять оружие");
}

FText AJamWeaponPickup::RefusalFor(const AActor* Hand) const
{
	// A walker (on the planetoid) can take it too when the ship mode keeps the unlock for the next zero-G pawn.
	return Hand->IsA<AJamZeroGPawn>() || GetWorld()->GetAuthGameMode<AJamShipGameMode>()
		? FText::GetEmpty() : LOCTEXT("NeedSuitHands", "только в скафандре");
}

bool AJamWeaponPickup::Give(AActor* Hand)
{
	const AJamZeroGPawn* Defaults = GetDefault<AJamZeroGPawn>();
	if (!Defaults->Weapons.IsValidIndex(WeaponIndex))
	{
		return false;
	}
	if (AJamZeroGPawn* Pawn = Cast<AJamZeroGPawn>(Hand))
	{
		Pawn->UnlockWeapon(WeaponIndex);
	}
	else if (AJamShipGameMode* Mode = GetWorld()->GetAuthGameMode<AJamShipGameMode>())
	{
		if (Mode->Progress.Weapons.Num() < Defaults->Weapons.Num())
		{
			Mode->Progress.Weapons.SetNumZeroed(Defaults->Weapons.Num());
		}
		Mode->Progress.Weapons[WeaponIndex] = true;
	}
	else
	{
		return false;
	}
	JamToast(this, FText::Format(LOCTEXT("GunUnlocked", "Получено: {0}"), Defaults->Weapons[WeaponIndex].Name));
	return true;
}

AJamToolPickup::AJamToolPickup()
{
	ActionLabel = LOCTEXT("TakeTool", "Взять инструмент");
	ToolName = LOCTEXT("Grapple", "Крюк-кошка");
}

bool AJamToolPickup::Give(AActor* Hand)
{
	if (AJamShipGameMode* Mode = GetWorld()->GetAuthGameMode<AJamShipGameMode>())
	{
		Mode->Progress.Tools.AddUnique(ToolId);
	}
	JamToast(this, FText::Format(LOCTEXT("ToolUnlocked", "Получено: {0} (пока без механики)"), ToolName));
	return true;
}

AJamSuitLocker::AJamSuitLocker()
{
	ActionLabel = LOCTEXT("SuitOn", "Надеть скафандр");
	bHideWhenTaken = false;
}

FText AJamSuitLocker::RefusalFor(const AActor* Hand) const
{
	if (!MKFindKeyring(Hand))
	{
		return LOCTEXT("NoKeyring", "некуда надеть");
	}
	return MKHolderHasKey(Hand, MKJamSuitKey()) ? LOCTEXT("SuitAlready", "скафандр уже надет") : FText::GetEmpty();
}

bool AJamSuitLocker::Give(AActor* Hand)
{
	UMKKeyringComponent* Keyring = MKFindKeyring(Hand);
	if (!Keyring || !Keyring->AddKey(MKJamSuitKey()))
	{
		return false;
	}
	JamToast(this, LOCTEXT("SuitDone", "Скафандр надет"));
	return true;
}

AJamCargo::AJamCargo()
{
	Tags.Add(TEXT("Cargo"));
	static ConstructorHelpers::FObjectFinder<UStaticMesh> Cube(TEXT("/Engine/BasicShapes/Cube.Cube"));
	Body->SetStaticMesh(Cube.Object);
	Body->SetRelativeScale3D(FVector(0.4f, 0.3f, 0.3f));
	MassKg = 6.0f;
}

FMKInteractionOffer AJamCargo::QueryInteraction(const AActor* Hand) const
{
	return Hand ? FMKInteractionOffer::Available(LOCTEXT("TakeCargo", "Забрать груз")) : FMKInteractionOffer::None();
}

void AJamCargo::CompleteInteraction(AActor* Hand)
{
	int32 Count = 0;
	int32 Total = 0;
	if (AJamShipGameMode* Mode = GetWorld()->GetAuthGameMode<AJamShipGameMode>())
	{
		Count = ++Mode->Progress.Cargo;
		Total = Mode->CargoTotal;
	}
	JamToast(this, FText::Format(LOCTEXT("CargoTaken", "Груз {0}/{1}"), Count, Total));
	Destroy();
}

#undef LOCTEXT_NAMESPACE
