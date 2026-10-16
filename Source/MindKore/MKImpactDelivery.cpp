#include "MKImpactDelivery.h"

#include "Engine/HitResult.h"
#include "GameFramework/Actor.h"
#include "MKHitZoneComponent.h"
#include "MKImpactReceiver.h"
#include "Perception/AISense_Damage.h"
#include "PhysicalMaterials/PhysicalMaterial.h"
#include "Sound/SoundBase.h"

FMKResolvedImpact MKResolveImpactFromHit(
	const FHitResult& Hit, const FVector& Direction, const FMKImpactResolveParams& Params,
	FMKImpactFeedback& OutFeedback)
{
	FMKResolvedImpact Resolved;
	Resolved.Hit = Hit;
	Resolved.ShotDirection = Direction.GetSafeNormal(UE_SMALL_NUMBER, FVector::ForwardVector);

	const UPrimitiveComponent* HitComponent = Hit.GetComponent();
	const UMKHitZoneComponent* HitZoneComponent = Cast<UMKHitZoneComponent>(HitComponent);
	Resolved.HitZone = MKResolveHitZoneFromComponent(HitComponent);

	const EPhysicalSurface SurfaceType = UPhysicalMaterial::DetermineSurfaceType(Hit.PhysMaterial.Get());
	OutFeedback = MKImpactResolver::ResolveFeedback(SurfaceType, HitZoneComponent, Resolved.HitZone, Params);

	if (Params.ImpactData)
	{
		// The sound of a body is by where the round landed, box or mesh alike.
		const EMKHitZone Region = MKBodyRegion(Resolved.HitZone, Hit.BoneName, OutFeedback.SurfaceKind);
		if (const TObjectPtr<USoundBase>* Sound = Params.ImpactData->BodyImpactSound.Find(Region); Sound && *Sound)
		{
			OutFeedback.ImpactSound = *Sound;
		}
	}

	Resolved.SurfaceKind = OutFeedback.SurfaceKind;
	Resolved.HitReaction = OutFeedback.HitReaction;
	Resolved.DamageMultiplier = OutFeedback.DamageMultiplier;
	Resolved.CameraKick = OutFeedback.CameraKick;
	return Resolved;
}

bool MKDeliverResolvedImpact(UObject* WorldContext, AActor* Instigator, const FMKResolvedImpact& ResolvedImpact)
{
	AActor* HitActor = ResolvedImpact.Hit.GetActor();
	if (!HitActor || !HitActor->GetClass()->ImplementsInterface(UMKImpactReceiver::StaticClass()))
	{
		return false;
	}

	// Single channel: the resolved impact carries the damage; the receiver
	// applies it to health AND drives its hit reaction. No parallel
	// ApplyPointDamage/OnTakePointDamage path.
	IMKImpactReceiver::Execute_ReceiveResolvedImpact(HitActor, ResolvedImpact);

	// The custom impact interface deliberately bypasses Unreal's damage
	// delegates, so AIPerception cannot infer this event for us. Publish the one
	// canonical resolved amount here, after the receiver has accepted the hit.
	if (ResolvedImpact.DamageAmount > 0.0f)
	{
		static const FName ResolvedImpactTag(TEXT("MK_ResolvedImpact"));
		const FVector EventLocation = Instigator
			? Instigator->GetActorLocation()
			: FVector(ResolvedImpact.Hit.TraceStart);
		UAISense_Damage::ReportDamageEvent(
			WorldContext,
			HitActor,
			Instigator,
			ResolvedImpact.DamageAmount,
			EventLocation,
			FVector(ResolvedImpact.Hit.ImpactPoint),
			ResolvedImpactTag);
	}
	return true;
}
