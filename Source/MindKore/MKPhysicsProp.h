#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "MKImpactReceiver.h"
#include "MKInteractable.h"
#include "MKPhysicsProp.generated.h"

class UStaticMeshComponent;
class UMKPhysicsPropComponent;

/**
 * The plain carriable thing: one simulated static mesh plus the prop contract.
 * A crate, a canister, a plank are instances of this with a different mesh and
 * mass - the mass is authored because a puzzle weighs props, and a mass derived
 * from a mesh's volume and a default density is nobody's decision.
 *
 * The offer is the hand's: "take" when the looking pawn can carry it, the
 * carrier's own physical reason when it cannot.
 *
 * It is also an impact receiver, for one reason: a crowbar blow (a Blunt
 * impact) shoves it at the contact point. A round's impact is received and
 * ignored, so gunfire moves props exactly as it did before the receiver
 * existed - not at all. That split is a HYPOTHESIS pending the author
 * (crowbar slice 2, question 3: does a shot push a prop?).
 */
UCLASS()
class MINDKORE_API AMKPhysicsProp : public AActor, public IMKInteractable, public IMKImpactReceiver
{
	GENERATED_BODY()

public:
	AMKPhysicsProp();

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "MindKore|Prop")
	TObjectPtr<UStaticMeshComponent> Body;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "MindKore|Prop")
	TObjectPtr<UMKPhysicsPropComponent> Prop;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MindKore|Prop", meta = (ClampMin = "0.1"))
	float MassKg = 12.0f;

	virtual FMKInteractionOffer QueryInteraction(const AActor* Hand) const override;
	virtual void CompleteInteraction(AActor* Hand) override;

	virtual void ReceiveResolvedImpact_Implementation(const FMKResolvedImpact& ResolvedImpact) override;

protected:
	virtual void OnConstruction(const FTransform& Transform) override;
};
