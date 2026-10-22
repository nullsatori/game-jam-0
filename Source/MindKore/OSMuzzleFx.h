#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "OSMuzzleFx.generated.h"

class UHeterogeneousVolumeComponent;
class UInstancedStaticMeshComponent;
class UMaterialInstanceDynamic;
class UMaterialInterface;
class UPointLightComponent;
class UStaticMesh;
struct FOSPortSmoke;

/** One gun's shot in vacuum: the flash and the powder smoke. Sizes are sprite diameters, cm. */
USTRUCT(BlueprintType)
struct FOSMuzzleFx
{
	GENERATED_BODY()

	/**
	 * The flash: a burst of hot gas at the muzzle made of FlashLobes ragged blobs
	 * (each shot its own shapes, turns and directions). White-hot for
	 * FlashPeakSeconds (1-2 frames at 60 fps), then a fast fall to a dim orange
	 * glow that cools out by FlashGlowSeconds, under the smoke.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OS|MuzzleFx", meta = (ClampMin = "1"))
	int32 FlashLobes = 4;

	/** A lobe's diameter when grown, cm (each 0.6..1.2 of it). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OS|MuzzleFx", meta = (ClampMin = "0.0"))
	float FlashSizeCm = 9.0f;

	/** How far the lobes are thrown from the muzzle, cm (each 0.3..1 of it). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OS|MuzzleFx", meta = (ClampMin = "0.0"))
	float FlashReachCm = 5.0f;

	/** 0: lobes go every way; higher: they lean out along the bore (the gun's X: the flash lives in the gun's space). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OS|MuzzleFx", meta = (ClampMin = "0.0"))
	float FlashForward = 1.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OS|MuzzleFx", meta = (ClampMin = "0.001"))
	float FlashPeakSeconds = 0.025f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OS|MuzzleFx", meta = (ClampMin = "0.01"))
	float FlashGlowSeconds = 0.12f;

	/** Muzzle-brake side jets (the SV-98): lobes out of both sides of the gun (its Y) reaching this far, cm. 0: none. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OS|MuzzleFx", meta = (ClampMin = "0.0"))
	float SideJetCm = 0.0f;

	/** Incandescent grains flying straight out of the muzzle (no air, no gravity), and their speed, cm/s. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OS|MuzzleFx", meta = (ClampMin = "0"))
	int32 Sparks = 3;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OS|MuzzleFx", meta = (ClampMin = "0.0"))
	float SparkSpeed = 500.0f;

	/** The flash's light on the gun, the smoke and around at its peak (unitless, no inverse square: about the same on all within ~1 m). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OS|MuzzleFx", meta = (ClampMin = "0.0"))
	float LightIntensity = 30.0f;

	/** Smoke puffs per shot. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OS|MuzzleFx", meta = (ClampMin = "0"))
	int32 SmokePuffs = 5;

	/** A puff's life, s (each one 0.75..1.25 of it). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OS|MuzzleFx", meta = (ClampMin = "0.1"))
	float SmokeSeconds = 3.0f;

	/** A puff's diameter at the muzzle and at the end of its life. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OS|MuzzleFx", meta = (ClampMin = "0.0"))
	float SmokeStartCm = 3.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OS|MuzzleFx", meta = (ClampMin = "0.0"))
	float SmokeEndCm = 16.0f;

	/** How far the puffs' centres are thrown from the muzzle: fast, then they freeze (no air to slow or lift them)... */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OS|MuzzleFx", meta = (ClampMin = "0.0"))
	float SmokeSpreadCm = 8.0f;

