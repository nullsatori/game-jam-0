#include "MKImpactTypes.h"

#include "MKHitZoneComponent.h"

FMKImpactFeedback UMKImpactDataAsset::GetFeedbackForSurface(EPhysicalSurface SurfaceType) const
{
	if (const FMKImpactFeedback* Feedback = SurfaceFeedback.Find(SurfaceType))
	{
		return *Feedback;
	}

	if (const FMKImpactFeedback* DefaultFeedback = SurfaceFeedback.Find(SurfaceType_Default))
	{
		return *DefaultFeedback;
	}

	return FMKImpactFeedback();
}

bool UMKImpactDataAsset::TryGetZoneFeedback(EMKHitZone HitZone, FMKImpactFeedback& OutFeedback) const
{
	if (const FMKImpactFeedback* Found = ZoneFeedback.Find(HitZone))
	{
		OutFeedback = *Found;
		return true;
	}

	return false;
}

EMKHitZone MKResolveHitZoneFromComponent(const UPrimitiveComponent* HitComponent)
{
	if (const UMKHitZoneComponent* HitZoneComponent = Cast<UMKHitZoneComponent>(HitComponent))
	{
		return HitZoneComponent->HitZone;
	}

	return EMKHitZone::Default;
}

EMKSurfaceKind MKSurfaceKindFromPhysical(EPhysicalSurface SurfaceType)
{
	switch (SurfaceType)
	{
	case SurfaceType1:
		return EMKSurfaceKind::Metal;
	case SurfaceType2:
		return EMKSurfaceKind::Concrete;
	case SurfaceType3:
		return EMKSurfaceKind::Flesh;
	case SurfaceType4:
		return EMKSurfaceKind::Armor;
	case SurfaceType5:
		return EMKSurfaceKind::Glass;
	case SurfaceType6:
		return EMKSurfaceKind::Wood;
	default:
		return EMKSurfaceKind::Default;
	}
}

bool MKIsFleshWound(EMKSurfaceKind SurfaceKind)
{
	return SurfaceKind == EMKSurfaceKind::Flesh || SurfaceKind == EMKSurfaceKind::Armor;
}

EMKHitZone MKBodyRegion(EMKHitZone HitZone, FName Bone, EMKSurfaceKind SurfaceKind)
{
	if (!MKIsFleshWound(SurfaceKind))
	{
		return EMKHitZone::Default;
	}
	if (SurfaceKind == EMKSurfaceKind::Armor)
	{
		return EMKHitZone::Armor;
	}
	if (HitZone != EMKHitZone::Default)
	{
		return HitZone;
	}

	const FString Name = Bone.ToString().ToLower();
	if (Name == TEXT("head") || Name.StartsWith(TEXT("neck")))
	{
		return EMKHitZone::Head;
	}
	const bool bLeft = Name.EndsWith(TEXT("_l"));
	if (bLeft || Name.EndsWith(TEXT("_r")))
	{
		for (const TCHAR* Arm : { TEXT("arm"), TEXT("hand"), TEXT("thumb"), TEXT("index"), TEXT("middle"), TEXT("ring"), TEXT("pinky") })
		{
			if (Name.Contains(Arm))
			{
				return bLeft ? EMKHitZone::LeftArm : EMKHitZone::RightArm;
			}
		}
		for (const TCHAR* Leg : { TEXT("thigh"), TEXT("calf"), TEXT("foot"), TEXT("ball") })
		{
			if (Name.Contains(Leg))
			{
				return bLeft ? EMKHitZone::LeftLeg : EMKHitZone::RightLeg;
			}
		}
	}
	return EMKHitZone::Torso;
}

