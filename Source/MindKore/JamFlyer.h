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

/**
 * Where a flock lives: a sphere (or a shell around a planetoid, InnerRadiusCm > 0).
 * On BeginPlay it spawns FlockSize AJamFlyer inside and hands them its numbers.
 * The flyers wander only inside it, notice the player only near it (AggroMarginCm)
 * and give up the chase when the player is LeashCm beyond it. Docs/Flyers.md.
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

	/** A flyer starts a hunt on sight only while the player is within the outer radius plus this, cm. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Jam|Flyer", meta = (ClampMin = "0.0"))
	float AggroMarginCm = 500.0f;

	/** A screech of a flyer that starts a hunt wakes the wandering flock within this, cm. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Jam|Flyer", meta = (ClampMin = "0.0"))
	float AlertRadiusCm = 2500.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Jam|Flyer")
	TSubclassOf<AJamFlyer> FlyerClass;

	float GetOuterRadius() const;

	const TArray<TWeakObjectPtr<AJamFlyer>>& GetFlock() const { return Flock; }

protected:
	virtual void BeginPlay() override;

private:
	TArray<TWeakObjectPtr<AJamFlyer>> Flock;
};

UENUM(BlueprintType)
enum class EJamFlyerState : uint8
{
	Wander,
	/** Hunting: wheel above the player, then dive. */
	Circle,
	Dive,
	/** Hunting: pull away after a bite or a miss, then circle again. */
	Retreat,
	Dead
};

/**
 * A flying predator after Morrowind's cliff racer (model: Scripts/blender/jam_flyer.py).
 *
 * Body is a simulated sphere (gravity off, rotation locked while alive); flight
 * is acceleration toward a desired velocity (no navmesh): wander targets in the
 * habitat, a sphere sweep ahead to steer around rock and hull, separation from
 * the flock. The look (Visual) turns to the velocity and banks into turns; the
 * wings flap and the tail swings by turning their mesh parts.
 *
 * Sight: range, a cone around the nose, a Visibility line to the player. On
 * sight near the habitat (or when shot) it screeches and hunts: wheels above
 * the player, dives at where the player will be, bites (damage and a shove on
 * the pawn through the impact system), pulls away, wheels again. The hunt ends
 * when the player is beyond the habitat's leash or unseen for LoseSightSeconds.
 * Rounds hurt it (IMKImpactReceiver); at zero health it is a tumbling corpse
 * that drifts and can be pushed, grabbed and shot further.
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

	/** A new wander target this close, cm (or after WanderRetargetSeconds). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Jam|Flyer|Flight", meta = (ClampMin = "0.0"))
	float WanderArriveCm = 400.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Jam|Flyer|Flight", meta = (ClampMin = "0.1"))
	float WanderRetargetSeconds = 7.0f;

	/** Share of wander legs that are swoops: low, close to the inner surface, fast. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Jam|Flyer|Flight", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float SwoopChance = 0.3f;

	/** Look ahead along the velocity for obstacles, seconds of flight. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Jam|Flyer|Flight", meta = (ClampMin = "0.0"))
	float AvoidSeconds = 0.9f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Jam|Flyer|Flight", meta = (ClampMin = "0.0"))
	float SeparationCm = 500.0f;

	// --- senses and the hunt --------------------------------------------------

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Jam|Flyer|Hunt", meta = (ClampMin = "0.0"))
	float SightRangeCm = 4500.0f;

	/** Half-angle of the view cone around the nose for noticing the player, degrees. A hunt tracks without it. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Jam|Flyer|Hunt", meta = (ClampMin = "0.0", ClampMax = "180.0"))
	float SightHalfAngleDeg = 70.0f;

	/** A hunt ends after this long without a line to the player, seconds. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Jam|Flyer|Hunt", meta = (ClampMin = "0.0"))
	float LoseSightSeconds = 4.0f;

	/** After a hunt ends it does not start another on sight for this long, seconds (shots still do). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Jam|Flyer|Hunt", meta = (ClampMin = "0.0"))
	float CalmSeconds = 3.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Jam|Flyer|Hunt", meta = (ClampMin = "0.0"))
	float CircleRadiusCm = 900.0f;

	/** The circle's centre sits this far over the player along the habitat's up, cm. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Jam|Flyer|Hunt")
	float CircleHeightCm = 500.0f;

	/** Seconds of wheeling before a dive, random in the range. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Jam|Flyer|Hunt")
	FVector2D CircleSeconds = FVector2D(1.5f, 3.5f);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Jam|Flyer|Hunt", meta = (ClampMin = "0.0"))
	float MaxDiveSeconds = 3.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Jam|Flyer|Hunt", meta = (ClampMin = "0.0"))
	float RetreatSeconds = 1.2f;

	/** A bite lands when the player's centre is this close to the flyer's, cm (the beak reaches 1.5 m forward). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Jam|Flyer|Hunt", meta = (ClampMin = "0.0"))
	float BiteReachCm = 190.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Jam|Flyer|Hunt", meta = (ClampMin = "0.0"))
	float BiteDamage = 8.0f;

	/** The shove of a bite on the pawn, kg cm/s along the dive: 45000 on 100 kg is 4.5 m/s. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Jam|Flyer|Hunt", meta = (ClampMin = "0.0"))
	float BiteImpulse = 45000.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Jam|Flyer|Hunt", meta = (ClampMin = "1.0"))
	float MaxHealth = 60.0f;

	/** A corpse in a gravity zone (the planetoid) drags so it comes to rest instead of rolling; in zero-G it keeps drifting. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Jam|Flyer|Hunt", meta = (ClampMin = "0.0"))
	float CorpseLinearDamping = 0.4f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Jam|Flyer|Hunt", meta = (ClampMin = "0.0"))
	float CorpseAngularDamping = 6.0f;

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

	/** One per downstroke (mid-beat): louder and brighter with the beat's size, higher with its rate; glides are silent. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Jam|Flyer|Sound")
	FJamSoundEvent FlapSound;

	/** A close pass (closest approach under FlyByDistanceCm at FlyByMinSpeedCm), Doppler. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Jam|Flyer|Sound")
	FJamSoundEvent FlyBySound;

	/** A round into it (alive or dead), at the hit. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Jam|Flyer|Sound")
	FJamSoundEvent HitSound;

	/** The bite, at the player: the beak, the wet tear and MindKore's gristle crunch. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Jam|Flyer|Sound")
	FJamSoundEvent BiteSound;

	/** Cloth flutter between beats: a loop on the body, its volume with the flight speed. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Jam|Flyer|Sound")
	TObjectPtr<USoundBase> FlutterLoop = nullptr;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Jam|Flyer|Sound", meta = (ClampMin = "0.0"))
	float FlutterVolume = 0.6f;

	/** A full beat's volume scale; under ~1/3 of FlapAmplitudeDeg none. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Jam|Flyer|Sound", meta = (ClampMin = "0.0"))
	float FlapVolume = 1.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Jam|Flyer|Sound", meta = (ClampMin = "0.0"))
	float FlyByDistanceCm = 500.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Jam|Flyer|Sound", meta = (ClampMin = "0.0"))
	float FlyByMinSpeedCm = 900.0f;

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

	UFUNCTION(BlueprintPure, Category = "Jam|Flyer")
	bool IsHunting() const { return State == EJamFlyerState::Circle || State == EJamFlyerState::Dive || State == EJamFlyerState::Retreat; }

	/** The habitat hands over its numbers (and itself, for the flock) right after spawning. */
	void SetHome(AJamFlyerHabitat* InHabitat);

	/** Start a hunt now (a flockmate's screech, a shot). */
	void Aggro(const TCHAR* Why);

	/** Bites landed so far (the probe reads it). */
	int32 GetBiteCount() const { return BiteCount; }

	/** Last time the player was in sight (game seconds; negative when never). */
	double GetLastSeenSeconds() const { return LastSeenSeconds; }

	static const TCHAR* StateName(EJamFlyerState InState);

	virtual void ReceiveResolvedImpact_Implementation(const FMKResolvedImpact& ResolvedImpact) override;
	virtual void Tick(float DeltaSeconds) override;

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

