#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Curves/CurveFloat.h"
#include "OSScope.h"
#include "OSWeapons.generated.h"

class UAudioComponent;
class UOSMuzzleFxComponent;
class UOSScopeComponent;
class UOSNixieCounterComponent;
class UMaterialInterface;
class UMKImpactDataAsset;
class USoundBase;
class USoundEffectSourcePresetChain;
class USoundSubmixBase;
class UStaticMesh;
class UStaticMeshComponent;

/** One key of a moving part's curve: seconds, share of the travel/turn, slopes in share per second (cubic, broken tangents). */
/** A knock on the gun in view at a moment of its cycle or reload: the springs' velocity jumps by this much. */
USTRUCT(BlueprintType)
struct FOSPoseKick
{
	GENERATED_BODY()

	/** Seconds after the shot / the reload's start / the round's start, as the list it is in says. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OS|Weapon|Pose")
	float Time = 0.0f;

	/** cm/s, camera frame (X forward, Y right, Z up). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OS|Weapon|Pose")
	FVector Move = FVector::ZeroVector;

	/** deg/s: pitch (muzzle up), yaw (right), roll (clockwise from behind). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OS|Weapon|Pose")
	FRotator Turn = FRotator::ZeroRotator;
};

struct FOSCycleKey
{
	float Time;
	float Value;
	float Arrive;
	float Leave;
};

MINDKORE_API FRuntimeFloatCurve OSMakeCycleCurve(std::initializer_list<FOSCycleKey> Keys);
/** The curve at Seconds; 0 when it has no keys. */
MINDKORE_API float OSEvalCycle(const FRuntimeFloatCurve& Curve, float Seconds);

/** Where a hinged lid is: held shut by its latch, swinging free on its hinge, or pressed shut by the hand. */
UENUM()
enum class EOSLidState : uint8
{
	Latched,
	Free,
	Hand,
};

/**
 * A loading lid hinged on the gun in view (the RMB-93's top loading window, OSRmb93.cpp,
 * Docs/RecoilWeapons-Rmb93.md). Angles are degrees open, 0 shut; positive turns the lid's free
 * (rear) edge up about the hinge's Y. Unlatched it is a body on its hinge: a kicker spring
 * throws it open, a weak torsion spring and the local gravity pull on it, the gun's own motion
 * (the mouse, the pose springs, the kick, the pawn) swings it, and it knocks against its stops.
 */
USTRUCT(BlueprintType)
struct FOSLid
{
	GENERATED_BODY()

	/** The lid, its origin on the hinge axis, turning about its own Y; none: no lid. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OS|Weapon|Lid")
	TObjectPtr<UStaticMesh> Mesh = nullptr;

	/** The hinge in ViewMesh's space, cm. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OS|Weapon|Lid")
	FVector HingeCm = FVector::ZeroVector;

	/** Hinge to the free edge, cm: a flat plate, its centre of mass half-way, its inertia m L^2 / 3. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OS|Weapon|Lid", meta = (ClampMin = "0.5"))
	float LengthCm = 8.0f;

	/** Where it stops open: it leans on the rear sight. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OS|Weapon|Lid", meta = (ClampMin = "1.0", ClampMax = "179.0"))
	float OpenStopDeg = 104.0f;

	/** Share of the speed it comes back off the open stop / off the receiver with. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OS|Weapon|Lid", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float OpenBounce = 0.2f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OS|Weapon|Lid", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float ShutBounce = 0.2f;

	/**
	 * The torsion spring in the hinge pulls toward SpringRestDeg (a little short of the stop), as stiff as SpringHz
	 * would ring the lid alone: weak, so gravity puts the lid where it lies and in low gravity it floats, swinging with the gun.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OS|Weapon|Lid")
	float SpringRestDeg = 95.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OS|Weapon|Lid", meta = (ClampMin = "0.0"))
	float SpringHz = 1.2f;

	/** The kicker under the lid: a stiff leaf pushing it open over its first KickerDeg (KickerHz as SpringHz). Its throw is the lid's flight open. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OS|Weapon|Lid", meta = (ClampMin = "0.0"))
	float KickerDeg = 40.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OS|Weapon|Lid", meta = (ClampMin = "0.0"))
	float KickerHz = 6.0f;

	/** The hinge's friction as a damping ratio of SpringHz' swing (viscous, 1/s = 2 x Damping x 2 pi SpringHz). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OS|Weapon|Lid", meta = (ClampMin = "0.0"))
	float Damping = 0.15f;

	/** How much of the gun's motion reaches it: 1 is the honest share, 0 a lid that only feels gravity and its springs. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OS|Weapon|Lid", meta = (ClampMin = "0.0"))
	float InertiaScale = 1.0f;

	/**
	 * The view's kick, which is drawn and not simulated, reaches an open lid as this jump of its speed at the shot, deg/s
	 * (the gun jerked back: the lid swings on toward its stop). The pawn's own push and the gun's real motion come through InertiaScale.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OS|Weapon|Lid")
	float ShotSpeedDeg = 600.0f;

	/** Once it has been up there in a reload, the lid cannot swing lower than this until the hand takes it: it rests on the round and the hand. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OS|Weapon|Lid", meta = (ClampMin = "0.0"))
	float ClearDeg = 62.0f;

	/** Seconds into a reload the palm strikes the latch (a reload that starts with the lid latched). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OS|Weapon|Lid", meta = (ClampMin = "0.0"))
	float StrikeSeconds = 0.14f;

	/** Seconds before a reload's end the hand catches the lid and presses it shut along Close. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OS|Weapon|Lid", meta = (ClampMin = "0.0"))
	float CloseSeconds = 0.22f;

	/** Seconds since the catch -> share of the angle it was caught at; its last key: shut and latched. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OS|Weapon|Lid")
	FRuntimeFloatCurve Close;

	/** Seconds since the catch the latch snaps (its sound and LatchKicks). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OS|Weapon|Lid", meta = (ClampMin = "0.0"))
	float LatchSeconds = 0.1f;

	/** Knocks on the pose springs: the palm on the latch (seconds into the reload), the lid pressed shut (seconds since the catch). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OS|Weapon|Lid")
	TArray<FOSPoseKick> StrikeKicks;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OS|Weapon|Lid")
	TArray<FOSPoseKick> LatchKicks;

	/** The latch struck (started StrikeSoundLead s before StrikeSeconds: its click on the moment). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OS|Weapon|Lid")
	TObjectPtr<USoundBase> StrikeSound = nullptr;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OS|Weapon|Lid")
	float StrikeSoundLead = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OS|Weapon|Lid", meta = (ClampMin = "0.1"))
	float StrikeSoundPitch = 1.0f;

	/** The lid knocking on a stop: only when it really hits one (5% of StopSoundFullDegPerSec), louder the faster (full at it). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OS|Weapon|Lid")
	TObjectPtr<USoundBase> StopSound = nullptr;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OS|Weapon|Lid", meta = (ClampMin = "0.1"))
	float StopSoundPitch = 1.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OS|Weapon|Lid", meta = (ClampMin = "1.0"))
	float StopSoundFullDegPerSec = 900.0f;

	/** The latch snapping shut (started LatchSoundLead s before LatchSeconds). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OS|Weapon|Lid")
	TObjectPtr<USoundBase> LatchSound = nullptr;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OS|Weapon|Lid")
	float LatchSoundLead = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OS|Weapon|Lid", meta = (ClampMin = "0.1"))
	float LatchSoundPitch = 1.0f;

	/** Runtime, the gun's own (it outlives a switch or a broken-off reload: an open lid stays open until a reload shuts it). */
	EOSLidState State = EOSLidState::Latched;
	float Angle = 0.0f;
	/** deg/s, opening positive. */
	float Speed = 0.0f;
	/** The hand's catch: the angle it was caught at and seconds since. */
	float CatchAngle = 0.0f;
	float SinceCatch = 0.0f;
	/** Up past ClearDeg in this reload: ClearDeg is its lower stop until the catch. */
	bool bHeldClear = false;
	/** A reload broken off with it open: the hand shuts it once the pump stroke is through (GetActionCycleSeconds). */
	bool bCatchPending = false;
};

/**
 * Smoke out of the holes of a perforated barrel shroud (the RMB-93's, Docs/RecoilWeapons-Rmb93.md): a
 * puff out of the holes at each shot, then, while the barrel is hot, a thin seep out of them that the
 * gun leaves behind as it moves. In a vacuum it floats on with the shooter's velocity; in air it is
 * slowed by it and, where there is gravity, hot smoke rises. Drawn as UOSMuzzleFxComponent's card puffs.
 */
USTRUCT(BlueprintType)
struct FOSPortSmoke
{
	GENERATED_BODY()

