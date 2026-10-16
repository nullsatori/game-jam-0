#pragma once

#include "CoreMinimal.h"
#include "Containers/ArrayView.h"
#include "Engine/DataAsset.h"
#include "PhysicalMaterials/PhysicalMaterial.h"
#include "MKImpactTypes.generated.h"

class UMaterialInterface;
class UNiagaraSystem;
class USoundBase;
class UPrimitiveComponent;

UENUM(BlueprintType)
enum class EMKSurfaceKind : uint8
{
	Default UMETA(DisplayName = "Default"),
	Metal UMETA(DisplayName = "Metal"),
	Concrete UMETA(DisplayName = "Concrete"),
	Flesh UMETA(DisplayName = "Flesh"),
	Armor UMETA(DisplayName = "Armor"),

	/**
	 * Glass, added 2026-08-22 alongside the glass material kit.
	 *
	 * Appended rather than inserted: these values are saved into assets, so
	 * putting Glass anywhere but the end would silently reassign every existing
	 * surface override in the project.
	 *
	 * It earns its own kind rather than borrowing Concrete because glass is the
	 * only surface on the level that a shot CHANGES (DESIGN_LEVEL section 1.3) -
	 * hitting it is a state transition, not a texture of impact. Falling through
	 * to Default would have given the loudest event on the map the quietest
	 * feedback in the table.
	 */
	Glass UMETA(DisplayName = "Glass"),

	/**
	 * Wood - doors, boards, trim, floors, bark - added 2026-09-29 for the
	 * crowbar, whose blow knocks and chips it rather than ringing and raising
	 * dust (the owner: "про дерево не забудь"). Appended for the same reason
	 * as Glass. A round on wood keeps the Default feedback until the impact
	 * data gives it its own (DA_Impact_Default and the fallback decals fall
	 * back to Default).
	 */
	Wood UMETA(DisplayName = "Wood")
};

// Named reactions the resolver can return. Today AMKImpactTarget only maps them
// to different flinch scales (see MKFlinchScaleForReaction) — HeavyStagger is
// not yet a real stagger, Death not yet a ragdoll. Keep the names: M3 enemies
// will hang real behaviour on the same enum rather than inventing a second one.
UENUM(BlueprintType)
enum class EMKHitReaction : uint8
{
	None UMETA(DisplayName = "None"),
	LightFlinch UMETA(DisplayName = "Light Flinch"),
	HeavyStagger UMETA(DisplayName = "Heavy Stagger"),
	ArmorDeflect UMETA(DisplayName = "Armor Deflect"),
	Death UMETA(DisplayName = "Death")
};

UENUM(BlueprintType)
enum class EMKHitZone : uint8
{
	Default UMETA(DisplayName = "Default"),
	Head UMETA(DisplayName = "Head"),
	Torso UMETA(DisplayName = "Torso"),
	LeftArm UMETA(DisplayName = "Left Arm"),
	RightArm UMETA(DisplayName = "Right Arm"),
	LeftLeg UMETA(DisplayName = "Left Leg"),
	RightLeg UMETA(DisplayName = "Right Leg"),
	Armor UMETA(DisplayName = "Armor")
};

/**
 * What delivered the impact. Receivers that model a round's anatomy (the wound
 * channel, "a round through the head kills") ask this before applying it.
 *
 * Round is the default so every impact built before the crowbar existed keeps
 * its meaning without being touched. Blunt is the crowbar (UMKMeleeComponent):
 * it shoves and hurts but opens no wound channel and is never an instant
 * headshot kill (MKResolveImpactOutcome).
 */
UENUM(BlueprintType)
enum class EMKImpactKind : uint8
{
	Round UMETA(DisplayName = "Round"),
	Blunt UMETA(DisplayName = "Blunt")
};

USTRUCT(BlueprintType)
struct FMKImpactFeedback
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MindKore|Impact")
	EMKSurfaceKind SurfaceKind = EMKSurfaceKind::Default;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MindKore|Impact")
	EMKHitReaction HitReaction = EMKHitReaction::LightFlinch;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MindKore|Impact")
	float DamageMultiplier = 1.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MindKore|Impact")
	float CameraKick = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MindKore|Impact")
	TObjectPtr<USoundBase> ImpactSound = nullptr;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MindKore|Impact")
	TObjectPtr<UNiagaraSystem> ImpactVfx = nullptr;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MindKore|Impact")
	TObjectPtr<UMaterialInterface> DecalMaterial = nullptr;
};

