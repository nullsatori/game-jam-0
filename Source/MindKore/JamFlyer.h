#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "JamFlyerAudio.h"
#include "MKImpactReceiver.h"
#include "JamFlyer.generated.h"

class AJamFlyer;
class USceneComponent;
class UPhysicalMaterial;
class UAudioComponent;
class USoundMix;
class USoundSubmixBase;
class USoundBase;
class USphereComponent;
class UStaticMeshComponent;

UENUM(BlueprintType)
enum class EJamFlyerState : uint8
{
	Wander,
	/** Hunting: wheel above the player (tightening), then dive with a token or feint. */
	Circle,
	Dive,
	/** Hunting: pull away after a bite or a miss, then circle again. */
	Retreat,
	Dead,
	/** Clinging to a roost spot, head out, wings folded; idle twitches. */
	Roost,
	/** Flying back to a free roost spot, then the grab. */
	Land,
	/** The scanner: hovers at face height a few metres off, darts in, stops, chirrs, backs off. Never bites. */
	Curious,
	/** Shadow passes overhead, from above and behind to ahead. Never bites. */
	Stalk,
	/** A fake dive that passes the player a few metres off (the fly-by whoosh), then circles again. */
	Feint,
	/** Knocked out of its flight by a round: tumbles, wings flailing, the shove carrying it. */
	Stagger,
	/** Hurt badly: flies off, then comes back. */
	Flee
};

/** A place on a roost surface (AJamFlyerHabitat::RoostOn) where one flyer clings, head out. */
struct FJamRoostSpot
{
	/** On the surface. */
	FVector Point = FVector::ZeroVector;
	/** The surface's outward normal there. */
	FVector Normal = FVector::UpVector;
	/** A tangent: which way the clinging flyer's back faces (a random roll about the normal: no grid). */
	FVector Side = FVector::ForwardVector;
	TWeakObjectPtr<AJamFlyer> Holder;
};

/** How far a flock that has noticed the player has gone (Docs/Flyers.md, "the ladder"). */
UENUM(BlueprintType)
enum class EJamFlockMood : uint8
{
	/** Scanner-like: hover at face height, dart in, stop, chirr, back off. Never bites. */
	Curious,
	/** Shadow passes overhead, above and behind. Never bites. */
	Stalking,
	/** Circling, dive tokens, feints, bites. */
	Hunting
};

/**
 * Where a flock lives: a sphere (or a shell around a planetoid, InnerRadiusCm > 0).
 * On BeginPlay it spawns FlockSize AJamFlyer inside. The flyers ask it for their
 * geometry (Contains, UpAt, PickPoint), wander only inside it, notice the player
 * only near it (AggroMarginCm) and give up the chase when the player is LeashCm
 * beyond it. It coordinates the flock: the mood ladder (Provoke, calming down),
 * the dive tokens and feints, the wake-up cascade, the shots it hears and, with
 * RoostOn, the roost: spots sampled on those actors' collision where most of the
 * flock clings while a few patrol. Docs/Flyers.md.
 */
UCLASS()
class MINDKORE_API AJamFlyerHabitat : public AActor
{
	GENERATED_BODY()

public:
	AJamFlyerHabitat();

	/** The outer radius is this sphere's (editor visualisation; no collision). */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Jam|Flyer")
	TObjectPtr<USphereComponent> Zone = nullptr;