	/** The holes, in ActionMesh's space (the shroud rides the action), cm; none: no port smoke. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OS|Weapon|Smoke")
	TArray<FVector> Ports;

	/** The bore's height in ActionMesh's space, cm: each hole faces away from it, across the bore. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OS|Weapon|Smoke")
	float BoreZCm = 0.0f;

	/** Heat a shot adds and how it dies (an exponential, s): the seep goes as the heat. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OS|Weapon|Smoke", meta = (ClampMin = "0.0"))
	float HeatPerShot = 1.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OS|Weapon|Smoke", meta = (ClampMin = "0.1"))
	float HeatSeconds = 4.0f;

	/** The seep holds at most this much heat's worth (a long burst does not fog the view). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OS|Weapon|Smoke", meta = (ClampMin = "0.0"))
	float MaxHeat = 3.0f;

	/** Puffs out of the holes at the shot, thrown this far, and seeping puffs per second per unit of heat. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OS|Weapon|Smoke", meta = (ClampMin = "0"))
	int32 ShotPuffs = 8;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OS|Weapon|Smoke", meta = (ClampMin = "0.0"))
	float ShotThrowCm = 2.5f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OS|Weapon|Smoke", meta = (ClampMin = "0.0"))
	float SeepPerSecond = 5.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OS|Weapon|Smoke", meta = (ClampMin = "0.0"))
	float SeepThrowCm = 0.8f;

	/** A puff's diameter out of the hole and at the end of its life, its life (each 0.75..1.25), peak opacity and drift, cm/s. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OS|Weapon|Smoke", meta = (ClampMin = "0.0"))
	float StartCm = 1.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OS|Weapon|Smoke", meta = (ClampMin = "0.0"))
	float EndCm = 6.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OS|Weapon|Smoke", meta = (ClampMin = "0.1"))
	float Seconds = 2.2f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OS|Weapon|Smoke", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float Opacity = 0.1f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OS|Weapon|Smoke", meta = (ClampMin = "0.0"))
	float DriftCm = 1.5f;

	/**
	 * The wisps envelop the gun (owner 22.10, «плавно обволакивать оружие»): for about ClingSeconds a wisp lives in the
	 * gun's space - it creeps back along the shroud, CreepCmPerSecond, and swirls round the bore, SwirlCmPerSecond - then
	 * hands over to the world (the air or the vacuum's rules below).
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Jam|Weapon|Smoke", meta = (ClampMin = "0.0"))
	float ClingSeconds = 1.5f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Jam|Weapon|Smoke", meta = (ClampMin = "0.0"))
	float SwirlCmPerSecond = 12.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Jam|Weapon|Smoke", meta = (ClampMin = "0.0"))
	float CreepCmPerSecond = 3.0f;

	/** In air only: the hot smoke's lift as a share of the local gravity (against it), and how fast the air takes its speed, s. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OS|Weapon|Smoke", meta = (ClampMin = "0.0"))
	float RiseShare = 0.05f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OS|Weapon|Smoke", meta = (ClampMin = "0.05"))
	float AirDragSeconds = 0.5f;

	/** Runtime: the heat as of HeatTime (world seconds), and the share of a seeping puff not yet let out. */
	float Heat = 0.0f;
	double HeatTime = 0.0;
	float SeepCarry = 0.0f;

	/** The heat at world time Now. */
	float HeatAt(double Now) const { return Heat * FMath::Exp(-float(Now - HeatTime) / HeatSeconds); }
};

/** One gun. Impulses are kg cm/s (velocity change = impulse / mass). */
USTRUCT(BlueprintType)
struct FOSWeapon
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OS|Weapon")
	FText Name;

