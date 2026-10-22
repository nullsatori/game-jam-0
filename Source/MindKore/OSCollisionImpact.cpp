#include "OSCollisionImpact.h"

#include "Components/PrimitiveComponent.h"
#include "Engine/World.h"
#include "MKImpactDelivery.h"
#include "MindKore.h"

bool OSDeliverCollisionImpact(AActor* Source, const UPrimitiveComponent* SourceBody, const FHitResult& Hit,
	const FVector& NormalImpulse, const FOSCollisionImpactSettings& Settings, double& InOutLastSeconds, float& OutSpeedCm)
{
	OutSpeedCm = 0.0f;
	const UWorld* World = Source ? Source->GetWorld() : nullptr;
	const float MassKg = SourceBody ? SourceBody->GetMass() : 0.0f;
	if (!World || MassKg <= UE_SMALL_NUMBER || !Hit.GetActor())
	{
		return false;
	}
	// As UMKPhysicsPropComponent reads it: the contact impulse over the own mass
	// is how much the contact changed this body's velocity along the normal.
	const float Impulse = NormalImpulse.Size();
	OutSpeedCm = Impulse / MassKg;
	const double Now = World->GetTimeSeconds();
	if (OutSpeedCm < Settings.MinSpeedCm || Impulse < Settings.MinImpulse || Now - InOutLastSeconds < Settings.CooldownSeconds)
	{
		return false;
	}
	InOutLastSeconds = Now;

	// The impulse pushes this body away from the other: the blow goes the other way.
	const FVector Direction = NormalImpulse.IsNearlyZero() ? -FVector(Hit.ImpactNormal) : -NormalImpulse.GetSafeNormal();
	FMKImpactResolveParams Params;
	FMKImpactFeedback Feedback;
	FMKResolvedImpact Resolved = MKResolveImpactFromHit(Hit, Direction, Params, Feedback);
	Resolved.ShotId = FGuid::NewGuid();
	Resolved.Kind = EMKImpactKind::Blunt;
	Resolved.SourceActor = Source;
	Resolved.ImpactTimeSeconds = Now;
	Resolved.DamageAmount = Impulse * Settings.DamagePerImpulse;
	Resolved.bHasPhysicsImpulse = false;
	const bool bDelivered = MKDeliverResolvedImpact(Source, Source, Resolved);
	UE_LOG(LogMindKore, Log, TEXT("Collision impact: %s -> %s at %.0f cm/s, impulse %.0f kg cm/s, damage %.1f, %s"),
		*GetNameSafe(Source), *GetNameSafe(Hit.GetActor()), OutSpeedCm, Impulse, Resolved.DamageAmount,
		bDelivered ? TEXT("delivered") : TEXT("no receiver"));
	return bDelivered;
}

void UOSCollisionImpactComponent::BeginPlay()
{
	Super::BeginPlay();
	if (UPrimitiveComponent* Root = Cast<UPrimitiveComponent>(GetOwner()->GetRootComponent()))
	{
		Root->SetNotifyRigidBodyCollision(true);
		Root->OnComponentHit.AddDynamic(this, &UOSCollisionImpactComponent::HandleHit);
	}
}

void UOSCollisionImpactComponent::HandleHit(UPrimitiveComponent* HitComponent, AActor* OtherActor, UPrimitiveComponent* OtherComp,
	FVector NormalImpulse, const FHitResult& Hit)
{
	float SpeedCm = 0.0f;
	OSDeliverCollisionImpact(GetOwner(), HitComponent, Hit, NormalImpulse, Settings, LastSeconds, SpeedCm);
}
