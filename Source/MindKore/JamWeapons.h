#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Curves/CurveFloat.h"
#include "JamScope.h"
#include "JamWeapons.generated.h"

class UAudioComponent;
class UEnhancedInputComponent;
class UInputAction;
class UInputMappingContext;
class UJamScopeComponent;
class UMaterialInterface;
class UMKImpactDataAsset;
class USoundBase;
class UStaticMesh;
class UStaticMeshComponent;

/** One key of a moving part's curve: seconds, share of the travel/turn, slopes in share per second (cubic, broken tangents). */
/** A knock on the gun in view at a moment of its cycle or reload: the springs' velocity jumps by this much. */
USTRUCT(BlueprintType)
struct FJamPoseKick
{
	GENERATED_BODY()

	/** Seconds after the shot / the reload's start / the round's start, as the list it is in says. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Jam|Weapon|Pose")
	float Time = 0.0f;

	/** cm/s, camera frame (X forward, Y right, Z up). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Jam|Weapon|Pose")
	FVector Move = FVector::ZeroVector;

	/** deg/s: pitch (muzzle up), yaw (right), roll (clockwise from behind). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Jam|Weapon|Pose")
	FRotator Turn = FRotator::ZeroRotator;
};

struct FJamCycleKey
{
	float Time;
	float Value;
	float Arrive;
	float Leave;
};

MINDKORE_API FRuntimeFloatCurve JamMakeCycleCurve(std::initializer_list<FJamCycleKey> Keys);
/** The curve at Seconds; 0 when it has no keys. */
MINDKORE_API float JamEvalCycle(const FRuntimeFloatCurve& Curve, float Seconds);

/** One gun. Impulses are kg cm/s (velocity change = impulse / mass). */
USTRUCT(BlueprintType)
struct FJamWeapon
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Jam|Weapon")
	FText Name;