USTRUCT(BlueprintType)
struct FMKResolvedImpact
{
	GENERATED_BODY()

	// One event identity: one discharge (retained by a travelling projectile) or
	// one crowbar swing. Invalid IDs are reserved for legacy callers; each such
	// call is a new event.
	UPROPERTY(BlueprintReadWrite, Category = "MindKore|Impact")
	FGuid ShotId;

	// Round or blunt. Round by default, so older callers keep their meaning.
	UPROPERTY(BlueprintReadWrite, Category = "MindKore|Impact")
	EMKImpactKind Kind = EMKImpactKind::Round;

	// The shooter, kept so it survives projectile destruction in deferred body
	// reactions; bullet time reads it (UMKTimeDomainSubsystem::NotifyDecisiveImpact).
	// Null for a crowbar swing by default, so a blunt impact never ends bullet
	// time - pending the author's decision (crowbar slice 2, question 1).
	UPROPERTY(BlueprintReadWrite, Category = "MindKore|Impact")
	TWeakObjectPtr<AActor> SourceActor;

	UPROPERTY(BlueprintReadWrite, Category = "MindKore|Impact")
	int32 InteractionIndex = 0;

	UPROPERTY(BlueprintReadWrite, Category = "MindKore|Impact")
	double ImpactTimeSeconds = 0.0;

	// Authored mechanical impulse, independent of health damage. SI N s;
	// the physics boundary converts to kg cm/s exactly once.
	UPROPERTY(BlueprintReadWrite, Category = "MindKore|Impact")
	FVector ImpulseNewtonSeconds = FVector::ZeroVector;

	UPROPERTY(BlueprintReadWrite, Category = "MindKore|Impact")
	bool bHasPhysicsImpulse = false;

	// Writable so a PIE fixture can name the hit bone; the resolver remains the
	// only gameplay writer.
	UPROPERTY(BlueprintReadWrite, Category = "MindKore|Impact")
	FHitResult Hit;

	UPROPERTY(BlueprintReadOnly, Category = "MindKore|Impact")
	EMKSurfaceKind SurfaceKind = EMKSurfaceKind::Default;

	UPROPERTY(BlueprintReadOnly, Category = "MindKore|Impact")
	EMKHitZone HitZone = EMKHitZone::Default;

	UPROPERTY(BlueprintReadOnly, Category = "MindKore|Impact")
	EMKHitReaction HitReaction = EMKHitReaction::LightFlinch;

	UPROPERTY(BlueprintReadOnly, Category = "MindKore|Impact")
	float DamageMultiplier = 1.0f;

	UPROPERTY(BlueprintReadOnly, Category = "MindKore|Impact")
	float CameraKick = 0.0f;

	UPROPERTY(BlueprintReadOnly, Category = "MindKore|Impact")
	float DamageAmount = 0.0f;

	UPROPERTY(BlueprintReadOnly, Category = "MindKore|Impact")
	FVector ShotDirection = FVector::ForwardVector;

	// How far the round had flown when it landed, cm from the shooter. Negative
	// when nobody knows - a crowbar, a caller that never set it - and a receiver
	// then treats the hit as the ordinary one. What a close round adds to a
	// living body is the receiver's to decide (MKComputeRangeMass); damage does
	// not read this.
	UPROPERTY(BlueprintReadWrite, Category = "MindKore|Impact")
	float RangeCm = -1.0f;
};

// Trigger-accepted pulse (before any impact). Not an impact result — only direction.
USTRUCT(BlueprintType)
struct FMKShotFiredPulse
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "MindKore|Weapon")
	FVector ShotDirection = FVector::ForwardVector;
};

