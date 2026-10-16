#pragma once

#include "CoreMinimal.h"

/**
 * Pure arithmetic behind carrying, throwing, jumping and load mechanisms
 * (Docs/DESIGN_PHYSICS_AND_TRAVERSAL.md). Free of UWorld so the rules can be
 * pinned by unit tests; every threshold that feeds them is a stand hypothesis
 * until measured.
 */

/**
 * Jump press remembered for a short real-time window (decision D2).
 *
 * The Chaos jump transition only fires from Walking, so a press a few frames
 * before landing is lost without this. The press is re-sent every frame until
 * the pawn is seen leaving the ground after a frame where it was sent while
 * grounded - that is the only observable sign the simulation consumed it - or
 * until the window runs out. No coyote time: that needs a jump out of Falling,
 * which is a different transition and a separate decision.
 */
struct FMKJumpBuffer
{
	double PressedAtSeconds = -1.0;
	bool bSentOnGround = false;
	bool bSentOnce = false;

	void Press(double NowSeconds)
	{
		PressedAtSeconds = NowSeconds;
		bSentOnGround = false;
		bSentOnce = false;
	}

	void Clear()
	{
		PressedAtSeconds = -1.0;
		bSentOnGround = false;
		bSentOnce = false;
	}

	/** Should this frame's input carry the jump press? Call once per game frame. */
	bool Update(double NowSeconds, bool bOnGround, double WindowSeconds)
	{
		if (PressedAtSeconds < 0.0)
		{
			return false;
		}
		// The window only ends a press that has reached the simulation at least
		// once. A frame longer than the window (a hitch, a 9 fps background
		// editor - where this was found) would otherwise eat the press whole.
		if ((bSentOnGround && !bOnGround) || (bSentOnce && NowSeconds - PressedAtSeconds > WindowSeconds))
		{
			Clear();
			return false;
		}
		bSentOnGround = bOnGround;
		bSentOnce = true;
		return true;
	}
};

/**
 * Launch velocity of a released prop (decision D7): the holder's velocity once,
 * plus one impulse along the aim limited both by what the arm can deliver and
 * by a speed cap, so a mug does not leave at rifle speed and a crate is merely
 * dropped forward.
 */
inline FVector MKThrowVelocity(const FVector& HolderVelocity, const FVector& AimDirection,
	float MassKg, float ImpulseNewtonSeconds, float MaxSpeedCm)
{
	const FVector Direction = AimDirection.GetSafeNormal();
	if (Direction.IsNearlyZero() || MassKg <= UE_SMALL_NUMBER)
	{
		return HolderVelocity;
	}
	// N s / kg = m/s; the project's physics boundary is centimetres.
	const float SpeedCm = FMath::Min(FMath::Max(ImpulseNewtonSeconds, 0.0f) / MassKg * 100.0f, FMath::Max(MaxSpeedCm, 0.0f));
	return HolderVelocity + Direction * SpeedCm;
}

/**
 * A held prop that cannot reach its target (pressed into a wall, caught in a
 * doorway) lags first and is let go only after the error has stayed above the
 * limit for a while: a single bad frame is not a reason to drop it.
 * Returns the updated over-limit time; release when it reaches GraceSeconds.
 */
inline float MKAdvanceHoldStrain(float StrainSeconds, float ErrorCm, float ErrorLimitCm, float DeltaSeconds)
{
	return ErrorCm > ErrorLimitCm ? StrainSeconds + FMath::Max(DeltaSeconds, 0.0f) : 0.0f;
}

/**
 * One meaningful contact per pair per window (decision D8). Async substeps
 * deliver several hit callbacks for what the player reads as one blow; after
 * the bodies have really separated for longer than the window, a new contact
 * is a new blow.
 */
inline bool MKIsNewContact(double LastContactSeconds, double NowSeconds, double WindowSeconds)
{
	return LastContactSeconds < 0.0 || NowSeconds - LastContactSeconds > WindowSeconds;
}

/**
 * Load mechanism with hysteresis (decision D11). The tray is "loaded" once the
 * resting mass has stayed at or above EngageKg for SettleSeconds, and unloaded
 * once it has stayed below ReleaseKg for the same time; between the two
 * thresholds the previous answer stands, so a crate rocking on the edge does
 * not make the bridge chatter.
 */
struct FMKLoadLatch
{
	bool bLoaded = false;
	float PendingSeconds = 0.0f;

	bool Update(float RestingMassKg, float EngageKg, float ReleaseKg, float SettleSeconds, float DeltaSeconds)
	{
		const bool bWantsChange = bLoaded ? RestingMassKg < ReleaseKg : RestingMassKg >= EngageKg;
		PendingSeconds = bWantsChange ? PendingSeconds + FMath::Max(DeltaSeconds, 0.0f) : 0.0f;
		if (bWantsChange && PendingSeconds >= SettleSeconds)
		{
			bLoaded = !bLoaded;
			PendingSeconds = 0.0f;
		}
		return bLoaded;
	}
};
