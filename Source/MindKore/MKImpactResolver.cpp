#include "MKImpactResolver.h"

#include "MKHitZoneComponent.h"

namespace
{
	float NextRandom(const FRandomStream* RandomStream)
	{
		return RandomStream ? RandomStream->FRand() : FMath::FRand();
	}
}

FMKImpactFeedback MKImpactResolver::ResolveFeedback(
	EPhysicalSurface SurfaceType,
	const UMKHitZoneComponent* HitZoneComponent,
	EMKHitZone HitZone,
	const FMKImpactResolveParams& Params,
	const FRandomStream* RandomStream)
{
	// Layer 1: data-asset (preferred) or built-in surface fallback.
	FMKImpactFeedback Feedback;
	if (Params.ImpactData)
	{
		Feedback = Params.ImpactData->GetFeedbackForSurface(SurfaceType);
	}
	else
	{
		// Surface identity comes from the shared mapping; only the per-surface
		// feel numbers live here.
		Feedback.SurfaceKind = MKSurfaceKindFromPhysical(SurfaceType);
		switch (Feedback.SurfaceKind)
		{
		case EMKSurfaceKind::Metal:
			Feedback.HitReaction = EMKHitReaction::LightFlinch;
			Feedback.CameraKick = 0.15f;
			break;
		case EMKSurfaceKind::Concrete:
			Feedback.HitReaction = EMKHitReaction::LightFlinch;
			Feedback.CameraKick = 0.1f;
			break;
		case EMKSurfaceKind::Flesh:
			Feedback.HitReaction = EMKHitReaction::HeavyStagger;
			Feedback.DamageMultiplier = 1.2f;
			Feedback.CameraKick = 0.2f;
			break;
		case EMKSurfaceKind::Armor:
			// ArmorDeflect is the reaction the plate produces (kick, shards),
			// not a damage rule: the round goes through - see
			// FMKImpactResolveParams::ArmorDamageMultiplier.
			Feedback.HitReaction = EMKHitReaction::ArmorDeflect;
			Feedback.DamageMultiplier = Params.ArmorDamageMultiplier;
			Feedback.CameraKick = 0.35f;
			break;
		case EMKSurfaceKind::Glass:
			// Loudest surface on the level (DESIGN_LEVEL section 1.3) and the
			// only one a shot permanently CHANGES - so it gets the largest kick
			// of any non-flesh surface. The kick is the shooter's own body
			// reacting to the noise, not the round meeting resistance, which is
			// why the damage multiplier stays at 1: glass stops nothing.
			Feedback.HitReaction = EMKHitReaction::LightFlinch;
			Feedback.CameraKick = 0.4f;
			break;
		default:
			break;
		}
	}

	// Layer 2: per-hit-zone authored overrides on the box component.
	if (HitZoneComponent)
	{
		const EMKSurfaceKind EffectiveSurface = HitZoneComponent->GetEffectiveSurfaceKind();
		if (EffectiveSurface != EMKSurfaceKind::Default)
		{
			Feedback.SurfaceKind = EffectiveSurface;
		}
		if (HitZoneComponent->HasDamageMultiplierOverride())
		{
			Feedback.DamageMultiplier = HitZoneComponent->DamageMultiplierOverride;
		}
		if (HitZoneComponent->HasReactionOverride())
		{
			Feedback.HitReaction = HitZoneComponent->ReactionOverride;
		}
	}

	// Layer 3: zone-class rules (headshot crit, armor deflect, limb reactions).
	// Prefer the data asset; fall back to the built-in rules when none is set.
	FMKImpactFeedback ZoneRule;
	if (Params.ImpactData && Params.ImpactData->TryGetZoneFeedback(HitZone, ZoneRule))
	{
		Feedback.SurfaceKind = ZoneRule.SurfaceKind;
		Feedback.HitReaction = ZoneRule.HitReaction;
		Feedback.DamageMultiplier = ZoneRule.DamageMultiplier;
		Feedback.CameraKick = ZoneRule.CameraKick;
		if (ZoneRule.ImpactSound) { Feedback.ImpactSound = ZoneRule.ImpactSound; }
		if (ZoneRule.ImpactVfx) { Feedback.ImpactVfx = ZoneRule.ImpactVfx; }
		if (ZoneRule.DecalMaterial) { Feedback.DecalMaterial = ZoneRule.DecalMaterial; }
	}
	else
	{
		ApplyBuiltInZoneRules(Feedback, HitZone, Params, RandomStream);
		// A zone box has no physical material of its own, so layer 1 gave it no
		// assets, and the rule above only names the surface it ends in. Its
		// sound, burst and decal are that surface's: a round into the head box
		// is answered like a round into the head under it (which lands on the
		// mesh and is flesh by its material).
		if (Params.ImpactData && HitZone != EMKHitZone::Default)
		{
			for (const TPair<TEnumAsByte<EPhysicalSurface>, FMKImpactFeedback>& Surface : Params.ImpactData->SurfaceFeedback)
			{
				if (Surface.Value.SurfaceKind != Feedback.SurfaceKind)
				{
					continue;
				}
				if (!Feedback.ImpactSound) { Feedback.ImpactSound = Surface.Value.ImpactSound; }
				if (!Feedback.ImpactVfx) { Feedback.ImpactVfx = Surface.Value.ImpactVfx; }
				if (!Feedback.DecalMaterial) { Feedback.DecalMaterial = Surface.Value.DecalMaterial; }
				break;
			}
		}
	}

	return Feedback;
}

void MKImpactResolver::ApplyBuiltInZoneRules(
	FMKImpactFeedback& Feedback,
	EMKHitZone HitZone,
	const FMKImpactResolveParams& Params,
	const FRandomStream* RandomStream)
{
	switch (HitZone)
	{
	case EMKHitZone::Head:
		Feedback.SurfaceKind = EMKSurfaceKind::Flesh;
		Feedback.DamageMultiplier = Params.HeadshotDamageMultiplier;
		Feedback.CameraKick = 0.42f;
		Feedback.HitReaction = NextRandom(RandomStream) < Params.HeadshotStaggerChance
			? EMKHitReaction::HeavyStagger
			: EMKHitReaction::LightFlinch;
		break;
	case EMKHitZone::Armor:
		Feedback.SurfaceKind = EMKSurfaceKind::Armor;
		Feedback.HitReaction = EMKHitReaction::ArmorDeflect;
		Feedback.DamageMultiplier = Params.ArmorDamageMultiplier;
		Feedback.CameraKick = 0.35f;
		break;
	case EMKHitZone::LeftArm:
	case EMKHitZone::RightArm:
	case EMKHitZone::LeftLeg:
	case EMKHitZone::RightLeg:
	case EMKHitZone::Torso:
		if (Feedback.SurfaceKind == EMKSurfaceKind::Default)
		{
			Feedback.SurfaceKind = EMKSurfaceKind::Flesh;
		}
		if (Feedback.HitReaction == EMKHitReaction::None)
		{
			Feedback.HitReaction = EMKHitReaction::HeavyStagger;
		}
		break;
	default:
		break;
	}
}