	/** Wander only farther than this from the centre: a planetoid's surface plus clearance. 0 = the whole ball. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Jam|Flyer", meta = (ClampMin = "0.0"))
	float InnerRadiusCm = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Jam|Flyer", meta = (ClampMin = "0"))
	int32 FlockSize = 3;

	/** A hunt ends when the player is this far beyond the outer radius, cm. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Jam|Flyer", meta = (ClampMin = "0.0"))
	float LeashCm = 2000.0f;

	/** A flyer notices the player on sight only while the player is within the outer radius plus this, cm. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Jam|Flyer", meta = (ClampMin = "0.0"))
	float AggroMarginCm = 500.0f;

	/** The wake-up cascade passes from a woken flyer to the nearest one asleep within this, cm. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Jam|Flyer", meta = (ClampMin = "0.0"))
	float AlertRadiusCm = 2500.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Jam|Flyer")
	TSubclassOf<AJamFlyer> FlyerClass;

	// --- the flock -------------------------------------------------------------------------

	/** Flyers diving at once (dive tokens); the others circle, tighten and feint. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Jam|Flyer|Flock", meta = (ClampMin = "1"))
	int32 MaxDivers = 2;

	/** Feint passes at once. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Jam|Flyer|Flock", meta = (ClampMin = "0"))
	int32 MaxFeints = 2;

	/** At least this between two dives starting, s: dives come one after another, not in a bunch. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Jam|Flyer|Flock", meta = (ClampMin = "0.0"))
	float DiveGapSeconds = 0.8f;

	/** Each step of the wake-up cascade (one screech, plus its roost row) after the last, s, random in the range. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Jam|Flyer|Flock")
	FVector2D WakeStepSeconds = FVector2D(0.2f, 0.6f);

	/** A shot this far beyond the outer radius is heard, cm (AJamFlyerHabitat::ReportShot). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Jam|Flyer|Flock", meta = (ClampMin = "0.0"))
	float HearingRangeCm = 5000.0f;

	/** With no provocation for this long the mood steps down a rung (hunting -> stalking -> curious), s. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Jam|Flyer|Flock", meta = (ClampMin = "1.0"))
	float CalmDownSeconds = 12.0f;

	/** A scanner dart (one flyer at a time in the flock) at most this often, s, random in the range. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Jam|Flyer|Flock")
	FVector2D DartEverySeconds = FVector2D(4.0f, 9.0f);

	/**
	 * While the player has no gun unlocked, the mood stops at Stalking (nothing dives or bites) and the
	 * roost wakes only from a touch. The first gun makes the usual rules apply; its first shot near the
	 * roost wakes it in the cascade (the payoff of the first EVA).
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Jam|Flyer|Flock")
	bool bSpareUnarmed = false;

	// --- the roost -------------------------------------------------------------------------

	/** Actors whose collision the flock clings to (a hull, a wreck, big rocks). Empty: no roost. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Jam|Flyer|Roost")
	TArray<TObjectPtr<AActor>> RoostOn;

	/** Share of the flock clinging; the rest patrol close (sentries). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Jam|Flyer|Roost", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float RoostShare = 0.75f;

	/** Spots at least this far apart (x0.85-1.3 per spot), cm. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Jam|Flyer|Roost", meta = (ClampMin = "50.0"))
	float RoostSpacingCm = 230.0f;

	/** The roost is densest at RoostFocus and thins with this falloff, cm; 0 = even. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Jam|Flyer|Roost", meta = (ClampMin = "0.0"))
	float RoostFocusFalloffCm = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Jam|Flyer|Roost")
	FVector RoostFocus = FVector::ZeroVector;

	/** The roost's spots and who clings where come from this seed: the same picture every play (0: new each play). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Jam|Flyer|Roost")
	int32 RoostSeed = 0;

	/** No spot within this of a door (the airlock stays passable), cm. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Jam|Flyer|Roost", meta = (ClampMin = "0.0"))
	float RoostClearOfDoorsCm = 400.0f;

	/** An armed player this close to a clinging flyer wakes the roost (the cascade), cm. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Jam|Flyer|Roost", meta = (ClampMin = "0.0"))
	float RoostWakeRadiusCm = 1000.0f;

	/** Closer than this the player bumps it: it peels off with a warning screech and a feint, armed or not, cm. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Jam|Flyer|Roost", meta = (ClampMin = "0.0"))
	float RoostTouchCm = 220.0f;

	/** A cascade step also unfolds the clinging ones within this of the woken one, silently (the row), cm. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Jam|Flyer|Roost", meta = (ClampMin = "0.0"))
	float RoostRowCm = 350.0f;

	/** A sentry lands and a clinging one peels off this often, s, random in the range. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Jam|Flyer|Roost")
	FVector2D RoostSwapSeconds = FVector2D(10.0f, 30.0f);

	/** Sentries' lazy loops run this far off the roost surface, cm, random in the range. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Jam|Flyer|Roost")
	FVector2D SentryStandoffCm = FVector2D(300.0f, 900.0f);

	// --- geometry (the flyers ask) -----------------------------------------------------------

	float GetOuterRadius() const;
	/** InnerRadiusCm, at most 90 % of the outer radius. */
	float GetInnerRadius() const;
	/** Inside the outer radius plus ExtraCm. */
	bool Contains(const FVector& Location, float ExtraCm) const;
	/** "Up" for banking and circling: away from the centre around a planetoid (an inner radius), world up in a plain ball. */
	FVector UpAt(const FVector& Location) const;
	/**
	 * A wander target ReachCm ahead (roughly along Forward) in the shell, ClearanceCm off its walls; a
	 * swoop goes low. A roost's sentries loop lazily along its surface (SentryStandoffCm off it).
	 */
	FVector PickPoint(const FVector& From, const FVector& Forward, bool bSwoop, float ReachCm, float ClearanceCm) const;
	/** Location moved into the shell (a noise outside is investigated at the near edge). */
	FVector ClampInside(const FVector& Location) const;

	// --- the flock --------------------------------------------------------------------------

	const TArray<TWeakObjectPtr<AJamFlyer>>& GetFlock() const { return Flock; }
	int32 CountIn(EJamFlyerState State) const;
	EJamFlockMood GetMood() const { return Mood; }
	/** No gun unlocked and bSpareUnarmed: the ladder stops at Stalking. */
	bool SparesPlayer() const;
	/** Raise the mood to Level (Spares: at most Stalking); Hunting starts the cascade from By or the flyer nearest Where. */
	void Provoke(EJamFlockMood Level, const FString& Why, const FVector& Where, AJamFlyer* By = nullptr);
	/** A dive may start: the mood hunts, under MaxDivers diving, DiveGapSeconds after the last. Takes the token. */
	bool TakeDiveToken();
	bool MayFeint() const;
	/** A scanner dart may start now (one at a time; DartEverySeconds). */
	bool TakeDart();
	/** A flyer screeched into the hunt: the cascade passes on from it, one step per WakeStepSeconds. */
	void Alert(AJamFlyer* Caller);
	/** Listeners (flock members in the noise's air) heard What at Where: the wandering ones investigate, the mood rises a rung (a roost: Hunting). */
	void HearNoise(const FVector& Where, const TArray<AJamFlyer*>& Listeners, const FString& What);
	/**
	 * A noise (a shot at the muzzle, a round landing) and its medium. Air: heard by the flyers inside the
	 * same breathable AJamGravityZone. Contact: Struck is a roost surface (or attached to one) - its roost
	 * feels it through the hull and wakes. Vacuum: nobody hears it. Called by UJamWeaponsComponent.
	 */
	static void ReportNoise(const UObject* WorldContext, const FVector& Where, const AActor* Struck, const TCHAR* What);
	/** Struck is one of RoostOn or attached to one. */
	bool IsRoostSurface(const AActor* Struck) const;