int32 MKSelectResolvedHit(TConstArrayView<FMKHitCandidate> Candidates, float SameActorToleranceCm)
{
	// The shot stops at the nearest BLOCKING hit; touches are only ever
	// candidates for the zone upgrade below.
	int32 BlockingIndex = INDEX_NONE;
	for (int32 Index = 0; Index < Candidates.Num(); ++Index)
	{
		if (!Candidates[Index].bBlocking)
		{
			continue;
		}

		if (BlockingIndex == INDEX_NONE || Candidates[Index].Distance < Candidates[BlockingIndex].Distance)
		{
			BlockingIndex = Index;
		}
	}

	if (BlockingIndex == INDEX_NONE)
	{
		return INDEX_NONE;
	}

	const FMKHitCandidate& Blocking = Candidates[BlockingIndex];
	const float ZoneWindow = Blocking.Distance + SameActorToleranceCm;

	int32 BestIndex = BlockingIndex;
	int32 BestPriority = MKGetHitZonePriority(Blocking.HitZone);

	for (int32 Index = 0; Index < Candidates.Num(); ++Index)
	{
		const FMKHitCandidate& Candidate = Candidates[Index];
		if (Candidate.ActorKey != Blocking.ActorKey || Candidate.Distance > ZoneWindow)
		{
			continue;
		}

		const int32 Priority = MKGetHitZonePriority(Candidate.HitZone);
		if (Priority > BestPriority)
		{
			BestPriority = Priority;
			BestIndex = Index;
		}
	}

	return BestIndex;
}

int32 MKGetHitZonePriority(EMKHitZone HitZone)
{
	switch (HitZone)
	{
	case EMKHitZone::Head:
		return 100;
	case EMKHitZone::Armor:
		return 95;
	case EMKHitZone::LeftArm:
	case EMKHitZone::RightArm:
		return 70;
	case EMKHitZone::LeftLeg:
	case EMKHitZone::RightLeg:
		return 65;
	case EMKHitZone::Torso:
		return 40;
	default:
		return 10;
	}
}

float MKFlinchScaleForReaction(
	EMKHitReaction Reaction,
	float LightScale,
	float HeavyScale,
	float ArmorScale,
	float DeathScale)
{
	switch (Reaction)
	{
	case EMKHitReaction::None:
		return 0.0f;
	case EMKHitReaction::LightFlinch:
		return LightScale;
	case EMKHitReaction::HeavyStagger:
		return HeavyScale;
	case EMKHitReaction::ArmorDeflect:
		return ArmorScale;
	case EMKHitReaction::Death:
		return DeathScale;
	default:
		return LightScale;
	}
}

bool MKContinueAfterImpact(
	EMKSurfaceKind SurfaceKind, int32 InteractionIndex, const FMKPassThroughPolicy& Policy)
{
	if (InteractionIndex + 1 >= FMath::Max(Policy.MaxInteractions, 1))
	{
		return false;
	}

	return MKIsFleshWound(SurfaceKind);
}

FMKImpactOutcome MKResolveImpactOutcome(
	EMKImpactKind Kind, EMKHitZone HitZone, bool bHeadshotIsLethal, bool bWasDead,
	float Health, float DamageAmount, int32 HeadshotsTaken, int32 HeadshotsToKill)
{
	const bool bRound = Kind == EMKImpactKind::Round;

	FMKImpactOutcome Outcome;
	Outcome.bRegistersWound = bRound;
	Outcome.bHeadHit = bRound && bHeadshotIsLethal && HitZone == EMKHitZone::Head;
	Outcome.HeadshotsAfter = FMath::Max(HeadshotsTaken, 0);
	if (bWasDead)
	{
		Outcome.bHeadshot = Outcome.bHeadHit;
		Outcome.bFatal = true;
		Outcome.HealthAfter = 0.0f;
		return Outcome;
	}
	if (Outcome.bHeadHit)
	{
		++Outcome.HeadshotsAfter;
	}

	// A blunt impact leaves at least 1 point, or whatever less the body already
	// had: it is never what ends a life (HYPOTHESIS, see the header).
	const float Floor = bRound ? 0.0f : FMath::Min(Health, 1.0f);
	const float Remaining = FMath::Max(Floor, Health - DamageAmount);
	Outcome.bHeadshot = Outcome.bHeadHit
		&& (Outcome.HeadshotsAfter >= FMath::Max(HeadshotsToKill, 1) || Remaining <= 0.0f);
	Outcome.bFatal = Outcome.bHeadshot || (bRound && Remaining <= 0.0f);
	Outcome.HealthAfter = Outcome.bFatal ? 0.0f : Remaining;
	return Outcome;
}
