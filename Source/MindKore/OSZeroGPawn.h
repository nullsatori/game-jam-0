#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Pawn.h"
#include "OSCollisionImpact.h"
#include "OSHullTools.h"
#include "OSWeapons.h"
#include "MKImpactReceiver.h"
#include "OSZeroGPawn.generated.h"

class AOSLadder;

class UCameraComponent;
class UInstancedStaticMeshComponent;
class UInputAction;
class UInputMappingContext;
class UOSHullToolsComponent;
class UOSOxygenComponent;
class UOSWeldProjectorComponent;
class UOSSuitProjectorComponent;
class UOSWeaponsComponent;
class UMaterialInstanceDynamic;
class UMaterialInterface;
class UPhysicsConstraintComponent;
class UMKInteractionComponent;
class UMKKeyringComponent;
class UMKVitalsComponent;
class UPhysicalMaterial;
class USoundBase;
class USphereComponent;
struct FInputActionValue;

/** What RMB does now (AOSZeroGPawn::Use; the HUD shows it). */
UENUM(BlueprintType)
enum class EOSRmbAction : uint8
{
	None,
	/** A gun in hand, nothing loose in reach: hold to aim. */
	Aim,
	/** A loose body in reach: take it (a gun in hand goes away - unarmed). */
	Grab,
	/** Holding a body or the hull: let go. */
	LetGo,
	/** Unarmed or a hull tool, nothing loose in reach: hold on to what is under the crosshair. */
	Cling,
};

/**
 * How the body moves now (AOSZeroGPawn::SetMode, one case each in Tick).
 * What each mode lets the hands and keys do is one table, AOSZeroGPawn::Allows.
 */
UENUM(BlueprintType)
enum class EOSMoveMode : uint8
{
	/** Zero-G: momentum is kept; WASD, Space and Ctrl are the thrusters, X the stabiliser. */
	Float,
	/** A gravity zone: gravity pulls, WASD walks on the ground, Space jumps. */
	Walk,
	/** On a ladder's rungs (gravity only): the keys climb, the body moves by sweeps. */
	Ladder,
	/** Holding on to a fixed thing (Cling): the keys are the arm; gravity still pulls, nothing walks. */
	Hang,
};

/**
 * What the hands hold (AOSZeroGPawn::SetHands, the one writer). The guns
 * (UOSWeaponsComponent) and the hull tools (UOSHullToolsComponent) read it;
 * what each key does with it is one table, AOSZeroGPawn::Route.
 */
UENUM(BlueprintType)
enum class EOSHands : uint8
{
	/** The current gun (Guns->CurrentWeapon), drawn or being drawn. */
	Gun,
	/** A hull tool (HullTools->GetTool); the gun is put away. */
	Tool,
	/** Both hands on a loose body (Grab), or unarmed at once with nothing in them (probes). */
	Held,
	/** Free and no gun picked up yet: read only - GetHands reports Gun this way until a gun is unlocked. */
	Empty,
};

/** A key the hands answer (AOSZeroGPawn::Press): a column of AOSZeroGPawn::Route's table. */
enum class EOSHandKey : uint8
{
	/** LMB pressed. */
	Primary,
	/** LMB down, every frame (an automatic gun). */
	PrimaryHeld,
	/** RMB pressed. */
	Secondary,
	/** The mouse wheel, a notch. */
	Wheel,
	/** R. */
	Reload,
};

/** What a key does now (AOSZeroGPawn::Route). */
enum class EOSHandAction : uint8
{
	None,
	/** The gun fires (Fire). */
	Fire,
	/** The gun fires while LMB is held, if it is automatic. */
	AutoFire,
	Reload,
	/** The next gun the wheel's way (holding a body: let go, that gun drawn). */
	NextGun,
	/** RMB with a gun: aim, or take a loose body in reach (Use). */
	Aim,
	/** RMB otherwise: take a loose body, hold on to the hull, let go (Use). */
	Hands,
	/** LMB holding a body: throw it (ThrowHeld). */
	Throw,
	ToolPrimary,
	ToolSecondary,
	/** The wheel with the strap: reel the last line. */
	Reel,
	/** LMB with the attachment menu up: apply, as letting Tab go (Docs/Attachments.md). */
	MenuApply,
	/** The wheel with the attachment menu up: the next row. */
	MenuSelect,
};

/** What a movement mode allows (AOSZeroGPawn::Allows). */
struct FOSMoveRules
{
	bool bFire = true;
	/** RMB: aim, grab, let go, hold on. */
	bool bUse = true;
	/** Left Alt hooks (letting go is always allowed). */
	bool bGrapple = true;
	/** Space as a jump: off the ground, the jet exit, off the ladder. */
	bool bJump = false;
	/** WASD, Space/Ctrl and X as the thrusters and the stabiliser. */
	bool bThrusters = false;
};

