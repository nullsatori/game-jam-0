#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "JamWeapons.generated.h"

class UEnhancedInputComponent;
class UInputAction;
class UInputMappingContext;
class UMaterialInterface;
class UMKImpactDataAsset;
class USoundBase;

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

	/** Pitch of the blast sound: a small gun cracks, a big one booms. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Jam|Weapon", meta = (ClampMin = "0.1"))
	float SoundPitch = 1.0f;

	/** Usable now. Set from bStartWithAllWeapons on BeginPlay; pickups unlock (UnlockWeapon). */
	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Transient, Category = "Jam|Weapon")
	bool bUnlocked = true;

	/** Rounds in the magazine now. Set to MagazineSize on BeginPlay. */
	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Transient, Category = "Jam|Weapon")
	int32 Ammo = 0;
};

/**
 * The guns of a jam pawn - the zero-G pawn and the test room's character
 * carry the same one: 1 pistol, 2 shotgun, 3 sniper; unlocks, magazines and
 * reserves, R reload, 1/2/3 and the wheel select, automatic fire while LMB is
 * held (the pawn binds LMB: it also throws).
 *
 * Fire is a hitscan per pellet on the Shot channel through
 * MKResolveImpactFromHit + MKDeliverResolvedImpact, plus HitImpulse on every
 * simulated body hit. The shooter's recoil is not applied here: Fire returns
 * it and the pawn applies it its own way (the zero-G body takes all of it,
 * the test room's character a small fraction).
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

	/** Played together, 2D, on every shot at the gun's SoundPitch. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Jam|Weapons")
	TArray<TObjectPtr<USoundBase>> ShotSounds;

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
	void FireRay(const FJamWeapon& Weapon, const FVector& Start, const FVector& Direction);
	void SelectStep(int32 Step);

	double NextShotSeconds = 0.0;
	double ReloadEndSeconds = 0.0;

	UPROPERTY(Transient)
	TArray<TObjectPtr<UInputAction>> InputActions;
};