	/** Per pellet, before the hit zone's multiplier. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Jam|Weapon", meta = (ClampMin = "0.0"))
	float Damage = 25.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Jam|Weapon", meta = (ClampMin = "0.01"))
	float FireInterval = 0.15f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Jam|Weapon", meta = (ClampMin = "1"))
	int32 MagazineSize = 12;

	/** Rounds outside the magazine at the start. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Jam|Weapon", meta = (ClampMin = "0"))
	int32 Reserve = 60;

	/** The whole reload; for a gun that loads round by round (ShellMesh), the seconds per round. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Jam|Weapon", meta = (ClampMin = "0.0"))
	float ReloadSeconds = 1.2f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Jam|Weapon", meta = (ClampMin = "1"))
	int32 Pellets = 1;

	/** Half-angle of the pellet cone, degrees. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Jam|Weapon", meta = (ClampMin = "0.0"))
	float SpreadDeg = 0.0f;

	/** Kick on the shooter per shot, along -view; the pawn decides how much of it moves it. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Jam|Weapon", meta = (ClampMin = "0.0"))
	float RecoilImpulse = 1500.0f;

	/** Push on a hit body per pellet, at the hit point. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Jam|Weapon", meta = (ClampMin = "0.0"))
	float HitImpulse = 3000.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Jam|Weapon", meta = (ClampMin = "100.0"))
	float RangeCm = 8000.0f;

	/** Keeps firing while LMB is held, at FireInterval. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Jam|Weapon")
	bool bAutomatic = false;

	/** Round speed, cm/s (near real: 35000 / 40000 / 80000): each pellet flies as a round with a tracer and hits on arrival. 0: hitscan. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Jam|Weapon", meta = (ClampMin = "0.0"))
	float ProjectileSpeed = 9000.0f;

	/** The view's kick per shot, degrees up; RecoilSettleFraction of it comes back by itself. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Jam|Weapon|Recoil", meta = (ClampMin = "0.0"))
	float KickPitchDeg = 1.5f;

	/** Sideways kick, degrees, randomly signed per shot; settles back like the pitch. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Jam|Weapon|Recoil", meta = (ClampMin = "0.0"))
	float KickYawDeg = 0.3f;

	/** The gun in view jumps back this far per shot and springs forward again. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Jam|Weapon|Recoil", meta = (ClampMin = "0.0"))
	float ViewKickBackCm = 3.0f;

	/** ...and its muzzle rises this much. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Jam|Weapon|Recoil", meta = (ClampMin = "0.0"))
	float ViewKickRiseDeg = 8.0f;

	/** The gun seen in first person; none: nothing in hand. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Jam|Weapon|View")
	TObjectPtr<UStaticMesh> ViewMesh = nullptr;

	/** Where ViewMesh sits relative to the camera (X forward, Y right, Z up). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Jam|Weapon|View")
	FTransform ViewOffset;

	/**
	 * The gun's moving part in view - the pistol's slide, the shotgun's fore-end, the
	 * SV-98's bolt - hung on ViewMesh with its origin at ActionPivot. After every shot
	 * it runs one cycle, ActionTravel and ActionTurn sampled at the seconds since the
	 * shot; the gun cannot fire again before the cycle is over (GetActionCycleSeconds).
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Jam|Weapon|Action")
	TObjectPtr<UStaticMesh> ActionMesh = nullptr;

	/** Where ActionMesh's origin sits in ViewMesh's space, cm (the bolt turns about ViewMesh's X through it). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Jam|Weapon|Action")
	FVector ActionPivot = FVector::ZeroVector;

	/** The part's travel along ViewMesh's X at ActionTravel 1, cm (signed: back is +X on the USP, -X on the long guns). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Jam|Weapon|Action")
	float ActionTravelCm = 0.0f;

	/** The part's turn about ViewMesh's X at ActionTurn 1, degrees (the bolt handle's lift). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Jam|Weapon|Action")
	float ActionTurnDeg = 0.0f;

	/** Seconds since the shot -> share of ActionTravelCm. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Jam|Weapon|Action")
	FRuntimeFloatCurve ActionTravel;

	/** Seconds since the shot -> share of ActionTurnDeg. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Jam|Weapon|Action")
	FRuntimeFloatCurve ActionTurn;

	/**
	 * The whole gun turned this way, in the camera's frame about the grip, at
	 * ActionBody 1: canted and swung across the view while the part works, so a
	 * stroke along the line of sight reads on screen. With PoseSprings it is where
	 * the springs pull, not the pose itself.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Jam|Weapon|Action")
	FRotator ActionBodyTurn = FRotator::ZeroRotator;

	/** ...and moved this far, cm in the camera's frame (X forward, Y right, Z up), at ActionBody 1. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Jam|Weapon|Action")
	FVector ActionBodyOffsetCm = FVector::ZeroVector;

	/** Seconds since the shot -> share of ActionBodyTurn and ActionBodyOffsetCm (a step: the springs make the motion). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Jam|Weapon|Action")
	FRuntimeFloatCurve ActionBody;

	/** The hammer (the USP's), on ViewMesh, turning about ViewMesh's Y through HammerPivot (the mesh's origin is its pin). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Jam|Weapon|Parts")
	TObjectPtr<UStaticMesh> HammerMesh = nullptr;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Jam|Weapon|Parts")
	FVector HammerPivot = FVector::ZeroVector;

	/** Its turn at HammerTurn 1, degrees about ViewMesh's Y (negative: the top toward -X). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Jam|Weapon|Parts")
	float HammerTurnDeg = 0.0f;

	/** Seconds since the shot -> share of HammerTurnDeg: 1 fallen, below 0 pushed back past cocked. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Jam|Weapon|Parts")
	FRuntimeFloatCurve HammerTurn;

	/** The spent case thrown out EjectSeconds after each shot, from EjectPort (ViewMesh space, cm); none: no case. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Jam|Weapon|Parts")
	TObjectPtr<UStaticMesh> CaseMesh = nullptr;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Jam|Weapon|Parts")
	FVector EjectPort = FVector::ZeroVector;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Jam|Weapon|Parts", meta = (ClampMin = "0.0"))
	float EjectSeconds = 0.0f;

	/** The case's speed on top of the shooter's, cm/s in the camera's frame (X forward, Y right, Z up). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Jam|Weapon|Parts")
	FVector EjectVelocity = FVector(-40.0f, 220.0f, 120.0f);

	/**
	 * The magazine, on ViewMesh in its frame, moved by MagTravelCm x MagTravel (seconds
	 * since the reload began). At MagEjectSeconds it leaves as a loose body in the world,
	 * its rounds with it (Ammo 0); the fresh one shows from MagInSeconds, the curve brings
	 * it home and at MagInSoundSeconds, the seat, it is filled from Reserve.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Jam|Weapon|Reload")
	TObjectPtr<UStaticMesh> MagMesh = nullptr;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Jam|Weapon|Reload")
	FVector MagTravelCm = FVector::ZeroVector;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Jam|Weapon|Reload")
	FRuntimeFloatCurve MagTravel;

	/** The magazine's way off the straight line: MagArcCm x MagArc (ViewMesh space), 0 at the well. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Jam|Weapon|Reload")
	FVector MagArcCm = FVector::ZeroVector;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Jam|Weapon|Reload")
	FRuntimeFloatCurve MagArc;

	/** Its tilt about ViewMesh's Y at MagTurn 1, degrees (straightens as it enters). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Jam|Weapon|Reload")
	float MagTurnDeg = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Jam|Weapon|Reload")
	FRuntimeFloatCurve MagTurn;

	/** The flick it leaves with, deg/s about ViewMesh's Y (randomised +-25%). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Jam|Weapon|Reload")
	float MagEjectSpinDeg = 0.0f;

	/** The point the magazine turns about (MagTurn), ViewMesh space, cm: its top, on the well's axis. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Jam|Weapon|Reload")
	FVector MagPivot = FVector::ZeroVector;

	/**
	 * How far out along MagTravelCm the magazine is clear of its well, cm. Nearer in, it only
	 * slides along the well's axis: MagArc and MagTurn fade to nothing over the last 2 cm before.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Jam|Weapon|Reload", meta = (ClampMin = "0.0"))
	float MagClearCm = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Jam|Weapon|Reload", meta = (ClampMin = "0.0"))
	float MagEjectSeconds = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Jam|Weapon|Reload", meta = (ClampMin = "0.0"))
	float MagInSeconds = 0.0f;

	/**
	 * A gun that loads round by round (the pump gun's tube): each round, ReloadSeconds
	 * long, rises from ShellFrom to ShellTo (ViewMesh space, cm; the mesh's base) on
	 * ShellInsert, tilted ShellTiltDeg about Y at the start, and counts at ShellSeatSeconds.
	 * A shot breaks the loading off when the tube holds a round.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Jam|Weapon|Reload")
	TObjectPtr<UStaticMesh> ShellMesh = nullptr;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Jam|Weapon|Reload")
	FVector ShellFrom = FVector::ZeroVector;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Jam|Weapon|Reload")
	FVector ShellTo = FVector::ZeroVector;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Jam|Weapon|Reload")
	float ShellTiltDeg = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Jam|Weapon|Reload")
	FRuntimeFloatCurve ShellInsert;

	/** The round's way off the straight line, ShellArcCm x ShellArc (ViewMesh space). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Jam|Weapon|Reload")
	FVector ShellArcCm = FVector::ZeroVector;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Jam|Weapon|Reload")
	FRuntimeFloatCurve ShellArc;

	/** Seconds into the round -> share of ShellTiltDeg: it turns into the port as it is thumbed in. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Jam|Weapon|Reload")
	FRuntimeFloatCurve ShellTurn;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Jam|Weapon|Reload", meta = (ClampMin = "0.0"))
	float ShellSeatSeconds = 0.0f;

	/** The whole gun during a reload, as ActionBodyTurn/Offset: ReloadBody (the springs' target) rises from the start and falls into the end. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Jam|Weapon|Reload")
	FRotator ReloadBodyTurn = FRotator::ZeroRotator;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Jam|Weapon|Reload")
	FVector ReloadBodyOffsetCm = FVector::ZeroVector;

	/** Seconds from the reload's start (and to its end, mirrored) -> share of ReloadBodyTurn/Offset; its last key is the lead before the first round. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Jam|Weapon|Reload")
	FRuntimeFloatCurve ReloadBody;

	/**
	 * The whole gun's pose is a mass on six damped springs (X, Y, Z, pitch, yaw, roll, as
	 * the scope's AimIn): ActionBody and ReloadBody only set where they pull to, each
	 * channel seeing them Delay late; the turns also trail the gun's motion
	 * (UJamWeaponsComponent::PoseTurnLag), so it moves in arcs. Fewer than 6: no springs.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Jam|Weapon|Pose")
	TArray<FJamAimSpring> PoseSprings;

	/** Knocks on the springs after the shot (the pump slammed, the bolt locked). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Jam|Weapon|Pose")
	TArray<FJamPoseKick> ShotKicks;

	/** ...after the reload's start (the magazine flicked out, slapped home). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Jam|Weapon|Pose")
	TArray<FJamPoseKick> ReloadKicks;

	/** ...after each round's start when loading round by round (the round pushed home). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Jam|Weapon|Pose")
	TArray<FJamPoseKick> RoundKicks;

	/** Mechanism sounds, 2D, none by default: the hammer HammerSoundSeconds after a shot. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Jam|Weapon|Sound")
	TObjectPtr<USoundBase> HammerSound = nullptr;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Jam|Weapon|Sound", meta = (ClampMin = "0.0"))
	float HammerSoundSeconds = 0.0f;

	/** The magazine let go, at MagEjectSeconds of the reload. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Jam|Weapon|Sound")
	TObjectPtr<USoundBase> MagOutSound = nullptr;

	/** The fresh magazine seated, at MagInSoundSeconds of the reload. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Jam|Weapon|Sound")
	TObjectPtr<USoundBase> MagInSound = nullptr;

	/** The seat: MagInSound plays and the rounds count (a magazine gun; a round-by-round one counts at ShellSeatSeconds). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Jam|Weapon|Sound", meta = (ClampMin = "0.0"))
	float MagInSoundSeconds = 0.0f;

	/** Each round pushed into the tube, at its ShellSeatSeconds. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Jam|Weapon|Sound")
	TObjectPtr<USoundBase> ShellInsertSound = nullptr;

	/**
	 * Putting the gun away and drawing it (owner 19.10: the pistol to a holster on the right
	 * hip, the shotgun over the right shoulder, the SV-98 over the left): the pose springs
	 * pull toward StowOffsetCm / StowTurn (out of view, camera frame) by StowPath (seconds
	 * into the put-away; its last key: gone) and back by DrawPath (seconds into the draw,
	 * from 1 to 0; its last key: drawn). The turns see both paths StowTurnLead s early
	 * (positive: the muzzle leads, negative: it trails the grip).
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Jam|Weapon|Switch")
	FVector StowOffsetCm = FVector::ZeroVector;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Jam|Weapon|Switch")
	FRotator StowTurn = FRotator::ZeroRotator;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Jam|Weapon|Switch")
	FRuntimeFloatCurve StowPath;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Jam|Weapon|Switch")
	FRuntimeFloatCurve DrawPath;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Jam|Weapon|Switch")
	float StowTurnLead = 0.0f;

	/**
	 * A slung long gun's way (owner 19.10): StowMidShare > 0 bends the place path through
	 * StowMidCm - rest -> Mid over path shares 0..StowMidShare, Mid -> StowOffsetCm after -
	 * so the rifle comes down vertical into the frame's side before it levels. With keys in
	 * StowTurnPath / DrawTurnPath the turns follow those (seconds -> share of StowTurn)
	 * instead of the place path shifted by StowTurnLead, with no spring Delay on them: the
	 * keys carry the timing, so a turn can start on the draw's first frame.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Jam|Weapon|Switch")
	FVector StowMidCm = FVector::ZeroVector;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Jam|Weapon|Switch", meta = (ClampMin = "0.0", ClampMax = "0.95"))
	float StowMidShare = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Jam|Weapon|Switch")
	FRuntimeFloatCurve StowTurnPath;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Jam|Weapon|Switch")
	FRuntimeFloatCurve DrawTurnPath;

	/** Seconds into the draw from which the gun may fire. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Jam|Weapon|Switch", meta = (ClampMin = "0.0"))
	float DrawReadySeconds = 0.3f;

	/** Knocks on the pose springs, seconds into the put-away / the draw. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Jam|Weapon|Switch")
	TArray<FJamPoseKick> StowKicks;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Jam|Weapon|Switch")
	TArray<FJamPoseKick> DrawKicks;

	/** The shoulder taking a heavy gun: a nudge of the view (pitch, yaw, degrees; it comes back) at these moments. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Jam|Weapon|Switch")
	FRotator StowViewNudge = FRotator::ZeroRotator;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Jam|Weapon|Switch")
	float StowViewNudgeSeconds = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Jam|Weapon|Switch")
	FRotator DrawViewNudge = FRotator::ZeroRotator;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Jam|Weapon|Switch")
	float DrawViewNudgeSeconds = 0.0f;

	/** Holster / sling sounds, 2D, at these seconds into the put-away and the draw. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Jam|Weapon|Sound")
	TObjectPtr<USoundBase> StowSound = nullptr;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Jam|Weapon|Sound")
	float StowSoundSeconds = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Jam|Weapon|Sound")
	TObjectPtr<USoundBase> DrawSound = nullptr;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Jam|Weapon|Sound")
	float DrawSoundSeconds = 0.0f;

	/** The shot, 2D. Space-treated offline (Scripts/jam_space_shots.py): muffled, reverberant, with a sub. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Jam|Weapon|Sound")
	TObjectPtr<USoundBase> ShotSound = nullptr;

	/** The pump or bolt cycling after a shot, 2D, CycleDelaySeconds after it; none: no cycle (the pistol). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Jam|Weapon|Sound")
	TObjectPtr<USoundBase> CycleSound = nullptr;

	/** When the cycle sound starts after the shot; its clicks match the keys of ActionTravel / ActionTurn. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Jam|Weapon|Sound", meta = (ClampMin = "0.0"))
	float CycleDelaySeconds = 0.0f;

	/** The dry click when it fires on an empty magazine, 2D. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Jam|Weapon|Sound")
	TObjectPtr<USoundBase> EmptySound = nullptr;

	/** Usable now. Set from bStartWithAllWeapons on BeginPlay; pickups unlock (UnlockWeapon). */
	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Transient, Category = "Jam|Weapon")
	bool bUnlocked = true;

	/** Rounds in the magazine now. Set to MagazineSize on BeginPlay. */
	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Transient, Category = "Jam|Weapon")
	int32 Ammo = 0;

	/** Seconds from a shot until the gun is home: the last key of ActionTravel / ActionTurn / ActionBody (0 without them). */
	float GetActionCycleSeconds() const;

	/**
	 * Runtime, per gun (review 19.10: one clock for all guns let the SV-98's bolt hold the pistol's trigger):
	 * seconds into this gun's last cycle. It runs only while the gun is in hand and wanted - drawn or being
	 * drawn - and stands still while it is put away or unarmed, so an abandoned cycle freezes where it was,
	 * its sound stopped, and finishes when the gun comes back: the pump, its sound and its case come together.
	 */
	float SinceShot = 1000.0f;
	float PrevSinceShot = 1000.0f;
	/** SinceShot from which it fires again: its cycle (or FireInterval) after a shot, FireInterval after a dry click. */
	float FireReadySince = 0.0f;
	/** The hammer / pump / bolt sound playing, stopped when the cycle freezes and picked up from its place after. */
	TWeakObjectPtr<UAudioComponent> MechanismSound;
};

