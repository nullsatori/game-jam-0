#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "JamLadder.generated.h"

class UBoxComponent;

/** Free view-directed movement near the ladder, without snapping to its centre. */
UCLASS()
class MINDKORE_API AJamLadder : public AActor
{
	GENERATED_BODY()
public:
	AJamLadder();

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Jam|Ladder")
	TObjectPtr<UBoxComponent> Target;

	/** Body-centre heights of the landings, relative to this actor. */
	UPROPERTY(EditAnywhere, Category = "Jam|Ladder", meta = (MakeEditWidget = "true"))
	FVector Top = FVector(0, -100, 200);

	UPROPERTY(EditAnywhere, Category = "Jam|Ladder", meta = (MakeEditWidget = "true"))
	FVector Bottom = FVector(0, -100, -200);

	UPROPERTY(EditAnywhere, Category = "Jam|Ladder", meta = (ClampMin = "1"))
	float ClimbSpeedCm = 320;

	bool IsInReach(const FVector& Position) const;
	bool CanEnter(const FVector& Position, const FVector& MoveDirection) const;
};
