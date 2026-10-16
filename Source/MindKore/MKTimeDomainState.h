#pragma once

#include "CoreMinimal.h"

enum class EMKSlowPhase : uint8 { Idle, Entering, Active, Exiting };
enum class EMKSlowExit : uint8 { Released, Exhausted, DecisiveImpact, OwnerLost };

/** Real, unpaused seconds only. Simulation time never pays the resource cost. */
struct MINDKORE_API FMKTimeDomainState
{
	EMKSlowPhase Phase = EMKSlowPhase::Idle;
	EMKSlowExit ExitReason = EMKSlowExit::Released;
	float Remaining = 3.0f;
	float Alpha = 0.0f;
	float EnterSeconds = 0.10f;
	float ExitSeconds = 0.08f;
	bool bHeld = false;

	bool IsSlowed() const { return Phase == EMKSlowPhase::Entering || Phase == EMKSlowPhase::Active; }
	bool Press()
	{
		if (bHeld) return false;
		bHeld = true;
		if (Phase != EMKSlowPhase::Idle || Remaining <= 0.0f) return false;
		Phase = EMKSlowPhase::Entering;
		return true;
	}
	void Exit(EMKSlowExit Reason)
	{
		if (!IsSlowed()) return;
		ExitReason = Reason;
		Phase = Alpha > 0.0f ? EMKSlowPhase::Exiting : EMKSlowPhase::Idle;
	}
	void Release() { bHeld = false; Exit(EMKSlowExit::Released); }
	void Advance(float RealDelta, bool bPaused)
	{
		if (bPaused || !FMath::IsFinite(RealDelta) || RealDelta <= 0.0f) return;
		float PresentationDelta = RealDelta;
		if (IsSlowed())
		{
			const float Used = FMath::Min(Remaining, RealDelta);
			Remaining = FMath::Max(0.0f, Remaining - Used);
			Alpha = FMath::Min(1.0f, Alpha + Used / FMath::Max(EnterSeconds, 0.001f));
			if (Alpha >= 1.0f) Phase = EMKSlowPhase::Active;
			if (Remaining > 0.0f) return;
			Exit(EMKSlowExit::Exhausted);
			PresentationDelta -= Used;
		}
		if (Phase == EMKSlowPhase::Exiting)
		{
			Alpha = FMath::Max(0.0f, Alpha - PresentationDelta / FMath::Max(ExitSeconds, 0.001f));
			if (Alpha <= 0.0f) Phase = EMKSlowPhase::Idle;
		}
	}
};
