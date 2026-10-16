#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "MKGate.generated.h"

class UStaticMeshComponent;

UENUM(BlueprintType)
enum class EMKGateState : uint8
{
	Closed,
	Opening,
	Open,
	Closing
};

/**
 * A panel that slides on a rail: night shutter, service grille, lift door.
 * Not a door in the player's hands — nothing on it is interactable. It moves
 * only when an AMKControlSwitch or an AMKLift tells it to, which is the
 * "procedure" register of DESIGN_INTERACTION_AND_PHYSICS §4.1: the building
 * changes state, the operator satisfied a condition somewhere else.
 *
 * The panel blocks the navmesh while closed and moving, and stops blocking
 * once open, through the ordinary Dynamic runtime generation: a Movable
 * primitive that affects navigation dirties its tiles as it moves. No
 * modifier volume, no nav link.
 *
 * Closing is swept. A body under a closing panel reverses it to Open — the
 * shutter never crushes and never traps.
 */
UCLASS()
class MINDKORE_API AMKGate : public AActor
{
	GENERATED_BODY()

public:
	AMKGate();

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "MindKore|Gate")
	TObjectPtr<UStaticMeshComponent> Panel = nullptr;

	/** Where the panel goes when open, in the actor's local frame, cm. Up for a shutter, sideways for a lift door. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MindKore|Gate")
	FVector OpenOffsetCm = FVector(0.0f, 0.0f, 230.0f);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MindKore|Gate", meta = (ClampMin = "0.05"))
	float TravelSeconds = 2.5f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MindKore|Gate")
	bool bStartOpen = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MindKore|Gate|Noise", meta = (ClampMin = "0.0"))
	float MoveNoiseLoudness = 0.6f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MindKore|Gate|Noise")
	FName NoiseTag = TEXT("MK_Gate");

	UPROPERTY(BlueprintReadOnly, Transient, Category = "MindKore|Gate")
	EMKGateState State = EMKGateState::Closed;

	/** 0 = closed position, 1 = open position. */
	UPROPERTY(BlueprintReadOnly, Transient, Category = "MindKore|Gate")
	float Alpha = 0.0f;

	UFUNCTION(BlueprintCallable, Category = "MindKore|Gate")
	void Open();

	UFUNCTION(BlueprintCallable, Category = "MindKore|Gate")
	void Close();

	UFUNCTION(BlueprintCallable, Category = "MindKore|Gate")
	void Toggle();

	UFUNCTION(BlueprintPure, Category = "MindKore|Gate")
	bool IsOpen() const { return State == EMKGateState::Open; }

	virtual void Tick(float DeltaSeconds) override;
	virtual void BeginPlay() override;

private:
	FVector ClosedRelativeLocation = FVector::ZeroVector;

	void SetState(EMKGateState NewState);
	void ApplyAlpha(bool bSweep);
};
