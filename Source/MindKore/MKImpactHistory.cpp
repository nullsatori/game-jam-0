#include "MKImpactHistory.h"

// OS lift: copied verbatim from MKBodyResponseComponent.cpp.
bool FMKImpactHistory::Accept(const FGuid& ShotId, int32 Interaction)
{
	if (!ShotId.IsValid()) return true;
	for (const FKey& Key : Keys)
		if (Key.Shot == ShotId && Key.Interaction == Interaction) return false;
	if (Keys.Num() < 256) Keys.Add({ShotId, Interaction});
	else { Keys[Next] = {ShotId, Interaction}; Next = (Next + 1) % 256; }
	return true;
}