UCLASS(BlueprintType)
class MINDKORE_API UMKImpactDataAsset : public UDataAsset
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "MindKore|Impact")
	TMap<TEnumAsByte<EPhysicalSurface>, FMKImpactFeedback> SurfaceFeedback;

	// Per-zone rules. Numeric fields (surface, reaction, damage, kick) replace the
	// built-in fallback for that zone. Soft refs (sound/VFX/decal) merge: zone
	// assets override when set, otherwise the surface/fallback assets remain.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "MindKore|Impact")
	TMap<EMKHitZone, FMKImpactFeedback> ZoneFeedback;

	// What a round sounds like in a body, by where it landed: the head is bone,
	// the chest is wet, a limb is dry, a plate is steel over a man. Keyed by the
	// region MKBodyRegion names - the zone box, or the bone when the round landed
	// on the mesh - so the box and the body under it sound alike. Replaces the
	// surface's sound when set; a region with no entry keeps it. Sound only: the
	// numbers above are not touched, which is why this is not ZoneFeedback (an
	// entry there replaces the zone's damage and reaction as well).
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "MindKore|Impact")
	TMap<EMKHitZone, TObjectPtr<USoundBase>> BodyImpactSound;

	UFUNCTION(BlueprintPure, Category = "MindKore|Impact")
	FMKImpactFeedback GetFeedbackForSurface(EPhysicalSurface SurfaceType) const;

	UFUNCTION(BlueprintPure, Category = "MindKore|Impact")
	bool TryGetZoneFeedback(EMKHitZone HitZone, FMKImpactFeedback& OutFeedback) const;
};

MINDKORE_API EMKHitZone MKResolveHitZoneFromComponent(const UPrimitiveComponent* HitComponent);
MINDKORE_API int32 MKGetHitZonePriority(EMKHitZone HitZone);

// Single source of truth for the physical-surface -> project-surface mapping.
// Returns Default for unmapped surfaces, which callers read as "no opinion"
// rather than as a concrete material.
MINDKORE_API EMKSurfaceKind MKSurfaceKindFromPhysical(EPhysicalSurface SurfaceType);

// Does a hit on this surface end in flesh? Worn armour is always penetrated
// (DESIGN_ENEMIES section 9.5), so a plate hit shows the wound behind the plate:
// same blood, same direction along the round, same mark on the body. Only
// presentation asks this - damage, hit reaction and the physical response keep
// reading SurfaceKind itself, where armour is still armour.
MINDKORE_API bool MKIsFleshWound(EMKSurfaceKind SurfaceKind);

// Where on a body a hit landed, for presentation that differs by anatomy (the
// sound of the round). A plate is Armor wherever it is. Otherwise the zone box
// names it; a hit on the mesh carries no zone, and the bone names it instead.
// Default when the surface is not a body at all.
// ponytail: the bone is read by its name (head / neck, an _l or _r limb bone),
// which holds for the mannequin naming every body here uses; a rig named
// otherwise reads as Torso - give it a table then.
MINDKORE_API EMKHitZone MKBodyRegion(EMKHitZone HitZone, FName Bone, EMKSurfaceKind SurfaceKind);

/**
 * What bounds a round that keeps going after it has hit something.
 *
 * Both numbers are scale, not taste. The advance is smaller than the thinnest
 * thing the round can cross: GX0 measured the magazine pouch's two faces 0.36 cm
 * apart, so a cursor that jumped further than that could step over a layer and
 * the round would pass through cloth that never opened. The interaction limit is
 * what a revolver round can plausibly cross - gear, body, gear - and it exists so
 * that a degenerate case ends rather than iterating. Today it also ends every
 * walk into a body: the walk re-finds the body it is inside at each step, so the
 * three contacts are one crossing, not three layers (see
 * UMKFirearmComponent::ResolveApplyAndFeedback).
 */
USTRUCT(BlueprintType)
struct FMKPassThroughPolicy
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MindKore|Impact", meta = (ClampMin = "1"))
	int32 MaxInteractions = 3;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MindKore|Impact", meta = (ClampMin = "0.01"))
	float AdvanceEpsilonCm = 0.2f;
};

/**
 * Does the round carry on past this contact?
 *
 * Flesh and worn armour do not stop it: armour is always penetrated
 * (DESIGN_ENEMIES 9.5) and the whole point of a through-channel is that the
 * round leaves. Everything else - walls, floors, the world - stops it, which is
 * why this asks about the surface rather than about the actor.
 *
 * InteractionIndex is the count of contacts this round has already resolved, so
 * the limit is on the round's life and not on any one body.
 */
MINDKORE_API bool MKContinueAfterImpact(
	EMKSurfaceKind SurfaceKind, int32 InteractionIndex, const FMKPassThroughPolicy& Policy);

