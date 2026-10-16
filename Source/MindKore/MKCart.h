#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "MKInteractable.h"
#include "MKCart.generated.h"

class UStaticMeshComponent;
class UPhysicsConstraintComponent;
class UMKPhysicsPropComponent;

/**
 * A service cart: pushed and pulled by the body, never carried, with a brake.
 *
 * The brake is a physical state (Docs/DESIGN_PHYSICS_AND_TRAVERSAL.md, D10): a
 * constraint to the world that locks the deck's horizontal travel and its
 * rotation. The cart stays a simulated body with mass and contacts, so the
 * movement backend sees the same kind of support under the player's feet
 * braked or not; switching simulation off would change that.
 *
 * TEMPORARY: the offer hangs on the whole cart. It moves to a visible brake
 * pedal once the interaction trace reports the hit component, not only the
 * actor.
 */
UCLASS()
class MINDKORE_API AMKCart : public AActor, public IMKInteractable
{
	GENERATED_BODY()

public:
	AMKCart();

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "MindKore|Cart")
	TObjectPtr<UStaticMeshComponent> Body;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "MindKore|Cart")
	TObjectPtr<UPhysicsConstraintComponent> Brake;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "MindKore|Cart")
	TObjectPtr<UMKPhysicsPropComponent> Prop;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MindKore|Cart", meta = (ClampMin = "1.0"))
	float MassKg = 45.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MindKore|Cart")
	bool bStartBraked = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MindKore|Cart|Noise", meta = (ClampMin = "0.0"))
	float BrakeNoiseLoudness = 0.25f;

	UFUNCTION(BlueprintPure, Category = "MindKore|Cart")
	bool IsBraked() const { return bBraked; }

	UFUNCTION(BlueprintCallable, Category = "MindKore|Cart")
	void SetBraked(bool bInBraked);

	virtual FMKInteractionOffer QueryInteraction(const AActor* Hand) const override;
	virtual void CompleteInteraction(AActor* Hand) override;

protected:
	virtual void OnConstruction(const FTransform& Transform) override;
	virtual void BeginPlay() override;

private:
	bool bBraked = false;
};
