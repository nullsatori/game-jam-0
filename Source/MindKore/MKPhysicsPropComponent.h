#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "MKPhysicsPropComponent.generated.h"

class UPrimitiveComponent;
class UMKPhysicsPropComponent;

UENUM(BlueprintType)
enum class EMKPropState : uint8
{
	Free,
	Held,
	/** Let go with a launch velocity; back to Free at the first meaningful contact or when the body sleeps. */
	Thrown
};

DECLARE_DYNAMIC_MULTICAST_DELEGATE_FourParams(FMKPropImpactSignature, UMKPhysicsPropComponent*, Prop,
	UPrimitiveComponent*, Other, FVector, Location, float, RelativeNormalSpeedCm);

/**
 * What makes a simulated body a thing the player can use
 * (Docs/DESIGN_PHYSICS_AND_TRAVERSAL.md, decision D4). Step, barricade, load,
 * noise source and projectile are roles of the same body; puzzles read these
 * properties and the body's mass, never the actor's class.
 *
 * The body itself is the owner's root primitive and stays a plain simulated
 * rigid body in every state: Held means a carrier's handle is attached to it,
 * not that anything else moves it.
 */
UCLASS(ClassGroup = (MindKore), meta = (BlueprintSpawnableComponent))
class MINDKORE_API UMKPhysicsPropComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UMKPhysicsPropComponent();

	/** Can be lifted off the ground by hand. False for things that are only pushed (a cart). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MindKore|Prop")
	bool bCarryable = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MindKore|Prop")
	bool bThrowable = true;

	/** Reserved for the telekinesis profile; nothing reads it until that slice. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MindKore|Prop")
	bool bRemoteGrabbable = true;

	/** Below this closing speed along the contact normal a touch is not an event. cm/s. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MindKore|Prop|Impact", meta = (ClampMin = "0.0"))
	float MinImpactSpeedCm = 120.0f;

	/** Callbacks against the same body inside this window are one blow. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MindKore|Prop|Impact", meta = (ClampMin = "0.0"))
	float ContactWindowSeconds = 0.25f;

	/** Hearing loudness at MinImpactSpeedCm; scales linearly with speed up to MaxImpactLoudness. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MindKore|Prop|Impact", meta = (ClampMin = "0.0"))
	float ImpactLoudness = 0.3f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MindKore|Prop|Impact", meta = (ClampMin = "0.0"))
	float MaxImpactLoudness = 1.0f;

	/**
	 * Game seconds after BeginPlay (level start, spawn, or a World Partition
	 * cell streaming in) during which a free prop's contacts are its settling,
	 * not blows: no noise, no OnPropImpact. On 24.09 a crate in the Admin archive
	 * reported ten full-loudness impacts in its first 2.1 s and sent the floor's
	 * enemies to check it (L0c). A thrown prop is heard even then; one knocked
	 * or shot inside the window is not. HYPOTHESIS 3 s: the 24.09 run was a
	 * 3 fps background editor.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MindKore|Prop|Impact", meta = (ClampMin = "0.0"))
	float SettleSeconds = 3.0f;

	/** One broadcast per meaningful contact, after deduplication. */
	UPROPERTY(BlueprintAssignable, Category = "MindKore|Prop")
	FMKPropImpactSignature OnPropImpact;

	UFUNCTION(BlueprintPure, Category = "MindKore|Prop")
	EMKPropState GetState() const { return State; }

	UFUNCTION(BlueprintPure, Category = "MindKore|Prop")
	AActor* GetHolder() const { return Holder.Get(); }

	/** The simulated body: the owner's root primitive. Null when the owner has none or it does not simulate. */
	UPrimitiveComponent* GetBody() const;

	float GetMassKg() const;

	/** Carrier bookkeeping. Only UMKCarryComponent calls these; one holder at a time. */
	bool MarkHeld(AActor* InHolder);
	void MarkReleased(bool bThrown);

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

private:
	UFUNCTION()
	void HandleBodyHit(UPrimitiveComponent* HitComponent, AActor* OtherActor, UPrimitiveComponent* OtherComp,
		FVector NormalImpulse, const FHitResult& Hit);

	EMKPropState State = EMKPropState::Free;
	TWeakObjectPtr<AActor> Holder;
	TMap<TWeakObjectPtr<UPrimitiveComponent>, double> LastContactSeconds;
	/** BeginPlay + SettleSeconds. */
	double SettledAtSeconds = 0.0;
};
