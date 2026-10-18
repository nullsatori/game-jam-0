#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "JamMuzzleFx.generated.h"

class UInstancedStaticMeshComponent;
class UMaterialInterface;
class UPointLightComponent;
class UStaticMesh;

/** One gun's shot in vacuum: the flash and the powder smoke. Sizes are sprite diameters, cm. */
USTRUCT(BlueprintType)
struct FJamMuzzleFx
{
	GENERATED_BODY()

	/** The flash: a ball of hot gas swelling from FlashStartCm to FlashEndCm and dying out over FlashSeconds. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Jam|MuzzleFx", meta = (ClampMin = "0.01"))
	float FlashSeconds = 0.06f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Jam|MuzzleFx", meta = (ClampMin = "0.0"))
	float FlashStartCm = 5.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Jam|MuzzleFx", meta = (ClampMin = "0.0"))
	float FlashEndCm = 14.0f;

	/** Muzzle-brake side jets (the SV-98): two lobes out of the gun's sides, this long at the end. 0: none. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Jam|MuzzleFx", meta = (ClampMin = "0.0"))
	float SideJetCm = 0.0f;

	/** The flash's light on the gun and around it, candela at its peak. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Jam|MuzzleFx", meta = (ClampMin = "0.0"))
	float LightCandela = 30.0f;

	/** Smoke puffs per shot. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Jam|MuzzleFx", meta = (ClampMin = "0"))
	int32 SmokePuffs = 5;

	/** A puff's life, s (each one 0.75..1.25 of it). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Jam|MuzzleFx", meta = (ClampMin = "0.1"))
	float SmokeSeconds = 3.0f;

	/** A puff's diameter at the muzzle and at the end of its life. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Jam|MuzzleFx", meta = (ClampMin = "0.0"))
	float SmokeStartCm = 3.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Jam|MuzzleFx", meta = (ClampMin = "0.0"))
	float SmokeEndCm = 16.0f;

	/** How far the puffs' centres end from the muzzle, in every direction... */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Jam|MuzzleFx", meta = (ClampMin = "0.0"))
	float SmokeSpreadCm = 8.0f;

	/** ...and how far the whole cloud rolls back along the barrel over the gun. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Jam|MuzzleFx", meta = (ClampMin = "0.0"))
	float SmokeBackCm = 6.0f;

	/** A puff's peak opacity: sparse smoke stays low. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Jam|MuzzleFx", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float SmokeOpacity = 0.15f;
};

/**
 * Muzzle flash and powder smoke of the jam guns, in vacuum: no air carries a
 * flame tongue or blows the smoke away. The flash is a short spherical burst
 * (plus side jets from a muzzle brake) with a light; the smoke is a few thin
 * puffs that swell and spread slowly from the muzzle, roll back over the gun
 * and fade over seconds.
 *
 * Everything lives in the owner's camera space, like the gun in view: the
 * cloud stays around the gun when the view turns or the recoil throws the
 * pawn (in world space a 7 m/s kick would leave it behind in a frame).
 * Sprites are engine planes in two instanced meshes, turned to the camera;
 * the look is in M_Jam_MuzzleFlash / M_Jam_MuzzleSmoke (Scripts/jam_muzzle_fx.py).
 *
 * UJamWeaponsComponent makes this on BeginPlay and calls Shot with the muzzle.
 */
UCLASS(ClassGroup = (Jam))
class MINDKORE_API UJamMuzzleFxComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UJamMuzzleFxComponent();

	/** By UJamWeaponsComponent's index: 0 pistol, 1 shotgun, 2 SV-98. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Jam|MuzzleFx")
	TArray<FJamMuzzleFx> Guns;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Jam|MuzzleFx")
	TObjectPtr<UStaticMesh> SpriteMesh = nullptr;

	/** Soft, loaded on BeginPlay: a hard reference roots them in the editor and Scripts/jam_muzzle_fx.py could not rebuild them. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Jam|MuzzleFx")
	TSoftObjectPtr<UMaterialInterface> FlashMaterial = TSoftObjectPtr<UMaterialInterface>(FSoftObjectPath(TEXT("/Game/JamArt/FX/M_Jam_MuzzleFlash.M_Jam_MuzzleFlash")));

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Jam|MuzzleFx")
	TSoftObjectPtr<UMaterialInterface> SmokeMaterial = TSoftObjectPtr<UMaterialInterface>(FSoftObjectPath(TEXT("/Game/JamArt/FX/M_Jam_MuzzleSmoke.M_Jam_MuzzleSmoke")));

	/** Oldest puffs go first past this. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Jam|MuzzleFx", meta = (ClampMin = "1"))
	int32 MaxPuffs = 160;

	/** A shot of gun Weapon from Muzzle (world). The flash rides GunMesh (the gun in view) through its kick; null: it stays put. */
	void Shot(int32 Weapon, const FVector& Muzzle, const USceneComponent* GunMesh);

	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

protected:
	virtual void BeginPlay() override;

private:
	struct FPuff
	{
		FVector Origin = FVector::ZeroVector;
		FVector Direction = FVector::ZeroVector;
		float SpreadCm = 0.0f;
		float BackCm = 0.0f;
		float StartCm = 0.0f;
		float EndCm = 0.0f;
		float Opacity = 0.0f;
		float Life = 1.0f;
		float Age = 0.0f;
		float Seed = 0.0f;
		float Spin = 0.0f;
	};

	/** A camera-facing sprite at Location (camera space), XAxis along its length. */
	static FTransform Sprite(const FVector& Location, const FVector& XAxis, float LengthCm, float WidthCm);

	UPROPERTY(Transient)
	TObjectPtr<UInstancedStaticMeshComponent> Flash = nullptr;

	UPROPERTY(Transient)
	TObjectPtr<UInstancedStaticMeshComponent> Smoke = nullptr;

	UPROPERTY(Transient)
	TObjectPtr<UPointLightComponent> Light = nullptr;

	TArray<FPuff> Puffs;
	int32 FlashGun = INDEX_NONE;
	/** The flash's muzzle in FlashGunMesh's space (or camera space without it). */
	FVector FlashOrigin = FVector::ZeroVector;
	TWeakObjectPtr<const USceneComponent> FlashGunMesh;
	float FlashAge = 0.0f;
	float FlashSeed = 0.0f;
};