	/**
	 * Flesh is their nervous system (AJamLivingWall::PainCall): hunt at once, gather over Where for
	 * DelaySeconds (they come to the wound; the player is on the way back by then), then go for the player.
	 */
	void AnswerPainCall(const FVector& Where, float DelaySeconds, const FString& From);
	/** A pain call gathers the flock this far out from the wound, away from the habitat's centre (out of a wreck's rooms), cm. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Jam|Flyer|Flock", meta = (ClampMin = "0.0"))
	float RallyOutCm = 1200.0f;

	/** While game time is under RallyUntil the hunt circles RallyPoint instead of the player and does not dive. */
	FVector RallyPoint = FVector::ZeroVector;
	double RallyUntil = -1.0;

	// --- the roost --------------------------------------------------------------------------

	bool IsRoost() const { return Spots.Num() > 0; }
	int32 GetSpotCount() const { return Spots.Num(); }
	const FJamRoostSpot* GetSpot(int32 Index) const { return Spots.IsValidIndex(Index) ? &Spots[Index] : nullptr; }
	/** A free spot near Near (one of the 3 nearest), now Flyer's; INDEX_NONE when none. */
	int32 ClaimSpot(AJamFlyer* Flyer, const FVector& Near);
	void ReleaseSpot(const AJamFlyer* Flyer, int32 Index);
	/** Fewer cling than RoostShare wants: a calm flyer goes back to the roost. */
	bool WantsMoreRoosting() const;

	/** The first and the last wake of the latest cascade (game seconds), and how many it woke (the probes read them). */
	double CascadeStartedSeconds = -1.0;
	double CascadeLastSeconds = -1.0;
	int32 CascadeWoken = 0;

protected:
	virtual void BeginPlay() override;

private:
	void SampleSpots();
	void CascadeStep();
	/** Every 0.5 s: calm the mood down, the roost swap. */
	void Pulse();

	TArray<TWeakObjectPtr<AJamFlyer>> Flock;
	TArray<FJamRoostSpot> Spots;
	TArray<TWeakObjectPtr<AJamFlyer>> Callers;
	/** Woken by the current cascade: each at most once, or one that cannot hunt (the player past its leash) is re-woken every step with a screech. */
	TSet<TWeakObjectPtr<AJamFlyer>> CascadeDone;
	FTimerHandle CascadeTimer;
	FTimerHandle PulseTimer;
	EJamFlockMood Mood = EJamFlockMood::Curious;
	double LastProvokedSeconds = -100.0;
	double NextDiveSeconds = 0.0;
	double NextDartSeconds = 0.0;
	double NextSwapSeconds = 0.0;
};

/**
 * A flying predator after Morrowind's cliff racer (model: Scripts/blender/jam_flyer.py).
 *
 * Body is a simulated sphere (gravity off, rotation locked while alive); flight
 * is acceleration toward a desired velocity (no navmesh): wander targets from the
 * habitat, a sphere sweep ahead to steer around rock and hull, separation from
 * the flock. The look (Visual) turns to the velocity and banks into turns; the
 * wings flap and the tail swings by turning their mesh parts.
 *
 * Sight: range, a cone around the nose, a Visibility line to the player. On
 * sight near the habitat it climbs the flock's mood ladder (Docs/Flyers.md):
 * curious (a scanner: hovers at face height, darts in, chirrs, backs off),
 * stalking (shadow passes overhead), hunting (wheels above the player, dives
 * with one of the flock's dive tokens or feints, bites - damage and a shove
 * through the impact system - pulls away). A round staggers it (a tumble); low
 * on health it flees and comes back. Shots are heard (AJamFlyerHabitat::ReportShot).
 * In a roost habitat most of the flock clings to a hull, head out, and wakes in a
 * cascade. At zero health it is a tumbling corpse that drifts and can be pushed,
 * grabbed and shot further.
 */
UCLASS()
class MINDKORE_API AJamFlyer : public AActor, public IMKImpactReceiver
{
	GENERATED_BODY()

public:
	AJamFlyer();

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Jam|Flyer")
	TObjectPtr<USphereComponent> Body = nullptr;

