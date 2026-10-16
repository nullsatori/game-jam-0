#include "MKCart.h"

#include "Components/StaticMeshComponent.h"
#include "MKPhysicsPropComponent.h"
#include "MindKore.h"
#include "PhysicsEngine/PhysicsConstraintComponent.h"

#define LOCTEXT_NAMESPACE "MKCart"

AMKCart::AMKCart()
{
	PrimaryActorTick.bCanEverTick = false;

	Body = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Body"));
	SetRootComponent(Body);
	Body->SetMobility(EComponentMobility::Movable);
	Body->SetCollisionProfileName(TEXT("PhysicsActor"));
	Body->SetCollisionResponseToChannel(ECC_Visibility, ECR_Block);
	Body->SetSimulatePhysics(true);
	Body->BodyInstance.bOverrideMass = true;
	Body->BodyInstance.SetMassOverride(MassKg);
	Body->SetCanEverAffectNavigation(false);

	Brake = CreateDefaultSubobject<UPhysicsConstraintComponent>(TEXT("Brake"));
	Brake->SetupAttachment(Body);
	// Horizontal travel and all rotation locked; vertical left free so a braked
	// cart still settles on its wheels and rides a moving floor.
	Brake->SetLinearXLimit(LCM_Locked, 0.0f);
	Brake->SetLinearYLimit(LCM_Locked, 0.0f);
	Brake->SetLinearZLimit(LCM_Free, 0.0f);
	Brake->SetAngularSwing1Limit(ACM_Locked, 0.0f);
	Brake->SetAngularSwing2Limit(ACM_Locked, 0.0f);
	Brake->SetAngularTwistLimit(ACM_Locked, 0.0f);
	Brake->SetDisableCollision(false);

	Prop = CreateDefaultSubobject<UMKPhysicsPropComponent>(TEXT("Prop"));
	Prop->bCarryable = false;
	Prop->bThrowable = false;
}

void AMKCart::OnConstruction(const FTransform& Transform)
{
	Super::OnConstruction(Transform);
	Body->BodyInstance.bOverrideMass = true;
	Body->BodyInstance.SetMassOverride(MassKg);
}

void AMKCart::BeginPlay()
{
	Super::BeginPlay();
	// The constraint is only ever built by SetBraked, where the cart stands.
	Brake->BreakConstraint();
	SetBraked(bStartBraked);
}

void AMKCart::SetBraked(bool bInBraked)
{
	if (bBraked == bInBraked)
	{
		return;
	}
	bBraked = bInBraked;
	if (bBraked)
	{
		// Second component empty: constrained to the world at the current pose.
		Brake->SetWorldTransform(Body->GetComponentTransform());
		Brake->SetConstrainedComponents(Body, NAME_None, nullptr, NAME_None);
	}
	else
	{
		Brake->BreakConstraint();
		Body->WakeAllRigidBodies();
	}
	MKReportMechanismNoise(this, GetActorLocation(), BrakeNoiseLoudness, TEXT("MK_CartBrake"));
	UE_LOG(LogMindKore, Log, TEXT("Cart %s: brake %s"), *GetName(), bBraked ? TEXT("on") : TEXT("off"));
}

FMKInteractionOffer AMKCart::QueryInteraction(const AActor* Hand) const
{
	if (!Hand)
	{
		return FMKInteractionOffer::None();
	}
	return FMKInteractionOffer::Available(bBraked ? LOCTEXT("Release", "Снять с тормоза") : LOCTEXT("Brake", "Поставить на тормоз"));
}

void AMKCart::CompleteInteraction(AActor* Hand)
{
	SetBraked(!bBraked);
}

#undef LOCTEXT_NAMESPACE
