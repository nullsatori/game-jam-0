#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "OSSmokeSim.h"
#include "OSWeapons.h"
#include "Tasks/Task.h"
#include "OSMuzzleFx.generated.h"

class UHeterogeneousVolumeComponent;
class UInstancedStaticMeshComponent;
class UMaterialInstanceDynamic;
class UMaterialInterface;
class UPointLightComponent;
class UStaticMesh;
class UTexture2D;
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

	/**
	 * Volumetric cloud (os.Fx.VolumetricSmoke 1): a smoke grid (FOSSmokeSim) of Cells a side of CellCm (the box is
	 * Cells x CellCm), drawn voxel for voxel. Docs/RecoilWeapons-SmokeSim.md.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OS|MuzzleFx|Volume", meta = (ClampMin = "8", ClampMax = "48"))
	int32 Cells = 24;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OS|MuzzleFx|Volume", meta = (ClampMin = "1.0"))
	float CellCm = 3.0f;

	/** Extinction of the dense band, 1/cm (the material's bands are 1/3, 2/3 and all of it). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OS|MuzzleFx|Volume", meta = (ClampMin = "0.0"))
	float DenseExtinction = 0.1f;

	/** A round through a cloud: within RadiusCm it pushes the gas out of its line and along it (cm/s) and throws Clear of it aside at once. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OS|MuzzleFx|Volume", meta = (ClampMin = "0.0"))
	float RoundRadiusCm = 8.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OS|MuzzleFx|Volume", meta = (ClampMin = "0.0"))
	float RoundPushCm = 120.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OS|MuzzleFx|Volume", meta = (ClampMin = "0.0"))
	float RoundAlongCm = 200.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OS|MuzzleFx|Volume", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float RoundClear = 0.7f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OS|MuzzleFx|Volume")
	FOSSmokeParams Sim;

	/** The grid steps this long after the shot (air / vacuum), s, then freezes (any push wakes it for a second). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OS|MuzzleFx|Volume", meta = (ClampMin = "0.1"))
	float ActiveAirSeconds = 3.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OS|MuzzleFx|Volume", meta = (ClampMin = "0.1"))
	float ActiveVacuumSeconds = 1.2f;

	/** How hard the material eats the cloud over the second half of its life (thin voxels first), and the life, s. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OS|MuzzleFx|Volume", meta = (ClampMin = "0.0"))
	float Erode = 0.9f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OS|MuzzleFx|Volume", meta = (ClampMin = "0.5"))
	float CloudSeconds = 6.0f;

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
 * smoke keeps the game's low-res look on purpose: a texel grid per puff, 3
 * density bands with Bayer-dithered steps, a faint scanline fringe on its
 * edge, a heat shimmer behind it for its first 100 ms. The volumetric cloud
 * (os.Fx.VolumetricSmoke 1) is a small gas simulation (FOSSmokeSim) drawn
 * voxel for voxel (Docs/RecoilWeapons-Fx.md, «Живой дым»).
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
 * M_OS_MuzzleFlash / M_OS_MuzzleSmoke / M_OS_MuzzleSmokeVolume
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
	TSoftObjectPtr<UMaterialInterface> FlashMaterial = TSoftObjectPtr<UMaterialInterface>(FSoftObjectPath(TEXT("/Game/OSArt/FX/M_OS_MuzzleFlash.M_OS_MuzzleFlash")));

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OS|MuzzleFx")
	TSoftObjectPtr<UMaterialInterface> SmokeMaterial = TSoftObjectPtr<UMaterialInterface>(FSoftObjectPath(TEXT("/Game/OSArt/FX/M_OS_MuzzleSmoke.M_OS_MuzzleSmoke")));

	/**
	 * Clouds alive at once (volumetric smoke). When a shot takes the last free one the oldest starts fading out over
	 * EvictSeconds (v6: it was cut off at once); only with none free at all is the oldest reused on the spot.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OS|MuzzleFx|Volume", meta = (ClampMin = "1"))
	int32 MaxClouds = 6;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OS|MuzzleFx|Volume", meta = (ClampMin = "0.05"))
	float EvictSeconds = 0.5f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OS|MuzzleFx|Volume")
	TSoftObjectPtr<UMaterialInterface> CloudMaterial = TSoftObjectPtr<UMaterialInterface>(FSoftObjectPath(TEXT("/Game/OSArt/FX/M_OS_MuzzleSmokeVolume.M_OS_MuzzleSmokeVolume")));

	/** Clouds whose grids step in one frame (the youngest); the rest hold still until they are among them again. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OS|MuzzleFx|Volume", meta = (ClampMin = "1"))
	int32 MaxActiveSims = 6;

	/** A push (a round, the pawn, a flyer, a hot case) wakes a frozen cloud's grid for this long, s. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OS|MuzzleFx|Volume", meta = (ClampMin = "0.0"))
	float WakeSeconds = 1.0f;

	/** How much the gas around a voxel shadows the material's ambient, and the gas toward the key light its key light (FOSSmokeSim::Pack). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OS|MuzzleFx|Volume", meta = (ClampMin = "0.0"))
	float AmbientOcclusion = 0.15f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OS|MuzzleFx|Volume", meta = (ClampMin = "0.0"))
	float KeyShadow = 1.2f;

	/** The drawn density is the gas less this share of its blur (5 cells): the soft cushion goes, rags stay. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OS|MuzzleFx|Volume", meta = (ClampMin = "0.0", ClampMax = "0.9"))
	float Sharpen = 0.75f;

	/** The key light for the material: its colour x its illuminance at the cloud / pi x this. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OS|MuzzleFx|Volume", meta = (ClampMin = "0.0"))
	float KeyStrength = 0.014f;

	/** ...at most this (a lamp right over the cloud, the flash inside it). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OS|MuzzleFx|Volume", meta = (ClampMin = "0.0"))
	float KeyMax = 3.0f;

	/** The material's room ambient (the bounced light the volume does not get) outside air: none to bounce in space. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OS|MuzzleFx|Volume", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float VacuumAmbientShare = 0.0f;

	/** A spent case smokes while hot: its heat's time constant, s (1 when it leaves the port). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OS|MuzzleFx", meta = (ClampMin = "0.05"))
	float CaseHeatSeconds = 1.4f;

	/**
	 * In a breathable zone (OSAirZoneAt) the air takes the velocity the smoke had from
	 * the gun with this time constant, s; in a vacuum it keeps it.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OS|MuzzleFx", meta = (ClampMin = "0.05"))
	float AirDragSeconds = 0.2f;

	/**
	 * The volume's drawn pixel, cm: fixed in the world for a cloud's whole life (v5: a pixel picked from the distance to
	 * the eye jumped 2x at thresholds and the cloud re-pixelated as you walked). The march step
	 * (r.HeterogeneousVolumes.StepSize, in cells) must stay at most half of it, or the pixels alias as the view turns.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OS|MuzzleFx|Volume", meta = (ClampMin = "0.5"))
	float VoxelPixelCm = 3.0f;

	/** Voxels this close to the eye fade out (cm): flying through a cloud never fills the lens. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OS|MuzzleFx|Volume", meta = (ClampMin = "0.0"))
	float EyeClearCm = 30.0f;

	/** Clouds alive now (tests). */
	int32 GetCloudCount() const;

	/** The living clouds in the world: centre, its shot's muzzle, distance from the eye (log lines of the stills). */
	FString DescribeClouds() const;

	/** Each living cloud's grid atlas (FOSSmokeSim::Pack, raw BGRA) to Saved/SmokeDump/<Name>_<slot>.raw (stills). */
	void DumpClouds(const FString& Name) const;

	/** Remove every cloud (stills). */
	void ClearClouds();

	/**
	 * A projectile through the smoke (owner 23.10: «чтобы проджектайлы дырявили как в кс2»): every round, pellet or
	 * hitscan line punches its own tunnel of RadiusCm from Start to End. Seen on the same frame - the material carves
	 * it - clean for TunnelHoldSeconds, then healing over TunnelHealSeconds (the radius shrinks, the edge goes ragged;
	 * twice as slow in a vacuum); the grid gets the hole and a push at once, its inflow closes it.
	 */
	void CarveRay(const FVector& Start, const FVector& End, float RadiusCm);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OS|MuzzleFx|Volume", meta = (ClampMin = "0.0"))
	float TunnelHoldSeconds = 0.8f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OS|MuzzleFx|Volume", meta = (ClampMin = "0.05"))
	float TunnelHealSeconds = 2.5f;

	/** Tunnels drawn at once (per cloud: the material's loop); the oldest go first. */
	static constexpr int32 MaxTunnels = 32;

	int32 GetTunnelCount() const { return Tunnels.Num(); }

	/**
	 * A radial blow at Location (an impact, a blast): the gas within RadiusCm thrown out of it at up to SpeedCm cm/s
	 * (half again in a vacuum, where no air holds it back), a share of it at the centre blown away. Normal leans it
	 * (an impact throws back out of the surface; zero: every way).
	 */
	void Burst(const FVector& Location, const FVector& Normal, float RadiusCm, float SpeedCm);

	/**
	 * Air leaving through a hole (an open airlock, a breach) for Seconds: clouds within ReachCm are drawn toward
	 * Location, faster the closer, and blown out along Direction past it. Nothing in the game decompresses yet
	 * (23.10): this is the hook for it.
	 */
	void AddAirFlow(const FVector& Location, const FVector& Direction, float SpeedCm, float ReachCm, float Seconds);

	/** The youngest cloud at least MinAge old: its box centre and half side (tests). */
	bool GetYoungestCloud(float MinAge, FVector& OutCentre, float& OutHalf) const;

	/** Oldest puffs go first past this. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OS|MuzzleFx", meta = (ClampMin = "1"))
	int32 MaxPuffs = 320;

	/**
	 * A shot of gun Weapon from Muzzle (world; X along the bore, Y out of the gun's side),
	 * the owner still at its velocity before the kick. The flash rides GunMesh (the gun in
	 * view) through its kick; null: it stays put in the camera's space. FlashScale and SmokeScale: a muzzle
	 * device's share of the flash (lobes' reach, size and light, sparks) and of the card smoke (puffs and their
	 * opacity) - the suppressor's (FOSAttachment, Docs/Attachments.md); the volumetric cloud is not scaled yet.
	 */
	void Shot(int32 Weapon, const FTransform& Muzzle, const USceneComponent* GunMesh, float FlashScale = 1.0f, float SmokeScale = 1.0f);

	/**
	 * The gas of a shot out of Def's vents (FOSGasVent, Docs/RecoilWeapons-Fx.md «Газовые отверстия»): jets into the
	 * shot's grid, streams of wisps over their Seconds. Muzzle is the muzzle's world frame at the shot, Action the part
	 * the Action-space vents ride. UOSWeaponsComponent::Fire calls this right after Shot.
	 */
	void Vents(const FOSWeapon& Def, const FTransform& Muzzle, const USceneComponent* Action);

	/**
	 * Count seeping wisps out of Def's bSeep vents (picked at random), born up to AgeSpread s ago (a seep spread over
	 * the frame); FOSPortSmoke's heat drives the count (UOSWeaponsComponent::UpdatePortSmoke).
	 */
	void Seep(const FOSWeapon& Def, const FTransform& Muzzle, const USceneComponent* Action, int32 Count, float AgeSpread);

	/** A spent case just thrown out: it drags a wisp and cuts the clouds it flies through, and smokes on the floor while hot. */
	void HotCase(AActor* Case);

	/** Vents still flowing as wisp streams (probes). */
	int32 GetFlowCount() const { return Flows.Num(); }

	/** Card puffs alive now (probes). */
	int32 GetPuffCount() const { return Puffs.Num(); }

	/** Spent cases still hot (stills). */
	int32 GetHotCaseCount() const { return HotCases.Num(); }

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
		/** A rising wisp sways across its rise (cm) and stands up along it; 0 in a vacuum. */
		float SwayCm = 0.0f;
		/** Opacity comes up over this long (no pop). */
		float FadeInSeconds = 0.05f;
		/**
		 * A shroud wisp clinging to the gun (ClingSeconds > 0): a path in Anchor's space round the bore (at BoreZ on
		 * the part's X axis) from Port - its angle Theta, radius Radius, creeping back, swirling - followed in the world
		 * with weight e^(-Age / ClingSeconds), the rest of its motion the world's. WorldPos is where it is.
		 */
		TWeakObjectPtr<const USceneComponent> Anchor;
		FVector Port = FVector::ZeroVector;
		float BoreZ = 0.0f;
		float Theta = 0.0f;
		float Radius = 0.0f;
		float SwirlRadPerSecond = 0.0f;
		float CreepCmPerSecond = 0.0f;
		float OutCm = 0.0f;
		float ClingSeconds = 0.0f;
		FVector WorldPos = FVector::ZeroVector;
		FVector PrevAttached = FVector::ZeroVector;
		/** The anchor's transform last frame: its motion is split off the path's and inherited only in part. */
		FTransform PrevGun;
		FVector AlongWorld = FVector::ForwardVector;
		/** Its own velocity once the gun lets go (a vacuum: on outward, straight), world cm/s. */
		FVector Velocity = FVector::ZeroVector;
	};

	/** A vent still flowing as wisps after a shot. */
	struct FFlow
	{
		FOSGasVent Vent;
		FOSPortSmoke Look;
		FTransform Muzzle;
		TWeakObjectPtr<const USceneComponent> Action;
		float Age = 0.0f;
		float Carry = 0.0f;
	};
	/** One wisp out of Vent (Speed: its flow's speed share now), born AgeOffset s ago. */
	void EmitWisp(const FOSGasVent& Vent, const FOSPortSmoke& Look, const FTransform& Muzzle, const USceneComponent* Action, float Speed, float AgeOffset, float OpacityScale = 1.0f);
	TArray<FFlow> Flows;
	/** A clinging wisp's point on its path in the gun's space at Age. */
	static FVector ClingPath(const FPuff& Puff, float Age);

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

	/** One volumetric smoke cloud in the world: a smoke grid drawn by a heterogeneous volume (M_OS_MuzzleSmokeVolume reads its atlas). */
	struct FCloud
	{
		TObjectPtr<UHeterogeneousVolumeComponent> Volume = nullptr;
		TObjectPtr<UMaterialInstanceDynamic> Material = nullptr;
		/** The grid's atlas (FOSSmokeSim::Pack), point-sampled by the material. */
		TObjectPtr<UTexture2D> Grid = nullptr;
		TUniquePtr<FOSSmokeSim> Sim;
		TArray<FColor> Atlas;
		int32 Gun = 0;
		/** The box's centre, world; it moves by whole cells after the gas (and with Carried / Rise). */
		FVector Centre = FVector::ZeroVector;
		/** The gun's velocity at the shot (and the shotgun's shoves), world cm/s: kept in a vacuum, dying in air. */
		FVector Carried = FVector::ZeroVector;
		/** Air, frozen: the whole box keeps rising at the gas's mean speed at the freeze, dying (tau 2 s). */
		FVector Rise = FVector::ZeroVector;
		/** The muzzle at the shot and its bore, world (the emission, log lines). */
		FVector Muzzle = FVector::ZeroVector;
		FVector Bore = FVector::ForwardVector;
		/** The box's axes: the bore skewed (yaw 23, roll 17) so no voxel axis lies along the view down the bore. */
		FQuat Basis = FQuat::Identity;
		float Age = -1.0f;
		float Seed = 0.0f;
		/** Seconds left awake after a push; the 60 Hz step's leftover time. */
		float Awake = 0.0f;
		float StepCarry = 0.0f;
		/** A vacuum cloud frozen: the box grows by Scale (free streaming frozen is a uniform scale), ExpandRate per second. */
		float Scale = 1.0f;
		float ExpandRate = 0.0f;
		bool bFrozen = false;
		/** The grid's steps run off the game thread (launched at the end of TickClouds); its result - the shift the box
		 *  owes the gas and a packed atlas - is taken on the game thread after the wait. */
		/** The key light toward which the grid's shadow is packed (box frame) and its colour x strength for the material. */
		FVector3f KeyDir = FVector3f::UpVector;
		FLinearColor KeyColour = FLinearColor::White;
		FString KeyName;
		/** Bodies in its box this frame (box frame): the grid's moving boundaries and the material's carve. */
		TArray<FOSSmokeSim::FMover> Movers;
		/** The tunnels through it this frame (box frame), for the material: 32 x 2 floats. */
		TObjectPtr<UTexture2D> TunnelTexture = nullptr;
		int32 TunnelCount = 0;
		/** Pushes that came while a step was in flight: applied once it is done (a round never misses a cloud). */
		struct FDeferredBlast
		{
			FVector Origin, Direction;
			float RadiusCm, Along, Push, Clear, LengthCm;
		};
		TArray<FDeferredBlast> Deferred;
		/** Fading out early (another shot needed its slot): seconds left, 0 when not. */
		float Evict = 0.0f;
		/** A vacuum cloud frozen keeps turning at its gas's angular velocity at the freeze (box frame axis x rad/s). */
		FVector3f Spin = FVector3f::ZeroVector;
		/** Gas in the last packed atlas; the frame of the shot (os.Fx.SmokeCost: shot-to-visible latency). */
		bool bHasGas = false;
		bool bShown = false;
		uint64 ShotFrame = 0;
		/** The jets feeding this cloud: world origin and direction at the shot, the vent's flow. */
		struct FJet
		{
			FVector Origin = FVector::ZeroVector;
			FVector Direction = FVector::ForwardVector;
			/** The gun's velocity at the shot: the vent rides on with the gun while it blows. */
			FVector GunVelocity = FVector::ZeroVector;
			FOSGasVent Vent;
			/** How far its front has got, cm (the grid's task moves it). */
			float FrontCm = 0.0f;
		};
		TArray<FJet> Jets;
		mutable UE::Tasks::FTask Pending;
		FIntVector PendingShift = FIntVector::ZeroValue;
		bool bPendingAtlas = false;
		/** The box's moves made while a step was in flight, box frame: owed to the floor planes once it is done. */
		FVector3f PlaneMove = FVector3f::ZeroVector;
		/** A step still in flight: the game thread never waits for it (v5) - the grid, the atlas and the planes wait. */
		bool IsBusy() const { return Pending.IsValid() && !Pending.IsCompleted(); }
	};

	/** A jet's gas let out by At (s after the shot) since its last front, laid into the grid (box at Centre, Basis). */
	static void LayJet(FOSSmokeSim& Sim, FCloud::FJet& Jet, const FQuat& Basis, const FVector& Centre, float At);

	/** A case still hot. */
	struct FHotCase
	{
		TWeakObjectPtr<AActor> Case;
		float Heat = 1.0f;
		/** The share of a card puff not let out yet. */
		float Carry = 0.0f;
	};

	void SpawnCloud(int32 Weapon, const FTransform& Muzzle, const FVector& GunVelocity);
	/** Waits for every grid still stepping off the game thread: before anything on the game thread touches a grid. */
	void WaitSims() const;
	void TickClouds(float DeltaTime);
	void TickHotCases(float DeltaTime);
	/** A push along Origin + t Direction (world) through every living cloud it crosses but Except (FOSSmokeSim::Impulse); wakes them. */
	void Blast(const FVector& Origin, const FVector& Direction, float RadiusCm, float Along, float Push, float Clear, const FCloud* Except, float LengthCm = 1e6f);
	/** The cloud the last shot spawned (Vents gives it its jets). */
	FCloud* LastSpawned = nullptr;
	/** A card puff at Where, world, thrown along Direction (cm over its life's first moments), in air rising. */
	FPuff& AddCardPuff(const FVector& Where, const FVector& Direction, float StartCm, float EndCm, float Opacity, float Seconds);

	TArray<FCloud> Clouds;
	/** Keeps the clouds' components and materials alive (FCloud is not reflected). */
	UPROPERTY(Transient)
	TArray<TObjectPtr<UObject>> CloudObjects;

	struct FTunnel
	{
		FVector Start, End;
		float RadiusCm = 3.0f;
		float Age = 0.0f;
		/** Heals twice as slow (no air to flow in). */
		bool bVacuum = false;
	};
	TArray<FTunnel> Tunnels;
	struct FAirFlow
	{
		FVector Location, Direction;
		float SpeedCm, ReachCm, Seconds;
	};
	TArray<FAirFlow> AirFlows;
	/** Where each body seen in a cloud was last frame: its velocity, whatever moves it (physics, a hand, a door). */
	TMap<TObjectKey<UPrimitiveComponent>, FVector> LastMoverAt;
	/** The bodies in a cloud's box now (one overlap query): its Movers. */
	void FindMovers(FCloud& Cloud, float Half, float DeltaTime, TSet<TObjectKey<UPrimitiveComponent>>& Seen);
	/** One cloud's share of a push (Blast): true if it touched gas. */
	bool BlastCloud(FCloud& Cloud, const FVector& Origin, const FVector& Direction, float RadiusCm, float Along, float Push, float Clear, float LengthCm);
	/** The tunnels through this cloud into its material (box frame, healed as of now). */
	void UpdateTunnels(FCloud& Cloud, float Half);
	/** The lights a cloud may take as its key, refreshed every second. */
	TArray<TWeakObjectPtr<class ULightComponent>> KeyLights;
	float KeyLightsAge = 1e6f;
	FString LastKeyName;
	/** The brightest light at Where, by its brightness over the distance squared (a directional light by its own). */
	void PickKey(const FVector& Where, const FQuat& Basis, FVector3f& OutDir, FLinearColor& OutColour);
	TArray<FHotCase> HotCases;

	TArray<FPuff> Puffs;
	TArray<FFlashBit> FlashBits;
	int32 FlashGun = INDEX_NONE;
	/** The flash's muzzle (X along the bore) in FlashGunMesh's space (or the camera's without it). */
	FTransform FlashMuzzle = FTransform::Identity;
	TWeakObjectPtr<const USceneComponent> FlashGunMesh;
	float FlashAge = 0.0f;
};
