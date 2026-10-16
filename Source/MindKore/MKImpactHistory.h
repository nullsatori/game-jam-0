#pragma once

#include "CoreMinimal.h"

// Jam lift: copied verbatim from MKBodyResponseComponent.h, the only part of
// the body response the glass pane and the vitals use. The body response
// itself (ragdoll, balance, get-up) is not lifted.

// Bounded replay guard. Invalid legacy keys are accepted as new events.
struct MINDKORE_API FMKImpactHistory
{
	bool Accept(const FGuid& ShotId, int32 Interaction);
	struct FKey { FGuid Shot; int32 Interaction; };
	TArray<FKey> Keys;
	int32 Next = 0;
};
