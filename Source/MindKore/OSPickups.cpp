#include "OSPickups.h"

#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "OSShip.h"
#include "OSZeroGPawn.h"
#include "MKKeyring.h"
#include "MindKore.h"
#include "UObject/ConstructorHelpers.h"

#define LOCTEXT_NAMESPACE "OSPickups"

namespace
{
	void OSToast(const UObject* WorldContext, const FText& Text)
	{
		UE_LOG(LogMindKore, Log, TEXT("Toast: %s"), *Text.ToString());
		if (AOSShipGameMode* Mode = WorldContext->GetWorld()->GetAuthGameMode<AOSShipGameMode>())
		{
			Mode->Toast(Text);
		}
	}
}

AOSPickupBase::AOSPickupBase()
{
	PrimaryActorTick.bCanEverTick = false;
	Mesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Mesh"));
	SetRootComponent(Mesh);
	static ConstructorHelpers::FObjectFinder<UStaticMesh> Cube(TEXT("/Engine/BasicShapes/Cube.Cube"));
	Mesh->SetStaticMesh(Cube.Object);
	Mesh->SetCollisionProfileName(TEXT("BlockAll"));
	Mesh->SetCanEverAffectNavigation(false);
}

FMKInteractionOffer AOSPickupBase::QueryInteraction(const AActor* Hand) const
{
	if (bTaken || !Hand)
	{
		return FMKInteractionOffer::None();
	}
	const FText Refusal = RefusalFor(Hand);
	return Refusal.IsEmpty() ? FMKInteractionOffer::Available(ActionLabel) : FMKInteractionOffer::Refused(ActionLabel, Refusal);
}

void AOSPickupBase::CompleteInteraction(AActor* Hand)
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

AOSWeaponPickup::AOSWeaponPickup()
{
	ActionLabel = LOCTEXT("TakeGun", "Взять оружие");
}

void AOSWeaponPickup::BeginPlay()
{
	Super::BeginPlay();
	// The class defaults are the pump shotgun as tuned in code, whatever the pawn holds in the slot by now.
	const UOSWeaponsComponent* Defaults = GetDefault<UOSWeaponsComponent>();
	const UOSScopeComponent* Sights = GetDefault<UOSScopeComponent>();
	bHasReplacement = Gun == EOSPickupGun::Rmb93 && Defaults->Weapons.IsValidIndex(WeaponIndex) && Sights->Aims.IsValidIndex(WeaponIndex);
	if (bHasReplacement)
	{
		Replacement = OSMakeRmb93(Defaults->Weapons[WeaponIndex]);
		ReplacementAim = OSMakeRmb93Aim(Sights->Aims[WeaponIndex]);
		bHasReplacement = Replacement.ViewMesh != nullptr;
	}
	if (Gun != EOSPickupGun::Slot && !bHasReplacement)
	{
		UE_LOG(LogMindKore, Warning, TEXT("Weapon pickup %s: no replacement gun for slot %d - it gives the slot's own"), *GetName(), WeaponIndex);
	}
}

FText AOSWeaponPickup::RefusalFor(const AActor* Hand) const
{
	// Any pawn with guns: the zero-G pawn and the walkers.
	return Hand->FindComponentByClass<UOSWeaponsComponent>() ? FText::GetEmpty() : LOCTEXT("NeedSuitHands", "нечем взять");
}

bool AOSWeaponPickup::Give(AActor* Hand)
{
	UOSWeaponsComponent* Guns = Hand->FindComponentByClass<UOSWeaponsComponent>();
	if (!Guns || !Guns->Weapons.IsValidIndex(WeaponIndex))
	{
		return false;
	}
	if (bHasReplacement)
	{
		if (!Guns->ApplyPickupWeapon(WeaponIndex, Replacement))
		{
			return false;
		}
		if (Guns->Scope && Guns->Scope->Aims.IsValidIndex(WeaponIndex))
		{
			Guns->Scope->Aims[WeaponIndex] = ReplacementAim;
		}
	}
	Guns->UnlockWeapon(WeaponIndex);
	// Taken while holding a body: the body is let go and the new gun drawn (a hull tool in hand stays).
	if (AOSZeroGPawn* Pawn = Cast<AOSZeroGPawn>(Hand); Pawn && Pawn->GetHands() == EOSHands::Held)
	{
		Pawn->SetHands(EOSHands::Gun);
	}
	OSToast(this, FText::Format(LOCTEXT("GunUnlocked", "Получено: {0}"), Guns->Weapons[WeaponIndex].Name));
	return true;
}

