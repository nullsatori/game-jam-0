#pragma once

#include "CoreMinimal.h"
#include "MKImpactResolver.h"
#include "MKImpactTypes.h"

class AActor;
class UObject;
struct FHitResult;

/**
 * The one way a resolved impact reaches the thing it hit, shared by the
 * firearm (a round's first contact in each crossing) and the crowbar
 * (UMKMeleeComponent).
 *
 * Both used to be private to UMKFirearmComponent (ResolveImpact/ApplyDamage);
 * their bodies were moved here unchanged in the crowbar's strike slice and the
 * two private functions are now one-line wrappers, so the crowbar
 * does not grow a parallel path that skips the AI damage report or the
 * receiver's own rules. The ShotId+index replay guard is one of those rules,
 * and only the enemy and the glass pane have it; this function dedupes nothing.
 */

/**
 * Classify one trace hit: hit zone, surface, reaction, damage multiplier and
 * camera kick, through MKImpactResolver. Fills OutFeedback for presentation.
 * Identity (ShotId, Kind, SourceActor), damage and impulse are the caller's.
 */
MINDKORE_API FMKResolvedImpact MKResolveImpactFromHit(
	const FHitResult& Hit, const FVector& Direction, const FMKImpactResolveParams& Params,
	FMKImpactFeedback& OutFeedback);

/**
 * Hand the impact to the hit actor if it implements IMKImpactReceiver, then
 * publish the canonical amount to AI perception (UAISense_Damage) when it
 * carries damage. Returns true when the hit actor implements the receiver and
 * the impact was handed to it (the call goes through ProcessEvent, which an
 * actor ignores until its world has initialized actors for play).
 *
 * Single channel: the resolved impact carries the damage; the receiver applies
 * it to health AND drives its hit reaction. No parallel
 * ApplyPointDamage/OnTakePointDamage path, and no caller may call
 * IMKImpactReceiver::Execute_ReceiveResolvedImpact itself.
 */
MINDKORE_API bool MKDeliverResolvedImpact(
	UObject* WorldContext, AActor* Instigator, const FMKResolvedImpact& ResolvedImpact);