/**
 * The zero-G player: a simulated sphere with its rotation locked, gravity
 * off and no damping, so momentum is kept until something changes it. The
 * camera follows the control rotation (free look in zero-G turns only the view).
 *
 * Ways to move, strongest first: push off what is in reach (RMB) - against a
 * wall you fly, against light junk the junk flies; throw what you hold (LMB),
 * taking the opposite momentum; the recoil of a shot (LMB, empty hands).
 *
 * Holding is an arm (UpdateArm, owner 19.10: «физика когда схватываешься уже
 * вайт накл»): the hand holds a point - on the hull (Cling) or on a loose body
 * (Grab) - and the shoulder (the eye) hangs from it on a stiff, springy reach.
 * Only the reach is held: around the hand the body swings and keeps its
 * momentum, and letting go keeps the velocity. On a fixed hold the thrust keys
 * are the arm (pull toward the hand, swing across it); a held loose body is the
 * second mass on the same arm (the hand steers it to the view), so a heavy part
 * drags the pawn as much as the pawn drags it. RMB lets go; LMB throws with
 * equal and opposite impulses.
 *
 * Towing and hull repair: the strap and the welder in hand (HullTools, keys
 * 4/5, Docs/HullRepair.md). The rope (AttachTether: a soft spherical distance
 * limit) has no key since the strap took over towing; probes still use it.
 *
 * LMB and RMB act on press (the pistol fires while LMB is held). Holding
 * Left Shift aims: the pawn draws where a push and the current gun's recoil
 * would send it, or where a throw sends the held prop - a dotted line (an
 * arc under a zone's gravity), the first contact marked, one bounce after it.
 *
 * Only the mouse turns the view; the body's rotation is locked, so hits, the
 * grab and the rope move it but never turn it.
 *
 * Crashes count: a hit above FeedbackMinSpeedCm thumps (sound, camera shake),
 * the blow is delivered to what was hit (CrashImpact, the same path as a
 * round), and above CrashDamageMinSpeedCm the pawn hurts itself.
 * Thrusters (WASD, Space/Ctrl, view-relative) are the precise movement: they
 * accelerate along the input direction until the velocity along it reaches
 * ThrustMaxSpeedCm and never brake anything else, so a push or a recoil on
 * top is not cancelled. X stabilises: a smooth brake to standstill.
 *
 * Space dust (a cube of tiny specks wrapped around the camera) shows motion.
 *
 * All velocity changes go into the body as impulses in kg cm/s, so they read
 * the body's mass (default 100 kg): the sniper's recoil 40000 is 4 m/s.
 *
 * Guns are movement: the shared UOSWeaponsComponent (Guns; 1/2/3, mouse
 * wheel, R). Every shot kicks the pawn back along the view by the gun's full
 * RecoilImpulse and pushes what it hits. Guns cost no oxygen.
 *
 * The same body walks: on the ship map AOSShipGameMode gives it the gravity
 * of the zone it is in (cabin and airlock straight down, the planetoid toward
 * its centre). There a force pulls the body down, a short sphere sweep along
 * gravity finds the ground, WASD steers the velocity along the ground (fast
 * acceleration and braking), Space jumps - and Space in the air over the
 * planetoid is the jet exit. The camera rises to eye height. Recoil, pushes
 * and throws still go into the body whole: a big gun fired at the floor lifts
 * you. The view's frame (AOSShipController) follows gravity.
 */
