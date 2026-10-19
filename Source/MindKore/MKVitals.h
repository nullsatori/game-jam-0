#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
// Jam lift: MKBodyResponseComponent.h replaced by MKImpactHistory.h - only FMKImpactHistory was used.
#include "MKImpactHistory.h"
#include "MKImpactTypes.h"
#include "MKVitals.generated.h"

/** One incoming hit worked through health and armour. See MKApplyIncomingDamage. */
struct FMKIncomingDamage
{
	/** Damage after the block, before armour. */
	float Damage = 0.0f;
	/** Taken by armour, one to one with the damage it prevented. */
	float Absorbed = 0.0f;
	float HealthDamage = 0.0f;
	float HealthAfter = 0.0f;
	float ArmorAfter = 0.0f;
};

/** A hit that was paid: what went into health and what the armour took. */
DECLARE_MULTICAST_DELEGATE_TwoParams(FMKVitalsHit, float /*HealthDamage*/, float /*Absorbed*/);

/** Health reached zero (once per life). */
DECLARE_MULTICAST_DELEGATE(FMKVitalsDepleted);

/**
 * DESIGN_SURVIVAL_AND_PICKUPS section 2, pure:
 *
 *   Absorbed = min(A, p x D);  A' = A - Absorbed;  H' = max(0, H - (D - Absorbed))
 *
 * A block first scales D by BlockDamageFraction, and only where a block applies
 * (MKBlockAppliesTo: not against a round). Negative or non-finite inputs are
 * read as zero; p is clamped to [0, 1].
 */
MINDKORE_API FMKIncomingDamage MKApplyIncomingDamage(
	float Health, float Armor, float Damage, float AbsorbFraction,
	EMKImpactKind Kind, bool bBlocked, float BlockDamageFraction);

/**
 * The player's health H and armour A, and the one writer of both
 * (DESIGN_SURVIVAL_AND_PICKUPS sections 2 and 16).
 *
 * Writers: ApplyIncoming takes and Restore gives back, nothing else. The pawn
 * calls ApplyIncoming for a close blow that gets through (crowbar slice 3);
 * the pockets call Restore when a consumable's use pays. The body response,
 * the presentation and the pawn itself never subtract health.
 *
 * NOT an impact receiver. The pawn does not implement IMKImpactReceiver, so an
 * enemy round still does not reach the player at all - that is a separate
 * SURVIVAL decision, not something this owner turns on by existing.
 * Jam lift: in the starter AJamCharacter DOES implement IMKImpactReceiver and
 * forwards a round to ApplyIncoming - the paragraph above describes MindKore.
 *
 * Transient state: there is no save game or checkpoint. Death is an event
 * (OnDepleted); what follows is decided by its listener - in the jam, the
 * game mode (AJamShipGameMode revives the same pawn with Revive).
 */
UCLASS(ClassGroup = (MindKore), meta = (BlueprintSpawnableComponent))
class MINDKORE_API UMKVitalsComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UMKVitalsComponent();

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "MindKore|Vitals", meta = (ClampMin = "1"))
	float MaxHealth = 100.0f;

	/** HYPOTHESIS (DESIGN_SURVIVAL_AND_PICKUPS section 14): what the armour holds. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "MindKore|Vitals", meta = (ClampMin = "0"))
	float MaxArmor = 50.0f;

	/** Armour at spawn. Zero: armour comes from capsules found. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "MindKore|Vitals", meta = (ClampMin = "0"))
	float StartingArmor = 0.0f;

	/** HYPOTHESIS (DESIGN_SURVIVAL_AND_PICKUPS section 2, p): share of each hit armour may take. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "MindKore|Vitals", meta = (ClampMin = "0", ClampMax = "1"))
	float AbsorbFraction = 0.6f;

	/**
	 * HYPOTHESIS: what a block that was not a parry lets through of a close
	 * blow. The author has not said what a plain block gives (crowbar
	 * architecture, question 2); this is a tunable share, not a ruling.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "MindKore|Vitals", meta = (ClampMin = "0", ClampMax = "1"))
	float BlockDamageFraction = 0.3f;

	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Transient, Category = "MindKore|Vitals")
	float Health = 100.0f;

	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Transient, Category = "MindKore|Vitals")
	float Armor = 0.0f;

	/**
	 * Take one hit. EventId is the event's id (a blow's AttackId): a second
	 * call with the same valid id changes nothing and returns false, so a hit
	 * reported by two layers is paid once. An invalid id is always a new event.
	 */
	bool ApplyIncoming(float Damage, EMKImpactKind Kind, bool bBlocked, const FGuid& EventId);

	/** After a hit that cost anything has been paid. The bodycam's projector shows the health on it. */
	FMKVitalsHit OnHit;

	/** The hit that took health to zero. Nothing here dies or restarts: the listener decides. */
	FMKVitalsDepleted OnDepleted;

	/** Back to full health and the starting armour (a revival). */
	void Revive();

	/**
	 * Give back health and armour, each up to its maximum; what does not fit is
	 * lost (SURVIVAL section 5: the whole pack is spent, the surplus is not kept).
	 */
	void Restore(float AddHealth, float AddArmor);

	UFUNCTION(BlueprintPure, Category = "MindKore|Vitals")
	bool IsDepleted() const { return Health <= 0.0f; }

protected:
	virtual void BeginPlay() override;

private:
	/** The body response's replay guard, reused: one entry per event id. */
	FMKImpactHistory History;
};