	/** Turned to the flight direction; the meshes hang under it. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Jam|Flyer")
	TObjectPtr<USceneComponent> Visual = nullptr;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Jam|Flyer")
	TObjectPtr<UStaticMeshComponent> Torso = nullptr;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Jam|Flyer")
	TObjectPtr<UStaticMeshComponent> WingL = nullptr;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Jam|Flyer")
	TObjectPtr<UStaticMeshComponent> WingR = nullptr;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Jam|Flyer")
	TObjectPtr<UStaticMeshComponent> Tail = nullptr;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Jam|Flyer")
	TObjectPtr<UStaticMeshComponent> TailTip = nullptr;

	// --- flight ---------------------------------------------------------------

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Jam|Flyer|Flight", meta = (ClampMin = "1.0"))
	float MassKg = 20.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Jam|Flyer|Flight", meta = (ClampMin = "0.0"))
	float CruiseSpeedCm = 700.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Jam|Flyer|Flight", meta = (ClampMin = "0.0"))
	float CircleSpeedCm = 1000.0f;

	/** The pawn flies at most 1200 and thrusts to 500: the dive outruns it. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Jam|Flyer|Flight", meta = (ClampMin = "0.0"))
	float DiveSpeedCm = 1800.0f;

	/** Steering acceleration cap while wandering, cm/s^2: wide wheeling turns. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Jam|Flyer|Flight", meta = (ClampMin = "0.0"))
	float WanderAccelCm = 900.0f;

	/** While hunting, cm/s^2. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Jam|Flyer|Flight", meta = (ClampMin = "0.0"))
	float HuntAccelCm = 2800.0f;

	/** The dive steers harder than the hunt by this. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Jam|Flyer|Flight", meta = (ClampMin = "0.0"))
	float DiveAccelScale = 1.3f;

	/** The velocity reaches the desired one in about this, s (capped by the acceleration). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Jam|Flyer|Flight", meta = (ClampMin = "0.01"))
	float SteerSeconds = 0.35f;

	/** A new wander target this close, cm (or after WanderRetargetSeconds). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Jam|Flyer|Flight", meta = (ClampMin = "0.0"))
	float WanderArriveCm = 400.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Jam|Flyer|Flight", meta = (ClampMin = "0.1"))
	float WanderRetargetSeconds = 7.0f;

	/** The next wander target this far ahead, cm, random in the range. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Jam|Flyer|Flight")
	FVector2D WanderReachCm = FVector2D(1500.0f, 3000.0f);

	/** Wander targets keep this far from the shell's walls, cm. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Jam|Flyer|Flight", meta = (ClampMin = "0.0"))
	float ShellClearanceCm = 300.0f;

	/** Share of wander legs that are swoops: low, close to the inner surface, fast. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Jam|Flyer|Flight", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float SwoopChance = 0.3f;

	/** A swoop at this times the cruise; other legs random in WanderSpeedScale. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Jam|Flyer|Flight", meta = (ClampMin = "0.0"))
	float SwoopSpeedScale = 1.5f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Jam|Flyer|Flight")
	FVector2D WanderSpeedScale = FVector2D(0.8f, 1.15f);

	/** A roost's sentries patrol at this times their wander speed: lazy loops. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Jam|Flyer|Flight", meta = (ClampMin = "0.0"))
	float SentrySpeedScale = 0.65f;

	/** Wandering turns at most this far off the flight at a time, degrees: a target behind is reached in an arc. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Jam|Flyer|Flight", meta = (ClampMin = "1.0", ClampMax = "180.0"))
	float MaxWanderTurnDeg = 60.0f;

	/** Wandering bobs this much along the up, cm/s. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Jam|Flyer|Flight", meta = (ClampMin = "0.0"))
	float WanderBobCm = 150.0f;

	/** Look ahead along the velocity for obstacles, seconds of flight. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Jam|Flyer|Flight", meta = (ClampMin = "0.0"))
	float AvoidSeconds = 0.9f;

	/** The obstacle sweep: a sphere this big, cm, AvoidExtraCm past the AvoidSeconds of flight. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Jam|Flyer|Flight", meta = (ClampMin = "0.0"))
	float AvoidRadiusCm = 60.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Jam|Flyer|Flight", meta = (ClampMin = "0.0"))
	float AvoidExtraCm = 150.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Jam|Flyer|Flight", meta = (ClampMin = "0.0"))
	float SeparationCm = 500.0f;

	/** The push away from a flockmate at zero distance, cm/s. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Jam|Flyer|Flight", meta = (ClampMin = "0.0"))
	float SeparationPushCm = 600.0f;

	// --- senses and the hunt --------------------------------------------------

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Jam|Flyer|Hunt", meta = (ClampMin = "0.0"))
	float SightRangeCm = 4500.0f;

	/** Half-angle of the view cone around the nose for noticing the player, degrees. Tracking needs no cone. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Jam|Flyer|Hunt", meta = (ClampMin = "0.0", ClampMax = "180.0"))
	float SightHalfAngleDeg = 70.0f;

	/** Sight checks this often, s. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Jam|Flyer|Hunt", meta = (ClampMin = "0.01"))
	float SenseSeconds = 0.1f;

	/** Tracking ends after this long without a line to the player, seconds. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Jam|Flyer|Hunt", meta = (ClampMin = "0.0"))
	float LoseSightSeconds = 4.0f;

	/** After tracking ends it does not notice the player on sight for this long, seconds (shots still do). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Jam|Flyer|Hunt", meta = (ClampMin = "0.0"))
	float CalmSeconds = 3.0f;

	/** A drawn gun held on it this long provokes the flock (a rung up the ladder), s. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Jam|Flyer|Hunt", meta = (ClampMin = "0.0"))
	float AimProvokeSeconds = 1.0f;

	/** "Held on it": the player's view within this of it, degrees. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Jam|Flyer|Hunt", meta = (ClampMin = "0.0"))
	float AimConeDeg = 5.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Jam|Flyer|Hunt", meta = (ClampMin = "0.0"))
	float CircleRadiusCm = 900.0f;

	/** Circling tightens to this over CircleTightenSeconds: the noose, cm. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Jam|Flyer|Hunt", meta = (ClampMin = "0.0"))
	float CircleTightRadiusCm = 600.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Jam|Flyer|Hunt", meta = (ClampMin = "0.1"))
	float CircleTightenSeconds = 4.0f;

	/** Each flyer's circle is this much wider or narrower (its own, from its seed), fraction. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Jam|Flyer|Hunt", meta = (ClampMin = "0.0", ClampMax = "0.9"))
	float CircleRadiusJitter = 0.15f;

	/** The circle's centre sits this far over the player along the habitat's up, cm. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Jam|Flyer|Hunt")
	float CircleHeightCm = 500.0f;

	/** How hard circling pulls back to its radius and height, 1/s. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Jam|Flyer|Hunt", meta = (ClampMin = "0.0"))
	float CircleGain = 1.5f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Jam|Flyer|Hunt", meta = (ClampMin = "0.0"))
	float CircleBobCm = 200.0f;

	/** Seconds of wheeling before a dive or a feint, random in the range. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Jam|Flyer|Hunt")
	FVector2D CircleSeconds = FVector2D(1.5f, 3.5f);

	/** No token and no feint: wheel this much longer, s, random in the range. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Jam|Flyer|Hunt")
	FVector2D CircleAgainSeconds = FVector2D(0.5f, 1.2f);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Jam|Flyer|Hunt", meta = (ClampMin = "0.0"))
	float MaxDiveSeconds = 3.0f;

	/** The dive leads the player at most this far ahead, s. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Jam|Flyer|Hunt", meta = (ClampMin = "0.0"))
	float DiveLeadSeconds = 0.8f;

	/** A dive or a feint that came this close and flies away has passed, cm. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Jam|Flyer|Hunt", meta = (ClampMin = "0.0"))
	float OvershootCm = 600.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Jam|Flyer|Hunt", meta = (ClampMin = "0.0"))
	float RetreatSeconds = 1.2f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Jam|Flyer|Hunt", meta = (ClampMin = "0.0"))
	float RetreatSpeedScale = 1.3f;

	/** The retreat climbs along the up this much per unit away from the player. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Jam|Flyer|Hunt", meta = (ClampMin = "0.0"))
	float RetreatClimb = 0.8f;

	/** No token: a feint instead with this chance (the rest keep wheeling). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Jam|Flyer|Hunt", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float FeintChance = 0.6f;

	/** A feint passes the player this far to a side, cm (the fly-by whoosh is heard inside FlyByDistanceCm). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Jam|Flyer|Hunt", meta = (ClampMin = "0.0"))
	float FeintMissCm = 350.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Jam|Flyer|Hunt", meta = (ClampMin = "0.0"))
	float FeintSpeedCm = 1500.0f;

	/** A bite lands when the player's centre is this close to the flyer's, cm (the beak reaches 1.5 m forward). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Jam|Flyer|Hunt", meta = (ClampMin = "0.0"))
	float BiteReachCm = 190.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Jam|Flyer|Hunt", meta = (ClampMin = "0.0"))
	float BiteDamage = 8.0f;

	/** The shove of a bite on the pawn, kg cm/s along the dive: 45000 on 100 kg is 4.5 m/s. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Jam|Flyer|Hunt", meta = (ClampMin = "0.0"))
	float BiteImpulse = 45000.0f;

	/** After a bite it bounces back this fast, cm/s, and climbs along the up at BiteBounceUpCm. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Jam|Flyer|Hunt", meta = (ClampMin = "0.0"))
	float BiteBounceBackCm = 300.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Jam|Flyer|Hunt", meta = (ClampMin = "0.0"))
	float BiteBounceUpCm = 500.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Jam|Flyer|Hunt", meta = (ClampMin = "1.0"))
	float MaxHealth = 60.0f;

	/** A corpse in a gravity zone (the planetoid) drags so it comes to rest instead of rolling; in zero-G it keeps drifting. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Jam|Flyer|Hunt", meta = (ClampMin = "0.0"))
	float CorpseLinearDamping = 0.4f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Jam|Flyer|Hunt", meta = (ClampMin = "0.0"))
	float CorpseAngularDamping = 6.0f;

	// --- the ladder: stalking and the scanner -------------------------------------------------

	/** The stalk's first leg ends this far above and behind the player's view, cm. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Jam|Flyer|Ladder", meta = (ClampMin = "0.0"))
	float StalkHeightCm = 600.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Jam|Flyer|Ladder", meta = (ClampMin = "0.0"))
	float StalkBehindCm = 800.0f;

	/** The pass over the head, cm/s. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Jam|Flyer|Ladder", meta = (ClampMin = "0.0"))
	float StalkPassSpeedCm = 1300.0f;

	/** Each stalk leg at most, s. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Jam|Flyer|Ladder", meta = (ClampMin = "0.1"))
	float StalkLegSeconds = 3.0f;

	/** Curious: it hovers this far from the player's eye, cm (its own, from its seed). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Jam|Flyer|Ladder")
	FVector2D CuriousDistanceCm = FVector2D(350.0f, 650.0f);

	/** ... this far off the view's line to a side, degrees (it drifts into view when the player turns). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Jam|Flyer|Ladder")
	FVector2D CuriousAngleDeg = FVector2D(25.0f, 60.0f);

	/** The dart stops this far in front of the player's face, cm. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Jam|Flyer|Ladder", meta = (ClampMin = "0.0"))
	float DartStopCm = 160.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Jam|Flyer|Ladder", meta = (ClampMin = "0.0"))
	float DartSpeedCm = 1400.0f;

	/** Holds dead still in front of the face ("the photo"), s. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Jam|Flyer|Ladder", meta = (ClampMin = "0.0"))
	float DartHoldSeconds = 0.6f;

	/** A flyer this curious (0-1, random per spawn) darts mid-fight too, between the token dives. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Jam|Flyer|Ladder", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float CuriousDartThreshold = 0.7f;

	// --- hits: the stagger and the flight -------------------------------------------------------

	/** A non-lethal hit tumbles it this long, s, random in the range. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Jam|Flyer|Hit")
	FVector2D StaggerSeconds = FVector2D(0.3f, 0.5f);

	/** The hit knocks it along the round this fast, cm/s (on top of the gun's own push), keeping StaggerKeepSpeed of its flight. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Jam|Flyer|Hit", meta = (ClampMin = "0.0"))
	float StaggerKnockCm = 500.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Jam|Flyer|Hit", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float StaggerKeepSpeed = 0.5f;

	/** The tumble's roll rate, degrees per second. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Jam|Flyer|Hit", meta = (ClampMin = "0.0"))
	float StaggerSpinDeg = 720.0f;

	/** At or under this share of its health a stagger ends in flight. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Jam|Flyer|Hit", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float FleeHealthFraction = 0.35f;

	/** Flees this long before it comes back, s. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Jam|Flyer|Hit", meta = (ClampMin = "0.0"))
	float FleeSeconds = 4.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Jam|Flyer|Hit", meta = (ClampMin = "0.0"))
	float FleeSpeedScale = 1.4f;

	// --- hearing ---------------------------------------------------------------------------------

	/** A heard shot draws a wandering flyer to it this long, s. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Jam|Flyer|Hearing", meta = (ClampMin = "0.0"))
	float InvestigateSeconds = 8.0f;

	/** Investigating flyers spread this far around the noise, cm. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Jam|Flyer|Hearing", meta = (ClampMin = "0.0"))
	float InvestigateSpreadCm = 600.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Jam|Flyer|Hearing", meta = (ClampMin = "0.0"))
	float InvestigateSpeedScale = 1.3f;

	// --- the roost -------------------------------------------------------------------------------

	/** Clinging, the body's centre sits this far off the surface along the normal, cm. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Jam|Flyer|Roost", meta = (ClampMin = "0.0"))
	float RoostStandoffCm = 80.0f;

	/** Clinging, the spike leans this far off the normal, degrees, random in the range. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Jam|Flyer|Roost")
	FVector2D RoostTiltDeg = FVector2D(4.0f, 14.0f);

	/** The folded wings: swept back along the body, a little down. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Jam|Flyer|Roost")
	float RoostWingSweepDeg = 80.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Jam|Flyer|Roost")
	float RoostWingFlapDeg = -10.0f;

	/** The tail bends this far to lie along the surface, degrees; the tip follows at half. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Jam|Flyer|Roost")
	float RoostTailCurlDeg = 75.0f;

	/** A clinging flyer far from the player thinks this often, s (twitches and a near player tick every frame). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Jam|Flyer|Roost", meta = (ClampMin = "0.0"))
	float RoostTickSeconds = 0.25f;

	/** An idle twitch (wing shiver, head turn, slow breath) this often, s, random in the range. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Jam|Flyer|Roost")
	FVector2D RoostTwitchSeconds = FVector2D(2.5f, 9.0f);

	/** A clinging flyer turns its head to a player within this, cm (the hull watches). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Jam|Flyer|Roost", meta = (ClampMin = "0.0"))
	float RoostStareCm = 1500.0f;

	/** ... at most this far off its spike, degrees. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Jam|Flyer|Roost", meta = (ClampMin = "0.0"))
	float RoostStareDeg = 25.0f;

	/** Landing gives up and picks another spot after this, s. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Jam|Flyer|Roost", meta = (ClampMin = "1.0"))
	float LandSeconds = 12.0f;

	// --- presentation ------------------------------------------------------------

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Jam|Flyer|Look", meta = (ClampMin = "0.0"))
	float FlapHz = 2.2f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Jam|Flyer|Look", meta = (ClampMin = "0.0"))
	float FlapAmplitudeDeg = 38.0f;

	// --- sound (Scripts/jam_flyer_sounds.py renders the stems; Docs/Flyers.md has the chain and the mix) ---

	/** The hunt's start: the falling call (the owner's Screech A); pushes AggroDuck. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Jam|Flyer|Sound")
	FJamSoundEvent AggroSound;

	/** The dive: the same call rising, Doppler while it comes. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Jam|Flyer|Sound")
	FJamSoundEvent DiveSound;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Jam|Flyer|Sound")
	FJamSoundEvent DeathSound;

	/** A stagger's cry: the death cry's stems, higher, cut after PainCrySeconds. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Jam|Flyer|Sound")
	FJamSoundEvent PainSound;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Jam|Flyer|Sound", meta = (ClampMin = "0.05"))
	float PainCrySeconds = 0.42f;

	/** The scanner's "photo": the beak clicks of the hit and the bite, pitched up, three in a row. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Jam|Flyer|Sound")
	FJamSoundEvent ChirrSound;

	/** One per downstroke (mid-beat): louder and brighter with the beat's size, higher with its rate; glides are silent. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Jam|Flyer|Sound")
	FJamSoundEvent FlapSound;

	/** A close pass (closest approach under FlyByDistanceCm at FlyByMinSpeedCm), Doppler. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Jam|Flyer|Sound")
	FJamSoundEvent FlyBySound;

	/** A round into it (alive or dead), at the hit; one per source and HitWindowSeconds. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Jam|Flyer|Sound")
	FJamSoundEvent HitSound;

	/**
	 * Hits from one source within this after the first are one hit sound, its volume from
	 * their summed damage: a shotgun's pellets land milliseconds apart, not in one frame
	 * (as AJamLivingWall::BlastWindowSeconds). Under the fastest gun's FireInterval (the
	 * pistol's 0.15 s). 0: every round its own sound.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Jam|Flyer|Sound", meta = (ClampMin = "0.0"))
	float HitWindowSeconds = 0.06f;

	/** The bite, at the player: the beak, the wet tear and MindKore's gristle crunch. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Jam|Flyer|Sound")
	FJamSoundEvent BiteSound;

	/** Cloth flutter between beats: a loop on the body, its volume with the flight speed. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Jam|Flyer|Sound")
	TObjectPtr<USoundBase> FlutterLoop = nullptr;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Jam|Flyer|Sound", meta = (ClampMin = "0.0"))
	float FlutterVolume = 0.6f;

	/** The roost's bed: the flutter of the nearest clinging ones at this, darker. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Jam|Flyer|Sound", meta = (ClampMin = "0.0"))
	float RoostFlutterVolume = 0.22f;

	/** A full beat's volume scale; under ~1/3 of FlapAmplitudeDeg none. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Jam|Flyer|Sound", meta = (ClampMin = "0.0"))
	float FlapVolume = 1.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Jam|Flyer|Sound", meta = (ClampMin = "0.0"))
	float FlyByDistanceCm = 500.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Jam|Flyer|Sound", meta = (ClampMin = "0.0"))
	float FlyByMinSpeedCm = 900.0f;

	/** A screech older than this is only its tail: a new one takes its slot first, s. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Jam|Flyer|Sound", meta = (ClampMin = "0.0"))
	float ScreechBodySeconds = 0.6f;

	/** The voice (aggro, dive, death): 50 m. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Jam|Flyer|Sound")
	FSoundAttenuationSettings VoiceAttenuation;

	/** Wings, flutter, fly-by: 25 m. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Jam|Flyer|Sound")
	FSoundAttenuationSettings WingsAttenuation;

	/** Hits and the bite: 30 m. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Jam|Flyer|Sound")
	FSoundAttenuationSettings ImpactAttenuation;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Jam|Flyer|Sound")
	FJamFlyerMix Mix;

	/** Pushed on an aggro screech: ducks the ambience class for a moment. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Jam|Flyer|Sound")
	TObjectPtr<USoundMix> AggroDuck = nullptr;

	/** In air (the listener breathes) every layer sends here instead of playing the suit's tail. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Jam|Flyer|Sound")
	TObjectPtr<USoundSubmixBase> AirReverb = nullptr;

	/** A bite shakes the player's camera this much, cm (the pawn's crash shake: a hard crash ~5). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Jam|Flyer|Hunt", meta = (ClampMin = "0.0"))
	float BiteShakeCm = 8.0f;

	/** A bite throws blood on the visor: blot size 0..1 (UJamVisorBloodComponent). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Jam|Flyer|Hunt", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float BiteVisorBlood = 0.9f;

	// --- state -----------------------------------------------------------------------

	UFUNCTION(BlueprintPure, Category = "Jam|Flyer")
	EJamFlyerState GetState() const { return State; }

	UFUNCTION(BlueprintPure, Category = "Jam|Flyer")
	float GetHealth() const { return Health; }

	/** On the attack rungs: circling, diving, feinting, pulling away, or knocked about / fleeing in between. */
	UFUNCTION(BlueprintPure, Category = "Jam|Flyer")
	bool IsHunting() const;