UCLASS()
class MINDKORE_API AOSZeroGPawn : public APawn, public IMKImpactReceiver
{
	GENERATED_BODY()

public:
	AOSZeroGPawn();

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "OS")
	TObjectPtr<USphereComponent> Body = nullptr;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "OS")
	TObjectPtr<UCameraComponent> Camera = nullptr;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "OS")
	TObjectPtr<UMKInteractionComponent> Interaction = nullptr;

	/** Tiny specks kept in a cube around the camera (world space), streaming past as you move. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "OS")
	TObjectPtr<UInstancedStaticMeshComponent> Dust = nullptr;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "OS")
	TObjectPtr<UMKKeyringComponent> Keyring = nullptr;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "OS")
	TObjectPtr<UMKVitalsComponent> Vitals = nullptr;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "OS")
	TObjectPtr<UOSOxygenComponent> Oxygen = nullptr;

	/** The guns (shared with the walkers); the recoil of a shot goes into Body whole. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "OS")
	TObjectPtr<UOSWeaponsComponent> Guns = nullptr;

	/** Hull repair in hand: 4 the strap, 5 the welder (Docs/HullRepair.md). While one is out, LMB, R and the wheel are its. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "OS")
	TObjectPtr<UOSHullToolsComponent> HullTools = nullptr;

	/** The suit's projector under the lens: lays the weld frame of a breach in view onto the hull as red laser light. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "OS")
	TObjectPtr<UOSWeldProjectorComponent> WeldProjector = nullptr;

	/** The same projector as a device of the suit (its own lasers in its own frame): the attachment menu draws with it (Docs/Attachments.md). */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "OS")
	TObjectPtr<UOSSuitProjectorComponent> SuitProjector = nullptr;

	// --- view ---------------------------------------------------------------

	/** In zero gravity, mouse look turns the view around its own axes (yaw about the view's up after a flip), without pitch limits. The physics body never turns. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OS|ZeroG|View")
	bool bFreeBodyRotation = true;

	/** Horizontal field of view, degrees: MindKore's bodycam (UMKCameraComponent::BodyCamFov). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OS|ZeroG|View", meta = (ClampMin = "60.0", ClampMax = "150.0"))
	float CameraFOV = 134.0f;

	/** MindKore's barrel lens pass (M_MK_LensDistort); keeps the 134-degree frame from looking stretched. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OS|ZeroG|View")
	bool bLensDistortion = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OS|ZeroG|View", meta = (ClampMin = "0"))
	int32 DustCount = 4000;

	/** Edge of the dust cube around the camera, cm. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OS|ZeroG|View", meta = (ClampMin = "100.0"))
	float DustCubeCm = 1200.0f;

	/** Edge of one speck's cube, cm (each 0.6-1.3 of it); the material draws a hard dot 1-2.5 screen pixels across (Scripts/os_dust.py). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OS|ZeroG|View", meta = (ClampMin = "0.1"))
	float DustSpeckCm = 3.0f;

	/** Soft additive mote material (Scripts/os_dust.py); loaded at BeginPlay. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OS|ZeroG|View")
	TSoftObjectPtr<UMaterialInterface> DustMaterial = TSoftObjectPtr<UMaterialInterface>(FSoftObjectPath(TEXT("/Game/OS/Materials/M_OS_Dust.M_OS_Dust")));

	// --- body ---------------------------------------------------------------

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OS|ZeroG|Body", meta = (ClampMin = "1.0"))
	float BodyMassKg = 100.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OS|ZeroG|Body", meta = (ClampMin = "0.0"))
	float BodyFriction = 0.2f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OS|ZeroG|Body", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float BodyRestitution = 0.3f;

	/** 0 = no cap. Clamped every tick. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OS|ZeroG|Body", meta = (ClampMin = "0.0"))
	float MaxSpeedCm = 1200.0f;

	// --- push-off (RMB) --------------------------------------------------------

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OS|ZeroG|Push", meta = (ClampMin = "10.0"))
	float PushReachCm = 180.0f;

	/** kg cm/s. Off a static thing all of it is yours: 30000 / 100 kg = 300 cm/s. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OS|ZeroG|Push", meta = (ClampMin = "0.0"))
	float PushImpulse = 30000.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OS|ZeroG|Push", meta = (ClampMin = "0.0"))
	float PushCooldownSeconds = 0.35f;

	// --- grab (E) and throw (LMB while holding) --------------------------------

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OS|ZeroG|Grab", meta = (ClampMin = "10.0"))
	float GrabReachCm = 250.0f;

	/** Only absurd masses are refused: a heavy part is grabbed and drags you. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OS|ZeroG|Grab", meta = (ClampMin = "1.0"))
	float MaxGrabMassKg = 5000.0f;

	/** The held thing's grab point is held this far in front of the eye (the arm's rest reach on a loose body), cm. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OS|ZeroG|Grab", meta = (ClampMin = "0.0"))
	float HoldDistanceCm = 150.0f;

	/** Off: the held thing stays where it was grabbed, relative to the pawn. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OS|ZeroG|Grab")
	bool bHoldInFront = true;

	/** kg cm/s given to the prop and taken by the pawn on a throw. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OS|ZeroG|Grab", meta = (ClampMin = "0.0"))
	float ThrowImpulse = 8000.0f;

	/** A light prop is not thrown faster than this (the impulse shrinks to fit). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OS|ZeroG|Grab", meta = (ClampMin = "1.0"))
	float MaxThrowSpeedCm = 1100.0f;

	// --- the arm: holding on (Cling) and holding a loose body (Grab) ----------------
	// One spring-damper along the line from the hand to the shoulder (the eye);
	// nothing across it on a fixed hold, so you swing. Solved implicitly on the
	// velocity in steps of at most 1/240 s within each tick (the same at any
	// frame rate), as equal and opposite
	// impulses on the pawn and a held body (momentum kept).

	/** Reach from the eye to the hand on a fixed hold: you hang at it, cm. A hold taken farther reels you in to it. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OS|ZeroG|Arm", meta = (ClampMin = "10.0"))
	float ArmLengthCm = 70.0f;

	/** A full pull-up brings the eye this close to the hand, cm. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OS|ZeroG|Arm", meta = (ClampMin = "0.0"))
	float ArmMinCm = 25.0f;

	/** Natural frequency of the reach spring, rad/s (an acceleration spring on the pair's reduced mass): stiff but elastic. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OS|ZeroG|Arm", meta = (ClampMin = "0.1"))
	float ArmStiffness = 22.0f;

	/** Damping ratio of the reach spring: under 1 the arm gives a little bounce when it catches you. 1 gives the catch
	 * and the pull-up tuned at 60 FPS, when the spring took one implicit step a frame and 0.8 damped like this. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OS|ZeroG|Arm", meta = (ClampMin = "0.0"))
	float ArmDampingRatio = 1.0f;

	/** On a fixed hold, thrust keys toward the hand shorten the reach at this rate (a pull-up; let go mid-pull to fling), away from it lengthen it back, cm/s. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OS|ZeroG|Arm", meta = (ClampMin = "0.0"))
	float ArmPullCmS = 400.0f;

	/** Without a pull the reach returns to its rest (ArmLengthCm, HoldDistanceCm) at this rate, cm/s. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OS|ZeroG|Arm", meta = (ClampMin = "0.0"))
	float ArmRelaxCmS = 300.0f;

	/** On a fixed hold, thrust keys across the arm pump the swing: cm/s^2 along the input, up to SwingMaxSpeedCm along it. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OS|ZeroG|Arm", meta = (ClampMin = "0.0"))
	float SwingAccelCm = 600.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OS|ZeroG|Arm", meta = (ClampMin = "0.0"))
	float SwingMaxSpeedCm = 500.0f;

	/** On a fixed hold without input the swing is braked by min(SwingDampAccelCm, speed x SwingDampRate), cm/s^2: a big swing lasts, a small one settles to rest (welding). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OS|ZeroG|Arm", meta = (ClampMin = "0.0"))
	float SwingDampAccelCm = 120.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OS|ZeroG|Arm", meta = (ClampMin = "0.0"))
	float SwingDampRate = 6.0f;

	/** A held loose body: the hand steers it across the arm to the view (bHoldInFront) or where it was grabbed, critically damped, rad/s. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OS|ZeroG|Arm", meta = (ClampMin = "0.0"))
	float HoldSteerStiffness = 10.0f;

	/** The arm catching harder than this (cm/s^2 along it) tugs the camera with the crash shake, ArmTugCmPer1000 cm per 1000 cm/s^2. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OS|ZeroG|Arm", meta = (ClampMin = "0.0"))
	float ArmTugMinAccelCm = 2500.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OS|ZeroG|Arm", meta = (ClampMin = "0.0"))
	float ArmTugCmPer1000 = 0.15f;

	// --- tether (T) ---------------------------------------------------------------

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OS|ZeroG|Tether", meta = (ClampMin = "10.0"))
	float TetherReachCm = 600.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OS|ZeroG|Tether", meta = (ClampMin = "10.0"))
	float TetherLengthCm = 800.0f;

	/** Soft limit spring when the rope is taut (Chaos soft linear limit). 0 = a hard limit. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OS|ZeroG|Tether", meta = (ClampMin = "0.0"))
	float TetherStiffness = 500.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OS|ZeroG|Tether", meta = (ClampMin = "0.0"))
	float TetherDamping = 50.0f;

	// --- grapple (Left Alt) ----------------------------------------------------------

	/** Alt hooks what is under the view within this, cm. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OS|ZeroG|Grapple", meta = (ClampMin = "100.0"))
	float GrappleRangeCm = 3000.0f;

	/**
	 * Pull along the line while hooked, kg cm/s^2 (100 kg: 150000 = 1500 cm/s^2).
	 * A hooked loose body gets the same pull back, so a light one comes to you
	 * and a heavy one or a wall takes you to it.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OS|ZeroG|Grapple", meta = (ClampMin = "0.0"))
	float GrapplePull = 150000.0f;

	/** The hook lets go by itself this close to its point, cm. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OS|ZeroG|Grapple", meta = (ClampMin = "0.0"))
	float GrappleArriveCm = 200.0f;

	// --- roll (Q/E) --------------------------------------------------------------

	/** Q/E roll the view in zero-G free rotation, deg/s. Ignored in a gravity zone. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OS|ZeroG|View", meta = (ClampMin = "0.0"))
	float RollRateDegPerSec = 90.0f;

	// --- prediction (while Left Shift is held) --------------------------------------

	/** How far ahead the path is drawn, seconds of flight. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OS|ZeroG|Prediction", meta = (ClampMin = "0.1"))
	float PredictSeconds = 5.0f;

	/** Normal velocity kept on the predicted bounce (the real one depends on both materials). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OS|ZeroG|Prediction", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float PredictRestitution = 0.3f;

	// --- crashes ------------------------------------------------------------------

	/** The pawn's blows on what it hits (glass breaks, dummies hurt). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OS|ZeroG|Crash")
	FOSCollisionImpactSettings CrashImpact;

	/** A hit faster than this thumps: sound and camera shake, cm/s. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OS|ZeroG|Crash", meta = (ClampMin = "0.0"))
	float FeedbackMinSpeedCm = 250.0f;

	/** A hit faster than this hurts the pawn itself, cm/s. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OS|ZeroG|Crash", meta = (ClampMin = "0.0"))
	float CrashDamageMinSpeedCm = 600.0f;

	/** Self damage per cm/s over CrashDamageMinSpeedCm. The speed is the velocity change along the normal, bounce included: a head-on 1000 cm/s at restitution 0.3 reads ~1300 and costs ~35. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OS|ZeroG|Crash", meta = (ClampMin = "0.0"))
	float CrashDamagePerCmS = 0.05f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OS|ZeroG|Crash")
	TObjectPtr<USoundBase> CrashSound = nullptr;

	/** Camera shake amplitude per 1000 cm/s of hit, cm. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OS|ZeroG|Crash", meta = (ClampMin = "0.0"))
	float ShakeCmPer1000 = 4.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OS|ZeroG|Crash", meta = (ClampMin = "0.0"))
	float ShakeSeconds = 0.25f;

	/** Camera kick of a shot per 10000 kg cm/s of recoil, cm (the same fading shake as a crash). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OS|Weapon", meta = (ClampMin = "0.0"))
	float RecoilShakeCmPer10000 = 1.5f;

	/** Mouse look scale before the scope's own (owner 19.10: 0.7 of the engine's raw rate). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OS|ZeroG|Input", meta = (ClampMin = "0.0"))
	float LookSensitivity = 0.7f;

	/** Tab let go sooner than this (real seconds) is a tap - the attachment's switch; held longer, the menu (Docs/Attachments.md). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OS|ZeroG|Input", meta = (ClampMin = "0.05"))
	float MenuTapSeconds = 0.25f;

	/** Tab down / up (the input bindings and the probes). */
	void MenuPressed();
	void MenuReleased();
	/** Tab held: opens the attachment menu once the hold passes MenuTapSeconds (from Tick). */
	void UpdateMenuHold();

	// --- thrusters and stabiliser ------------------------------------------------

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OS|ZeroG|Thrusters")
	bool bThrustersEnabled = true;

	/** Acceleration along the input direction while a thrust key is held, cm/s^2. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OS|ZeroG|Thrusters", meta = (ClampMin = "0.0"))
	float ThrustAccelCm = 250.0f;

	/** Thrust stops adding once the velocity along the input direction reaches this, cm/s (it never brakes). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OS|ZeroG|Thrusters", meta = (ClampMin = "0.0"))
	float ThrustMaxSpeedCm = 500.0f;

	/** X: the stabiliser's deceleration cap, cm/s^2. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OS|ZeroG|Thrusters", meta = (ClampMin = "0.0"))
	float StabiliserAccelCm = 300.0f;

	/** X: deceleration = speed x this (1/s), up to the cap - an ease-out near standstill. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OS|ZeroG|Thrusters", meta = (ClampMin = "0.0"))
	float StabiliserRate = 2.0f;

	// --- walking in a gravity zone (the ship map: cabin, airlock, planetoid) ------
	// AOSShipGameMode sets the zone's gravity every tick (SetGravity). Inside,
	// the body falls toward it (a force; world gravity stays 0), WASD walks on
	// the ground, Space jumps; thrusters and the stabiliser are off.

	/** Target speed along the ground, cm/s. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OS|ZeroG|Walk", meta = (ClampMin = "0.0"))
	float WalkSpeedCm = 450.0f;

	/** On the ground with input: how fast the ground velocity turns toward the target, cm/s^2. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OS|ZeroG|Walk", meta = (ClampMin = "0.0"))
	float WalkAccelCm = 3000.0f;

	/** On the ground without input: the brake (friction feel, no sliding), cm/s^2. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OS|ZeroG|Walk", meta = (ClampMin = "0.0"))
	float WalkBrakingCm = 4000.0f;

	/** In the air inside a zone: input adds along its direction up to WalkSpeedCm, never brakes, cm/s^2. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OS|ZeroG|Walk", meta = (ClampMin = "0.0"))
	float AirAccelCm = 400.0f;

	/** Walking, a single kick (a shot, a push, a hit) that changes the speed along -gravity by more than this leaves the ground as a jump does, cm/s. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OS|ZeroG|Walk", meta = (ClampMin = "0.0"))
	float LaunchThresholdCm = 150.0f;

	/** Space on the ground: speed along -gravity, cm/s. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OS|ZeroG|Walk", meta = (ClampMin = "0.0"))
	float JumpSpeedCm = 420.0f;

	/** Space again in the air in a radial zone (the planetoid): a jet push up that leaves its gravity, cm/s. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OS|ZeroG|Walk", meta = (ClampMin = "0.0"))
	float JetExitSpeedCm = 1600.0f;

	/** Ground: a blocking hit of the body's sphere swept this far along gravity, cm. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OS|ZeroG|Walk", meta = (ClampMin = "0.0"))
	float GroundProbeCm = 12.0f;

	/** Ground steeper than this (from -gravity) is a wall, degrees. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OS|ZeroG|Walk", meta = (ClampMin = "0.0", ClampMax = "89.0"))
	float MaxGroundAngleDeg = 50.0f;

	/** The camera rises this far above the body's centre along -gravity (eyes ~1.5 m over the floor), cm. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OS|ZeroG|Walk", meta = (ClampMin = "0.0"))
	float EyeHeightCm = 110.0f;

	/** The camera keeps this far off geometry: a sphere this size swept from the body's centre to the eye stops the eye under a ceiling, a cave plug or a low duct (review #44), cm. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OS|ZeroG|View", meta = (ClampMin = "1.0"))
	float EyeRadiusCm = 15.0f;

	/** In gravity, holding on to a fixed thing, the eye stays at least this far off the held surface's plane: a pull-up or a swing stops there, the head does not enter the ceiling (in zero-G the eye is the body's centre and the body keeps it off), cm. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OS|ZeroG|Arm", meta = (ClampMin = "0.0"))
	float HeadClearanceCm = 30.0f;

	/** How fast the camera moves to and from that height, 1/s. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OS|ZeroG|Walk", meta = (ClampMin = "0.1"))
	float EyeRiseRate = 4.0f;

	/** Inside a zone a hit thumps (and hurts) only above this, so walking into a wall or landing a jump stays quiet, cm/s. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OS|ZeroG|Walk", meta = (ClampMin = "0.0"))
	float GroundFeedbackMinSpeedCm = 700.0f;

	/** On flesh (MK_Flesh: the planetoid, living walls): a wet step every StepCm walked, and on every touchdown. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OS|ZeroG|Walk")
	TObjectPtr<USoundBase> FleshStepSound = nullptr;

	/** On flesh a crash or hard fall plays this instead of CrashSound (the plate). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OS|ZeroG|Walk")
	TObjectPtr<USoundBase> FleshCrashSound = nullptr;

	/** About 3.5 steps a second at WalkSpeedCm 450. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OS|ZeroG|Walk", meta = (ClampMin = "1.0"))
	float StepCm = 130.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OS|ZeroG|Walk", meta = (ClampMin = "0.0"))
	float StepVolume = 0.35f;

	/** The meat samples pitched down this far (random in the range): heavier and wetter. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OS|ZeroG|Walk")
	FVector2D FleshPitchRange = FVector2D(0.4f, 0.55f);

	// --- bullet time presentation (as AOSCharacter) --------------------------

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OS|BulletTime")
	TObjectPtr<UMaterialInterface> BulletTimeMaterial = nullptr;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OS|BulletTime")
	TObjectPtr<USoundBase> BulletTimeEntrySound = nullptr;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OS|BulletTime")
	TObjectPtr<USoundBase> BulletTimeExitSound = nullptr;

	// --- actions ---------------------------------------------------------------

	/** Push off whatever the view meets within PushReachCm. False when nothing is in reach or on cooldown. */
	UFUNCTION(BlueprintCallable, Category = "OS|ZeroG")
	bool Push();

	/** Fire the current gun (Guns) from the view; its recoil goes into the body. False when it cannot fire now. */
	UFUNCTION(BlueprintCallable, Category = "OS|ZeroG")
	bool Fire();

	/** Shake the camera (the crash shake: a fading random offset), cm; a stronger shake in progress is kept. A flyer's bite uses it. */
	void KickCamera(float AmplitudeCm);

	/** Join Target to the body at Point (world). False when something is held, Target does not simulate or is absurdly heavy. */
	UFUNCTION(BlueprintCallable, Category = "OS|ZeroG")
	bool Grab(UPrimitiveComponent* Target, FVector Point);

	UFUNCTION(BlueprintCallable, Category = "OS|ZeroG")
	void Release();

	/** Throw the held prop and take the opposite momentum. */
	UFUNCTION(BlueprintCallable, Category = "OS|ZeroG")
	void ThrowHeld();

	UFUNCTION(BlueprintPure, Category = "OS|ZeroG")
	UPrimitiveComponent* GetGrabbed() const { return GrabbedBody.Get(); }

	/** Rope to Target's origin. False when Target does not simulate or is out of reach. */
	UFUNCTION(BlueprintCallable, Category = "OS|ZeroG")
	bool AttachTether(UPrimitiveComponent* Target);

	UFUNCTION(BlueprintCallable, Category = "OS|ZeroG")
	void DetachTether();

	UFUNCTION(BlueprintPure, Category = "OS|ZeroG")
	UPrimitiveComponent* GetTethered() const { return TetheredBody.Get(); }

	/** Hold on to a fixed thing at Point (RMB on the hull): the body hangs from the hand on the arm and settles to rest there. False when Target simulates. */
	bool Cling(UPrimitiveComponent* Target, const FVector& Point);

	void LetGoCling();

	bool IsClinging() const { return ClingTarget.IsValid(); }

	/** The hand (world) on the hold or the held body; the eye when the hands are free. */
	FVector GetHandLocation() const;

	/** The arm now: the eye's distance from the hand, and the reach it is pulled to, cm. */
	float GetArmLengthCm() const;
	float GetArmRestCm() const { return ArmRestCm; }

	/** The eye: the arm's shoulder end and the camera without its shake (body centre plus the eased eye offset, kept off geometry). */
	FVector ShoulderLocation() const;

	/** The simulated body E would grab now (null when none or something is held). */
	UPrimitiveComponent* GetGrabCandidate() const { return GrabCandidate.Get(); }

	/** What RMB would do now: aim with a gun out, grab a loose body in reach, let go, or hold on. */
	EOSRmbAction GetRmbAction() const;

	/** The HUD's quiet RMB line: a loose body in reach is taken ("with a gun out, the gun goes away"); empty otherwise. */
	FString GetRmbPrompt() const;

	/** What the hands hold now (Empty: free, no gun picked up yet). */
	UFUNCTION(BlueprintPure, Category = "OS|Hands")
	EOSHands GetHands() const;

	/**
	 * The hands' one writer: leave what they hold (its exit), take NewHands (its enter).
	 * Gun -> Tool: the gun is put away on its path and its reload abandoned. Gun -> Held:
	 * gone at once (a grab). Held -> what was in hand before the hold (a gun with the draw).
	 * Leaving Held lets the body go. Tool: NewTool, or None for the last one. Empty means Gun.
	 */
	void SetHands(EOSHands NewHands, EOSHullTool NewTool = EOSHullTool::None);

	/** A tool's key: take it; its key again puts it away (back to the gun). */
	void TakeTool(EOSHullTool Tool);

	/** The hands' table: what Key does with what they hold, in this move mode (the counterpart to Allows). */
	EOSHandAction Route(EOSHandKey Key) const;

	/** One key through the table (the input bindings and the probes). Value: the wheel's direction. */
	void Press(EOSHandKey Key, float Value = 0.0f);

	/** A digit key: slots 0..N-1 the guns in Guns->Weapons order, then the strap and the welder (GetToolSlot). */
	void PressSlot(int32 Slot);

	/** The slot of a hull tool: right after the guns (3 guns: the strap 3, key 4; the welder 4, key 5). */
	int32 GetToolSlot(EOSHullTool Tool) const { return Guns->Weapons.Num() + int32(Tool) - 1; }

	/** RMB pressed (IA_ZG_Use): aim, grab, let go or hold on - GetRmbAction. Public for os.Guns.RmbProbe. */
	void Use();

	UFUNCTION(BlueprintCallable, Category = "OS")
	void ToggleCrowbar();

	UFUNCTION(BlueprintPure, Category = "OS")
	bool HasCrowbar() const;

	UFUNCTION(BlueprintPure, Category = "OS|ZeroG")
	FVector GetBodyVelocity() const;

	UFUNCTION(BlueprintPure, Category = "OS|ZeroG")
	float GetHeldMassKg() const;

	UFUNCTION(BlueprintPure, Category = "OS|ZeroG")
	float GetPushCooldownRemaining() const;

	/** Console probe: aim (as Left Shift) or stop; the next drawn path logs its first contact. */
	void ProbeAim(bool bAim);

	/** Left Alt: hook what is under the view, or let go. */
	void ToggleGrapple();

	UPrimitiveComponent* GetGrappled() const { return GrappleTarget.Get(); }

	/** Console probe: thrust input for the next tick (view frame: X forward, Y right, Z up) and the stabiliser. */
	void ProbeThrust(const FVector& Input, bool bStabilise) { ThrustInput = Input; bStabilising = bStabilise; }

	/** Gravity here (unit direction, cm/s^2), set every tick by AOSShipGameMode; zero strength = zero-G. */
	void SetGravity(const FVector& Direction, float StrengthCm, bool bRadial);

	UFUNCTION(BlueprintPure, Category = "OS|ZeroG")
	EOSMoveMode GetMoveMode() const { return MoveMode; }

	/** What the current mode allows. */
	const FOSMoveRules& Allows() const;

	UFUNCTION(BlueprintPure, Category = "OS|Ladder")
	bool IsClimbingLadder() const { return ActiveLadder != nullptr; }

	UFUNCTION(BlueprintPure, Category = "OS|ZeroG|Walk")
	bool IsInGravity() const { return GravityStrengthCm > 0.0f; }

	/** Unit; zero outside every gravity zone. */
	FVector GetGravityDirection() const { return GravityDirection; }

	UFUNCTION(BlueprintPure, Category = "OS|ZeroG|Walk")
	bool IsGrounded() const { return bGrounded; }

	/** Space pressed: a jump on the ground, the jet exit in the air over a planetoid; nothing in zero-G (Space thrusts there). */
	UFUNCTION(BlueprintCallable, Category = "OS|ZeroG|Walk")
	void JumpPressed();

	/**
	 * Death and revival (AOSShipGameMode): the hands let go of everything -
	 * the held body, the hold on the hull (the arm), the rope, the hook, the
	 * ladder, the torch, the strap's line on the belt, the aim, bullet time and
	 * the keys held. The strap's lines between two bodies, the keys on the ring,
	 * the guns, their ammo and the tool in hand stay.
	 */
	void LetGoOfEverything();

	/** Revival: LetGoOfEverything, the body at Location at rest, full health and air. */
	void ReviveAt(const FVector& Location);

	/** Counted for the probes (os.Head.Probe, os.ZeroG.Trace): the body's hits, their impulses summed (kg cm/s), camera shakes started. */
	int32 ProbeContacts = 0;
	float ProbeContactImpulse = 0.0f;
	int32 ProbeShakes = 0;

	virtual void ReceiveResolvedImpact_Implementation(const FMKResolvedImpact& ResolvedImpact) override;

	virtual void Tick(float DeltaSeconds) override;
	virtual void SetupPlayerInputComponent(UInputComponent* PlayerInputComponent) override;
	virtual void PawnClientRestart() override;

