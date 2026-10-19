#include "MKVitals.h"

#include "GameFramework/Actor.h"
#include "MindKore.h"
// Jam lift: MKMeleeDefence.h cut - only MKBlockAppliesTo was used; its one-line rule is copied below.

namespace
{
	float MKNonNegative(float Value)
	{
		return FMath::IsFinite(Value) ? FMath::Max(Value, 0.0f) : 0.0f;
	}

	// Jam lift: copied from MKMeleeDefence.cpp (MKBlockAppliesTo) so the melee defence module is not needed.
	bool MKBlockAppliesTo(EMKImpactKind Kind)
	{
		// A crowbar held across the body does not stop a round (the task's rule,
		// crowbar slice 3). Only the close blow is blocked.
		return Kind != EMKImpactKind::Round;
	}
}

FMKIncomingDamage MKApplyIncomingDamage(
	float Health, float Armor, float Damage, float AbsorbFraction,
	EMKImpactKind Kind, bool bBlocked, float BlockDamageFraction)
{
	FMKIncomingDamage Result;
	const float H = MKNonNegative(Health);
	const float A = MKNonNegative(Armor);
	const float P = FMath::Clamp(MKNonNegative(AbsorbFraction), 0.0f, 1.0f);

	Result.Damage = MKNonNegative(Damage);
	if (bBlocked && MKBlockAppliesTo(Kind))
	{
		Result.Damage *= FMath::Clamp(MKNonNegative(BlockDamageFraction), 0.0f, 1.0f);
	}
	// Armour is spent one to one with the damage it prevents, never scaled
	// first and discovered short afterwards (SURVIVAL section 2, "порядок важен").
	Result.Absorbed = FMath::Min(A, P * Result.Damage);
	Result.ArmorAfter = A - Result.Absorbed;
	Result.HealthDamage = Result.Damage - Result.Absorbed;
	Result.HealthAfter = FMath::Max(0.0f, H - Result.HealthDamage);
	return Result;
}

UMKVitalsComponent::UMKVitalsComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
	Health = MaxHealth;
	Armor = StartingArmor;
}

void UMKVitalsComponent::BeginPlay()
{
	Super::BeginPlay();
	Health = MaxHealth;
	Armor = StartingArmor;
}

bool UMKVitalsComponent::ApplyIncoming(float Damage, EMKImpactKind Kind, bool bBlocked, const FGuid& EventId)
{
	if (!History.Accept(EventId, 0))
	{
		UE_LOG(LogMindKore, Verbose, TEXT("Vitals %s: event %s already paid"), *GetNameSafe(GetOwner()), *EventId.ToString());
		return false;
	}
	const FMKIncomingDamage Result =
		MKApplyIncomingDamage(Health, Armor, Damage, AbsorbFraction, Kind, bBlocked, BlockDamageFraction);
	const float HealthBefore = Health;
	Health = Result.HealthAfter;
	Armor = Result.ArmorAfter;
	UE_LOG(LogMindKore, Log, TEXT("Vitals %s: %s hit %.1f%s -> armour %.1f, health %.1f -> %.1f (armour left %.1f)"),
		*GetNameSafe(GetOwner()), Kind == EMKImpactKind::Blunt ? TEXT("blunt") : TEXT("round"),
		Result.Damage, bBlocked ? TEXT(" (blocked)") : TEXT(""), Result.Absorbed, HealthBefore, Health, Armor);

	if (Result.Damage > 0.0f)
	{
		OnHit.Broadcast(Result.HealthDamage, Result.Absorbed);
	}

	if (HealthBefore > 0.0f && Health <= 0.0f)
	{
		// Death is the listener's (the jam's game mode); without one zero health is held at zero.
		UE_LOG(LogMindKore, Warning, TEXT("Vitals %s: health reached 0, %d death listener(s)"), *GetNameSafe(GetOwner()),
			OnDepleted.IsBound() ? 1 : 0);
		OnDepleted.Broadcast();
	}
	return true;
}

void UMKVitalsComponent::Revive()
{
	Health = MaxHealth;
	Armor = StartingArmor;
	UE_LOG(LogMindKore, Log, TEXT("Vitals %s: revived, health %.1f, armour %.1f"), *GetNameSafe(GetOwner()), Health, Armor);
}

void UMKVitalsComponent::Restore(float AddHealth, float AddArmor)
{
	// Health held at zero is restored like any other: Restore does not know
	// about death (OnDepleted's listener owns it). SURVIVAL section 6 wants
	// death to win over a use that pays on the same frame - not enforced here.
	const float HealthBefore = Health;
	const float ArmorBefore = Armor;
	Health = FMath::Max(Health, FMath::Min(Health + MKNonNegative(AddHealth), MaxHealth));
	Armor = FMath::Max(Armor, FMath::Min(Armor + MKNonNegative(AddArmor), MaxArmor));
	UE_LOG(LogMindKore, Log, TEXT("Vitals %s: restored, health %.1f -> %.1f, armour %.1f -> %.1f"),
		*GetNameSafe(GetOwner()), HealthBefore, Health, ArmorBefore, Armor);
}
