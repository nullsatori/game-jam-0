#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "MKImpactReceiver.h"
#include "MKImpactTypes.h"
#include "MKImpactTarget.generated.h"

class AMKImpactTarget;

DECLARE_MULTICAST_DELEGATE_OneParam(FMKTargetDiedNative, AMKImpactTarget* /* Target */);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FMKTargetDiedSignature, AMKImpactTarget*, Target);

/**
 * Shared base for impact-target dummies. Owns health, an explicit dead state,
 * and a single "whole-visual" flinch driven off the resolved impact. Subclasses
 * provide the visual (static box rig or skeletal mesh) and point FlinchRoot at
 * the component that should be nudged on a hit.
 *
 * Damage flows through one channel: the firearm calls ReceiveResolvedImpact,
 * which both applies DamageAmount to health and triggers the flinch. There is no
 * parallel ApplyPointDamage / OnTakePointDamage path.
 *
 * Death: crossing zero health sets bIsDead, broadcasts OnTargetDied, then either
 * resets for range practice (bResetHealthInsteadOfDestroying) or hides and
 * disables collision. Subclasses override HandleDeath to add ragdoll etc.
 */
UCLASS(Abstract)
class MINDKORE_API AMKImpactTarget : public AActor, public IMKImpactReceiver
{
	GENERATED_BODY()

public:
	AMKImpactTarget();

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MindKore|Target")
	float MaxHealth = 100.0f;

	// Runtime health. Reset to MaxHealth in BeginPlay.
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Transient, Category = "MindKore|Target")
	float CurrentHealth = 100.0f;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Transient, Category = "MindKore|Target")
	bool bIsDead = false;

	// Range dummies: fire OnTargetDied, then ResetHealth so they can be shot again.
	// Enemies: leave false — stay dead after HandleDeath hides/disables them.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MindKore|Target")
	bool bResetHealthInsteadOfDestroying = true;

	// C++ listeners (tests, AI). Prefer this over the dynamic BP delegate.
	FMKTargetDiedNative OnTargetDiedNative;

	UPROPERTY(BlueprintAssignable, Category = "MindKore|Target")
	FMKTargetDiedSignature OnTargetDied;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MindKore|Target|Flinch")
	float FlinchDistance = 12.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MindKore|Target|Flinch")
	float FlinchRecoverySpeed = 10.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MindKore|Target|Flinch")
	float FlinchRotationDegrees = 5.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MindKore|Target|Flinch")
	float LightFlinchScale = 1.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MindKore|Target|Flinch")
	float HeavyFlinchScale = 1.75f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MindKore|Target|Flinch")
	float ArmorFlinchScale = 0.45f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MindKore|Target|Flinch")
	float DeathFlinchScale = 2.25f;

	UFUNCTION(BlueprintCallable, Category = "MindKore|Target")
	void ResetHealth();

	UFUNCTION(BlueprintPure, Category = "MindKore|Target")
	bool IsDead() const { return bIsDead; }

	virtual void ReceiveResolvedImpact_Implementation(const FMKResolvedImpact& ResolvedImpact) override;

protected:
	virtual void BeginPlay() override;
	virtual void Tick(float DeltaSeconds) override;

	// Subclasses set this to the component that should be nudged on a hit.
	UPROPERTY(Transient)
	TObjectPtr<USceneComponent> FlinchRoot = nullptr;

	void ApplyDamageToHealth(float DamageAmount);
	void ApplyVisualFlinch(const FVector& ShotDirection, EMKHitReaction Reaction);

	// Called once when health crosses zero. Broadcasts OnTargetDied, then either
	// ResetHealth (range) or DisableAsCorpse (stay dead). Override to add ragdoll
	// before calling Super, or replace the corpse path entirely.
	virtual void HandleDeath();

	// Stay-dead path: hide + no collision. Range mode never reaches here.
	virtual void DisableAsCorpse();

private:
	FVector FlinchRestLocation = FVector::ZeroVector;
	FRotator FlinchRestRotation = FRotator::ZeroRotator;
	FVector FlinchOffset = FVector::ZeroVector;
	FRotator FlinchRotationOffset = FRotator::ZeroRotator;
};