// One trace hit reduced to what the selection rule needs. The hit actor is
// carried as an opaque key because the rule only ever compares identity —
// that keeps MKSelectResolvedHit a pure function unit tests can drive without
// a UWorld or spawned actors.
struct FMKHitCandidate
{
	float Distance = 0.0f;
	bool bBlocking = false;
	const void* ActorKey = nullptr;
	EMKHitZone HitZone = EMKHitZone::Default;
};

// Picks which trace hit a shot resolves against; returns INDEX_NONE if the
// trace found no blocking hit.
//
// A multi-trace returns non-blocking "touch" hits alongside the blocking one,
// so the nearest element of the array is not necessarily the hit that stopped
// the shot — the blocking hit has to be selected explicitly.
//
// Hits on the SAME actor within SameActorToleranceCm of the blocking hit then
// compete on MKGetHitZonePriority, so a shot grazing both an arm box and the
// head box registers as a headshot.
//
// The priority pass is live: hit-zone boxes Overlap the Shot channel and the
// character mesh (MKCharacterBody profile) blocks it - see the
// UMKHitZoneComponent constructor - so a multi-trace reaches the mesh and
// collects the zone boxes on the way. Chaos then drops every touch at or beyond
// the blocking distance (FinishQueryHelper in Chaos/Interface/SQTypes.h), so
// only zones strictly IN FRONT of the body ever reach this rule.
// Consequence on the humanoid rig (MKHumanoidHitZoneSpecs): ArmorPlate sits
// 14 cm in front of the spine over the same bones as UpperTorso, so a
// centre-chest shot arrives with the plate as a nearer touch and resolves as
// Armor rather than Torso; where the plate's face coincides with the blocking
// face the touch is pruned instead, and the same shot reads as Torso. Armour no
// longer attenuates damage (DESIGN_ENEMIES section 9.5), so that choice changes
// the reaction and the surface, not the amount.
MINDKORE_API int32 MKSelectResolvedHit(TConstArrayView<FMKHitCandidate> Candidates, float SameActorToleranceCm);

/** What one accepted impact does to a body with health. See MKResolveImpactOutcome. */
struct FMKImpactOutcome
{
	/** The wound channel records it. Rounds only. */
	bool bRegistersWound = false;
	/** A round found the head while the anatomical rule is on, lethal or not. */
	bool bHeadHit = false;
	/** This head round is the one that kills. Rounds only. */
	bool bHeadshot = false;
	/** The body is (or stays) dead after this impact. */
	bool bFatal = false;
	/** Health after the impact; 0 when fatal. */
	float HealthAfter = 0.0f;
	/** Head rounds a living body has taken, this one included. */
	int32 HeadshotsAfter = 0;
};

/**
 * The receiver-side rule for one impact on a body with health, pure so it can be
 * tested without an enemy.
 *
 * Round: the wound channel records every hit. With bHeadshotIsLethal a head
 * round is counted, and the one that brings the count to HeadshotsToKill kills
 * whatever the health says. Earlier head rounds subtract their damage like any
 * other round, so one of them still kills a body whose health it exhausts, and
 * that death is a headshot too (the owner's choice, 2026-10-01: a body already
 * wounded can die of its first head round, with no reaction). HeadshotsToKill
 * 1 is the old rule: the first head round kills.
 * Blunt: no wound, no headshot, and never fatal to a living body - health is
 * reduced but not below 1 (or below what it already was). HYPOTHESIS pending
 * the author (crowbar slice 2, question 2): whether a crowbar can kill at all.
 * A corpse stays a corpse for either kind, and its count does not grow.
 */
MINDKORE_API FMKImpactOutcome MKResolveImpactOutcome(
	EMKImpactKind Kind, EMKHitZone HitZone, bool bHeadshotIsLethal, bool bWasDead,
	float Health, float DamageAmount, int32 HeadshotsTaken = 0, int32 HeadshotsToKill = 1);

// Pure flinch-distance multiplier for a reaction. Returns 0 for None so callers
// can early-out without a second switch.
MINDKORE_API float MKFlinchScaleForReaction(
	EMKHitReaction Reaction,
	float LightScale,
	float HeavyScale,
	float ArmorScale,
	float DeathScale);
