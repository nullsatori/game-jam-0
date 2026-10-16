#include "MKPhysicsPropComponent.h"

#include "Components/PrimitiveComponent.h"
#include "Engine/World.h"
#include "MKInteractable.h"
#include "MKPropMath.h"
#include "MindKore.h"

UMKPhysicsPropComponent::UMKPhysicsPropComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
}

UPrimitiveComponent* UMKPhysicsPropComponent::GetBody() const
{
	const AActor* Owner = GetOwner();
	UPrimitiveComponent* Body = Owner ? Cast<UPrimitiveComponent>(Owner->GetRootComponent()) : nullptr;
	return Body && Body->IsSimulatingPhysics() ? Body : nullptr;
}

float UMKPhysicsPropComponent::GetMassKg() const
{
	const UPrimitiveComponent* Body = GetBody();
	return Body ? Body->GetMass() : 0.0f;
}

void UMKPhysicsPropComponent::BeginPlay()
{
	Super::BeginPlay();
	UPrimitiveComponent* Body = GetBody();
	if (!Body)
	{
		UE_LOG(LogMindKore, Warning, TEXT("Prop %s: root is not a simulating primitive; it cannot be carried or report impacts"),
			*GetNameSafe(GetOwner()));
		return;
	}
	Body->SetNotifyRigidBodyCollision(true);
	Body->OnComponentHit.AddDynamic(this, &UMKPhysicsPropComponent::HandleBodyHit);
	SettledAtSeconds = (GetWorld() ? GetWorld()->GetTimeSeconds() : 0.0) + SettleSeconds;
}

void UMKPhysicsPropComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	// The carrier holds a weak pointer and lets go on its next tick; the state
	// here is cleared so nothing reads a holder off a dying prop.
	State = EMKPropState::Free;
	Holder.Reset();
	Super::EndPlay(EndPlayReason);
}

bool UMKPhysicsPropComponent::MarkHeld(AActor* InHolder)
{
	if (!InHolder || State == EMKPropState::Held || !GetBody())
	{
		return false;
	}
	State = EMKPropState::Held;
	Holder = InHolder;
	return true;
}

void UMKPhysicsPropComponent::MarkReleased(bool bThrown)
{
	State = bThrown ? EMKPropState::Thrown : EMKPropState::Free;
	Holder.Reset();
	// CCD only for the flight of a thrown body (decision D7): cheap props must
	// not tunnel through a door leaf, idle props must not pay for it.
	if (UPrimitiveComponent* Body = GetBody())
	{
		Body->SetUseCCD(bThrown);
	}
}

void UMKPhysicsPropComponent::HandleBodyHit(UPrimitiveComponent* HitComponent, AActor* OtherActor,
	UPrimitiveComponent* OtherComp, FVector NormalImpulse, const FHitResult& Hit)
{
	// A held prop scraping along a wall is the carrier's business, not a blow.
	if (State == EMKPropState::Held || !HitComponent || OtherActor == GetOwner())
	{
		return;
	}
	const float MassKg = HitComponent->GetMass();
	if (MassKg <= UE_SMALL_NUMBER)
	{
		return;
	}
	// The callback's impulse (kg cm/s) over the prop's mass is how much the
	// contact changed its velocity along the normal. That is the closing speed
	// relative to the other body whether or not that body was moving, which a
	// world-space velocity read here would not be.
	const float RelativeSpeedCm = NormalImpulse.Size() / MassKg;
	if (RelativeSpeedCm < MinImpactSpeedCm)
	{
		return;
	}

	const UWorld* World = GetWorld();
	const double Now = World ? World->GetTimeSeconds() : 0.0;
	if (State == EMKPropState::Free && Now < SettledAtSeconds)
	{
		UE_LOG(LogMindKore, Verbose, TEXT("Prop %s: settling contact at %.0f cm/s, no impact (t=%.2f)"),
			*GetNameSafe(GetOwner()), RelativeSpeedCm, Now);
		return;
	}
	double& Last = LastContactSeconds.FindOrAdd(OtherComp, -1.0);
	const bool bNew = MKIsNewContact(Last, Now, ContactWindowSeconds);
	Last = Now;
	if (!bNew)
	{
		return;
	}
	for (auto It = LastContactSeconds.CreateIterator(); It; ++It)
	{
		if (!It.Key().IsValid())
		{
			It.RemoveCurrent();
		}
	}

	if (State == EMKPropState::Thrown)
	{
		MarkReleased(/*bThrown=*/false);
	}

	const float Loudness = FMath::Min(ImpactLoudness * RelativeSpeedCm / FMath::Max(MinImpactSpeedCm, 1.0f), MaxImpactLoudness);
	// The noise is where the prop landed and belongs to the prop: the listener
	// learns where something fell, not where the thrower stands.
	MKReportMechanismNoise(GetOwner(), Hit.ImpactPoint, Loudness, TEXT("MK_PropImpact"));
	OnPropImpact.Broadcast(this, OtherComp, Hit.ImpactPoint, RelativeSpeedCm);
}
