#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "MKInteractable.h"
#include "MKKeyPickup.generated.h"

class UStaticMeshComponent;

/**
 * A key on a hook. Taken once, by a hand that carries a keyring
 * (UMKKeyringComponent); after that it is hidden and stops answering the eye.
 *
 * Taking it touches nothing else: the door it fits is opened by a hand at
 * that door, not remotely from here (AMKDoor::RequiredKeyId).
 *
 * The actor is not destroyed, so a probe can still read bTaken.
 */
UCLASS()
class MINDKORE_API AMKKeyPickup : public AActor, public IMKInteractable
{
	GENERATED_BODY()

public:
	AMKKeyPickup();

	/** What the eye finds. Blocks Visibility only; the body walks through it. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "MindKore|Key")
	TObjectPtr<UStaticMeshComponent> Mesh = nullptr;

	/** Matches AMKDoor::RequiredKeyId / AMKLift::RequiredKeyId. NAME_None = offers nothing. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MindKore|Key")
	FName KeyId = NAME_None;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MindKore|Key")
	FText ActionLabel;

	UPROPERTY(BlueprintReadOnly, Transient, Category = "MindKore|Key")
	bool bTaken = false;

	// IMKInteractable
	virtual FMKInteractionOffer QueryInteraction(const AActor* Hand) const override;
	virtual void CompleteInteraction(AActor* Hand) override;
};