protected:
	virtual void BeginPlay() override;

	/** The body takes a single kick in kg cm/s (velocity change = Impulse / mass); walking, one up past LaunchThresholdCm leaves the ground. */
	void AddBodyImpulse(const FVector& Impulse);

	/** Where the view aims from: the eye (ShoulderLocation), without the camera shake. */
	FVector ViewLocation() const;
	FVector ViewDirection() const;
	void RefreshGrabCandidate();

	TWeakObjectPtr<UPrimitiveComponent> GrabbedBody;
	/** The fixed thing held on to (Cling). */
	TWeakObjectPtr<UPrimitiveComponent> ClingTarget;
	/** The hand on the hold or the held body, in its component space. */
	FVector HandLocal = FVector::ZeroVector;
	/** A fixed hold: the held surface's normal at the hand, on the eye's side, in its component space. */
	FVector HoldNormalLocal = FVector::ZeroVector;
	/** The eye eases onto HeadClearanceCm over the held plane in about this, s. */
	static constexpr float HeadCushionSeconds = 0.1f;
	/** How far the body's sphere is swept toward the hand to find what it rests against, cm. */
	static constexpr float ArmBlockProbeCm = 2.0f;
	/** A held loose body: its rest reach, and the direction from the eye to the hand it is kept at when not held in front (world; the body never turns). */
	float HoldRestCm = 0.0f;
	FVector HoldDirection = FVector::ForwardVector;
	/** The reach the arm pulls to now, cm. */
	float ArmRestCm = 0.0f;
	/** The arm's steps end a tick faster than the velocity Chaos moves the body on (UpdateArm): the difference, as
	 * the pawn's impulse (kg cm/s), goes into the next tick, or to the pawn when the hand lets go of a hold (HandOverArmCarry). */
	FVector ArmCarry = FVector::ZeroVector;
	void UpdateArm(float DeltaSeconds);
	void HandOverArmCarry();
	void UpdateDust();

	UPROPERTY(Transient)
	TObjectPtr<UPhysicsConstraintComponent> TetherConstraint = nullptr;

	TWeakObjectPtr<UPrimitiveComponent> TetheredBody;

	/** The hooked component and the hook point in its local space. */
	TWeakObjectPtr<UPrimitiveComponent> GrappleTarget;
	FVector GrappleLocalPoint = FVector::ZeroVector;
	TWeakObjectPtr<UPrimitiveComponent> GrabCandidate;
	FVector GrabCandidatePoint = FVector::ZeroVector;

	// Input state, consumed in Tick.
	FVector ThrustInput = FVector::ZeroVector;
	bool bStabilising = false;
	double LastPushSeconds = -100.0;