/**
 * The guns of a jam pawn - the zero-G pawn and the test room's character
 * carry the same one: 1 pistol, 2 shotgun, 3 sniper; unlocks, magazines and
 * reserves, R reload, 1/2/3 and the wheel select, automatic fire while LMB is
 * held (the pawn binds LMB: it also throws).
 *
 * Each pellet is a round flying at ProjectileSpeed with a tracer (or a
 * hitscan at 0): every tick its path is traced on the Shot channel and a hit
 * goes through MKResolveImpactFromHit + MKDeliverResolvedImpact, plus
 * HitImpulse on every simulated body hit. Pellets of one shot land at
 * slightly different times; AJamLivingWall gathers them into one blast.
 *
 * The shooter's push is not applied here: Fire returns it and the pawn applies
 * it its own way (the zero-G body takes all of it, the test room's character a
 * small fraction). The view's kick is applied here, through the owner's control
 * rotation, and the gun in view (ViewMesh, on the owner's camera) kicks too and
 * runs its moving part's cycle (ActionMesh), which also gates the next shot.
 */
UCLASS(ClassGroup = (Jam), meta = (BlueprintSpawnableComponent))
class MINDKORE_API UJamWeaponsComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UJamWeaponsComponent();

	/** 1 pistol, 2 shotgun, 3 sniper by default. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Jam|Weapons")
	TArray<FJamWeapon> Weapons;

	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Transient, Category = "Jam|Weapons")
	int32 CurrentWeapon = 0;

	/** Unarmed (Holster): CurrentWeapon stays as it was, out of hand. */
	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Transient, Category = "Jam|Weapons")
	bool bHolstered = false;



	/** Off (the ship map): every gun starts locked until a pickup unlocks it. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Jam|Weapons")
	bool bStartWithAllWeapons = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Jam|Weapons")
	TObjectPtr<UMKImpactDataAsset> ImpactData = nullptr;

	/** The tracer: this mesh (the engine sphere) stretched along the flight, in this material (MindKore's M_MK_Tracer). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Jam|Weapons|Tracer")
	TObjectPtr<UStaticMesh> TracerMesh = nullptr;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Jam|Weapons|Tracer")
	TObjectPtr<UMaterialInterface> TracerMaterial = nullptr;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Jam|Weapons|Tracer", meta = (ClampMin = "1.0"))
	float TracerLengthCm = 120.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Jam|Weapons|Tracer", meta = (ClampMin = "0.1"))
	float TracerThicknessCm = 3.0f;

	/** Rounds fly from the eye; their tracers start at the muzzle and join that path over this distance. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Jam|Weapons|Tracer", meta = (ClampMin = "1.0"))
	float TracerConvergeCm = 600.0f;

	/** The view's kick arrives over this, not in one frame (MindKore's CameraRecoilAttackSeconds). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Jam|Weapons|Recoil", meta = (ClampMin = "0.0"))
	float RecoilAttackSeconds = 0.03f;

	/** Share of each kick that comes back by itself; the rest stays (the player brings the view down). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Jam|Weapons|Recoil", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float RecoilSettleFraction = 0.7f;

	/** Degrees per second the settling share comes back at, linearly (MindKore's CameraRecoilRecoverySpeed). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Jam|Weapons|Recoil", meta = (ClampMin = "0.1"))
	float RecoilRecoverDegPerSec = 13.0f;

	/** How fast the gun in view springs back after a kick, 1/s. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Jam|Weapons|Recoil", meta = (ClampMin = "0.1"))
	float ViewKickReturnRate = 12.0f;

	/** The gun in view, on the owner's camera; made on BeginPlay, its mesh follows the gun in hand. */
	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Transient, Category = "Jam|Weapons")
	TObjectPtr<UStaticMeshComponent> ViewModel = nullptr;

	/** The gun's moving part (FJamWeapon::ActionMesh), on ViewModel. */
	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Transient, Category = "Jam|Weapons")
	TObjectPtr<UStaticMeshComponent> ActionInView = nullptr;

	/** The hammer, the magazine and the round being loaded, on ViewModel (JamWeaponParts.cpp). */
	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Transient, Category = "Jam|Weapons")
	TObjectPtr<UStaticMeshComponent> HammerInView = nullptr;

	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Transient, Category = "Jam|Weapons")
	TObjectPtr<UStaticMeshComponent> MagInView = nullptr;

	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Transient, Category = "Jam|Weapons")
	TObjectPtr<UStaticMeshComponent> ShellInView = nullptr;

	/** Spent cases and dropped magazines kept in the world at most; the oldest goes first. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Jam|Weapons|Parts", meta = (ClampMin = "0"))
	int32 MaxDebris = 48;

	/** ...and any farther than this from the shooter goes. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Jam|Weapons|Parts", meta = (ClampMin = "100.0"))
	float DebrisRangeCm = 5000.0f;

	/** Loose cases and magazines in the world now. */
	int32 GetDebrisCount() const { return Debris.Num(); }

	/** A drawn gun fires only once its pose is this close to rest (and DrawReadySeconds into the draw). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Jam|Weapons|Pose", meta = (ClampMin = "0.0"))
	float DrawReadyPlaceCm = 4.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Jam|Weapons|Pose", meta = (ClampMin = "0.0"))
	float DrawReadyTurnDeg = 10.0f;

	/** The gun's turns trail its motion: pitch per cm/s up (it dips while rising), yaw and roll per cm/s right. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Jam|Weapons|Pose")
	FVector PoseTurnLag = FVector(0.12f, 0.06f, 0.1f);

	/** Pitch and roll damping (at least) while the gun swings back out of a reload: it floats past by half as much there. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Jam|Weapons|Pose", meta = (ClampMin = "0.05", ClampMax = "2.0"))
	float PoseExitTurnDamping = 0.63f;

	/** The pose springs now: place cm and turn degrees (pitch, yaw, roll), camera frame. */
	FVector GetPosePlace() const { return Pose.Place; }
	FVector GetPoseTurn() const { return Pose.Turn; }

	/**
	 * The pose springs run from rest over a shot's cycle (bReload false) or a reload
	 * (round-by-round guns load Shells rounds) of gun Weapon, sampled every Step s:
	 * place and turn per sample. For stills and traces; the game steps them live.
	 */
	void TracePose(int32 Weapon, bool bReload, int32 Shells, float Seconds, float Step, TArray<FVector>& OutPlace, TArray<FVector>& OutTurn,
		int32 SwitchPhase = 0) const;

	/** Every gun's aim and the SV-98's scope (JamScope.h): made in BindInput; UpdateViewModel takes the shown gun's aimed pose from it (AimPose). */
	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Transient, Category = "Jam|Weapons")
	TObjectPtr<UJamScopeComponent> Scope = nullptr;

	/**
	 * The muzzle of the gun in view, world space, as it is drawn this frame (kick
	 * included): X out of the bore, along the gun's own line. ViewModel's Muzzle
	 * socket (every gun has one); without a gun in view, the owner's location and
	 * view. For flash and smoke; rounds fly from the eye, not from here.
	 */
	UFUNCTION(BlueprintPure, Category = "Jam|Weapons")
	FTransform GetMuzzleTransform() const;

	/** Rounds in flight now. */
	int32 GetRoundsInFlight() const { return Rounds.Num(); }

	/** Used when the resolved feedback has no decal of its own. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Jam|Weapons")
	TObjectPtr<UMaterialInterface> DefaultImpactDecal = nullptr;

	/**
	 * Fire the current gun from Start along Direction (the pawn's view). OutRecoil
	 * is the kick on the shooter, kg cm/s, for the pawn to apply. False when it
	 * cannot fire now (nothing unlocked, interval, reload, empty - which starts a reload).
	 */
	bool Fire(const FVector& Start, const FVector& Direction, FVector& OutRecoil);

	UFUNCTION(BlueprintCallable, Category = "Jam|Weapons")
	void SelectWeapon(int32 Index);

	UFUNCTION(BlueprintCallable, Category = "Jam|Weapons")
	void Reload();

	/** Unlock a gun and take it in hand. */
	UFUNCTION(BlueprintCallable, Category = "Jam|Weapons")
	void UnlockWeapon(int32 Index);

	UFUNCTION(BlueprintPure, Category = "Jam|Weapons")
	bool IsWeaponUnlocked(int32 Index) const { return Weapons.IsValidIndex(Index) && Weapons[Index].bUnlocked; }

	/** The gun in hand, or null: none unlocked yet, or unarmed (Holster). */
	const FJamWeapon* GetWeapon() const { return !bHolstered && IsWeaponUnlocked(CurrentWeapon) ? &Weapons[CurrentWeapon] : nullptr; }

	/**
	 * Unarmed, at once: the gun leaves the view in the same frame and does not fire (owner 18-19.10: a
	 * grab - AJamZeroGPawn::Grab). Rearm (the grab let go) or a selection brings a gun back with a Draw.
	 */
	UFUNCTION(BlueprintCallable, Category = "Jam|Weapons")
	void Holster();

	/** Back from unarmed to the last gun, with a Draw (AJamZeroGPawn::Release). */
	UFUNCTION(BlueprintCallable, Category = "Jam|Weapons")
	void Rearm();

	/**
	 * A gun coming back into hand out of unarmed (Rearm, a selection): logged. The draw itself is
	 * UpdateSwitch's (JamWeaponParts.cpp), as for any switch - the gun, wanted again, comes in from
	 * away along its own path. Fire waits for IsDrawing.
	 */
	void Draw();

	/** A put-away or draw still running for the gun in hand: no shot yet (!IsWeaponReady). */
	bool IsDrawing() const;

	UFUNCTION(BlueprintPure, Category = "Jam|Weapons")
	bool IsHolstered() const { return bHolstered; }

	/** The current gun is drawn far enough to fire (DrawReadySeconds) and no put-away is wanted. */
	UFUNCTION(BlueprintPure, Category = "Jam|Weapons")
	bool IsWeaponReady() const;

	/** For probes and stands: any put-away / draw finished at once - the current gun drawn and ready. */
	void FinishSwitch();

	/** The gun drawn in view now (the old one while it goes away), or INDEX_NONE. */
	int32 GetShownWeapon() const { return ShownWeapon; }

	/** Spent cases thrown since play began (probes: a switch throws none). */
	int32 GetCasesThrown() const { return CasesThrown; }

	/** Cycle sounds (pump, bolt) played since play began (probes). */
	int32 GetCycleSounds() const { return CycleSoundsPlayed; }


	/** Seconds left of a reload; 0 when not reloading. */
	float GetReloadRemaining() const;

	/** The HUD line: «[n] Name ammo/reserve» (and the reload), or «без оружия». */
	FString Describe() const;

	/** R reload, 1/2/3 select, the mouse wheel steps through the unlocked guns. */
	void BindInput(UEnhancedInputComponent& Input, UInputMappingContext& Mapping);

	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

