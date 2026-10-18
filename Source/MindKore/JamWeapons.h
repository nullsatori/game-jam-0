#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Curves/CurveFloat.h"
#include "JamWeapons.generated.h"

class UEnhancedInputComponent;
class UInputAction;
class UInputMappingContext;
class UMaterialInterface;
class UMKImpactDataAsset;
class USoundBase;
class UStaticMesh;
class UStaticMeshComponent;

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

	/** Round speed, cm/s: each pellet flies as a round with a tracer and hits on arrival. 0: hitscan. */
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
	 * stroke along the line of sight reads on screen.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Jam|Weapon|Action")
	FRotator ActionBodyTurn = FRotator::ZeroRotator;

	/** ...and moved this far, cm in the camera's frame (X forward, Y right, Z up), at ActionBody 1. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Jam|Weapon|Action")
	FVector ActionBodyOffsetCm = FVector::ZeroVector;

	/** Seconds since the shot -> share of ActionBodyTurn and ActionBodyOffsetCm. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Jam|Weapon|Action")
	FRuntimeFloatCurve ActionBody;

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

	/** The gun in hand, or null when it is not unlocked (no guns yet). */
	const FJamWeapon* GetWeapon() const { return IsWeaponUnlocked(CurrentWeapon) ? &Weapons[CurrentWeapon] : nullptr; }

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
		TWeakObjectPtr<UStaticMeshComponent> Tracer;
	};

	/** Trace Start..End for a round of Weapon; deliver the hit. True when something stopped it. */
	bool TraceShot(const FJamWeapon& Weapon, const FVector& Start, const FVector& End, const FVector& Direction);
	void UpdateRounds(float DeltaTime);
	void UpdateRecoil(float DeltaTime);
	void UpdateViewModel(float DeltaTime);
	/** Turn the owner's view by degrees in the view's own frame. */
	void TurnView(float PitchDeg, float YawDeg) const;
	void SelectStep(int32 Step);

	TArray<FRound> Rounds;
	float KickPitchLeft = 0.0f;
	float KickYawLeft = 0.0f;
	float KickAttackLeft = 0.0f;
	float SettlePitch = 0.0f;
	float SettleYaw = 0.0f;
	/** Seconds since the gun in view last fired: drives its kick and its moving part's cycle. */
	float SinceShotSeconds = 1000.0f;
	int32 ShownWeapon = INDEX_NONE;
	/** The pending CycleSound of the last shot; a switch cancels it. */
	FTimerHandle CycleTimer;

	double NextShotSeconds = 0.0;
	double ReloadEndSeconds = 0.0;

	UPROPERTY(Transient)
	TArray<TObjectPtr<UInputAction>> InputActions;
};