private:
	void BuildInput();
	void Look(const FInputActionValue& Value);
	void Thrust(const FInputActionValue& Value);
	void Lift(const FInputActionValue& Value);
	void PushPressed();
	/**
	 * How long Tab has been down, s of undilated frame time (FApp::GetDeltaTime: the same at any frame rate and in
	 * bullet time); < 0: up. A tap is shorter than MenuTapSeconds; the menu opens only once Tab is held past it
	 * (owner 23.10: the laser's switch must not flash the menu).
	 */
	float MenuHeldSeconds = -1.0f;
	/** Tab is down and the menu not asked for yet (Tick asks once MenuHeldSeconds passes MenuTapSeconds). */
	bool bMenuPending = false;
	/** Stored: Gun, Tool or Held (GetHands reads a Gun with none unlocked as Empty). */
	EOSHands Hands = EOSHands::Gun;
	/** What the hands go back to when a held body is let go: Gun or Tool. */
	EOSHands HandsBeforeHeld = EOSHands::Gun;
	void Roll(const FInputActionValue& Value);
	void UpdateGrapple();
	/** Under AOSShipGameMode only after the AOSToolPickup with OSGrappleTool was taken; under another mode always. */
	bool HasGrapple() const;
	UFUNCTION()
	void HandleBodyHit(UPrimitiveComponent* HitComponent, AActor* OtherActor, UPrimitiveComponent* OtherComp,
		FVector NormalImpulse, const FHitResult& Hit);
	double LastCrashSeconds = -100.0;
	/** The last crash that thumped (sound, shake, self damage): its later contacts are cut off by this, and a touchdown step right after it is skipped. */
	double LastThumpSeconds = -100.0;
	float ShakeRemaining = 0.0f;
	float ShakeAmplitude = 0.0f;
	void DrawPrediction() const;
	void DrawPath(const FVector& Start, const FVector& Velocity, float RadiusCm, const AActor* Ignored, const FColor& Color) const;

	bool bAiming = false;
	mutable bool bLogNextPrediction = false;
	void ToggleTether();
	void DrawTether() const;
	void BulletTimePressed();
	void BulletTimeReleased();
	EOSMoveMode MoveMode = EOSMoveMode::Float;
	/** Float or Walk, by the gravity here: where the hands-free body goes back to. */
	EOSMoveMode FreeMode() const { return IsInGravity() ? EOSMoveMode::Walk : EOSMoveMode::Float; }
	/** Leaves the current mode (its exit) and enters NewMode (its enter). */
	void SetMode(EOSMoveMode NewMode);
	/** A strap line tied to the body is taut (it pulled in its last tick). */
	bool IsStrapPulling() const;
	void ApplyThrusters(float DeltaSeconds);
	void Walk(float DeltaSeconds);
	void ClimbLadder(float DeltaSeconds);
	void BeginLadderClimb(AOSLadder* Ladder);
	void EndLadderClimb();
	void RestoreLadderBodyRadius();
	UPROPERTY(Transient)
	TObjectPtr<AOSLadder> ActiveLadder = nullptr;
	TWeakObjectPtr<AOSLadder> JumpedFromLadder;
	bool bRestoreLadderRadius = false;
	float LadderOriginalRadiusCm = 40;
	double LadderCooldownUntil = 0;
	/** Over the top: carried onto the ladder's Top landing whatever the keys do. */
	bool bLadderMounting = false;
	/** Climbed since the last rung sound, cm. */
	float LadderTravelCm = 0;
	/** The depth (ladder's local Y) flush with the rungs, found on the grab and narrowed on touching them; climbing eases the body to it. */
	float LadderFaceY = 0;
	void PlayRung(float Volume);
	/** Walkable ground under the body; the hit (normal, surface) in OutHit. */
	bool ProbeGround(FHitResult& OutHit) const;
	void PlayFleshStep(const FHitResult& Ground, float Volume);

	FVector GravityDirection = FVector::ZeroVector;
	float GravityStrengthCm = 0.0f;
	bool bRadialGravity = false;
	bool bGrounded = false;
	float StepTravelCm = 0.0f;
	double LastGroundedSeconds = -100.0;
	/** A touchdown whose step waits for the landing's contact (Walk). */
	double TouchdownSeconds = -100.0;
	bool bTouchdownStepPending = false;
	double LastJumpSeconds = -100.0;
	/** The eye's offset from the body's centre (world space), eased toward the eye height. */
	FVector EyeOffset = FVector::ZeroVector;
	/** How far the eye is pushed down from EyeOffset under a ceiling (UpdateEye), cm. */
	float EyeDropCm = 0.0f;
	/** The eye as it is: EyeOffset less the drop. The camera (with the shake) and the arm's shoulder sit here. */
	FVector EyeShownOffset = FVector::ZeroVector;
	/** EyeOffset eased to the gravity's eye height, then kept EyeRadiusCm off geometry (review #44). */
	void UpdateEye(float DeltaSeconds);
	void UpdateBulletTimePresentation();

	UPROPERTY(Transient)
	TObjectPtr<UPhysicalMaterial> BodyMaterial = nullptr;

	UPROPERTY(Transient)
	TObjectPtr<UInputMappingContext> Mapping = nullptr;

	UPROPERTY(Transient)
	TArray<TObjectPtr<UInputAction>> Actions;

	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> BulletTimeMID = nullptr;

	bool bBulletTimeWasSlowed = false;
};
