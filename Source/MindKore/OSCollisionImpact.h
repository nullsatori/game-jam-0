#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "OSCollisionImpact.generated.h"

class UPrimitiveComponent;
struct FHitResult;

/** When a physical collision counts as a blow, and how hard. */
USTRUCT(BlueprintType)
struct FOSCollisionImpactSettings
{
	GENERATED_BODY()

	/** Closing speed below this is a touch, cm/s (contact impulse / own mass). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OS|Collision", meta = (ClampMin = "0.0"))
	float MinSpeedCm = 300.0f;

	/** Contact impulse below this is a touch, kg cm/s: a light thing does not break glass. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OS|Collision", meta = (ClampMin = "0.0"))
	float MinImpulse = 5000.0f;

	/** Damage per kg cm/s of contact impulse: 100 kg at 600 cm/s gives 30. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OS|Collision", meta = (ClampMin = "0.0"))
	float DamagePerImpulse = 0.0005f;

	/** One blow per this many seconds per body: a grinding contact reports every frame. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OS|Collision", meta = (ClampMin = "0.0"))
	float CooldownSeconds = 0.15f;
};

/**
 * A physical collision as an impact: the body Source owns hit Hit's actor with
 * NormalImpulse (the OnComponentHit arguments). Above the settings' speed and
 * impulse it goes through MKResolveImpactFromHit + MKDeliverResolvedImpact as
 * a Blunt impact (no extra physics impulse - the collision already moved
 * both), so a heavy fast prop breaks an AMKGlassPane and hurts a pawn.
 * Returns the closing speed in OutSpeedCm; true when delivered to a receiver.
 */
MINDKORE_API bool OSDeliverCollisionImpact(AActor* Source, const UPrimitiveComponent* SourceBody, const FHitResult& Hit,
	const FVector& NormalImpulse, const FOSCollisionImpactSettings& Settings, double& InOutLastSeconds, float& OutSpeedCm);

/** Makes its owner's root body deliver its collisions as impacts (see OSDeliverCollisionImpact). */
UCLASS(ClassGroup = (OS), meta = (BlueprintSpawnableComponent))
class MINDKORE_API UOSCollisionImpactComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OS|Collision")
	FOSCollisionImpactSettings Settings;

protected:
	virtual void BeginPlay() override;

private:
	UFUNCTION()
	void HandleHit(UPrimitiveComponent* HitComponent, AActor* OtherActor, UPrimitiveComponent* OtherComp,
		FVector NormalImpulse, const FHitResult& Hit);

	double LastSeconds = -100.0;
};
