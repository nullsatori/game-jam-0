#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "JamLadder.generated.h"

class UBoxComponent;
class USoundBase;

/**
 * A Source-style (CS:GO/CS2) ladder, without snapping to its centre: walking
 * into Target holds on (no use key). On it W climbs up or down by the view's
 * pitch (up while the view is no more than ClimbDownPitchDeg below level), S
 * the reverse, A/D along the rungs, at a fixed speed with no gravity; keys
 * released, the body stops. Off: over the top onto Top, down onto the floor,
 * off the side, or Space. Gravity zones only: in zero-G the ladder is ignored
 * (RMB still clings to the rungs as to any fixed surface).
 */
UCLASS()
class MINDKORE_API AJamLadder : public AActor
{
	GENERATED_BODY()
public:
	AJamLadder();

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Jam|Ladder")
	TObjectPtr<UBoxComponent> Target;

	/** Body-centre landings, relative to this actor: Z bounds the climb; climbing up to Top.Z carries the body over the lip to Top. */
	UPROPERTY(EditAnywhere, Category = "Jam|Ladder", meta = (MakeEditWidget = "true"))
	FVector Top = FVector(0, -100, 200);

	UPROPERTY(EditAnywhere, Category = "Jam|Ladder", meta = (MakeEditWidget = "true"))
	FVector Bottom = FVector(0, -100, -200);

	/** cm/s. Source climbs at 200 u/s, 0.78-0.8 of its 250 walk; 0.78 of our WalkSpeedCm 450. */
	UPROPERTY(EditAnywhere, Category = "Jam|Ladder", meta = (ClampMin = "1"))
	float ClimbSpeedCm = 350;

	/** W climbs down only with the view pitched further below level than this; level or a little down climbs up (Source's dead zone), degrees. */
	UPROPERTY(EditAnywhere, Category = "Jam|Ladder", meta = (ClampMin = "0", ClampMax = "89"))
	float ClimbDownPitchDeg = 15;

	/** A rung struck on the grab and every RungStepCm climbed: MindKore's armour plate (recorded donor, CREDITS.md), pitched down. */
	UPROPERTY(EditAnywhere, Category = "Jam|Ladder")
	TObjectPtr<USoundBase> RungSound = nullptr;

	/** 3.5 clanks a second at 350 cm/s, the walk's step rate (Source: one per 0.35 s). */
	UPROPERTY(EditAnywhere, Category = "Jam|Ladder", meta = (ClampMin = "1"))
	float RungStepCm = 100;

	UPROPERTY(EditAnywhere, Category = "Jam|Ladder", meta = (ClampMin = "0"))
	float RungVolume = 0.3f;

	UPROPERTY(EditAnywhere, Category = "Jam|Ladder")
	FVector2D RungPitchRange = FVector2D(0.5f, 0.62f);

	bool IsInReach(const FVector& Position) const;

	/** Moving along MoveDirection grabs here. On the ground only a move into the ladder that climbs up grabs (to climb down from a top landing, step into the well). */
	bool CanEnter(const FVector& Position, const FVector& MoveDirection, bool bOnGround = false) const;

	/** Where W along this view climbs: +1 up, -1 down (S: the opposite). */
	float ClimbSign(const FVector& ViewDirection) const;
};
