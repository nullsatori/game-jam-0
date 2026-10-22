#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
// OS lift: MKHands.h include cut - the hands/slot owner is not lifted; the crowbar is the MK.Crowbar actor tag (MKFastenerCrowbarTag).
#include "MKInteractable.h"
#include "MKFastener.generated.h"

class AMKFastener;
class AMKPhysicsProp;
class UPrimitiveComponent;
class USoundBase;
class UStaticMeshComponent;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FMKFastenerPried, AMKFastener*, Fastener);

/** What a fastener offers a hand. Pure, see MKResolveFastenerOffer. */
UENUM(BlueprintType)
enum class EMKFastenerOffer : uint8
{
	/** Already pried, or no hand. (OS lift: the "no UMKHandsComponent" case is gone with the hands owner.) */
	None,
	Available,
	/** The crowbar has not been taken. */
	RefuseNoTool,
	/** A prop is held. */
	RefuseHandsBusy,
	/** The crowbar is taken but the revolver is in hand. */
	RefuseToolNotInHand
};

/**
 * The crowbar is an actor tag on the hand (the interacting pawn): a pawn that
 * carries MKFastenerCrowbarTag ("MK.Crowbar") can pry. The starter pawn toggles
 * it with G; a game gives it when the crowbar is picked up.
 */
inline FName MKFastenerCrowbarTag() { return FName(TEXT("MK.Crowbar")); }

/**
 * OS lift: the hands/slot inputs (bHasHands, EMKHandsSlot) cut - MindKore's
 * hands owner is not lifted, so "crowbar in hand" is one bool. RefuseHandsBusy
 * and RefuseToolNotInHand stay in the enum but this rule no longer returns them.
 */
inline EMKFastenerOffer MKResolveFastenerOffer(bool bPried, bool bHasCrowbar)
{
	if (bPried)
	{
		return EMKFastenerOffer::None;
	}
	return bHasCrowbar ? EMKFastenerOffer::Available : EMKFastenerOffer::RefuseNoTool;
}

/**
 * A fastening the crowbar tears off: boards across an opening, a hasp, a bar,
 * a grille. Prying opens the way by removing the fastening, never by picking a
 * lock - AMKDoor::bLocked and its only releaser AMKControlSwitch are untouched
 * here, and a crowbar at a locked door is refused by the door as before.
 *
 * A standalone blocker for now. A fastening mounted ON an AMKDoor (a bar across
 * a leaf) needs a new input to MKResolveDoorVerdict and is not in this slice.
 *
 * On pry: out of the navmesh (the way AMKDoor lets its leaf out), SpawnOnPry
 * dropped at SpawnTransforms (the boards), one mechanism noise; the fastening
 * itself is hidden with its collision off or, a lid (bBlockerFalls), falls to
 * the floor and clatters once there (LandSound). Whether the dynamic navmesh
 * actually rebuilds through the gap has not been seen in PIE yet.
 */
UCLASS()
class MINDKORE_API AMKFastener : public AActor, public IMKInteractable
{
	GENERATED_BODY()

public:
	AMKFastener();

	/** The fastening itself. Blocks bodies and the eye until pried. Mesh set on the instance. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "MindKore|Fastener")
	TObjectPtr<UStaticMeshComponent> Blocker = nullptr;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MindKore|Fastener")
	FText ActionLabel;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MindKore|Fastener")
	FText NoToolReason;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MindKore|Fastener")
	FText ToolNotInHandReason;

	/** What falls out when it is pried (a plank). Nothing when unset. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MindKore|Fastener")
	TSubclassOf<AMKPhysicsProp> SpawnOnPry;

	/** One SpawnOnPry per entry, relative to this actor. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MindKore|Fastener", meta = (MakeEditWidget = "true"))
	TArray<FTransform> SpawnTransforms;

	/**
	 * A lid or a cover: pried, the fastening itself comes away from the wall it
	 * closed (its +X) and lies on the floor to the level's end, a loose thing out
	 * of the navmesh. Off: it vanishes, as boards do (SpawnOnPry drops their pieces).
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MindKore|Fastener")
	bool bBlockerFalls = false;

	/** A falling lid's clatter, once, where it lands; quieter the lighter the lid. None: it lands silent. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MindKore|Fastener")
	TObjectPtr<USoundBase> LandSound = nullptr;

	/** AI noise of the tear. The AI's hearing currently delivers nothing (WORK_QUEUE); do not design around it. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MindKore|Fastener|Noise", meta = (ClampMin = "0.0"))
	float PryNoiseLoudness = 0.6f;

	UPROPERTY(BlueprintReadOnly, Transient, Category = "MindKore|Fastener")
	bool bPried = false;

	/**
	 * Once, at the pry, after the pieces have dropped: what the fastening shut
	 * learns it here (the АРМ dies with its cabinet, the alarm panel reports its
	 * cover). This actor stays the one owner of the event.
	 */
	UPROPERTY(BlueprintAssignable, Category = "MindKore|Fastener")
	FMKFastenerPried OnPried;

	/** The offer for Hand, as the pure rule sees it. */
	EMKFastenerOffer ResolveOffer(const AActor* Hand) const;

	// IMKInteractable
	virtual FMKInteractionOffer QueryInteraction(const AActor* Hand) const override;
	virtual void CompleteInteraction(AActor* Hand) override;

private:
	UFUNCTION()
	void HandleLidHit(UPrimitiveComponent* HitComponent, AActor* OtherActor, UPrimitiveComponent* OtherComp,
		FVector NormalImpulse, const FHitResult& Hit);

	bool bLanded = false;
	double PriedSeconds = 0.0;
};
