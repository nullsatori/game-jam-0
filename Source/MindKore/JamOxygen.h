#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "MKPhysicsProp.h"
#include "JamOxygen.generated.h"

/**
 * The suit's oxygen, in seconds of breathing. Breathing drains 1/s; the
 * owner may charge thrusters and the stabiliser (both 0 by default); guns are free. Empty:
 * the owner's UMKVitalsComponent takes SuffocationDamagePerSecond, paid once
 * a second. It only spends air: death is the vitals' event (OnDepleted), and
 * the game mode decides what follows (AJamShipGameMode revives in the cabin).
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
	float ThrusterPerSecond = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Jam|Oxygen", meta = (ClampMin = "0.0"))
	float StabiliserPerSecond = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Jam|Oxygen", meta = (ClampMin = "0.0"))
	float SuffocationDamagePerSecond = 10.0f;

	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Transient, Category = "Jam|Oxygen")
	float Seconds = 180.0f;

	UFUNCTION(BlueprintCallable, Category = "Jam|Oxygen")
	void Spend(float Amount);

	UFUNCTION(BlueprintCallable, Category = "Jam|Oxygen")
	void Refill(float Amount);

	UFUNCTION(BlueprintPure, Category = "Jam|Oxygen")
	bool IsEmpty() const { return Seconds <= 0.0f; }

	/** In air (the cabin, the planetoid's warm shell): breathing restores RefillPerSecond instead of draining. Set every tick by the zone owner. */
	void SetBreathable(bool bInAir, float RefillPerSecond) { bBreathable = bInAir; BreathableRefillPerSecond = RefillPerSecond; }

	UFUNCTION(BlueprintPure, Category = "Jam|Oxygen")
	bool IsBreathable() const { return bBreathable; }

	UFUNCTION(BlueprintPure, Category = "Jam|Oxygen")
	float GetFraction() const { return CapacitySeconds > 0.0f ? Seconds / CapacitySeconds : 0.0f; }

	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

protected:
	virtual void BeginPlay() override;

private:
	float SuffocationAccumulator = 0.0f;
	bool bBreathable = false;
	float BreathableRefillPerSecond = 0.0f;
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
