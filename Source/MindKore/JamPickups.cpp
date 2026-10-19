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
	// Any pawn with guns: the zero-G pawn and the walkers.
	return Hand->FindComponentByClass<UJamWeaponsComponent>() ? FText::GetEmpty() : LOCTEXT("NeedSuitHands", "нечем взять");
}

bool AJamWeaponPickup::Give(AActor* Hand)
{
	UJamWeaponsComponent* Guns = Hand->FindComponentByClass<UJamWeaponsComponent>();
	if (!Guns || !Guns->Weapons.IsValidIndex(WeaponIndex))
	{
		return false;
	}
	Guns->UnlockWeapon(WeaponIndex);
	// Taken while holding a body: the body is let go and the new gun drawn (a hull tool in hand stays).
	if (AJamZeroGPawn* Pawn = Cast<AJamZeroGPawn>(Hand); Pawn && Pawn->GetHands() == EJamHands::Held)
	{
		Pawn->SetHands(EJamHands::Gun);
	}
	JamToast(this, FText::Format(LOCTEXT("GunUnlocked", "Получено: {0}"), Guns->Weapons[WeaponIndex].Name));
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
		Mode->Tools.AddUnique(ToolId);
	}
	JamToast(this, FText::Format(LOCTEXT("ToolUnlocked", "Получено: {0}"), ToolName));
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
		Count = ++Mode->Cargo;
		Total = Mode->CargoTotal;
	}
	JamToast(this, FText::Format(LOCTEXT("CargoTaken", "Груз {0}/{1}"), Count, Total));
	Destroy();
}

#undef LOCTEXT_NAMESPACE