	/** ...and how far the whole cloud rolls back along the barrel over the gun. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OS|MuzzleFx", meta = (ClampMin = "0.0"))
	float SmokeBackCm = 6.0f;

	/** A puff's peak opacity (of its densest band): sparse smoke stays low. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OS|MuzzleFx", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float SmokeOpacity = 0.15f;

	/** After the throw, each puff keeps drifting straight on at this speed, cm/s: no buoyancy. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OS|MuzzleFx", meta = (ClampMin = "0.0"))
	float SmokeDriftCm = 2.0f;

	/**
	 * Share of the puffs (and of the gas) out of a muzzle brake's side ports: half
	 * out of each side of the gun, leaning back, in a row reaching further out - two
	 * jets across the bore; the rest go on out of the bore (every way without a brake).
	 * Puffs out of the ports are drawn out along their jet, not along the shot.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OS|MuzzleFx", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float SmokeBrake = 0.0f;

	/** Puffs are wisps drawn out along the shot, up to this length over width. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OS|MuzzleFx", meta = (ClampMin = "1.0"))
	float SmokeStretch = 1.5f;

	/** 0: a whole puff; 1: torn into rags and holes. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OS|MuzzleFx", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float SmokeTear = 0.2f;

	/** Volumetric cloud (os.Fx.VolumetricSmoke 1): its box, cm, and voxels along each side. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OS|MuzzleFx|Volume", meta = (ClampMin = "10.0"))
	float CloudCm = 60.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OS|MuzzleFx|Volume", meta = (ClampMin = "8", ClampMax = "64"))
	int32 CloudVoxels = 32;

	/**
	 * The gas's speed out of the muzzle along the bore at the shot, cm/s (a brake
	 * keeps only 1 - SmokeBrake of it forward), on top of the gun's own velocity; the
	 * throw dies within a second down to SmokeDriftCm, the gun's velocity stays
	 * (AirDragSeconds in air).
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OS|MuzzleFx|Volume", meta = (ClampMin = "0.0"))
	float CloudThrowCm = 60.0f;

	/** Extinction of the densest band, 1/cm: thin smoke stays low. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OS|MuzzleFx|Volume", meta = (ClampMin = "0.0"))
	float CloudExtinction = 0.04f;

	/** A round through an older cloud bores a tunnel this wide (radius, cm) and sheds a vortex ring. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OS|MuzzleFx|Volume", meta = (ClampMin = "0.0"))
	float TunnelCm = 3.0f;

	/** This gun's blast shoves the older clouds ahead of the muzzle along the bore, cm/s (the shotgun). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OS|MuzzleFx|Volume", meta = (ClampMin = "0.0"))
	float ShoveCm = 0.0f;
};

/**
 * Muzzle flash and powder smoke of the OS guns, in vacuum: no air carries a
 * flame tongue or blows the smoke away. The flash is a brief irregular burst
 * of hot gas - ragged lobes, a few sparks, side jets from a muzzle brake - with
 * a light; the smoke is a few thin puffs, lit by the scene, thrown out fast
 * and then frozen, drifting on without buoyancy, fading over seconds. The
 * smoke keeps the game's low-res look on purpose: a texel grid per puff, 5
 * density bands with Bayer-dithered steps, a faint scanline fringe on its
 * edge, a heat shimmer behind it for its first 100 ms.
 *
 * The flash is the gun's: its ~0.1 s of hot gas stays on the muzzle socket, in
 * the gun's own space, through the kick. The smoke is the world's (owner 19.10,
 * «делаем честную физику»): born where the gas leaves the gun, thrown along the
 * muzzle's exhaust at the shot (the bore, or a brake's side ports), with the
 * gun's velocity at the shot (the shooter's before the kick). The throw dies as
 * the gas spreads; the gun's velocity is the centre of mass's and stays in a
 * vacuum (only air takes it, AirDragSeconds). Nothing ties it to the shooter -
 * a recoil carries the pawn away from it. That holds for the volumetric clouds (os.Fx.VolumetricSmoke 1) and
 * for the card puffs (0) alike: the cards are worked out in the world and only
 * drawn as sprites turned to the owner's camera. Flash and cards are engine
 * planes in two instanced meshes (the flash's on that camera, the cards' at the
 * world's origin); the look is in
 * M_Jam_MuzzleFlash / M_Jam_MuzzleSmoke / M_Jam_MuzzleSmokeVolume
 * (Scripts/os_muzzle_fx.py).
 *
 * UOSWeaponsComponent makes this on BeginPlay and calls Shot with the muzzle
 * before the shot's recoil reaches the owner.
 */
UCLASS(ClassGroup = (OS))
class MINDKORE_API UOSMuzzleFxComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UOSMuzzleFxComponent();

