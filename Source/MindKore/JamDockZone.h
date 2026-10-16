#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "JamDockZone.generated.h"

class UBoxComponent;
class UPrimitiveComponent;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FJamDockCompleted, class AJamDockZone*, Zone);

/**
 * Where loot is delivered: a box at the ship. Every loose body carrying
 * RequiredTag inside it is counted; once inside, a part is captured softly -
 * its damping goes up and a weak pull draws it to the box centre - so it
 * settles instead of drifting out. OnCompleted fires once, when the count
 * first reaches RequiredCount.
 */
UCLASS()
class MINDKORE_API AJamDockZone : public AActor
{
	GENERATED_BODY()

public:
	AJamDockZone();

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Jam|Dock")
	TObjectPtr<UBoxComponent> Zone = nullptr;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Jam|Dock")
	FName RequiredTag = TEXT("Ship.Part");

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Jam|Dock", meta = (ClampMin = "1"))
	int32 RequiredCount = 4;

	/** Linear and angular damping of a captured part, 1/s. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Jam|Dock", meta = (ClampMin = "0.0"))
	float CaptureDamping = 1.5f;

	/** Acceleration toward the box centre on a captured part, cm/s^2. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Jam|Dock", meta = (ClampMin = "0.0"))
	float CapturePullAccelCm = 40.0f;

	UPROPERTY(BlueprintAssignable, Category = "Jam|Dock")
	FJamDockCompleted OnCompleted;

	UFUNCTION(BlueprintPure, Category = "Jam|Dock")
	int32 GetCount() const { return Count; }

	UFUNCTION(BlueprintPure, Category = "Jam|Dock")
	bool IsCompleted() const { return bCompleted; }

	virtual void Tick(float DeltaSeconds) override;

private:
	/** Captured last tick: a part that leaves gets its zero-G damping back. */
	TArray<TWeakObjectPtr<UPrimitiveComponent>> Captured;
	int32 Count = 0;
	bool bCompleted = false;
};
