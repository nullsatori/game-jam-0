#pragma once

#include "CoreMinimal.h"
#include "Curves/CurveFloat.h"
#include "OSWeaponCurves.generated.h"

// The guns' keyed motion, shared by the parts (OSWeaponParts.cpp) and the attachments (OSWeaponAttach.cpp).

/** A knock on the gun in view at a moment of its cycle or reload: the springs' velocity jumps by this much. */
USTRUCT(BlueprintType)
struct FOSPoseKick
{
	GENERATED_BODY()

	/** Seconds after the shot / the reload's start / the round's start, as the list it is in says. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OS|Weapon|Pose")
	float Time = 0.0f;

	/** cm/s, camera frame (X forward, Y right, Z up). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OS|Weapon|Pose")
	FVector Move = FVector::ZeroVector;

	/** deg/s: pitch (muzzle up), yaw (right), roll (clockwise from behind). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OS|Weapon|Pose")
	FRotator Turn = FRotator::ZeroRotator;
};

/** One key of a moving part's curve: seconds, share of the travel/turn, slopes in share per second (cubic, broken tangents). */
struct FOSCycleKey
{
	float Time;
	float Value;
	float Arrive;
	float Leave;
};

MINDKORE_API FRuntimeFloatCurve OSMakeCycleCurve(std::initializer_list<FOSCycleKey> Keys);
/** The curve at Seconds; 0 when it has no keys. */
MINDKORE_API float OSEvalCycle(const FRuntimeFloatCurve& Curve, float Seconds);
