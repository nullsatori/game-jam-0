#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "MKShelfBlock.generated.h"

UENUM(BlueprintType)
enum class EMKShelfPosition : uint8
{
	PositionI,
	Moving,
	PositionII,
	/** Wanted to move, something stands in the rail. Retries every tick until clear. */
	Blocked
};

/**
 * One block of mobile archive shelving on rails, two stable positions, one
 * crank (an AMKControlSwitch). The archive puzzle of
 * DESIGN_LEVEL_ADMIN_REWORK §5.3 is this actor plus one bolted door.
 *
 * Deterministic on purpose: the actor translates itself between two points;
 * the rack meshes ride along as attached children (attach them in the level,
 * set them Movable). Nothing is simulated, nothing can wedge, and both
 * positions are reachable from each other for the whole game.
 *
 * Obstruction: before every step the destination slab is overlapped for
 * pawns. Anyone in it - player or enemy, standing in the aisle the block is
 * closing or wedged inside its footprint - holds the block where it is
 * (Blocked). It resumes on its own when the slab clears, so a body in the
 * mechanism is a pause, never a crush and never a dead end.
 *
 * Navigation follows the attached meshes through Dynamic runtime generation.
 */
UCLASS()
class MINDKORE_API AMKShelfBlock : public AActor
{
	GENERATED_BODY()

public:
	AMKShelfBlock();

	/** Position II relative to position I (the placed transform), local cm. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MindKore|Shelf")
	FVector TravelOffsetCm = FVector(344.0f, 0.0f, 0.0f);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MindKore|Shelf", meta = (ClampMin = "0.1"))
	float TravelSeconds = 4.5f;

	/** Half extent of the block footprint in the actor's local frame, cm. Set to the racks' bounds. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MindKore|Shelf")
	FVector FootprintHalfExtentCm = FVector(46.0f, 585.0f, 106.0f);

	/** Local offset of the footprint centre from the actor origin. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MindKore|Shelf")
	FVector FootprintCenterCm = FVector(0.0f, 585.0f, 106.0f);

	/** Extra clearance around the slab when testing for bodies, cm. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MindKore|Shelf", meta = (ClampMin = "0.0"))
	float ObstructionPaddingCm = 20.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MindKore|Shelf|Noise", meta = (ClampMin = "0.0"))
	float MoveNoiseLoudness = 0.5f;

	UPROPERTY(BlueprintReadOnly, Transient, Category = "MindKore|Shelf")
	EMKShelfPosition State = EMKShelfPosition::PositionI;

	/** 0 = position I, 1 = position II. */
	UPROPERTY(BlueprintReadOnly, Transient, Category = "MindKore|Shelf")
	float Alpha = 0.0f;

	/** The crank: go to the other position. Reverses mid-travel. */
	UFUNCTION(BlueprintCallable, Category = "MindKore|Shelf")
	void Toggle();

	UFUNCTION(BlueprintCallable, Category = "MindKore|Shelf")
	void MoveTo(EMKShelfPosition Target);

	/** Which actors stand in the way right now, for the log and the probe. */
	UFUNCTION(BlueprintCallable, Category = "MindKore|Shelf")
	TArray<AActor*> FindObstructions(bool bTowardII) const;

	virtual void Tick(float DeltaSeconds) override;
	virtual void BeginPlay() override;

private:
	FVector PositionILocation = FVector::ZeroVector;
	bool bTowardII = false;
	bool bReportedBlock = false;

	void SetState(EMKShelfPosition NewState);
	FVector WorldTravel() const;
};
