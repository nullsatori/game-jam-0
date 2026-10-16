#include "MKKeyPickup.h"

#include "Components/StaticMeshComponent.h"
#include "MKKeyring.h"
#include "MindKore.h"

#define LOCTEXT_NAMESPACE "MKKeyPickup"

AMKKeyPickup::AMKKeyPickup()
{
	PrimaryActorTick.bCanEverTick = false;

	Mesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Mesh"));
	SetRootComponent(Mesh);
	// Found by the interaction trace (Visibility), ignored by everything else:
	// a key on a hook must not stop a body, a round or the navmesh.
	Mesh->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	Mesh->SetCollisionObjectType(ECC_WorldDynamic);
	Mesh->SetCollisionResponseToAllChannels(ECR_Ignore);
	Mesh->SetCollisionResponseToChannel(ECC_Visibility, ECR_Block);
	Mesh->SetGenerateOverlapEvents(false);
	Mesh->SetCanEverAffectNavigation(false);

	ActionLabel = LOCTEXT("Take", "Взять ключ");
}

FMKInteractionOffer AMKKeyPickup::QueryInteraction(const AActor* Hand) const
{
	if (bTaken || KeyId.IsNone() || !MKFindKeyring(Hand))
	{
		return FMKInteractionOffer::None();
	}
	return FMKInteractionOffer::Available(ActionLabel);
}

void AMKKeyPickup::CompleteInteraction(AActor* Hand)
{
	UMKKeyringComponent* Keyring = MKFindKeyring(Hand);
	if (bTaken || KeyId.IsNone() || !Keyring)
	{
		return;
	}
	Keyring->AddKey(KeyId);
	bTaken = true;
	SetActorHiddenInGame(true);
	SetActorEnableCollision(false);
	UE_LOG(LogMindKore, Log, TEXT("Key %s: '%s' taken by %s"), *GetName(), *KeyId.ToString(), *Hand->GetName());
}

#undef LOCTEXT_NAMESPACE