	/** Per pellet, before the hit zone's multiplier. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OS|Weapon", meta = (ClampMin = "0.0"))
	float Damage = 25.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OS|Weapon", meta = (ClampMin = "0.01"))
	float FireInterval = 0.15f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OS|Weapon", meta = (ClampMin = "1"))
	int32 MagazineSize = 12;

	/** Rounds outside the magazine at the start. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OS|Weapon", meta = (ClampMin = "0"))
	int32 Reserve = 60;

	/** The whole reload; for a gun that loads round by round (ShellMesh), the seconds per round. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OS|Weapon", meta = (ClampMin = "0.0"))
	float ReloadSeconds = 1.2f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OS|Weapon", meta = (ClampMin = "1"))
	int32 Pellets = 1;

	/** Half-angle of the pellet cone, degrees. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OS|Weapon", meta = (ClampMin = "0.0"))
	float SpreadDeg = 0.0f;

	/** Kick on the shooter per shot, along -view; the pawn decides how much of it moves it. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OS|Weapon", meta = (ClampMin = "0.0"))
	float RecoilImpulse = 1500.0f;

	/** Push on a hit body per pellet, at the hit point. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OS|Weapon", meta = (ClampMin = "0.0"))
	float HitImpulse = 3000.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OS|Weapon", meta = (ClampMin = "100.0"))
	float RangeCm = 8000.0f;

	/** Keeps firing while LMB is held, at FireInterval. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OS|Weapon")
	bool bAutomatic = false;

	/** Round speed, cm/s (near real: 35000 / 40000 / 80000): each pellet flies as a round with a tracer and hits on arrival. 0: hitscan. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OS|Weapon", meta = (ClampMin = "0.0"))
	float ProjectileSpeed = 9000.0f;

	/** The view's kick per shot, degrees up; RecoilSettleFraction of it comes back by itself. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OS|Weapon|Recoil", meta = (ClampMin = "0.0"))
	float KickPitchDeg = 1.5f;

	/** Sideways kick, degrees, randomly signed per shot; settles back like the pitch. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OS|Weapon|Recoil", meta = (ClampMin = "0.0"))
	float KickYawDeg = 0.3f;

	/** The gun in view jumps back this far per shot and springs forward again. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OS|Weapon|Recoil", meta = (ClampMin = "0.0"))
	float ViewKickBackCm = 3.0f;

	/** ...and its muzzle rises this much. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OS|Weapon|Recoil", meta = (ClampMin = "0.0"))
	float ViewKickRiseDeg = 8.0f;

	/** The gun seen in first person; none: nothing in hand. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OS|Weapon|View")
	TObjectPtr<UStaticMesh> ViewMesh = nullptr;

	/** Optional Nixie material slot on ViewMesh; -1 leaves all materials untouched. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OS|Weapon|View", meta = (ClampMin = "-1"))
	int32 NixieMaterialSlot = INDEX_NONE;

	/** Where ViewMesh sits relative to the camera (X forward, Y right, Z up). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OS|Weapon|View")
	FTransform ViewOffset;

	/**
	 * The gun's moving part in view - the pistol's slide, the shotgun's fore-end, the
	 * SV-98's bolt - hung on ViewMesh with its origin at ActionPivot. After every shot
	 * it runs one cycle, ActionTravel and ActionTurn sampled at the seconds since the
	 * shot; the gun cannot fire again before the cycle is over (GetActionCycleSeconds).
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OS|Weapon|Action")
	TObjectPtr<UStaticMesh> ActionMesh = nullptr;

	/** Where ActionMesh's origin sits in ViewMesh's space, cm (the bolt turns about ViewMesh's X through it). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OS|Weapon|Action")
	FVector ActionPivot = FVector::ZeroVector;

	/** The part's travel along ViewMesh's X at ActionTravel 1, cm (signed: back is +X on the USP, -X on the long guns). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OS|Weapon|Action")
	float ActionTravelCm = 0.0f;

	/** The part's turn about ViewMesh's X at ActionTurn 1, degrees (the bolt handle's lift). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OS|Weapon|Action")
	float ActionTurnDeg = 0.0f;

	/** Seconds since the shot -> share of ActionTravelCm. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OS|Weapon|Action")
	FRuntimeFloatCurve ActionTravel;

	/** Seconds since the shot -> share of ActionTurnDeg. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OS|Weapon|Action")
	FRuntimeFloatCurve ActionTurn;

	/**
	 * The whole gun turned this way, in the camera's frame about the grip, at
	 * ActionBody 1: canted and swung across the view while the part works, so a
	 * stroke along the line of sight reads on screen. With PoseSprings it is where
	 * the springs pull, not the pose itself.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OS|Weapon|Action")
	FRotator ActionBodyTurn = FRotator::ZeroRotator;

	/** ...and moved this far, cm in the camera's frame (X forward, Y right, Z up), at ActionBody 1. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OS|Weapon|Action")
	FVector ActionBodyOffsetCm = FVector::ZeroVector;

	/** Seconds since the shot -> share of ActionBodyTurn and ActionBodyOffsetCm (a step: the springs make the motion). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OS|Weapon|Action")
	FRuntimeFloatCurve ActionBody;

	/** The hammer (the USP's), on ViewMesh, turning about ViewMesh's Y through HammerPivot (the mesh's origin is its pin). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OS|Weapon|Parts")
	TObjectPtr<UStaticMesh> HammerMesh = nullptr;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OS|Weapon|Parts")
	FVector HammerPivot = FVector::ZeroVector;

	/** Its turn at HammerTurn 1, degrees about ViewMesh's Y (negative: the top toward -X). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OS|Weapon|Parts")
	float HammerTurnDeg = 0.0f;

	/** Seconds since the shot -> share of HammerTurnDeg: 1 fallen, below 0 pushed back past cocked. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OS|Weapon|Parts")
	FRuntimeFloatCurve HammerTurn;

	/** The spent case thrown out EjectSeconds after each shot, from EjectPort (ViewMesh space, cm); none: no case. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OS|Weapon|Parts")
	TObjectPtr<UStaticMesh> CaseMesh = nullptr;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OS|Weapon|Parts")
	FVector EjectPort = FVector::ZeroVector;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OS|Weapon|Parts", meta = (ClampMin = "0.0"))
	float EjectSeconds = 0.0f;

	/** The case's speed on top of the shooter's, cm/s in the camera's frame (X forward, Y right, Z up). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OS|Weapon|Parts")
	FVector EjectVelocity = FVector(-40.0f, 220.0f, 120.0f);

	/** The case leaves lying along the bore, tumbling end over end about the gun's Y (out of the bottom: the RMB-93); else across, about the view's X. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OS|Weapon|Parts")
	bool bEjectAlongBore = false;

	/**
	 * The magazine, on ViewMesh in its frame, moved by MagTravelCm x MagTravel (seconds
	 * since the reload began). At MagEjectSeconds it leaves as a loose body in the world,
	 * its rounds with it (Ammo 0); the fresh one shows from MagInSeconds, the curve brings
	 * it home and at MagInSoundSeconds, the seat, it is filled from Reserve.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OS|Weapon|Reload")
	TObjectPtr<UStaticMesh> MagMesh = nullptr;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OS|Weapon|Reload")
	FVector MagTravelCm = FVector::ZeroVector;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OS|Weapon|Reload")
	FRuntimeFloatCurve MagTravel;

	/** The magazine's way off the straight line: MagArcCm x MagArc (ViewMesh space), 0 at the well. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OS|Weapon|Reload")
	FVector MagArcCm = FVector::ZeroVector;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OS|Weapon|Reload")
	FRuntimeFloatCurve MagArc;

	/** Its tilt about ViewMesh's Y at MagTurn 1, degrees (straightens as it enters). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OS|Weapon|Reload")
	float MagTurnDeg = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OS|Weapon|Reload")
	FRuntimeFloatCurve MagTurn;

	/** The flick it leaves with, deg/s about ViewMesh's Y (randomised +-25%). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OS|Weapon|Reload")
	float MagEjectSpinDeg = 0.0f;

	/** The point the magazine turns about (MagTurn), ViewMesh space, cm: its top, on the well's axis. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OS|Weapon|Reload")
	FVector MagPivot = FVector::ZeroVector;

	/**
	 * How far out along MagTravelCm the magazine is clear of its well, cm. Nearer in, it only
	 * slides along the well's axis: MagArc and MagTurn fade to nothing over the last 2 cm before.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OS|Weapon|Reload", meta = (ClampMin = "0.0"))
	float MagClearCm = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OS|Weapon|Reload", meta = (ClampMin = "0.0"))
	float MagEjectSeconds = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OS|Weapon|Reload", meta = (ClampMin = "0.0"))
	float MagInSeconds = 0.0f;

	/**
	 * A gun that loads round by round (the pump gun's tube): each round, ReloadSeconds
	 * long, rises from ShellFrom to ShellTo (ViewMesh space, cm; the mesh's base) on
	 * ShellInsert, tilted ShellTiltDeg about Y at the start, and counts at ShellSeatSeconds.
	 * A shot breaks the loading off when the tube holds a round.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OS|Weapon|Reload")
	TObjectPtr<UStaticMesh> ShellMesh = nullptr;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OS|Weapon|Reload")
	FVector ShellFrom = FVector::ZeroVector;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OS|Weapon|Reload")
	FVector ShellTo = FVector::ZeroVector;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OS|Weapon|Reload")
	float ShellTiltDeg = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OS|Weapon|Reload")
	FRuntimeFloatCurve ShellInsert;

	/** The round's way off the straight line, ShellArcCm x ShellArc (ViewMesh space). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OS|Weapon|Reload")
	FVector ShellArcCm = FVector::ZeroVector;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OS|Weapon|Reload")
	FRuntimeFloatCurve ShellArc;

	/** Seconds into the round -> share of ShellTiltDeg: it turns into the port as it is thumbed in. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OS|Weapon|Reload")
	FRuntimeFloatCurve ShellTurn;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OS|Weapon|Reload", meta = (ClampMin = "0.0"))
	float ShellSeatSeconds = 0.0f;

	/** The whole gun during a reload, as ActionBodyTurn/Offset: ReloadBody (the springs' target) rises from the start and falls into the end. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OS|Weapon|Reload")
	FRotator ReloadBodyTurn = FRotator::ZeroRotator;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OS|Weapon|Reload")
	FVector ReloadBodyOffsetCm = FVector::ZeroVector;

	/** Seconds from the reload's start (and to its end, mirrored) -> share of ReloadBodyTurn/Offset; its last key is the lead before the first round. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OS|Weapon|Reload")
	FRuntimeFloatCurve ReloadBody;

	/**
	 * The whole gun's pose is a mass on six damped springs (X, Y, Z, pitch, yaw, roll, as
	 * the scope's AimIn): ActionBody and ReloadBody only set where they pull to, each
	 * channel seeing them Delay late; the turns also trail the gun's motion
	 * (UOSWeaponsComponent::PoseTurnLag), so it moves in arcs. Fewer than 6: no springs.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OS|Weapon|Pose")
	TArray<FOSAimSpring> PoseSprings;

	/** Knocks on the springs after the shot (the pump slammed, the bolt locked). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OS|Weapon|Pose")
	TArray<FOSPoseKick> ShotKicks;

	/** ...after the reload's start (the magazine flicked out, slapped home). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OS|Weapon|Pose")
	TArray<FOSPoseKick> ReloadKicks;

	/** ...after each round's start when loading round by round (the round pushed home). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OS|Weapon|Pose")
	TArray<FOSPoseKick> RoundKicks;

	/**
	 * The slide stop (owner 19.10: «затворная задержка»). A gun with keys here locks its action open on the
	 * last round: that shot's stroke runs SlideLockTravel / SlideLockHammer (seconds since the shot, shares
	 * of ActionTravelCm / HammerTurnDeg; their last keys hold) instead of ActionTravel / HammerTurn, and the
	 * part stays back - the trigger does nothing - until a reload. With the slide locked that is the empty
	 * reload, EmptyReloadSeconds long: the magazine as in the tactical one, then the slide released.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OS|Weapon|SlideLock")
	FRuntimeFloatCurve SlideLockTravel;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OS|Weapon|SlideLock")
	FRuntimeFloatCurve SlideLockHammer;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OS|Weapon|SlideLock", meta = (ClampMin = "0.0"))
	float EmptyReloadSeconds = 0.0f;

	/**
	 * Seconds into the empty reload -> share of ActionTravelCm, from its first key on (it starts at the
	 * locked share): the slide running home. The hammer, resting on the slide, rides forward with it to
	 * cocked. At SlideReleaseSeconds the slide stop is down: the slide is free (no longer locked).
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OS|Weapon|SlideLock")
	FRuntimeFloatCurve SlideReleaseTravel;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OS|Weapon|SlideLock", meta = (ClampMin = "0.0"))
	float SlideReleaseSeconds = 0.0f;

	/** The gun in the empty reload, instead of ReloadBody (the same stages, then held for the thumb and out with the slam). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OS|Weapon|SlideLock")
	FRuntimeFloatCurve EmptyReloadBody;

	/** Knocks on the pose springs in the empty reload, on top of ReloadKicks (the thumb on the slide stop, the slam). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OS|Weapon|SlideLock")
	TArray<FOSPoseKick> SlideReleaseKicks;

	/** The slide stop pressed, started at SlideReleaseSoundSeconds into the empty reload. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OS|Weapon|SlideLock")
	TObjectPtr<USoundBase> SlideReleaseSound = nullptr;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OS|Weapon|SlideLock", meta = (ClampMin = "0.0"))
	float SlideReleaseSoundSeconds = 0.0f;

	/** The slide slamming home, started at SlideHomeSoundSeconds at this pitch. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OS|Weapon|SlideLock")
	TObjectPtr<USoundBase> SlideHomeSound = nullptr;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OS|Weapon|SlideLock", meta = (ClampMin = "0.0"))
	float SlideHomeSoundSeconds = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OS|Weapon|SlideLock", meta = (ClampMin = "0.1"))
	float SlideHomeSoundPitch = 1.0f;

	/** Runtime: the slide is held open by the slide stop (the last round went); a reload releases it. */
	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Transient, Category = "OS|Weapon|SlideLock")
	bool bSlideLocked = false;

	/** The gun has a slide stop (SlideLockTravel has keys). */
	bool CanLockOpen() const;

	/** Mechanism sounds, 2D, none by default: the hammer HammerSoundSeconds after a shot. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OS|Weapon|Sound")
	TObjectPtr<USoundBase> HammerSound = nullptr;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OS|Weapon|Sound", meta = (ClampMin = "0.0"))
	float HammerSoundSeconds = 0.0f;

	/** The magazine let go, at MagEjectSeconds of the reload. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OS|Weapon|Sound")
	TObjectPtr<USoundBase> MagOutSound = nullptr;

	/** The fresh magazine seated, at MagInSoundSeconds of the reload. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OS|Weapon|Sound")
	TObjectPtr<USoundBase> MagInSound = nullptr;

	/** The seat: MagInSound plays and the rounds count (a magazine gun; a round-by-round one counts at ShellSeatSeconds). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OS|Weapon|Sound", meta = (ClampMin = "0.0"))
	float MagInSoundSeconds = 0.0f;

	/** Each round pushed into the tube, at its ShellSeatSeconds. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OS|Weapon|Sound")
	TObjectPtr<USoundBase> ShellInsertSound = nullptr;

	/** A hinged loading lid (the RMB-93's top window): struck open at a reload's start, pressed shut at its end. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OS|Weapon|Lid")
	FOSLid Lid;

	/** Smoke out of a perforated shroud's holes (the RMB-93's). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OS|Weapon|Smoke")
	FOSPortSmoke PortSmoke;

	/**
	 * Putting the gun away and drawing it (owner 19.10: the pistol to a holster on the right
	 * hip, the shotgun over the right shoulder, the SV-98 over the left): the pose springs
	 * pull toward StowOffsetCm / StowTurn (out of view, camera frame) by StowPath (seconds
	 * into the put-away; its last key: gone) and back by DrawPath (seconds into the draw,
	 * from 1 to 0; its last key: drawn). The turns see both paths StowTurnLead s early
	 * (positive: the muzzle leads, negative: it trails the grip).
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OS|Weapon|Switch")
	FVector StowOffsetCm = FVector::ZeroVector;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OS|Weapon|Switch")
	FRotator StowTurn = FRotator::ZeroRotator;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OS|Weapon|Switch")
	FRuntimeFloatCurve StowPath;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OS|Weapon|Switch")
	FRuntimeFloatCurve DrawPath;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OS|Weapon|Switch")
	float StowTurnLead = 0.0f;

	/**
	 * A slung long gun's way (owner 19.10): StowMidShare > 0 bends the place path through
	 * StowMidCm - rest -> Mid over path shares 0..StowMidShare, Mid -> StowOffsetCm after -
	 * so the rifle comes down vertical into the frame's side before it levels. With keys in
	 * StowTurnPath / DrawTurnPath the turns follow those (seconds -> share of StowTurn)
	 * instead of the place path shifted by StowTurnLead, with no spring Delay on them: the
	 * keys carry the timing, so a turn can start on the draw's first frame.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OS|Weapon|Switch")
	FVector StowMidCm = FVector::ZeroVector;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OS|Weapon|Switch", meta = (ClampMin = "0.0", ClampMax = "0.95"))
	float StowMidShare = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OS|Weapon|Switch")
	FRuntimeFloatCurve StowTurnPath;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OS|Weapon|Switch")
	FRuntimeFloatCurve DrawTurnPath;

	/** Seconds into the draw from which the gun may fire. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OS|Weapon|Switch", meta = (ClampMin = "0.0"))
	float DrawReadySeconds = 0.3f;

	/** Knocks on the pose springs, seconds into the put-away / the draw. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OS|Weapon|Switch")
	TArray<FOSPoseKick> StowKicks;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OS|Weapon|Switch")
	TArray<FOSPoseKick> DrawKicks;

	/** The shoulder taking a heavy gun: a nudge of the view (pitch, yaw, degrees; it comes back) at these moments. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OS|Weapon|Switch")
	FRotator StowViewNudge = FRotator::ZeroRotator;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OS|Weapon|Switch")
	float StowViewNudgeSeconds = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OS|Weapon|Switch")
	FRotator DrawViewNudge = FRotator::ZeroRotator;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OS|Weapon|Switch")
	float DrawViewNudgeSeconds = 0.0f;

	/** Holster / sling sounds, 2D, at these seconds into the put-away and the draw. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OS|Weapon|Sound")
	TObjectPtr<USoundBase> StowSound = nullptr;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OS|Weapon|Sound")
	float StowSoundSeconds = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OS|Weapon|Sound")
	TObjectPtr<USoundBase> DrawSound = nullptr;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OS|Weapon|Sound")
	float DrawSoundSeconds = 0.0f;

	/** The shot, 2D. Space-treated offline (Scripts/os_space_shots.py): muffled, reverberant, with a sub. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OS|Weapon|Sound")
	TObjectPtr<USoundBase> ShotSound = nullptr;

	/**
	 * In air only (UOSWeaponsComponent::PlayShotSound), over ShotSound at its pitch: the crack the suit cuts -
	 * the donor's first milliseconds with their highs (Scripts/os_air_sounds.py).
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OS|Weapon|Sound")
	TObjectPtr<USoundBase> ShotAirSound = nullptr;

	/** The pump or bolt cycling after a shot, 2D, CycleDelaySeconds after it; none: no cycle (the pistol). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OS|Weapon|Sound")
	TObjectPtr<USoundBase> CycleSound = nullptr;

	/** When the cycle sound starts after the shot; its clicks match the keys of ActionTravel / ActionTurn. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OS|Weapon|Sound", meta = (ClampMin = "0.0"))
	float CycleDelaySeconds = 0.0f;

	/**
	 * The ammo display (NixieMaterialSlot) counts the round off CounterSeconds after the shot, on the cycle's
	 * clock: a pump's home, the next round chambered (the tube's count drops then, and the eye is back on the gun
	 * after the flash - owner 22.10: a change under the flash was never seen); 0: at the shot, as the pistol.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OS|Weapon", meta = (ClampMin = "0.0"))
	float CounterSeconds = 0.0f;

	/** The dry click when it fires on an empty magazine, 2D; none with the slide locked open (the trigger is dead there). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OS|Weapon|Sound")
	TObjectPtr<USoundBase> EmptySound = nullptr;

	/** Usable now. Set from bStartWithAllWeapons on BeginPlay; pickups unlock (UnlockWeapon). */
	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Transient, Category = "OS|Weapon")
	bool bUnlocked = true;

	/** Rounds in the magazine now. Set to MagazineSize on BeginPlay. */
	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Transient, Category = "OS|Weapon")
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
 * The guns of a OS pawn - the zero-G pawn and the test room's character
 * carry the same one: 1 pistol, 2 shotgun, 3 sniper; unlocks, magazines and
 * reserves, R reload (never by itself), 1/2/3 and the wheel select, one shot per
 * LMB click - fire while held only for a bAutomatic gun, none by default (the pawn
 * binds LMB: it also throws).
 *
 * Each pellet is a round flying at ProjectileSpeed with a tracer (or a
 * hitscan at 0): every tick its path is traced on the Shot channel and a hit
 * goes through MKResolveImpactFromHit + MKDeliverResolvedImpact, plus
 * HitImpulse on every simulated body hit. Pellets of one shot land at
 * slightly different times; AOSLivingWall gathers them into one blast.
 *
 * The shooter's push is not applied here: Fire returns it and the pawn applies
 * it its own way (the zero-G body takes all of it, the test room's character a
 * small fraction). The view's kick is applied here, through the owner's control
 * rotation, and the gun in view (ViewMesh, on the owner's camera) kicks too and
 * runs its moving part's cycle (ActionMesh), which also gates the next shot.
 */
UCLASS(ClassGroup = (OS), meta = (BlueprintSpawnableComponent))
class MINDKORE_API UOSWeaponsComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UOSWeaponsComponent();

	/** 1 pistol, 2 shotgun, 3 sniper by default. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OS|Weapons")
	TArray<FOSWeapon> Weapons;

	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Transient, Category = "OS|Weapons")
	int32 CurrentWeapon = 0;

	/** Off (the ship map): every gun starts locked until a pickup unlocks it. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OS|Weapons")
	bool bStartWithAllWeapons = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OS|Weapons")
	TObjectPtr<UMKImpactDataAsset> ImpactData = nullptr;

	/**
	 * Air (Docs/RecoilWeapons-Sound.md, «В воздухе»): while the owner breathes (IsInAir) every gun sound is also sent
	 * here at AirReverbSend - SM_OS_CabinReverb, a short metal room. A sound keeps the medium it started in.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OS|Weapons|Air")
	TObjectPtr<USoundSubmixBase> AirReverb = nullptr;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OS|Weapons|Air", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float AirReverbSend = 0.3f;

	/** In air every sound but the shot (mechanisms, magazines, the slide stop, holster and sling) plays through this: a light lift of the highs. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OS|Weapons|Air")
	TObjectPtr<USoundEffectSourcePresetChain> AirLift = nullptr;

	/** The tracer: this mesh (the engine sphere) stretched along the flight, in this material (MindKore's M_MK_Tracer). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OS|Weapons|Tracer")
	TObjectPtr<UStaticMesh> TracerMesh = nullptr;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OS|Weapons|Tracer")
	TObjectPtr<UMaterialInterface> TracerMaterial = nullptr;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OS|Weapons|Tracer", meta = (ClampMin = "1.0"))
	float TracerLengthCm = 120.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OS|Weapons|Tracer", meta = (ClampMin = "0.1"))
	float TracerThicknessCm = 3.0f;

	/** Rounds fly from the eye; their tracers start at the muzzle and join that path over this distance. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OS|Weapons|Tracer", meta = (ClampMin = "1.0"))
	float TracerConvergeCm = 600.0f;

	/** The view's kick arrives over this, not in one frame (MindKore's CameraRecoilAttackSeconds). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OS|Weapons|Recoil", meta = (ClampMin = "0.0"))
	float RecoilAttackSeconds = 0.03f;

	/** Share of each kick that comes back by itself; the rest stays (the player brings the view down). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OS|Weapons|Recoil", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float RecoilSettleFraction = 0.7f;

	/** Degrees per second the settling share comes back at, linearly (MindKore's CameraRecoilRecoverySpeed). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OS|Weapons|Recoil", meta = (ClampMin = "0.1"))
	float RecoilRecoverDegPerSec = 13.0f;

	/** How fast the gun in view springs back after a kick, 1/s. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OS|Weapons|Recoil", meta = (ClampMin = "0.1"))
	float ViewKickReturnRate = 12.0f;

	/** The gun in view, on the owner's camera; made on BeginPlay, its mesh follows the gun in hand. */
	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Transient, Category = "OS|Weapons")
	TObjectPtr<UStaticMeshComponent> ViewModel = nullptr;

	/** Created only for a weapon opting into NixieMaterialSlot; fed by equip/shot/reload events. */
	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Transient, Category = "OS|Weapons")
	TObjectPtr<UOSNixieCounterComponent> AmmoCounter = nullptr;

	/** The gun's moving part (FOSWeapon::ActionMesh), on ViewModel. */
	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Transient, Category = "OS|Weapons")
	TObjectPtr<UStaticMeshComponent> ActionInView = nullptr;

	/** The hammer, the magazine and the round being loaded, on ViewModel (OSWeaponParts.cpp). */
	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Transient, Category = "OS|Weapons")
	TObjectPtr<UStaticMeshComponent> HammerInView = nullptr;

	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Transient, Category = "OS|Weapons")
	TObjectPtr<UStaticMeshComponent> MagInView = nullptr;

	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Transient, Category = "OS|Weapons")
	TObjectPtr<UStaticMeshComponent> ShellInView = nullptr;

	/** A hinged loading lid (FOSWeapon::Lid), on ViewModel at its hinge (OSWeaponLid.cpp). */
	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Transient, Category = "OS|Weapons")
	TObjectPtr<UStaticMeshComponent> LidInView = nullptr;

	/** The lid of the gun in view, or null (none, or no gun shown); and its knocks on a stop since play began (probes). */
	const FOSLid* GetShownLid() const;
	int32 GetLidKnocks() const { return LidKnocks; }

	/** Spent cases and dropped magazines kept in the world at most; the oldest goes first. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OS|Weapons|Parts", meta = (ClampMin = "0"))
	int32 MaxDebris = 48;

	/** ...and any farther than this from the shooter goes. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OS|Weapons|Parts", meta = (ClampMin = "100.0"))
	float DebrisRangeCm = 5000.0f;

	/** Loose cases and magazines in the world now. */
	int32 GetDebrisCount() const { return Debris.Num(); }
	/** The case or magazine thrown last, if it is still there (stills). */
	AActor* GetLastDebris() const { return Debris.IsEmpty() ? nullptr : Debris.Last().Get(); }

	/** A drawn gun fires only once its pose is this close to rest (and DrawReadySeconds into the draw). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OS|Weapons|Pose", meta = (ClampMin = "0.0"))
	float DrawReadyPlaceCm = 4.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OS|Weapons|Pose", meta = (ClampMin = "0.0"))
	float DrawReadyTurnDeg = 10.0f;

	/** The gun's turns trail its motion: pitch per cm/s up (it dips while rising), yaw and roll per cm/s right. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OS|Weapons|Pose")
	FVector PoseTurnLag = FVector(0.12f, 0.06f, 0.1f);

	/** Pitch and roll damping (at least) while the gun swings back out of a reload: it floats past by half as much there. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OS|Weapons|Pose", meta = (ClampMin = "0.05", ClampMax = "2.0"))
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
		int32 SwitchPhase = 0, bool bEmpty = false) const;

	/** Every gun's aim and the SV-98's scope (OSScope.h): made on BeginPlay; UpdateViewModel takes the shown gun's aimed pose from it (AimPose). */
	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Transient, Category = "OS|Weapons")
	TObjectPtr<UOSScopeComponent> Scope = nullptr;

	/**
	 * The muzzle of the gun in view, world space, as it is drawn this frame (kick
	 * included): X out of the bore, along the gun's own line. ViewModel's Muzzle
	 * socket (every gun has one); without a gun in view, the owner's location and
	 * view. For flash and smoke; rounds fly from the eye, not from here.
	 */
	UFUNCTION(BlueprintPure, Category = "OS|Weapons")
	FTransform GetMuzzleTransform() const;

	/** Rounds in flight now. */
	int32 GetRoundsInFlight() const { return Rounds.Num(); }

	/** Used when the resolved feedback has no decal of its own. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OS|Weapons")
	TObjectPtr<UMaterialInterface> DefaultImpactDecal = nullptr;

	/**
	 * Fire the current gun from Start along Direction (the pawn's view). OutRecoil
	 * is the kick on the shooter, kg cm/s, for the pawn to apply. False when it
	 * cannot fire now (nothing unlocked, interval, reload, empty or the slide locked; neither starts a reload: R does).
	 */
	bool Fire(const FVector& Start, const FVector& Direction, FVector& OutRecoil);

	/** The gun the hands hold or take next (a switch, when one is in hand). Not the hands: AOSZeroGPawn::PressSlot. */
	UFUNCTION(BlueprintCallable, Category = "OS|Weapons")
	void SelectWeapon(int32 Index);

	/** The next unlocked gun Step's way (the wheel). */
	void SelectStep(int32 Step);

	UFUNCTION(BlueprintCallable, Category = "OS|Weapons")
	void Reload();

	/** Unlock a gun and take it in hand. */
	UFUNCTION(BlueprintCallable, Category = "OS|Weapons")
	void UnlockWeapon(int32 Index);

	/** Install a pickup's definition before UnlockWeapon. A fresh pickup starts with a full magazine. */
	bool ApplyPickupWeapon(int32 Index, const FOSWeapon& Definition);

	UFUNCTION(BlueprintPure, Category = "OS|Weapons")
	bool IsWeaponUnlocked(int32 Index) const { return Weapons.IsValidIndex(Index) && Weapons[Index].bUnlocked; }

	/** The gun in hand, or null: none unlocked yet, or the owner's hands hold something else (AOSZeroGPawn::GetHands). */
	const FOSWeapon* GetWeapon() const;

	/**
	 * The hands leave the gun (AOSZeroGPawn::SetHands): its reload is abandoned. At once, the gun
	 * leaves the view in the same frame (owner 18-19.10: a grab); otherwise UpdateSwitch puts it away
	 * on its path (a hull tool). The hands coming back bring it in with a draw.
	 */
	void Holster(bool bAtOnce);

	/** Back from unarmed (a held body let go): the current gun, logged and drawn (AOSZeroGPawn::SetHands). */
	void Rearm();

	/**
	 * A gun coming back into hand out of unarmed (Rearm): logged. The draw itself is UpdateSwitch's
	 * (OSWeaponParts.cpp), as for any switch - the gun, wanted again, comes in from away along its
	 * own path. Fire waits for IsDrawing.
	 */
	void Draw();

	/** A put-away or draw still running for the gun in hand: no shot yet (!IsWeaponReady). */
	bool IsDrawing() const;

	/** The current gun is drawn far enough to fire (DrawReadySeconds) and no put-away is wanted. */
	UFUNCTION(BlueprintPure, Category = "OS|Weapons")
	bool IsWeaponReady() const;

	/** For probes and stands: any put-away / draw finished at once - the current gun drawn and ready. */
	void FinishSwitch();

	/** The gun drawn in view now (the old one while it goes away), or INDEX_NONE. */
	int32 GetShownWeapon() const { return ShownWeapon; }

	/** Spent cases thrown since play began (probes: a switch throws none). */
	int32 GetCasesThrown() const { return CasesThrown; }

	/** Cycle sounds (pump, bolt) played since play began (probes). */
	int32 GetCycleSounds() const { return CycleSoundsPlayed; }

	/**
	 * The one place the guns read the medium: the owner breathes (UOSOxygenComponent::IsBreathable, which the game
	 * mode sets each tick from OSAirZoneAt - the flyers read the same). No oxygen component: vacuum.
	 */
	bool IsInAir() const;

	/** Gun sounds started since play began in air / in vacuum, a shot's crack included (probes). */
	int32 GetSoundsInAir() const { return SoundsInAir; }
	int32 GetSoundsInVacuum() const { return SoundsInVacuum; }


	/** Seconds left of a reload; 0 when not reloading. */
	float GetReloadRemaining() const;

	/** The HUD line: «[n] Name ammo/reserve» (and the reload; also while a hull tool is out), or «без оружия» (holding a body, none unlocked). */
	FString Describe() const;

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
	bool TraceShot(const FOSWeapon& Weapon, const FVector& Start, const FVector& End, const FVector& Direction, FVector* OutImpact = nullptr);
	/** Where a round's tracer streak ends in front. */
	FVector TracerHead(const FRound& Round) const;
	void UpdateRounds(float DeltaTime);
	void UpdateRecoil(float DeltaTime);
	void UpdateViewModel(float DeltaTime);
	void SetViewMeshes(const FOSWeapon* Weapon);
	void UpdateCounterAmmo();
	/** The display powers once the gun is half-way into frame (the draw), not when its mesh is set behind the shoulder: its warm-up is seen. */
	void PowerCounter();
	bool bCounterPowerPending = false;
	/** Turn the owner's view by degrees in the view's own frame. */
	void TurnView(float PitchDeg, float YawDeg) const;
	/** The owner's hands hold the gun (AOSZeroGPawn::GetHands); an owner without hands always. */
	bool IsInHand() const;

	// OSWeaponParts.cpp: the hammer, cases, magazines and round-by-round loading.
	/** Fill the guns' parts and reload curves; from the constructor (ConstructorHelpers). */
	void SetupWeaponParts();
	void MakePartComponents(USceneComponent& Parent);
	/** Start the reload's timeline; returns its length. */
	float StartReloadTimeline(const FOSWeapon& Weapon);
	/** Seconds into the reload, or < 0 when none (os.Weapons.ReloadPoseAt holds one). */
	float GetReloadTime() const;
	/** Pose the parts for this frame; null: hide them. */
	void UpdateParts(const FOSWeapon* Weapon, float SinceShot);
	/** The empty reload (the slide released at its end) runs or is held (os.Weapons.ReloadPoseAt with the slide locked). */
	bool IsEmptyReload(const FOSWeapon& Weapon) const;
