#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "MKInteractable.h"
#include "MKInteractionComponent.generated.h"

/**
 * The player's hand for level mechanisms (DESIGN_INTERACTION_AND_PHYSICS
 * §10.1). Looks, asks the one object under the reticle what it offers, and on
 * the use key completes it. Knows no door, gate or switch class: only the
 * IMKInteractable contract.
 *
 * Look picks the candidate: a line trace from the player view point,
 * Visibility channel, ReachCm long. A near miss counts - a ball of
 * LookSlackCm swept along the same line, no farther than what the line met -
 * and a candidate is held for HoldSeconds after the look slips off it: a
 * small thing on the floor is not lost to every step and sway of a chest
 * camera, and the device's frame does not blink. Another thing looked at
 * replaces a held one at once. The use key acts on the candidate, held or
 * not, so the frame always shows what the key will do.
 *
 * The doc asks for a second hand-anchor check so nobody uses a lever through
 * a wall; with a 220 cm reach and blocking traces the wall case cannot arise,
 * so that check is deferred until a hold interaction or a longer reach needs it.
 *
 * Reflected so the PIE probe (Tools/mk_editor.py) can read CurrentOffer and
 * call Interact() through Python.
 */
UCLASS(ClassGroup = (MindKore), meta = (BlueprintSpawnableComponent))
class MINDKORE_API UMKInteractionComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UMKInteractionComponent();

	/** How far the hand reaches from the lens, cm. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MindKore|Interaction", meta = (ClampMin = "50.0"))
	float ReachCm = 220.0f;

	/** HYPOTHESIS: how far beside the line of the look a thing may be and still be looked at, cm. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MindKore|Interaction", meta = (ClampMin = "0.0"))
	float LookSlackCm = 6.0f;

	/** HYPOTHESIS: how long a candidate is kept after the look slips off it, game seconds. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MindKore|Interaction", meta = (ClampMin = "0.0"))
	float HoldSeconds = 0.25f;

	/** Actor currently looked at that implements IMKInteractable, or null. */
	UPROPERTY(BlueprintReadOnly, Transient, Category = "MindKore|Interaction")
	TObjectPtr<AActor> Candidate = nullptr;

	/** Offer from Candidate this frame. Empty Action when nothing is looked at. */
	UPROPERTY(BlueprintReadOnly, Transient, Category = "MindKore|Interaction")
	FMKInteractionOffer CurrentOffer;

	/** The component of Candidate the look landed on, and where: what the device frame hugs. */
	UPROPERTY(BlueprintReadOnly, Transient, Category = "MindKore|Interaction")
	TObjectPtr<UPrimitiveComponent> CandidateComponent = nullptr;

	UPROPERTY(BlueprintReadOnly, Transient, Category = "MindKore|Interaction")
	FVector CandidateHitPoint = FVector::ZeroVector;

	/** Use key. Completes the current offer if it is available; logs why not otherwise. */
	UFUNCTION(BlueprintCallable, Category = "MindKore|Interaction")
	void Interact();

	/** Re-run the look trace now (the tick does this too; probes call it after a teleport). */
	UFUNCTION(BlueprintCallable, Category = "MindKore|Interaction")
	void RefreshCandidate();

	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

private:
	bool GetViewPoint(FVector& OutLocation, FVector& OutDirection) const;

	// When the look was last on Candidate itself, game seconds.
	double CandidateSeenSeconds = 0.0;
};

class UBoxComponent;

/**
 * One handle of an object that offers several (a cross, a keyboard): a small
 * box the look can land on. Visibility only - bodies and the navmesh pass
 * through it. Call from the owner's constructor.
 */
MINDKORE_API UBoxComponent* MKCreateHandTarget(AActor& Owner, FName Name, USceneComponent* Parent, const FVector& Location, const FVector& HalfSizeCm);

class UStaticMesh;
class UStaticMeshComponent;

/**
 * What the eye reads on such an object (a resistor in its socket, a lamp): a
 * 100 cm engine shape sized in centimetres, without collision, so the look
 * passes through it to the handle it shows. Call from the owner's constructor.
 */
MINDKORE_API UStaticMeshComponent* MKCreatePartMesh(AActor& Owner, FName Name, USceneComponent* Parent, UStaticMesh* Shape, const FVector& Location, const FVector& SizeCm);

/**
 * The component Hand's look landed on, read by such an object in its Query
 * and Complete to tell its handles apart. Null when Hand has no look or looks
 * at nothing.
 */
MINDKORE_API const UPrimitiveComponent* MKLookedAtComponent(const AActor* Hand);
