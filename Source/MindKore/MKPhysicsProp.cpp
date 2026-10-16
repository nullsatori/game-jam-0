#include "MKPhysicsProp.h"

#include "Components/StaticMeshComponent.h"
#include "MKCarryComponent.h"
#include "MKPhysicsBridge.h"
#include "MKPhysicsPropComponent.h"

#define LOCTEXT_NAMESPACE "MKPhysicsProp"

AMKPhysicsProp::AMKPhysicsProp()
{
	PrimaryActorTick.bCanEverTick = false;

	Body = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Body"));
	SetRootComponent(Body);
	Body->SetMobility(EComponentMobility::Movable);
	Body->SetCollisionProfileName(TEXT("PhysicsActor"));
	// The hand finds the prop with the same visibility trace as every mechanism.
	Body->SetCollisionResponseToChannel(ECC_Visibility, ECR_Block);
	Body->SetSimulatePhysics(true);
	Body->BodyInstance.bOverrideMass = true;
	Body->BodyInstance.SetMassOverride(MassKg);
	// A prop that can be stood on moves; a navmesh baked around it would lie.
	Body->SetCanEverAffectNavigation(false);

	Prop = CreateDefaultSubobject<UMKPhysicsPropComponent>(TEXT("Prop"));
}

void AMKPhysicsProp::OnConstruction(const FTransform& Transform)
{
	Super::OnConstruction(Transform);
	Body->BodyInstance.bOverrideMass = true;
	Body->BodyInstance.SetMassOverride(MassKg);
}

FMKInteractionOffer AMKPhysicsProp::QueryInteraction(const AActor* Hand) const
{
	const UMKCarryComponent* Carry = Hand ? Hand->FindComponentByClass<UMKCarryComponent>() : nullptr;
	if (!Carry || Carry->GetHeld() == Prop)
	{
		return FMKInteractionOffer::None();
	}
	FText Refusal;
	return Carry->CanGrab(Prop, Refusal)
		? FMKInteractionOffer::Available(LOCTEXT("Take", "Взять"))
		: FMKInteractionOffer::Refused(LOCTEXT("Take", "Взять"), Refusal);
}

void AMKPhysicsProp::CompleteInteraction(AActor* Hand)
{
	if (UMKCarryComponent* Carry = Hand ? Hand->FindComponentByClass<UMKCarryComponent>() : nullptr)
	{
		Carry->TryGrab(Prop);
	}
}

void AMKPhysicsProp::ReceiveResolvedImpact_Implementation(const FMKResolvedImpact& ResolvedImpact)
{
	// Blunt only: a round keeps its pre-receiver behaviour on props (none). See
	// the class comment - HYPOTHESIS pending the author.
	if (ResolvedImpact.Kind != EMKImpactKind::Blunt || !ResolvedImpact.bHasPhysicsImpulse || !Body)
	{
		return;
	}
	if (FBodyInstance* Instance = Body->GetBodyInstance())
	{
		// The bridge converts N s to kg cm/s exactly once and refuses a body that
		// is not simulating.
		MKPhysicsBridge::ApplyPointImpulse(
			*Instance, ResolvedImpact.ImpulseNewtonSeconds, FVector(ResolvedImpact.Hit.ImpactPoint));
	}
}

#undef LOCTEXT_NAMESPACE
