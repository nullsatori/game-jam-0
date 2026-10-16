#include "MKHitZoneComponent.h"

#include "PhysicalMaterials/PhysicalMaterial.h"

UMKHitZoneComponent::UMKHitZoneComponent()
{
	// Hit zones are pure query volumes for the gunfire trace's zone-priority
	// logic (MKSelectResolvedHit in MKImpactTypes.cpp): they Overlap the
	// dedicated "Shot" trace channel (ECC_GameTraceChannel2, see
	// DefaultEngine.ini) so a multi-trace collects them as candidates, but
	// they deliberately Ignore everything else — most importantly
	// ECC_Visibility, which must pass straight through them. Zones used to
	// be BlockAll with an explicit Visibility block, which made an enemy's
	// own hit-zone boxes occlude both its own AI sight (AISense_Sight also
	// traces Visibility) and gunfire past the first zone hit. The character
	// mesh itself now carries the "MKCharacterBody" collision profile and is
	// what blocks Visibility/Shot for a body outside its explicit zones.
	SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	SetCollisionResponseToAllChannels(ECR_Ignore);
	SetCollisionResponseToChannel(ECC_GameTraceChannel2, ECR_Overlap);
	SetGenerateOverlapEvents(false);
}

EMKSurfaceKind UMKHitZoneComponent::GetEffectiveSurfaceKind() const
{
	if (SurfaceKind != EMKSurfaceKind::Default)
	{
		return SurfaceKind;
	}

	if (HitZone == EMKHitZone::Armor)
	{
		return EMKSurfaceKind::Armor;
	}

	if (const UPhysicalMaterial* PhysicalMaterial = GetBodyInstance() ? GetBodyInstance()->GetSimplePhysicalMaterial() : nullptr)
	{
		const EMKSurfaceKind FromPhysical = MKSurfaceKindFromPhysical(UPhysicalMaterial::DetermineSurfaceType(PhysicalMaterial));
		if (FromPhysical != EMKSurfaceKind::Default)
		{
			return FromPhysical;
		}
	}

	// Default, not Flesh: an unconfigured zone has no opinion, and the resolver
	// reads Default as "leave the surface alone". Assuming flesh here would
	// silently make crates and vehicles bleed once non-human targets exist.
	return EMKSurfaceKind::Default;
}