	/** Following the player on any rung (curious, stalking, hunting): sight needs no cone. */
	bool IsTracking() const { return IsHunting() || State == EJamFlyerState::Curious || State == EJamFlyerState::Stalk; }

	/** 0-1, random per spawn: the curious ones dart at the player even mid-fight. */
	float GetCuriosity() const { return Curiosity; }

	/** The habitat hands itself over right after spawning (its geometry and the flock). */
	void SetHome(AJamFlyerHabitat* InHabitat);

	/** Into the hunt now with the aggro screech (a cascade, a hit), from any calm state; the cascade passes on. */
	void Aggro(const TCHAR* Why, bool bScreech = true);

	/** Cling to a spot right away (the habitat's spawn). */
	void RoostAt(int32 SpotIndex);

	/** Fly back to a free roost spot and cling. */
	void StartLanding(const TCHAR* Why);

	/** Leave the roost (the wing snap) for Wander; the swap and a touch. */
	void PeelOff(const TCHAR* Why);

	/** A heard shot: a wandering flyer drifts to Where for InvestigateSeconds. */
	void Investigate(const FVector& Where);

	/** Notice the player now as if seen (the probes and the clip). */
	void Notice(const TCHAR* Why);

	/** In a scanner dart (in, holding still or backing off). */
	bool IsDarting() const { return DartPhase != 0; }

