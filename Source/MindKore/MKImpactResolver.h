#pragma once

#include "CoreMinimal.h"
#include "MKImpactTypes.h"

class UMKHitZoneComponent;

// Tuning the resolver needs from the owning firearm. Passed by value-ish struct
// so the resolution logic itself stays free of component state and can be unit
// tested without a UWorld.
struct FMKImpactResolveParams
{
	const UMKImpactDataAsset* ImpactData = nullptr;
	float HeadshotDamageMultiplier = 2.25f;
	float HeadshotStaggerChance = 0.85f;

	// What worn armour does to this shot's damage, supplied by the firing weapon
	// (UMKFirearmComponent::ArmorDamageMultiplier). Armour is a property of the
	// round, not of the plate: the player's RSh-12 penetrates at full damage
	// (DESIGN_ENEMIES section 9.5) while the enemy sidearm keeps the historical
	// attenuation, which is the default here. The armour zone and the armour
	// surface both read this one value instead of each carrying a literal.
	float ArmorDamageMultiplier = 0.35f;
};

// Pure impact-feedback resolution, layered in a fixed order:
//   1. data asset per-surface entry (or the built-in surface fallback),
//   2. per-component authored overrides on the hit-zone box,
//   3. zone-class rules (headshot crit, armor deflect, limb reactions),
//      from the data asset when present, else the built-in rules.
// Deterministic when a RandomStream is supplied (tests); falls back to
// FMath::FRand for gameplay.
namespace MKImpactResolver
{
	MINDKORE_API FMKImpactFeedback ResolveFeedback(
		EPhysicalSurface SurfaceType,
		const UMKHitZoneComponent* HitZoneComponent,
		EMKHitZone HitZone,
		const FMKImpactResolveParams& Params,
		const FRandomStream* RandomStream = nullptr);

	MINDKORE_API void ApplyBuiltInZoneRules(
		FMKImpactFeedback& Feedback,
		EMKHitZone HitZone,
		const FMKImpactResolveParams& Params,
		const FRandomStream* RandomStream = nullptr);
}
