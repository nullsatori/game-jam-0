#pragma once

#include "CoreMinimal.h"
#include "OSWeaponCurves.h"
#include "UObject/SoftObjectPtr.h"
#include "OSWeaponAttach.generated.h"

class UMaterialInterface;
class USoundBase;
class UStaticMesh;
class UOSWeaponsComponent;

/** What a place on a gun takes (Docs/Attachments.md, «Места»). */
UENUM(BlueprintType)
enum class EOSMountType : uint8
{
	/** The Picatinny-style rail: the laser, the torch. Rolled on from the muzzle's end. */
	Rail,
	/** The muzzle's thread: the suppressor. Screwed on. */
	Muzzle,
	/** The top rail (the RMB-93): nothing in the test set fits it yet. */
	Top,
	/** The magazine well: always holds the stock magazine or one in its place; a change is a reload. */
	Magazine,
};

/** One place on a gun (FOSWeapon::Mounts). */
USTRUCT(BlueprintType)
struct FOSWeaponMount
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OS|Weapon|Attach")
	EOSMountType Type = EOSMountType::Rail;

	/**
	 * The seat in ViewMesh's space (or in Socket's, when set): X toward the muzzle along the rail or the bore,
	 * Z up. A module sits at the origin; it rolls on from +X (the rail's mouth) toward -X, into the slot from
	 * below (bUnder) or from above.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OS|Weapon|Attach")
	FTransform Transform;

	/** ViewMesh's socket the seat is relative to (the muzzle's: "Muzzle"); none: ViewMesh's own space. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OS|Weapon|Attach")
	FName Socket;

	/** The module hangs under the rail (the pistol's frame rail, the pump's fore-end): it rises into the slot. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OS|Weapon|Attach")
	bool bUnder = true;

	/** The attachment on it now (FOSAttachment::Id); none: empty - or the stock magazine. */
	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Transient, Category = "OS|Weapon|Attach")
	FName Mounted;
};

/**
 * One attachment (Docs/Attachments.md): what it is, where it goes, how the hand puts it on (owner 22.10 14:11:
 * pulled from the pouch and rolled onto the rail, detents, a stop, a latch; the suppressor screwed on), and
 * what the gun does while it is on. The motion is a pure function of the seconds since Apply
 * (OSSeatPoseAt), so stills and any frame rate see the same.
 */