protected:
	virtual void BeginPlay() override;

private:
	struct FRound
	{
		int32 Weapon = 0;
		FVector Position = FVector::ZeroVector;
		FVector Direction = FVector::ForwardVector;
		/** Muzzle minus eye at the shot: the tracer's offset, fading over TracerConvergeCm. */
		FVector MuzzleOffset = FVector::ZeroVector;
		float TravelledCm = 0.0f;
		/** Hit this frame: its streak ends at the impact for one frame, then it goes. */
		bool bLanded = false;
		TWeakObjectPtr<UStaticMeshComponent> Tracer;
	};

	/** Trace Start..End for a round of Weapon; deliver the hit. True when something stopped it (where: OutImpact). */
	bool TraceShot(const FJamWeapon& Weapon, const FVector& Start, const FVector& End, const FVector& Direction, FVector* OutImpact = nullptr);
	/** Where a round's tracer streak ends in front. */
	FVector TracerHead(const FRound& Round) const;
	void UpdateRounds(float DeltaTime);
	void UpdateRecoil(float DeltaTime);
	void UpdateViewModel(float DeltaTime);
	/** Turn the owner's view by degrees in the view's own frame. */
	void TurnView(float PitchDeg, float YawDeg) const;
	void SelectStep(int32 Step);

	// JamWeaponParts.cpp: the hammer, cases, magazines and round-by-round loading.
	/** Fill the guns' parts and reload curves; from the constructor (ConstructorHelpers). */
	void SetupWeaponParts();
	void MakePartComponents(USceneComponent& Parent);
	/** Start the reload's timeline; returns its length. */
	float StartReloadTimeline(const FJamWeapon& Weapon);
	/** Seconds into the reload, or < 0 when none (jam.Weapons.ReloadPoseAt holds one). */
	float GetReloadTime() const;
	/** Pose the parts for this frame; null: hide them. */
	void UpdateParts(const FJamWeapon* Weapon, float SinceShot);