private:
	void SetState(EJamFlyerState NewState, const FString& Why);
	void Sense();
	void Think(const FVector& Location, const FVector& Velocity);
	FVector DesiredVelocity(const FVector& Location, const FVector& Velocity, float& OutAccel);
	FVector Avoid(const FVector& Location, const FVector& Desired) const;
	void Bite(APawn* Player);
	void Die(const FVector& Push);
	void Animate(float DeltaSeconds, const FVector& Velocity, const FVector& Accel);
	void Screech(const TCHAR* Why, bool bDive);
	FVector HeadLocation() const;
	APawn* Player() const;
	/** "Up" for banking and circling: away from the centre around a planetoid (an inner radius), world up in a plain ball. */
	FVector Up(const FVector& Location) const;
	/** Inside the home's outer radius plus ExtraCm. */
	bool AtHome(const FVector& Location, float ExtraCm) const;
	/** A wander target ahead (roughly along Forward), moved into the home's shell; a swoop goes low. */
	FVector PickWanderPoint(const FVector& From, const FVector& Forward, bool bSwoop) const;

	EJamFlyerState State = EJamFlyerState::Wander;
	float Health = 60.0f;

	TWeakObjectPtr<AJamFlyerHabitat> Habitat;
	FVector HomeCentre = FVector::ZeroVector;
	float HomeOuterCm = 2000.0f;
	float HomeInnerCm = 0.0f;
	float LeashCm = 2000.0f;
	float AggroMarginCm = 500.0f;

	FVector WanderPoint = FVector::ZeroVector;
	float WanderSpeedScale = 1.0f;
	double WanderPickedSeconds = -100.0;

	double StateSeconds = 0.0;
	double StateUntil = 0.0;
	double LastSeenSeconds = -100.0;
	double CalmUntil = 0.0;
	double NextSenseSeconds = 0.0;
	bool bSeesPlayer = false;
	int32 BiteCount = 0;
	FVector DiveDirection = FVector::ForwardVector;

	// Presentation.
	float FlapPhase = 0.0f;
	float Seed = 0.0f;
	FVector LastVelocity = FVector::ZeroVector;
	FVector SmoothedAccel = FVector::ZeroVector;

	/** Every flyer in play (the wings' and the screeches' mix ranks them by distance). */
	static TArray<TWeakObjectPtr<AJamFlyer>> AllFlyers;

	FJamSoundPlay VoicePlay;
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
	/** A screech may start: under Mix.MaxScreeches, or it takes the farthest one's place. */
	bool TakeScreechSlot(const TCHAR* Why);
	void UpdateSounds(float DeltaSeconds);
	float Doppler() const;
	void PlayFlap(float Strength, float Rate);
	/** The corpse falls in a gravity zone (the planetoid's radial pull), drifts elsewhere. */
	void FallAsCorpse(float DeltaSeconds);
	bool bLanded = false;

	UPROPERTY(Transient)
	TObjectPtr<UPhysicalMaterial> FleshMaterial = nullptr;
	double DiedSeconds = 0.0;
};
