#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "MKPhysicsProp.h"
#include "JamOxygen.generated.h"

/**
 * The suit's oxygen, in seconds of breathing. Breathing drains 1/s; the
 * owner spends more for thrusters and the stabiliser (Spend); guns are free. Empty:
 * the owner's UMKVitalsComponent takes SuffocationDamagePerSecond, paid once
 * a second. Vitals at zero: this component is the zero-G mode's death owner -
 * it disables the player's input and restarts the level after RestartDelaySeconds.
 */
UCLASS(ClassGroup = (Jam), meta = (BlueprintSpawnableComponent))
class MINDKORE_API UJamOxygenComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UJamOxygenComponent();

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Jam|Oxygen", meta = (ClampMin = "1.0"))
	float CapacitySeconds = 180.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Jam|Oxygen", meta = (ClampMin = "0.0"))
	float BreathingPerSecond = 1.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Jam|Oxygen", meta = (ClampMin = "0.0"))
	float ThrusterPerSecond = 2.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Jam|Oxygen", meta = (ClampMin = "0.0"))
	float StabiliserPerSecond = 3.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Jam|Oxygen", meta = (ClampMin = "0.0"))
	float SuffocationDamagePerSecond = 10.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Jam|Oxygen", meta = (ClampMin = "0.0"))
	float RestartDelaySeconds = 3.0f;

	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Transient, Category = "Jam|Oxygen")
	float Seconds = 180.0f;

	UFUNCTION(BlueprintCallable, Category = "Jam|Oxygen")
	void Spend(float Amount);

	UFUNCTION(BlueprintCallable, Category = "Jam|Oxygen")
	void Refill(float Amount);

	UFUNCTION(BlueprintPure, Category = "Jam|Oxygen")
	bool IsEmpty() const { return Seconds <= 0.0f; }

	UFUNCTION(BlueprintPure, Category = "Jam|Oxygen")
	bool IsDead() const { return bDead; }

	UFUNCTION(BlueprintPure, Category = "Jam|Oxygen")
	float GetFraction() const { return CapacitySeconds > 0.0f ? Seconds / CapacitySeconds : 0.0f; }

	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

protected:
	virtual void BeginPlay() override;

private:
	float SuffocationAccumulator = 0.0f;
	bool bDead = false;
};

/**
 * A floating oxygen bottle: a small physics prop (it drifts and can be
 * pushed), but the hand's offer is "breathe" instead of "take" - E adds
 * RefillSeconds to the hand's UJamOxygenComponent and the bottle is gone.
 */
UCLASS()
class MINDKORE_API AJamOxygenCanister : public AMKPhysicsProp
{
	GENERATED_BODY()

public:
	AJamOxygenCanister();

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Jam|Oxygen", meta = (ClampMin = "0.0"))
	float RefillSeconds = 60.0f;

	virtual FMKInteractionOffer QueryInteraction(const AActor* Hand) const override;
	virtual void CompleteInteraction(AActor* Hand) override;
};