	/** Bites landed so far (the probe reads it). */
	int32 GetBiteCount() const { return BiteCount; }

	/** Last time the player was in sight (game seconds; negative when never). */
	double GetLastSeenSeconds() const { return LastSeenSeconds; }

	int32 GetRoostSpot() const { return RoostSpot; }

	static const TCHAR* StateName(EJamFlyerState InState);

	virtual void ReceiveResolvedImpact_Implementation(const FMKResolvedImpact& ResolvedImpact) override;
	virtual void Tick(float DeltaSeconds) override;

	/** Tick cost of every flyer (jam.Flyer.Cost): seconds and ticks, clinging and the rest. */
	static double TickSecondsRoost;
	static double TickSecondsOther;
	static int32 TicksRoost;
	static int32 TicksOther;

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

private:
	void SetState(EJamFlyerState NewState, const FString& Why);
	void Sense();
	void Think(const FVector& Location, const FVector& Velocity);
	/** Tracking ends (leash, lost sight): calm down, back to the roost if it wants more, else wander. */
	void GoCalm(const FString& Why);
	/** Notice on sight (or Notice): the rung of the flock's mood. */
	void OnNoticed(const FString& Why);
	/** A stagger is over: flee, or back on the rung of the mood. */
	void EndStagger();
	void ThinkRoost(float DeltaSeconds);
	/** A drawn gun held on it for AimProvokeSeconds provokes the flock a rung. */
	void CheckAim(float DeltaSeconds);
	/** Off the roost spot: the push off the surface and the wing snap. */
	void Unfold();
	FVector DesiredVelocity(const FVector& Location, const FVector& Velocity, float& OutAccel);
	FVector Avoid(const FVector& Location, const FVector& Desired) const;
	void Bite(APawn* Player);
	void Die(const FVector& Push);
	void Stagger(const FVector& Direction);
	void Animate(float DeltaSeconds, const FVector& Velocity, const FVector& Accel);
	/** The clinging pose of Visual and the parts (with the idle twitch and the stare). */
	void PoseRoost(float Blend);
	void Screech(const TCHAR* Why, bool bDive);
	void PlayPain();
	void PlayChirr();
	FVector HeadLocation() const;
	APawn* Player() const;
	/** The player's eye and the view's flat forward (perpendicular to Up). */
	void PlayerView(const APawn* Target, FVector& OutEye, FVector& OutForward) const;
	FVector Up(const FVector& Location) const;
	/** The spike's direction at its spot: the normal tilted by its own random lean. */
	FVector RoostDirection(const FJamRoostSpot& Spot) const;
	FVector RoostCentre(const FJamRoostSpot& Spot) const;