USTRUCT(BlueprintType)
struct FOSAttachment
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OS|Attach")
	FName Id;

	/** The menu's row, capitals (MK Pribor N): «ЛЦУ». */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OS|Attach")
	FText Name;

	/** The menu's second line for the selected row. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OS|Attach")
	FText Description;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OS|Attach")
	EOSMountType MountType = EOSMountType::Rail;

	/** The guns it fits (FOSWeapon::Id). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OS|Attach")
	TArray<FName> Fits;

	/** The module on the gun: an engine primitive in the cloud (Docs/Attachments.md, «Арт»), scaled; none: no module (the magazine). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OS|Attach|Look")
	TObjectPtr<UStaticMesh> Mesh = nullptr;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OS|Attach|Look")
	TObjectPtr<UMaterialInterface> Material = nullptr;

	/** The correction from the mount's seat to the module (its mesh's own origin and scale). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OS|Attach|Look")
	FTransform SeatOffset;

	/** It has a switch (Tab tapped): the laser, the torch. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OS|Attach")
	bool bHasSwitch = false;

	// --- Putting it on (owner 22.10 14:11) ---

	/** Where the hand brings it from: off the seat in the camera's frame, cm (forward, right, up) - the pouch, out of frame. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OS|Attach|Seat")
	FVector PouchOffsetCm = FVector(4.0f, 9.0f, -14.0f);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OS|Attach|Seat", meta = (ClampMin = "0.05"))
	float ApproachSeconds = 0.22f;

	/** The way from the pouch bows this far down at its middle, cm. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OS|Attach|Seat")
	float ApproachArcCm = 3.0f;

	/** Its turn out of the pouch (pitch nose down, yaw, roll), straightening over the last 5 cm. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OS|Attach|Seat")
	FRotator ApproachTurn = FRotator(-30.0f, 0.0f, 20.0f);

	/** The rail's mouth: this far toward the muzzle from the seat, cm, and RailDropCm off the rail. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OS|Attach|Seat")
	float RailMouthCm = 2.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OS|Attach|Seat")
	float RailDropCm = 0.8f;

	/** Seconds the rise into the slot takes after the approach (with 1 mm past). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OS|Attach|Seat")
	float RiseSeconds = 0.08f;

	/** The roll along the rail, share of RailMouthCm by seconds since Apply: the push, the run, the stop, its 6% rebound. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OS|Attach|Seat")
	FRuntimeFloatCurve RailTravel;

	/** The detents it rides over, seconds since Apply (and their mirror taking it off). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OS|Attach|Seat")
	TArray<float> Detents = { 0.36f, 0.42f, 0.48f };

	/** Each detent lifts it this far off and back in 20 ms, cm. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OS|Attach|Seat")
	float DetentJitterCm = 0.03f;

	/** The stop (seconds since Apply), the latch, the lens lit, everything settled. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OS|Attach|Seat")
	float StopSeconds = 0.52f;

	/** The rebound off the stop, share of RailMouthCm (the curve carries it; for the record and the docs). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OS|Attach|Seat")
	float StopRebound = 0.06f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OS|Attach|Seat")
	float LockSeconds = 0.6f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OS|Attach|Seat")
	float LensSeconds = 0.62f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OS|Attach|Seat")
	float SeatSeconds = 0.72f;

	/** Taking it off: the whole way back to the pouch. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OS|Attach|Seat")
	float UnseatSeconds = 0.55f;

	/** The gun's pose springs on touching the rail, on the stop, on the latch (FOSPoseKick::Time: seconds since Apply). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OS|Attach|Seat")
	FOSPoseKick SeatKick;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OS|Attach|Seat")
	FOSPoseKick StopKick;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OS|Attach|Seat")
	FOSPoseKick LockKick;

	/** The gun gives under the hand's push: back along the bore and a roll, following the roll on; forward taking it off. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OS|Attach|Seat")
	float YieldCm = 1.2f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OS|Attach|Seat")
	float YieldRollDeg = 3.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OS|Attach|Seat")
	float UnseatYieldCm = 0.8f;

	/** The muzzle's thread (the suppressor): turns, the travel onto the muzzle, the seconds; off: UnthreadSeconds. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OS|Attach|Seat")
	float ThreadTurns = 2.5f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OS|Attach|Seat")
	float ThreadTravelCm = 1.2f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OS|Attach|Seat")
	float ThreadSeconds = 1.2f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OS|Attach|Seat")
	float UnthreadSeconds = 0.35f;

	/** The last quarter turn: the hand's squeeze (cm/s forward, deg/s roll). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OS|Attach|Seat")
	FOSPoseKick TightenKick;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OS|Attach|Sound")
	TObjectPtr<USoundBase> PouchSound = nullptr;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OS|Attach|Sound")
	TObjectPtr<USoundBase> RailClick = nullptr;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OS|Attach|Sound")
	TObjectPtr<USoundBase> DetentTick = nullptr;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OS|Attach|Sound")
	TObjectPtr<USoundBase> StopKnock = nullptr;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OS|Attach|Sound")
	TObjectPtr<USoundBase> LockClick = nullptr;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OS|Attach|Sound")
	TObjectPtr<USoundBase> SwitchSound = nullptr;

	// --- What it does on the gun ---

	/** The laser (UOSLaserSightComponent): its lens on the module, in the module's space before SeatOffset, cm. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OS|Attach|Effect")
	bool bLaser = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OS|Attach|Effect")
	FVector LensOffsetCm = FVector::ZeroVector;

	/** A torch: a spotlight on the module along the bore; 0: none. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OS|Attach|Effect", meta = (ClampMin = "0.0"))
	float TorchCandelas = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OS|Attach|Effect", meta = (ClampMin = "1.0", ClampMax = "80.0"))
	float TorchConeDeg = 18.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OS|Attach|Effect", meta = (ClampMin = "100.0"))
	float TorchRangeCm = 2500.0f;

	/** A muzzle device: the shot's sound in its place (none: the gun's own at ShotVolume), the flash and smoke scaled, the muzzle moved out along the bore. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OS|Attach|Effect")
	TSoftObjectPtr<USoundBase> ShotSoundOverride;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OS|Attach|Effect", meta = (ClampMin = "0.0"))
	float ShotVolume = 1.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OS|Attach|Effect", meta = (ClampMin = "0.0"))
	float MuzzleFlashScale = 1.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OS|Attach|Effect", meta = (ClampMin = "0.0"))
	float MuzzleSmokeScale = 1.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OS|Attach|Effect")
	float MuzzleOffsetCm = 0.0f;

	/** A magazine (owner 22.10 14:16): its size in the stock's place (0: unchanged), its mesh, how much longer it is (travel and how far it shows below the grip). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OS|Attach|Effect", meta = (ClampMin = "0"))
	int32 MagazineSizeOverride = 0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OS|Attach|Effect")
	TObjectPtr<UStaticMesh> MagMeshOverride = nullptr;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OS|Attach|Effect", meta = (ClampMin = "0.0"))
	float MagExtraCm = 0.0f;

	/** Clear (owner 22.10 14:19): the rounds show inside it - N = Ammo, a staggered double stack, the follower and the spring. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OS|Attach|Effect")
	bool bClearMagazine = false;

	/** M_OS_ClearMag (built on the PC); missing: an engine translucent material. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OS|Attach|Effect")
	TSoftObjectPtr<UMaterialInterface> ClearMaterial;

	bool FitsGun(FName GunId) const { return Fits.Contains(GunId); }
};

/** Where a module is at Seconds since Apply (OSSeatPoseAt): in its mount's frame, and what the gun does meanwhile. */
struct FOSSeatPose
{
	/** In the mount's frame (X toward the muzzle, Z up), cm and degrees (pitch, yaw, roll). */
	FVector Place = FVector::ZeroVector;
	FRotator Turn = FRotator::ZeroRotator;
	bool bShown = false;
	/** The lens is lit (the laser seated, latched). */
	bool bLens = false;
	/** The gun's give: cm back along the bore (negative: forward) and degrees of roll, camera frame. */
	float YieldCm = 0.0f;
	float YieldRollDeg = 0.0f;
	/** The motion is over. */
	bool bDone = false;
};

