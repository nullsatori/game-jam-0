#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "MKCarryComponent.generated.h"

class UPhysicsHandleComponent;
class UMKPhysicsPropComponent;

/**
 * Numbers of one way of holding a prop. Hands are the only profile today; the
 * telekinesis slice adds a second set of numbers to the same component rather
 * than a second handle (Docs/DESIGN_PHYSICS_AND_TRAVERSAL.md, D3 and D12).
 */
USTRUCT(BlueprintType)
struct FMKCarryProfile
{
	GENERATED_BODY()

	/** Heaviest body this profile lifts, kg. Heavier things are pushed, not carried. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MindKore|Carry", meta = (ClampMin = "0.0"))
	float MaxMassKg = 35.0f;

	/** Clear distance from the eye to the near side of the prop, cm. The prop's own radius is added. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MindKore|Carry", meta = (ClampMin = "0.0"))
	float HoldClearanceCm = 55.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MindKore|Carry", meta = (ClampMin = "0.0"))
	float LinearStiffness = 900.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MindKore|Carry", meta = (ClampMin = "0.0"))
	float LinearDamping = 120.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MindKore|Carry", meta = (ClampMin = "0.0"))
	float AngularStiffness = 1200.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MindKore|Carry", meta = (ClampMin = "0.0"))
	float AngularDamping = 200.0f;

	/** Furthest surface of a prop this profile can take hold of, from the eye, cm. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MindKore|Carry", meta = (ClampMin = "0.0"))
	float MaxReachCm = 220.0f;

	/**
	 * The handle's target walks toward the hold point no faster than this, cm/s.
	 * The handle's own exponential interpolation is not a speed limit: the gym
	 * probe of 17.09.2026 saw a 14 kg crate reach 25 m/s on its way to the hands.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MindKore|Carry", meta = (ClampMin = "1.0"))
	float ApproachSpeedCm = 450.0f;

	/** Prop further than this from its hold point is lagging, cm. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MindKore|Carry", meta = (ClampMin = "1.0"))
	float ErrorLimitCm = 70.0f;

	/** Lagging for this long lets go. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MindKore|Carry", meta = (ClampMin = "0.0"))
	float ErrorGraceSeconds = 0.35f;

	/** What the arm delivers on a throw, N s. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MindKore|Carry", meta = (ClampMin = "0.0"))
	float ThrowImpulseNewtonSeconds = 60.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MindKore|Carry", meta = (ClampMin = "0.0"))
	float MaxThrowSpeedCm = 1100.0f;
};

UENUM(BlueprintType)
enum class EMKCarryRelease : uint8
{
	Put,
	Thrown,
	/** Pressed into something for too long. */
	Strained,
	/** The carrier ended up standing on what it holds. */
	StoodOn,
	/** Prop destroyed, carrier's body handed to physics, end of play. */
	Lost
};

/**
 * The one owner of "a prop is in the player's hands". Holds through a single
 * UPhysicsHandleComponent with finite stiffness: the prop stays a simulated
 * body, collides with the world and lags when pressed into a wall. It is never
 * teleported and its world collision is never switched off.
 */
UCLASS(ClassGroup = (MindKore), meta = (BlueprintSpawnableComponent))
class MINDKORE_API UMKCarryComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UMKCarryComponent();

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MindKore|Carry")
	FMKCarryProfile Hands;

	/**
	 * The hands are in the middle of something that cannot be dropped (a round
	 * half-way into the cylinder, a crowbar swing or block). The crowbar merely
	 * in hand does not set it (crowbar slice 3). Written only by
	 * UMKHandsComponent::RefreshCarryGate; the carrier only refuses to start a
	 * grab while it is set. BlueprintReadWrite is historical - no Blueprint may
	 * write it, or the hands owner and that Blueprint fight every frame.
	 * Jam lift: nothing in the starter writes it (UMKHandsComponent is not lifted).
	 */
	UPROPERTY(Transient, BlueprintReadWrite, Category = "MindKore|Carry")
	bool bHandsBusy = false;

	/** Reason the last TryGrab refused, for the log and the interaction offer. Empty when it succeeded. */
	UFUNCTION(BlueprintPure, Category = "MindKore|Carry")
	FText GetLastRefusal() const { return LastRefusal; }

	UFUNCTION(BlueprintPure, Category = "MindKore|Carry")
	bool IsHolding() const { return Held != nullptr; }

	UFUNCTION(BlueprintPure, Category = "MindKore|Carry")
	UMKPhysicsPropComponent* GetHeld() const { return Held; }

	/** Would TryGrab succeed right now? Fills the refusal text when not. */
	bool CanGrab(const UMKPhysicsPropComponent* Prop, FText& OutRefusal) const;

	UFUNCTION(BlueprintCallable, Category = "MindKore|Carry")
	bool TryGrab(UMKPhysicsPropComponent* Prop);

	/** Let go with no added velocity. */
	UFUNCTION(BlueprintCallable, Category = "MindKore|Carry")
	void Put();

	/** Let go with one launch velocity along the view. A prop that may not be thrown is put down. */
	UFUNCTION(BlueprintCallable, Category = "MindKore|Carry")
	void Throw();

	/** The carrier's body is no longer under its control (ragdoll, death). */
	void NotifyCarrierLostControl();

	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

private:
	bool GetViewPoint(FVector& OutLocation, FRotator& OutRotation) const;
	FVector HoldPoint(const FVector& ViewLocation, const FRotator& ViewRotation) const;
	bool IsStandingOn(const UMKPhysicsPropComponent* Prop) const;
	void Release(EMKCarryRelease Reason, const FVector& LaunchVelocity);

	UPROPERTY(Transient)
	TObjectPtr<UPhysicsHandleComponent> Handle = nullptr;

	UPROPERTY(Transient)
	TObjectPtr<UMKPhysicsPropComponent> Held = nullptr;

	/** Yaw of the prop relative to the view at the moment it was taken, kept while carried. */
	float HeldYawOffset = 0.0f;
	float HeldRadiusCm = 0.0f;
	float StrainSeconds = 0.0f;
	/** Where the handle is being asked to be; trails the hold point at ApproachSpeedCm. */
	FVector HandleTarget = FVector::ZeroVector;
	FText LastRefusal;
};