	EJamFlyerState State = EJamFlyerState::Wander;
	float Health = 60.0f;

	TWeakObjectPtr<AJamFlyerHabitat> Habitat;

	FVector WanderPoint = FVector::ZeroVector;
	float WanderSpeedFactor = 1.0f;
	double WanderPickedSeconds = -100.0;
	FVector NoiseCentre = FVector::ZeroVector;
	FVector NoisePoint = FVector::ZeroVector;
	double NoiseUntil = -100.0;

	double StateSeconds = 0.0;
	double StateUntil = 0.0;
	double LastSeenSeconds = -100.0;
	double CalmUntil = 0.0;
	double NextSenseSeconds = 0.0;
	bool bSeesPlayer = false;
	float AimedSeconds = 0.0f;
	int32 BiteCount = 0;
	FVector DiveDirection = FVector::ForwardVector;

	// The ladder, the feint, the stagger.
	float Curiosity = 0.5f;
	int32 StalkLeg = 0;
	FVector FeintAim = FVector::ZeroVector;
	/** 0 none, 1 in, 2 holding still, 3 backing off. */
	int32 DartPhase = 0;
	double DartPhaseUntil = 0.0;
	/** A mid-fight dart: back to circling after it. */
	bool bDartFromHunt = false;
	EJamFlyerState StaggerFrom = EJamFlyerState::Wander;
	float TumbleDeg = 0.0f;
	float TumbleSign = 1.0f;