	/** By UOSWeaponsComponent's index: 0 pistol, 1 shotgun, 2 SV-98. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OS|MuzzleFx")
	TArray<FOSMuzzleFx> Guns;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OS|MuzzleFx")
	TObjectPtr<UStaticMesh> SpriteMesh = nullptr;

	/** Soft, loaded on BeginPlay: a hard reference roots them in the editor and Scripts/os_muzzle_fx.py could not rebuild them. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OS|MuzzleFx")
	TSoftObjectPtr<UMaterialInterface> FlashMaterial = TSoftObjectPtr<UMaterialInterface>(FSoftObjectPath(TEXT("/Game/JamArt/FX/M_Jam_MuzzleFlash.M_Jam_MuzzleFlash")));

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OS|MuzzleFx")
	TSoftObjectPtr<UMaterialInterface> SmokeMaterial = TSoftObjectPtr<UMaterialInterface>(FSoftObjectPath(TEXT("/Game/JamArt/FX/M_Jam_MuzzleSmoke.M_Jam_MuzzleSmoke")));

	/** Clouds alive at once (volumetric smoke); the oldest is reused. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OS|MuzzleFx|Volume", meta = (ClampMin = "1"))
	int32 MaxClouds = 4;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OS|MuzzleFx|Volume")
	TSoftObjectPtr<UMaterialInterface> CloudMaterial = TSoftObjectPtr<UMaterialInterface>(FSoftObjectPath(TEXT("/Game/JamArt/FX/M_Jam_MuzzleSmokeVolume.M_Jam_MuzzleSmokeVolume")));

	/** A cloud's life, s. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OS|MuzzleFx|Volume", meta = (ClampMin = "0.5"))
	float CloudSeconds = 7.0f;

	/**
	 * In a breathable zone (OSAirZoneAt) the air takes the velocity the smoke had from
	 * the gun with this time constant, s; in a vacuum it keeps it.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OS|MuzzleFx", meta = (ClampMin = "0.05"))
	float AirDragSeconds = 1.5f;

	/** Voxels this close to the eye fade out (cm): flying through a cloud never fills the lens. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OS|MuzzleFx|Volume", meta = (ClampMin = "0.0"))
	float EyeClearCm = 30.0f;

	/** Clouds alive now (tests). */
	int32 GetCloudCount() const;

	/** The living clouds in the world: centre, its shot's muzzle, distance from the eye (log lines of the stills). */
	FString DescribeClouds() const;

	/** Remove every cloud (stills). */
	void ClearClouds();

	/** Oldest puffs go first past this. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OS|MuzzleFx", meta = (ClampMin = "1"))
	int32 MaxPuffs = 160;

	/**
	 * A shot of gun Weapon from Muzzle (world; X along the bore, Y out of the gun's side),
	 * the owner still at its velocity before the kick. The flash rides GunMesh (the gun in
	 * view) through its kick; null: it stays put in the camera's space. FlashScale and SmokeScale: a muzzle
	 * device's share of the flash (lobes' reach, size and light, sparks) and of the card smoke (puffs and their
	 * opacity) - the suppressor's (FOSAttachment, Docs/Attachments.md); the volumetric cloud is not scaled yet.
	 */
	void Shot(int32 Weapon, const FTransform& Muzzle, const USceneComponent* GunMesh, float FlashScale = 1.0f, float SmokeScale = 1.0f);

	/**
	 * Count card puffs out of a perforated shroud's holes (FOSPortSmoke, the RMB-93's), each out of a hole picked at
	 * random on Action (the part the shroud rides) as it is now, thrown ThrowCm out across the bore, born up to AgeSpread s
	 * ago (a seep spread over the frame). In the world from then on, as the muzzle's cards; in air, hot smoke rises.
	 */
	void PortPuffs(const FOSPortSmoke& Vent, const USceneComponent& Action, int32 Count, float ThrowCm, float AgeSpread);

	/** Card puffs alive now (probes). */
	int32 GetPuffCount() const { return Puffs.Num(); }

	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

protected:
	virtual void BeginPlay() override;

private:
	/** A card puff, in the world: thrown from the muzzle at the shot, its directions in the bore's frame then. */
	struct FPuff
	{
		FVector Origin = FVector::ZeroVector;
		FQuat Bore = FQuat::Identity;
		/** The gun's velocity at the shot, world cm/s: kept in a vacuum, dying in air (AirDragSeconds), as a cloud's. */
		FVector GunVelocity = FVector::ZeroVector;
		/** AirDragSeconds if the shot was in air, 0 in a vacuum. */
		float AirDragSeconds = 0.0f;
		FVector Direction = FVector::ZeroVector;
		/** The wisp's long axis, bore frame: the shot, or its jet out of a brake port. */
		FVector Along = FVector::ForwardVector;
		float SpreadCm = 0.0f;
		float BackCm = 0.0f;
		float StartCm = 0.0f;
		float EndCm = 0.0f;
		float Opacity = 0.0f;
		float Life = 1.0f;
		float Age = 0.0f;
		float Seed = 0.0f;
		float Spin = 0.0f;
		float DriftCm = 0.0f;
		float Stretch = 1.0f;
		float Tear = 0.0f;
		/** In air, hot smoke's lift (world cm/s^2) and the air's hold on it, s: it rises toward Lift x LiftDragSeconds. */
		FVector Lift = FVector::ZeroVector;
		float LiftDragSeconds = 0.0f;
	};

