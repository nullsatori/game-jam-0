#include "MKCarryComponent.h"

#include "Components/PrimitiveComponent.h"
#include "Engine/World.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
// OS lift: MKChaosMoverComponent.h / MKMoverPawn.h cut - the starter pawn is an ACharacter.
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "MKPhysicsPropComponent.h"
#include "MKPropMath.h"
#include "MindKore.h"
#include "PhysicsEngine/PhysicsHandleComponent.h"

#define LOCTEXT_NAMESPACE "MKCarry"

UMKCarryComponent::UMKCarryComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	// The hold point follows this frame's view, so it is set before physics runs.
	PrimaryComponentTick.TickGroup = TG_PrePhysics;
}

void UMKCarryComponent::BeginPlay()
{
	Super::BeginPlay();
	// Created here rather than as a default subobject: the handle is an
	// implementation detail of this component, and the pawn's Blueprint
	// children should not grow a serialized component for it.
	Handle = NewObject<UPhysicsHandleComponent>(GetOwner(), TEXT("MKCarryHandle"));
	Handle->RegisterComponent();
}

void UMKCarryComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (Held)
	{
		Release(EMKCarryRelease::Lost, FVector::ZeroVector);
	}
	Super::EndPlay(EndPlayReason);
}

bool UMKCarryComponent::GetViewPoint(FVector& OutLocation, FRotator& OutRotation) const
{
	const APawn* Pawn = Cast<APawn>(GetOwner());
	const APlayerController* PlayerController = Pawn ? Cast<APlayerController>(Pawn->GetController()) : nullptr;
	if (!PlayerController)
	{
		return false;
	}
	PlayerController->GetPlayerViewPoint(OutLocation, OutRotation);
	return true;
}

FVector UMKCarryComponent::HoldPoint(const FVector& ViewLocation, const FRotator& ViewRotation) const
{
	// Pitch is limited so that looking at the floor lowers the prop to the
	// ground in front of the feet instead of pushing it into the carrier's legs,
	// and looking up does not lift it over the head.
	FRotator Aim = ViewRotation;
	Aim.Pitch = FMath::ClampAngle(Aim.Pitch, -50.0f, 35.0f);
	return ViewLocation + Aim.Vector() * (Hands.HoldClearanceCm + HeldRadiusCm);
}

bool UMKCarryComponent::IsStandingOn(const UMKPhysicsPropComponent* Prop) const
{
	// OS lift: AMKMoverPawn/UMKChaosMoverComponent floor check replaced by ACharacter's CharacterMovement CurrentFloor.
	const ACharacter* Character = Cast<ACharacter>(GetOwner());
	const UCharacterMovementComponent* Movement = Character ? Character->GetCharacterMovement() : nullptr;
	if (!Prop || !Movement || !Movement->IsMovingOnGround() || !Movement->CurrentFloor.IsWalkableFloor())
	{
		return false;
	}
	const FHitResult& Floor = Movement->CurrentFloor.HitResult;
	const AActor* FloorActor = Floor.GetActor();
	if (!FloorActor)
	{
		return false;
	}
	if (FloorActor == Prop->GetOwner())
	{
		return true;
	}
	// One link further: the carrier stands on a crate that stands on the cart
	// it is trying to lift. Deeper stacks are not chased in this slice.
	const FBox Bounds = FloorActor->GetComponentsBoundingBox(/*bNonColliding=*/false);
	const FVector Start(Bounds.GetCenter().X, Bounds.GetCenter().Y, Bounds.Min.Z + 1.0f);
	FCollisionQueryParams Params(SCENE_QUERY_STAT(MKCarrySupportChain), /*bTraceComplex=*/false, FloorActor);
	FHitResult Below;
	return GetWorld()->LineTraceSingleByChannel(Below, Start, Start - FVector(0.0f, 0.0f, 12.0f), ECC_PhysicsBody, Params)
		&& Below.GetActor() == Prop->GetOwner();
}

bool UMKCarryComponent::CanGrab(const UMKPhysicsPropComponent* Prop, FText& OutRefusal) const
{
	OutRefusal = FText::GetEmpty();
	if (!Prop || !Prop->GetBody())
	{
		OutRefusal = LOCTEXT("NotAProp", "не поднять");
		return false;
	}
	if (Held || bHandsBusy)
	{
		OutRefusal = LOCTEXT("HandsFull", "руки заняты");
		return false;
	}
	if (Prop->GetState() == EMKPropState::Held)
	{
		OutRefusal = LOCTEXT("HeldByOther", "уже держат");
		return false;
	}
	if (!Prop->bCarryable || Prop->GetMassKg() > Hands.MaxMassKg)
	{
		OutRefusal = LOCTEXT("TooHeavy", "слишком тяжело");
		return false;
	}
	if (IsStandingOn(Prop))
	{
		OutRefusal = LOCTEXT("StandingOnIt", "стою на нём");
		return false;
	}
	// The look trace has its own reach, but the carrier is callable without it
	// and must not take a crate from across the room on its own authority.
	FVector ViewLocation;
	FRotator ViewRotation;
	FVector Closest;
	if (GetViewPoint(ViewLocation, ViewRotation)
		&& Prop->GetBody()->GetClosestPointOnCollision(ViewLocation, Closest) > Hands.MaxReachCm)
	{
		OutRefusal = LOCTEXT("OutOfReach", "не дотянуться");
		return false;
	}
	return true;
}

