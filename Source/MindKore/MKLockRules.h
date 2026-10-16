#pragma once

#include "CoreMinimal.h"

/** What a hand at a door gets. One answer; the door acts on it (AMKDoor). */
enum class EMKDoorVerdict : uint8
{
	/** Hand on the inside of a bolted door: the bolt comes off. */
	Unbolt,
	RefuseBolted,
	/** The building's lock (service mode, or locked behind the player). A key does not open it. */
	RefuseLocked,
	/** Key lock engaged and the hand carries the key: the key turns. The next use opens. */
	TurnKey,
	RefuseNoKey,
	/** No lock in the way: open or close. */
	Operate
};

/**
 * The order the locks are read in, oldest owner first: the bolt, then the
 * building's lock, then the key lock. The building outranks the key, so a
 * door that locked itself behind the player (bLocked) refuses even a hand
 * that carries its key (PLAN_ADMIN_ENTRANCE_KEY §5.3).
 */
inline EMKDoorVerdict MKResolveDoorVerdict(bool bBolted, bool bHandInside, bool bLocked, bool bKeyLockEngaged, bool bHandHasKey)
{
	if (bBolted)
	{
		return bHandInside ? EMKDoorVerdict::Unbolt : EMKDoorVerdict::RefuseBolted;
	}
	if (bLocked)
	{
		return EMKDoorVerdict::RefuseLocked;
	}
	if (bKeyLockEngaged)
	{
		return bHandHasKey ? EMKDoorVerdict::TurnKey : EMKDoorVerdict::RefuseNoKey;
	}
	return EMKDoorVerdict::Operate;
}