	// The roost.
	int32 RoostSpot = INDEX_NONE;
	float RoostBlend = 0.0f;
	double LandedSeconds = -100.0;
	double NextTwitchSeconds = 0.0;
	/** 0 shiver, 1 head turn, 2 breath. */
	int32 TwitchKind = 0;
	double TwitchStart = -100.0;
	float TwitchLength = 0.0f;
	FVector TwitchAxis = FVector::ZeroVector;
	FVector StareDirection = FVector::ZeroVector;

	// Presentation.
	float FlapPhase = 0.0f;
	float Seed = 0.0f;
	FVector LastVelocity = FVector::ZeroVector;
	FVector SmoothedAccel = FVector::ZeroVector;

	/** Every flyer in play (the wings' and the screeches' mix ranks them by distance). */
	static TArray<TWeakObjectPtr<AJamFlyer>> AllFlyers;

	FJamSoundPlay VoicePlay;
	double VoiceStartedSeconds = -100.0;
	bool bVoiceDoppler = false;
	FJamSoundPlay FlyByPlay;
	double LastFlyBySeconds = -100.0;
	double NextOcclusionSeconds = 0.0;

	UPROPERTY(Transient)
	TObjectPtr<UAudioComponent> Flutter = nullptr;

	/** In air (the player breathes), or in vacuum (heard through the suit). */
	bool ListenerInAir() const;
	/** Among the nearest Mix.MaxWingSets flyers to the player (their wings are heard). */
	bool WingsHeard() const;
	/** A screech may start: under Mix.MaxScreeches, or it takes an old one's (only its tail left) or the farthest one's place. */
	bool TakeScreechSlot(const TCHAR* Why);
	void UpdateSounds(float DeltaSeconds);
	/** The rounds of one source inside HitWindowSeconds, waiting to be one hit sound. */
	struct FPendingHit
	{
		TWeakObjectPtr<AActor> Source;
		FVector Point = FVector::ZeroVector;
		float Damage = 0.0f;
		int32 Rounds = 0;
	};
	TArray<FPendingHit> PendingHits;
	void PlayHit(TWeakObjectPtr<AActor> Source);
	float Doppler() const;
	void PlayFlap(float Strength, float Rate);
	/** The corpse falls in a gravity zone (the planetoid's radial pull), drifts elsewhere. */
	void FallAsCorpse(float DeltaSeconds);
	bool bLanded = false;

	UPROPERTY(Transient)
	TObjectPtr<UPhysicalMaterial> FleshMaterial = nullptr;
	double DiedSeconds = 0.0;
};