	/** A camera-facing sprite at Location, XAxis along its length (projected on the view plane); all in camera space. */
	static FTransform Sprite(const FVector& Location, const FVector& XAxis, float LengthCm, float WidthCm);

	UPROPERTY(Transient)
	TObjectPtr<UInstancedStaticMeshComponent> Flash = nullptr;

	UPROPERTY(Transient)
	TObjectPtr<UInstancedStaticMeshComponent> Smoke = nullptr;

	UPROPERTY(Transient)
	TObjectPtr<UPointLightComponent> Light = nullptr;

	/** The owner's camera: the eye the sprites turn to and the rounds leave from. */
	UPROPERTY(Transient)
	TObjectPtr<USceneComponent> View = nullptr;

	/** One piece of the flash: a lobe of gas or a spark, relative to the muzzle in the bore's frame. */
	struct FFlashBit
	{
		FVector Direction = FVector::ForwardVector;
		float ReachCm = 0.0f;
		float SizeCm = 0.0f;
		/** Length over width (a jet, a spark streak). */
		float Stretch = 1.0f;
		float Brightness = 1.0f;
		/** Times the burst's heat: 1 the white-hot core, lower stays yellow-orange. */
		float Heat = 1.0f;
		float Seed = 0.0f;
		float Angle = 0.0f;
		/** Sparks: cm/s, 0 for a lobe. */
		float Speed = 0.0f;
		float Life = 0.1f;
	};

	/** A wake through a cloud: a round's tunnel (with its vortex ring), the pawn's or a flyer's passage. Cloud space. */
	struct FWake
	{
		FVector Origin = FVector::ZeroVector;
		FVector Direction = FVector::ForwardVector;
		float RadiusCm = 0.0f;
		float Age = 0.0f;
		/** Ring speed along the wake, cm/s (0: no ring). */
		float RingSpeed = 0.0f;
	};

	/** One volumetric smoke cloud in the world: a heterogeneous volume voxelising M_Jam_MuzzleSmokeVolume. */
	struct FCloud
	{
		TObjectPtr<UHeterogeneousVolumeComponent> Volume = nullptr;
		TObjectPtr<UMaterialInstanceDynamic> Material = nullptr;
		int32 Gun = 0;
		FVector Centre = FVector::ZeroVector;
		/** The centre of mass's velocity from the gun at the shot, world cm/s: kept in a vacuum, dying in air. */
		FVector Carried = FVector::ZeroVector;
		/** The exhaust's throw and shoves on top of Carried, dying down to DriftVelocity. */
		FVector Velocity = FVector::ZeroVector;
		FVector DriftVelocity = FVector::ZeroVector;
		/** The muzzle at the shot, world (log lines). */
		FVector Muzzle = FVector::ZeroVector;
		/** The cloud's axes: the muzzle's frame at the shot (X along the bore); for a brake turned so X runs out of its ports (the puffs are drawn out along X). */
		FQuat Basis = FQuat::Identity;
		float Age = -1.0f;
		float Seed = 0.0f;
		/** Puffs: direction and reach from the centre, cloud space. */
		TArray<FVector4f> PuffThrow;
		TArray<FWake> Wakes;
	};

	void SpawnCloud(int32 Weapon, const FTransform& Muzzle, const FVector& GunVelocity);
	void TickClouds(float DeltaTime);
	/** A wake along Origin + t Direction (world) through every living cloud it crosses but Except. */
	void AddWake(const FVector& Origin, const FVector& Direction, float RadiusCm, float RingSpeed, const FCloud* Except);

	TArray<FCloud> Clouds;
	/** Keeps the clouds' components and materials alive (FCloud is not reflected). */
	UPROPERTY(Transient)
	TArray<TObjectPtr<UObject>> CloudObjects;
	FVector LastEye = FVector::ZeroVector;

	TArray<FPuff> Puffs;
	TArray<FFlashBit> FlashBits;
	int32 FlashGun = INDEX_NONE;
	/** The flash's muzzle (X along the bore) in FlashGunMesh's space (or the camera's without it). */
	FTransform FlashMuzzle = FTransform::Identity;
	TWeakObjectPtr<const USceneComponent> FlashGunMesh;
	float FlashAge = 0.0f;
};