bool UMKCarryComponent::TryGrab(UMKPhysicsPropComponent* Prop)
{
	FVector ViewLocation;
	FRotator ViewRotation;
	if (!Handle || !CanGrab(Prop, LastRefusal) || !GetViewPoint(ViewLocation, ViewRotation) || !Prop->MarkHeld(GetOwner()))
	{
		UE_LOG(LogMindKore, Log, TEXT("Carry: grab of %s refused: %s"),
			*GetNameSafe(Prop ? Prop->GetOwner() : nullptr), *LastRefusal.ToString());
		return false;
	}

	UPrimitiveComponent* Body = Prop->GetBody();
	Held = Prop;
	HeldRadiusCm = Body->Bounds.SphereRadius;
	HeldYawOffset = FRotator::NormalizeAxis(Body->GetComponentRotation().Yaw - ViewRotation.Yaw);
	StrainSeconds = 0.0f;

	Handle->LinearStiffness = Hands.LinearStiffness;
	Handle->LinearDamping = Hands.LinearDamping;
	Handle->AngularStiffness = Hands.AngularStiffness;
	Handle->AngularDamping = Hands.AngularDamping;
	// The approach is rate-limited in TickComponent; the handle's own smoothing
	// only takes the corners off that.
	Handle->InterpolationSpeed = 50.0f;
	HandleTarget = Body->GetCenterOfMass();
	Handle->bSoftLinearConstraint = true;
	Handle->bSoftAngularConstraint = true;
	// Grabbed at the centre of mass and started from where the prop is: the
	// interpolated target then carries it to the hold point at a finite rate.
	Body->WakeAllRigidBodies();
	Handle->GrabComponentAtLocationWithRotation(Body, NAME_None, Body->GetCenterOfMass(), Body->GetComponentRotation());

	UE_LOG(LogMindKore, Log, TEXT("Carry: took %s (%.1f kg)"), *Prop->GetOwner()->GetName(), Prop->GetMassKg());
	return true;
}

void UMKCarryComponent::Put()
{
	if (Held)
	{
		Release(EMKCarryRelease::Put, FVector::ZeroVector);
	}
}

void UMKCarryComponent::Throw()
{
	if (!Held)
	{
		return;
	}
	FVector ViewLocation;
	FRotator ViewRotation;
	if (!Held->bThrowable || !GetViewPoint(ViewLocation, ViewRotation))
	{
		Release(EMKCarryRelease::Put, FVector::ZeroVector);
		return;
	}
	// The carrier's velocity already contains whatever it stands on, so the
	// support's motion enters the launch exactly once.
	const FVector Launch = MKThrowVelocity(GetOwner()->GetVelocity(), ViewRotation.Vector(), Held->GetMassKg(),
		Hands.ThrowImpulseNewtonSeconds, Hands.MaxThrowSpeedCm);
	Release(EMKCarryRelease::Thrown, Launch);
}

void UMKCarryComponent::NotifyCarrierLostControl()
{
	if (Held)
	{
		Release(EMKCarryRelease::Lost, FVector::ZeroVector);
	}
}

void UMKCarryComponent::Release(EMKCarryRelease Reason, const FVector& LaunchVelocity)
{
	UMKPhysicsPropComponent* Prop = Held;
	Held = nullptr;
	StrainSeconds = 0.0f;
	if (Handle)
	{
		Handle->ReleaseComponent();
	}
	if (!IsValid(Prop))
	{
		return;
	}
	const bool bThrown = Reason == EMKCarryRelease::Thrown;
	Prop->MarkReleased(bThrown);
	if (UPrimitiveComponent* Body = Prop->GetBody())
	{
		if (bThrown)
		{
			// One velocity, set once. No impulse on top and nothing per frame.
			Body->SetPhysicsLinearVelocity(LaunchVelocity);
		}
		Body->WakeAllRigidBodies();
	}
	UE_LOG(LogMindKore, Log, TEXT("Carry: released %s (%s)"), *GetNameSafe(Prop->GetOwner()), *UEnum::GetValueAsString(Reason));
}

void UMKCarryComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
	if (!Held)
	{
		return;
	}
	UPrimitiveComponent* Body = IsValid(Held) ? Held->GetBody() : nullptr;
	FVector ViewLocation;
	FRotator ViewRotation;
	if (!Body || !GetViewPoint(ViewLocation, ViewRotation))
	{
		Release(EMKCarryRelease::Lost, FVector::ZeroVector);
		return;
	}
	// Jumped onto the thing in hand: let go, never ride it.
	if (IsStandingOn(Held))
	{
		Release(EMKCarryRelease::StoodOn, FVector::ZeroVector);
		return;
	}

	const FVector Target = HoldPoint(ViewLocation, ViewRotation);
	HandleTarget = FMath::VInterpConstantTo(HandleTarget, Target, DeltaTime, Hands.ApproachSpeedCm);
	Handle->SetTargetLocationAndRotation(HandleTarget, FRotator(0.0f, ViewRotation.Yaw + HeldYawOffset, 0.0f));

	// Against the rate-limited target, not the hold point: a prop on its way to
	// the hands is not strained, a prop that cannot follow a target moving at a
	// known finite speed is being held back by something.
	const float ErrorCm = FVector::Dist(Body->GetCenterOfMass(), HandleTarget);
	StrainSeconds = MKAdvanceHoldStrain(StrainSeconds, ErrorCm, Hands.ErrorLimitCm, DeltaTime);
	if (StrainSeconds >= Hands.ErrorGraceSeconds)
	{
		Release(EMKCarryRelease::Strained, FVector::ZeroVector);
	}
}

#undef LOCTEXT_NAMESPACE
