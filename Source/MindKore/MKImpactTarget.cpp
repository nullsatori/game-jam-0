#include "MKImpactTarget.h"

#include "Components/SceneComponent.h"

AMKImpactTarget::AMKImpactTarget()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.bStartWithTickEnabled = false;
	SetCanBeDamaged(true);
}

void AMKImpactTarget::BeginPlay()
{
	Super::BeginPlay();

	CurrentHealth = MaxHealth;
	bIsDead = false;
	if (FlinchRoot)
	{
		FlinchRestLocation = FlinchRoot->GetRelativeLocation();
		FlinchRestRotation = FlinchRoot->GetRelativeRotation();
	}
}

void AMKImpactTarget::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	if (!FlinchRoot)
	{
		SetActorTickEnabled(false);
		return;
	}

	FlinchOffset = FMath::VInterpTo(FlinchOffset, FVector::ZeroVector, DeltaSeconds, FlinchRecoverySpeed);
	FlinchRotationOffset = FMath::RInterpTo(FlinchRotationOffset, FRotator::ZeroRotator, DeltaSeconds, FlinchRecoverySpeed);

	if (FlinchOffset.IsNearlyZero(0.05f) && FlinchRotationOffset.IsNearlyZero(0.05f))
	{
		FlinchOffset = FVector::ZeroVector;
		FlinchRotationOffset = FRotator::ZeroRotator;
		FlinchRoot->SetRelativeLocation(FlinchRestLocation);
		FlinchRoot->SetRelativeRotation(FlinchRestRotation);
		SetActorTickEnabled(false);
		return;
	}

	FlinchRoot->SetRelativeLocation(FlinchRestLocation + FlinchOffset);
	FlinchRoot->SetRelativeRotation(FlinchRestRotation + FlinchRotationOffset);
}

void AMKImpactTarget::ResetHealth()
{
	bIsDead = false;
	CurrentHealth = MaxHealth;
	SetActorHiddenInGame(false);
	SetActorEnableCollision(true);
}

void AMKImpactTarget::ReceiveResolvedImpact_Implementation(const FMKResolvedImpact& ResolvedImpact)
{
	// Stay-dead corpses ignore further shots. Range dummies reset inside
	// HandleDeath, so bIsDead is false again by the time the next shot arrives.
	if (bIsDead)
	{
		return;
	}

	const bool bLethal = (CurrentHealth - ResolvedImpact.DamageAmount) <= 0.0f;
	ApplyDamageToHealth(ResolvedImpact.DamageAmount);

	// Flinch after death handling so range reset does not wipe a Death shove,
	// and so stay-dead corpses still get one big knock before DisableAsCorpse.
	const EMKHitReaction Reaction = bLethal ? EMKHitReaction::Death : ResolvedImpact.HitReaction;
	ApplyVisualFlinch(ResolvedImpact.ShotDirection, Reaction);
}

void AMKImpactTarget::ApplyDamageToHealth(float DamageAmount)
{
	if (bIsDead || DamageAmount <= 0.0f)
	{
		return;
	}

	const bool bWasAlive = CurrentHealth > 0.0f;
	CurrentHealth = FMath::Max(0.0f, CurrentHealth - DamageAmount);

	if (bWasAlive && CurrentHealth <= 0.0f)
	{
		HandleDeath();
	}
}

void AMKImpactTarget::HandleDeath()
{
	bIsDead = true;
	OnTargetDiedNative.Broadcast(this);
	OnTargetDied.Broadcast(this);

	if (bResetHealthInsteadOfDestroying)
	{
		// Range practice: announce the kill, then stand back up.
		ResetHealth();
	}
	else
	{
		DisableAsCorpse();
	}
}

void AMKImpactTarget::DisableAsCorpse()
{
	SetActorHiddenInGame(true);
	SetActorEnableCollision(false);
}

void AMKImpactTarget::ApplyVisualFlinch(const FVector& ShotDirection, EMKHitReaction Reaction)
{
	if (!FlinchRoot)
	{
		return;
	}

	const float ReactionScale = MKFlinchScaleForReaction(
		Reaction,
		LightFlinchScale,
		HeavyFlinchScale,
		ArmorFlinchScale,
		DeathFlinchScale);
	if (ReactionScale <= 0.0f)
	{
		return;
	}

	const FVector Direction = ShotDirection.GetSafeNormal(UE_SMALL_NUMBER, FVector::ForwardVector);

	FlinchOffset = Direction * FlinchDistance * ReactionScale;
	FlinchRotationOffset = FRotator(
		-FMath::Clamp(Direction.X, -1.0f, 1.0f) * FlinchRotationDegrees * ReactionScale,
		FMath::Clamp(Direction.Y, -1.0f, 1.0f) * FlinchRotationDegrees * ReactionScale,
		-FMath::Clamp(Direction.Y, -1.0f, 1.0f) * FlinchRotationDegrees * 0.65f * ReactionScale);

	SetActorTickEnabled(true);
}
