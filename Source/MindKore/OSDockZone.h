#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "OSDockZone.generated.h"

class UBoxComponent;
class UPrimitiveComponent;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOSDockCompleted, class AOSDockZone*, Zone);

/**
 * Where loot is delivered: a box at the ship. Every loose body carrying
 * RequiredTag inside it is counted; once inside, a part is captured softly -
 * its damping goes up and a weak pull draws it to the box centre - so it
 * settles instead of drifting out. The count is live: a part pushed back out
 * counts no more (IsCompleted goes false again). OnCompleted fires once, when
 * the count first reaches RequiredCount.
 */
UCLASS()
class MINDKORE_API AOSDockZone : public AActor
{
	GENERATED_BODY()

public:
	AOSDockZone();

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "OS|Dock")
	TObjectPtr<UBoxComponent> Zone = nullptr;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OS|Dock")
	FName RequiredTag = TEXT("Ship.Part");

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OS|Dock", meta = (ClampMin = "1"))
	int32 RequiredCount = 4;

	/** Linear and angular damping of a captured part, 1/s. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OS|Dock", meta = (ClampMin = "0.0"))
	float CaptureDamping = 1.5f;

	/** Acceleration toward the box centre on a captured part, cm/s^2. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OS|Dock", meta = (ClampMin = "0.0"))
	float CapturePullAccelCm = 40.0f;

	UPROPERTY(BlueprintAssignable, Category = "OS|Dock")
	FOSDockCompleted OnCompleted;

	UFUNCTION(BlueprintPure, Category = "OS|Dock")
	int32 GetCount() const { return Count; }

	/** Enough parts inside now. */
	UFUNCTION(BlueprintPure, Category = "OS|Dock")
	bool IsCompleted() const { return Count >= RequiredCount; }

	virtual void Tick(float DeltaSeconds) override;

private:
	/** Captured last tick: a part that leaves gets its zero-G damping back. */
	TArray<TWeakObjectPtr<UPrimitiveComponent>> Captured;
	int32 Count = 0;
	/** OnCompleted has fired (once per run). */
	bool bAnnounced = false;
};