public:
	/** The moving part's share of ActionTravelCm (X) and the hammer's of HammerTurnDeg (Y) at shot time Since: the cycle, a slide lock, its release. */
	FVector2D GetSlideAndHammer(const FOSWeapon& Weapon, float Since) const;
	/** The magazine on ViewMesh at Time into a reload (the one path its place and turn take). */
	static FTransform GetMagPose(const FOSWeapon& Weapon, float Time);
private:
	/** The shot's events the running gun's cycle clock crossed this frame: its case, hammer and cycle sound. */
	void UpdateCycleEvents();
	/** Reload events crossed since the last tick: the magazine let go and seated, a round counted, their sounds. */
	void UpdatePartEvents(float DeltaTime);
	// OSWeaponLid.cpp: the hinged lid and the shroud's smoke.
	/** The lid of Weapon (shown) this frame: the hand's events, its swing on its hinge as GunWorld (the gun posed, without the drawn kick) moves. */
	void UpdateLid(FOSWeapon& Weapon, const FTransform& GunWorld, float DeltaTime);
	/** The shroud's seep while the barrel is hot (FOSPortSmoke), from the gun in view. */
	void UpdatePortSmoke(FOSWeapon& Weapon, float DeltaTime);
	/** The shot's share: the barrel's heat and the puff out of the holes. */
	void PortSmokeShot(FOSWeapon& Weapon);
	/** The local gravity the lid feels, world cm/s^2: the zones' (OSGravityAt), the world's for a walking character; read every 0.25 s and at once when the global scale changes. */
	FVector GetGunGravity();
	/** The reload in progress strikes the lid open (it began latched). */
	bool bReloadStrikesLid = true;
	/** A shot since the lid's last update (ShotSpeedDeg). */
	bool bLidShot = false;
	/** The lid's frame a tick ago and before (finite differences: the hinge's acceleration and the gun's spin). */
	int32 LidTrackWeapon = INDEX_NONE;
	int32 LidTrackFrames = 0;
	FVector LidPrevHinge = FVector::ZeroVector;
	FVector LidPrevVelocity = FVector::ZeroVector;
	FQuat LidPrevRotation = FQuat::Identity;
	FVector LidPrevSpin = FVector::ZeroVector;
	float LidPrevReload = -1.0f;
	double LidLastKnockSeconds = -1.0;
	int32 LidKnocks = 0;
	FVector GunGravity = FVector::ZeroVector;
	double GunGravitySeconds = -1.0;
	float GunGravityScale = -1.0f;
	TWeakObjectPtr<UOSMuzzleFxComponent> MuzzleFx;
	void SpawnDebris(UStaticMesh* Mesh, const FTransform& Where, const FVector& Velocity, const FVector& SpinDegPerSec, float MassKg);

	TArray<FRound> Rounds;
	TArray<TWeakObjectPtr<AActor>> Debris;
	double ReloadStartSeconds = 0.0;
	/** Rounds this reload loads one by one (ShellMesh guns). */
	int32 ShellsPlanned = 0;
	/** This reload releases a locked slide (FOSWeapon::EmptyReloadSeconds); it started this far in (a magazine already seated: the release only). */
	bool bEmptyReload = false;
	float ReloadFromSeconds = 0.0f;
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
	void StepPose(const FOSWeapon& Weapon, FPoseState& State, float Since0, float Since1, float Reload0, float Reload1,
		float ReloadLength, int32 Shells, float DeltaTime, const FSwitchSpan& Switch = FSwitchSpan(), bool bEmpty = false, bool bLidStrike = true) const;
	/** Run the switch this frame; returns the gun to show (null: away) and the span the springs step over. */
	const FOSWeapon* UpdateSwitch(float DeltaTime, bool bHandsBusy, FSwitchSpan& OutSpan);
	/** Unarmed at once: no gun in view, the next draw from away. */
	void ClearView();
	ESwitch SwitchStage = ESwitch::Away;
	float SwitchTime = 0.0f;
	FSwitchSpan PoseSwitch;
	/** Live, or re-run from rest for os.Weapons.PoseAt / ReloadPoseAt. */
	void UpdatePose(const FOSWeapon& Weapon, float DeltaTime);
	FPoseState Pose;
	int32 PoseWeapon = INDEX_NONE;
	float PosePrevSince = 1000.0f;
	float PosePrevReload = -1.0f;
	float KickPitchLeft = 0.0f;
	float KickYawLeft = 0.0f;
	float KickAttackLeft = 0.0f;
	float SettlePitch = 0.0f;
	float SettleYaw = 0.0f;
	/** Seconds since the last shot, for the kick in view (the parts' cycle runs on FOSWeapon::SinceShot). */
	float SinceKickSeconds = 1000.0f;
	int32 ShownWeapon = INDEX_NONE;
	/** The gun whose cycle clock runs: the shown one while drawn or being drawn, else none. */
	int32 CycleWeapon = INDEX_NONE;
	/**
	 * Gun.MechanismSound, Into seconds into its file: 0 on its crossing (a long frame plays it late rather than
	 * skip its click), mid-way when a frozen cycle picks up again.
	 */
	void PlayMechanismSound(FOSWeapon& Gun, USoundBase* Sound, float Into);
	/**
	 * Every gun sound but the shot starts here, 2D: in air with the lift (AirLift) and the room's send. Null
	 * Sound: nothing. The medium is read once, at the start, and kept for the sound's whole length.
	 */
	UAudioComponent* PlayGunSound(USoundBase* Sound, float Volume = 1.0f, float Pitch = 1.0f, float StartSeconds = 0.0f);
	/** The shot: ShotSound as it is; in air ShotAirSound over it at the same pitch, both into the room. */
	void PlayShotSound(const FOSWeapon& Weapon, float Pitch);
	/** Starts one sound 2D in the medium given (no send or lift in vacuum), counted. */
	UAudioComponent* StartGunSound(USoundBase* Sound, float Volume, float Pitch, float StartSeconds, bool bAir, bool bLift);
	int32 SoundsInAir = 0;
	int32 SoundsInVacuum = 0;

	double ReloadEndSeconds = 0.0;
	/** R pressed during the shot's cycle: the reload starts when it is through (a shot or a switch drops it). */
	bool bReloadQueued = false;
};

/**
 * The RMB-93 «Рысь» in the pump shotgun's slot (OSRmb93.cpp, Docs/RecoilWeapons-Rmb93.md): built from the pump
 * shotgun's definition with its own meshes, forward pump stroke, bottom ejection, top loading window with its
 * hinged lid, perforated shroud and Nixie counter. A pickup with Gun = Rmb93 puts it in.
 */
MINDKORE_API FOSWeapon OSMakeRmb93(const FOSWeapon& PumpShotgun);

/** The RMB-93's sight line on the pump shotgun's aim (its springs kept). */
MINDKORE_API FOSGunAim OSMakeRmb93Aim(const FOSGunAim& PumpAim);
