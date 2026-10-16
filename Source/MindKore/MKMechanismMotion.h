#pragma once

#include "CoreMinimal.h"

/**
 * Pure arithmetic shared by the level mechanisms (door, gate, shelf block).
 * Free functions so the state rules can be tested without a world: a
 * mechanism that mis-steps is a level bug, and a level bug found in PIE is
 * ten times the cost of one found here.
 */

/** Move Alpha (0..1) toward 0 or 1 at a constant rate of 1/DurationSeconds. DurationSeconds <= 0 snaps. */
inline float MKAdvanceAlpha(float Alpha, bool bTowardOne, float DurationSeconds, float DeltaSeconds)
{
	const float Target = bTowardOne ? 1.0f : 0.0f;
	if (DurationSeconds <= KINDA_SMALL_NUMBER || DeltaSeconds <= 0.0f)
	{
		return DurationSeconds <= KINDA_SMALL_NUMBER ? Target : FMath::Clamp(Alpha, 0.0f, 1.0f);
	}
	const float Step = DeltaSeconds / DurationSeconds;
	return FMath::Clamp(FMath::FInterpConstantTo(Alpha, Target, 1.0f, Step), 0.0f, 1.0f);
}

/**
 * Which way a hinged leaf swings so that it opens AWAY from whoever pushes it.
 * DoorForward is the leaf's closed-state facing (actor +X); ToInstigator runs
 * from the hinge to the hand. Returns +1 or -1 as a yaw multiplier. A hand
 * exactly in the door plane counts as inside (-1), so the answer is never
 * "do nothing".
 */
inline float MKDoorSwingSign(const FVector& DoorForward, const FVector& ToInstigator)
{
	// Leaf lies along local +Y. Rotating it by +yaw carries it toward local -X,
	// so a hand on the +X (front) side gets +1 and the leaf leaves it behind.
	const float Side = FVector::DotProduct(DoorForward.GetSafeNormal2D(), ToInstigator.GetSafeNormal2D());
	return Side > 0.0f ? 1.0f : -1.0f;
}

/**
 * A sliding block may only step when nothing stands in the volume it is about
 * to occupy. Given the block's current box and the step it wants to take,
 * returns the box to test (the destination slab, padded so a capsule touching
 * the face counts). Kept separate from the overlap query so the test can pin
 * the geometry of "in the way".
 */
inline FBox MKShelfStepBox(const FVector& Center, const FVector& HalfExtent, const FVector& StepDelta, float PaddingCm)
{
	const FVector NextCenter = Center + StepDelta;
	const FVector Pad(PaddingCm, PaddingCm, 0.0f);
	// Union of the current and next slab: a pawn already inside the block's
	// footprint (it walked in while parked) blocks the step too.
	FBox Box(Center - HalfExtent - Pad, Center + HalfExtent + Pad);
	Box += FBox(NextCenter - HalfExtent - Pad, NextCenter + HalfExtent + Pad);
	return Box;
}

/**
 * Does a walker's remaining route pass through a doorway? Points are in DOOR
 * space: the closed leaf lies along +Y from the hinge at the origin, the wall
 * is the plane X = 0. Points[0] is where the walker stands now, the rest is
 * what is left of his path. A man walking the corridor PAST a door has a route
 * that never changes side of the wall, so the door stays shut for him; standing
 * near a door is not a reason to open it.
 */
inline bool MKRouteCrossesDoorway(TConstArrayView<FVector> Points, float LeafWidthCm, float MarginCm)
{
	for (int32 Index = 1; Index < Points.Num(); ++Index)
	{
		const FVector& A = Points[Index - 1];
		const FVector& B = Points[Index];
		if ((A.X > 0.0f) == (B.X > 0.0f))
		{
			continue;
		}
		const float T = A.X / (A.X - B.X);
		const float CrossY = FMath::Lerp(A.Y, B.Y, T);
		if (CrossY >= -MarginCm && CrossY <= LeafWidthCm + MarginCm)
		{
			return true;
		}
	}
	return false;
}