/**
 * The module of Attachment at Seconds since Apply, put on (bOn) or taken off, on a mount of Type, coming from
 * (or going to) PouchInMount (the pouch in the mount's frame, cm). A pure function of time (owner 22.10 14:11):
 * the approach on its arc, the rise into the slot, the roll over the detents to the stop, the rebound, the latch;
 * the suppressor screwed on along the bore. Docs/Attachments.md, «Модуль на стволе».
 */
MINDKORE_API FOSSeatPose OSSeatPoseAt(const FOSAttachment& Attachment, const FOSWeaponMount& Mount, bool bOn, float Seconds, const FVector& PouchInMount);

/** The whole motion's length: on or off, rail or thread. */
MINDKORE_API float OSSeatLength(const FOSAttachment& Attachment, EOSMountType Type, bool bOn);

/** The test set (Docs/Vision/Attachments.md §8 and the 14:16 addendum): Laser, Torch, Suppressor, BigMag. Constructor-only (ConstructorHelpers). */
MINDKORE_API TArray<FOSAttachment> OSMakeAttachments();

/** A mount type's name on the menu's header, capitals: «ПЛАНКА», «ДУЛО», «ВЕРХ», «МАГАЗИН». */
MINDKORE_API FText OSMountName(EOSMountType Type);

/**
 * The preset's gear (UOSTestPreset field "gear", Docs/DebugMenu.md): the guns unlocked by Id and the attachments
 * as "Id" (in the pouch) or "Id@GunId" (on that gun).
 */
MINDKORE_API void OSReadGear(const UOSWeaponsComponent& Guns, TArray<FString>& OutGuns, TArray<FString>& OutAttachments);
MINDKORE_API void OSApplyGear(UOSWeaponsComponent& Guns, const TArray<FString>& InGuns, const TArray<FString>& InAttachments);

/**
 * Put another gun into slot Index (os.Guns.Swap, a preset's gear): "Rmb93" the RMB-93 built from the pump's class
 * defaults, "Slot"/"Shotgun" the slot's own gun back - like AOSWeaponPickup with Gun = Rmb93, aim included. Open
 * stays open; attachments move over where they fit (TransferAttachments). False: an unknown name or slot.
 */
MINDKORE_API bool OSSwapGun(UOSWeaponsComponent& Guns, int32 Index, const FString& To);

/** os.Attach.Cost: the laser's ticks and the clear magazine's rebuilds, summed since the last call (defined and summed outside Shipping only). */
struct FOSAttachCost
{
	double LaserSeconds = 0.0;
	double LaserMaxSeconds = 0.0;
	int64 LaserFrames = 0;
	double ClearSeconds = 0.0;
	double ClearMaxSeconds = 0.0;
	int64 ClearRebuilds = 0;
	int64 SinceFrame = 0;
};
extern MINDKORE_API FOSAttachCost GOSAttachCost;

/** Times its scope into one of GOSAttachCost's sums. */
struct FOSAttachCostScope
{
	double& Sum;
	double& Max;
	int64& Count;
	double Start;
	FOSAttachCostScope(double& InSum, double& InMax, int64& InCount) : Sum(InSum), Max(InMax), Count(InCount), Start(FPlatformTime::Seconds()) {}
	~FOSAttachCostScope()
	{
		const double Spent = FPlatformTime::Seconds() - Start;
		Sum += Spent;
		Max = FMath::Max(Max, Spent);
		++Count;
	}
};