public:
	/** The magazine on ViewMesh at Time into a reload (the one path its place and turn take). */
	static FTransform GetMagPose(const FJamWeapon& Weapon, float Time);
private:
	/** The shot's events the running gun's cycle clock crossed this frame: its case, hammer and cycle sound. */
	void UpdateCycleEvents();
	/** Reload events crossed since the last tick: the magazine let go and seated, a round counted, their sounds. */
	void UpdatePartEvents(float DeltaTime);
	void SpawnDebris(UStaticMesh* Mesh, const FTransform& Where, const FVector& Velocity, const FVector& SpinDegPerSec, float MassKg);

	TArray<FRound> Rounds;
	TArray<TWeakObjectPtr<AActor>> Debris;
	double ReloadStartSeconds = 0.0;
	/** Rounds this reload loads one by one (ShellMesh guns). */
	int32 ShellsPlanned = 0;
	int32 CasesThrown = 0;
	int32 CycleSoundsPlayed = 0;
	float PrevReloadTime = -1.0f;

	struct FPoseState
	{
		FVector Place = FVector::ZeroVector;
		FVector PlaceVelocity = FVector::ZeroVector;
		/** Pitch, yaw, roll. */
		FVector Turn = FVector::ZeroVector;
		FVector TurnVelocity = FVector::ZeroVector;
		/** Swinging back out of a reload (its last 0.1 s on, until the turns settle). */
		bool bExit = false;
	};
	/** Where the gun is in a switch: put away (Stow), drawn (Draw), away; Time0..Time1 the step's span. */
	enum class ESwitch : uint8 { Drawn, Stow, Draw, Away };
	struct FSwitchSpan
	{
		ESwitch Stage = ESwitch::Drawn;
		float Time0 = 0.0f;
		float Time1 = 0.0f;
	};
	/** Step the pose springs over shot time Since0..Since1 and reload time Reload0..Reload1 (< 0: none). */
	void StepPose(const FJamWeapon& Weapon, FPoseState& State, float Since0, float Since1, float Reload0, float Reload1,
		float ReloadLength, int32 Shells, float DeltaTime, const FSwitchSpan& Switch = FSwitchSpan()) const;
	/** Run the switch this frame; returns the gun to show (null: away) and the span the springs step over. */
	const FJamWeapon* UpdateSwitch(float DeltaTime, bool bHandsBusy, FSwitchSpan& OutSpan);
	/** Unarmed at once: no gun in view, the next draw from away. */
	void ClearView();
	ESwitch SwitchStage = ESwitch::Away;
	float SwitchTime = 0.0f;
	FSwitchSpan PoseSwitch;
	/** Live, or re-run from rest for jam.Weapons.PoseAt / ReloadPoseAt. */
	void UpdatePose(const FJamWeapon& Weapon, float DeltaTime);
	FPoseState Pose;
	int32 PoseWeapon = INDEX_NONE;
	float PosePrevSince = 1000.0f;
	float PosePrevReload = -1.0f;
	float KickPitchLeft = 0.0f;
	float KickYawLeft = 0.0f;
	float KickAttackLeft = 0.0f;
	float SettlePitch = 0.0f;
	float SettleYaw = 0.0f;
	/** Seconds since the last shot, for the kick in view (the parts' cycle runs on FJamWeapon::SinceShot). */
	float SinceKickSeconds = 1000.0f;
	int32 ShownWeapon = INDEX_NONE;
	/** The gun whose cycle clock runs: the shown one while drawn or being drawn, else none. */
	int32 CycleWeapon = INDEX_NONE;
	/**
	 * Gun.MechanismSound, Into seconds into its file: 0 on its crossing (a long frame plays it late rather than
	 * skip its click), mid-way when a frozen cycle picks up again.
	 */
	void PlayMechanismSound(FJamWeapon& Gun, USoundBase* Sound, float Into);

	double ReloadEndSeconds = 0.0;
	/** R pressed during the shot's cycle: the reload starts when it is through (a shot or a switch drops it). */
	bool bReloadQueued = false;

	UPROPERTY(Transient)
	TArray<TObjectPtr<UInputAction>> InputActions;
};