AOSAttachmentPickup::AOSAttachmentPickup()
{
	ActionLabel = LOCTEXT("TakeAttachment", "Взять обвес");
}

FText AOSAttachmentPickup::RefusalFor(const AActor* Hand) const
{
	const UOSWeaponsComponent* Guns = Hand->FindComponentByClass<UOSWeaponsComponent>();
	if (!Guns)
	{
		return LOCTEXT("NeedSuitHandsAttach", "нечем взять");
	}
	if (!Guns->FindAttachment(AttachmentId))
	{
		return LOCTEXT("UnknownAttachment", "не наш обвес");
	}
	return Guns->HasAttachment(AttachmentId) ? LOCTEXT("HaveAttachment", "уже есть") : FText::GetEmpty();
}

bool AOSAttachmentPickup::Give(AActor* Hand)
{
	UOSWeaponsComponent* Guns = Hand->FindComponentByClass<UOSWeaponsComponent>();
	const FOSAttachment* Attachment = Guns ? Guns->FindAttachment(AttachmentId) : nullptr;
	if (!Attachment || Guns->HasAttachment(AttachmentId))
	{
		return false;
	}
	// First the word, then the give: «— в сумке, Tab» (GiveAttachment, when no place in hand is free) lands last.
	OSToast(this, FText::Format(LOCTEXT("AttachmentTaken", "Получено: {0}"), Attachment->Name));
	return Guns->GiveAttachment(AttachmentId);
}

AOSToolPickup::AOSToolPickup()
{
	ActionLabel = LOCTEXT("TakeTool", "Взять инструмент");
	ToolName = LOCTEXT("Grapple", "Крюк-кошка");
}

bool AOSToolPickup::Give(AActor* Hand)
{
	if (AOSShipGameMode* Mode = GetWorld()->GetAuthGameMode<AOSShipGameMode>())
	{
		Mode->Tools.AddUnique(ToolId);
	}
	OSToast(this, FText::Format(LOCTEXT("ToolUnlocked", "Получено: {0}"), ToolName));
	return true;
}

AOSSuitLocker::AOSSuitLocker()
{
	ActionLabel = LOCTEXT("SuitOn", "Надеть скафандр");
	bHideWhenTaken = false;
}

FText AOSSuitLocker::RefusalFor(const AActor* Hand) const
{
	if (!MKFindKeyring(Hand))
	{
		return LOCTEXT("NoKeyring", "некуда надеть");
	}
	return MKHolderHasKey(Hand, OSSuitKey()) ? LOCTEXT("SuitAlready", "скафандр уже надет") : FText::GetEmpty();
}

bool AOSSuitLocker::Give(AActor* Hand)
{
	UMKKeyringComponent* Keyring = MKFindKeyring(Hand);
	if (!Keyring || !Keyring->AddKey(OSSuitKey()))
	{
		return false;
	}
	OSToast(this, LOCTEXT("SuitDone", "Скафандр надет"));
	return true;
}

AOSCargo::AOSCargo()
{
	Tags.Add(TEXT("Cargo"));
	static ConstructorHelpers::FObjectFinder<UStaticMesh> Cube(TEXT("/Engine/BasicShapes/Cube.Cube"));
	Body->SetStaticMesh(Cube.Object);
	Body->SetRelativeScale3D(FVector(0.4f, 0.3f, 0.3f));
	MassKg = 6.0f;
}

FMKInteractionOffer AOSCargo::QueryInteraction(const AActor* Hand) const
{
	return Hand ? FMKInteractionOffer::Available(LOCTEXT("TakeCargo", "Забрать груз")) : FMKInteractionOffer::None();
}

void AOSCargo::CompleteInteraction(AActor* Hand)
{
	int32 Count = 0;
	int32 Total = 0;
	if (AOSShipGameMode* Mode = GetWorld()->GetAuthGameMode<AOSShipGameMode>())
	{
		Count = ++Mode->Cargo;
		Total = Mode->CargoTotal;
	}
	OSToast(this, FText::Format(LOCTEXT("CargoTaken", "Груз {0}/{1}"), Count, Total));
	Destroy();
}

#undef LOCTEXT_NAMESPACE
