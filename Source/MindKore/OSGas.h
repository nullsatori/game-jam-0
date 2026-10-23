#pragma once

#include "CoreMinimal.h"
#include "OSSmokeSim.h"
#include "Subsystems/WorldSubsystem.h"
#include "Tasks/Task.h"
#include "UObject/ObjectKey.h"
#include "OSGas.generated.h"

class UHeterogeneousVolumeComponent;
class UMaterialInstanceDynamic;
class UMaterialInterface;
class UTexture2D;
class ULightComponent;
class USceneComponent;
class UPrimitiveComponent;

/**
 * Gas v11 (Docs/RecoilWeapons-SmokeV11.md, contract Docs/RecoilWeapons-SmokeV11-Contract.md): every gas in the world
 * - a gun's muzzle and shroud, a hot case, a weld - is parcels (FOSGasParcel) in clouds owned by UOSGasSubsystem.
 * A coarse velocity grid (air only) gives the parcels pressure, buoyancy and the flow round bodies; the parcels carry
 * the density and are splatted with noise into a finer render grid each step.
 */

/** How a cloud looks: one look per cloud (different looks, different clouds). */
USTRUCT(BlueprintType)
struct FOSGasLook
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OS|Gas")
	FLinearColor Colour = FLinearColor(0.50f, 0.52f, 0.56f);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OS|Gas")
	FLinearColor WarmColour = FLinearColor(0.62f, 0.50f, 0.38f);

	/** Extinction of the dense band, 1/cm. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OS|Gas", meta = (ClampMin = "0.0"))
	float Extinction = 0.045f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OS|Gas")
	bool bEmbers = true;

	/** A cloud lives this long at most, s; it thins out over its last 40%. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OS|Gas", meta = (ClampMin = "0.5"))
	float LifeSeconds = 6.0f;

	bool operator==(const FOSGasLook& O) const
	{
		return Colour == O.Colour && WarmColour == O.WarmColour && Extinction == O.Extinction && bEmbers == O.bEmbers && LifeSeconds == O.LifeSeconds;
	}
};

/** What a source lets out: its parcels' recipe. */
USTRUCT(BlueprintType)
struct FOSGasRecipe
{
	GENERATED_BODY()

	/** A burst: this many parcels over BurstSeconds. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OS|Gas", meta = (ClampMin = "0"))
	int32 BurstCount = 0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OS|Gas", meta = (ClampMin = "0.0"))
	float BurstSeconds = 0.1f;

	/** Speed out of the source along its direction, cm/s, times U(SpeedMin, 1). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OS|Gas", meta = (ClampMin = "0.0"))
	float SpeedCm = 300.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OS|Gas", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float SpeedMin = 0.3f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OS|Gas", meta = (ClampMin = "0.0"))
	float ConeDeg = 8.0f;

	/** A vortex ring round the axis: its radius, cm (0: none), and how many turns its core makes per second. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OS|Gas", meta = (ClampMin = "0.0"))
	float RingCm = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OS|Gas", meta = (ClampMin = "0.0"))
	float RingTurnsPerSecond = 2.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OS|Gas", meta = (ClampMin = "0.1"))
	float RadiusMinCm = 3.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OS|Gas", meta = (ClampMin = "0.1"))
	float RadiusMaxCm = 5.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OS|Gas", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float Mass = 1.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OS|Gas", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float Heat = 1.0f;

	/**
	 * Let out in a vacuum instead (0: as in air): a longer, even release so the cone's tail is solid from the muzzle to its
	 * front (owner 23.10: «дробинками»), the speeds' spread and the parcels' starting radius.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OS|Gas", meta = (ClampMin = "0"))
	int32 VacuumBurstCount = 0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OS|Gas", meta = (ClampMin = "0.0"))
	float VacuumBurstSeconds = 0.15f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OS|Gas", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float VacuumSpeedMin = 0.4f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OS|Gas", meta = (ClampMin = "0.1"))
	float VacuumRadiusMinCm = 6.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OS|Gas", meta = (ClampMin = "0.1"))
	float VacuumRadiusMaxCm = 8.0f;

	/** For this long a parcel keeps the carrier's velocity plus its own small throw (a shroud's wisps hug the gun), s. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OS|Gas", meta = (ClampMin = "0.0"))
	float ClingSeconds = 0.0f;

	/** A parcel's life, s. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OS|Gas", meta = (ClampMin = "0.1"))
	float LifeSeconds = 6.0f;
};

/** One parcel of gas, box... cloud-origin frame (cm, world axes). */
struct FOSGasParcel
{
	FVector3f P = FVector3f::ZeroVector;
	FVector3f V = FVector3f::ZeroVector;
	/** Where it was let out (its noise flows with it: the noise is read at P - Birth). */
	FVector3f Birth = FVector3f::ZeroVector;
	/** Own throw while clinging; the carrier's velocity then. */
	FVector3f Throw = FVector3f::ZeroVector;
	FVector3f Carrier = FVector3f::ZeroVector;
	float RadiusCm = 3.0f;
	float Mass = 1.0f;
	float Heat = 1.0f;
	float Age = 0.0f;
	float Life = 6.0f;
	float Cling = 0.0f;
	uint32 Seed = 0;
};

/**
 * Owns every gas cloud in the world (contract §6): their background steps, atlases and drawing; the zones (air,
 * vacuum, gravity) from OSAirZoneAt / OSGravityAt; tunnels, bursts, air flows and bodies apply to all clouds, called
 * by anyone. A source does not make a cloud: it asks for one (the nearest of its look in reach, or a new one).
 */
UCLASS()
class MINDKORE_API UOSGasSubsystem : public UTickableWorldSubsystem
{
	GENERATED_BODY()

public:
	static UOSGasSubsystem* Get(const UObject* WorldContext);

	virtual void Deinitialize() override;
	virtual void OnWorldBeginPlay(UWorld& InWorld) override;
	virtual void Tick(float DeltaTime) override;
	virtual TStatId GetStatId() const override;
	virtual bool DoesSupportWorldType(const EWorldType::Type WorldType) const override;

	/**
	 * A source: Recipe's parcels from Where (world) along Direction, carried at CarrierVelocity (a gun's at the shot).
	 * Attached: Where and Direction are in Attach's space and the source follows it. A burst source ends by itself;
	 * a rate source (SetRate) runs until Stop. Returns its id.
	 */
	int32 AddSource(const FOSGasRecipe& Recipe, const FOSGasLook& Look, const FVector& Where, const FVector& Direction, const FVector& CarrierVelocity,
		const USceneComponent* Attach = nullptr);
	/** Parcels per second for a source (0: none); Stop removes it. */
	void SetRate(int32 Source, float PerSecond);
	void Stop(int32 Source);
	/** Count parcels at once (a seep, a case's puff). */
	void EmitNow(const FOSGasRecipe& Recipe, const FOSGasLook& Look, const FVector& Where, const FVector& Direction, const FVector& CarrierVelocity, int32 Count);

	/** A projectile's tunnel (as UOSMuzzleFxComponent::CarveRay): drawn at once, healing; the parcels in it lose mass and are pushed out. */
	void CarveRay(const FVector& Start, const FVector& End, float RadiusCm);
	/** A radial blow (as UOSMuzzleFxComponent::Burst), half again in a vacuum. */
	void Burst(const FVector& Location, const FVector& Normal, float RadiusCm, float SpeedCm);
	/** Air leaving through a hole (as UOSMuzzleFxComponent::AddAirFlow). */
	void AddAirFlow(const FVector& Location, const FVector& Direction, float SpeedCm, float ReachCm, float Seconds);

	/** Clouds alive; parcels in all of them (tests, log lines). */
	int32 GetCloudCount() const;
	int32 GetParcelCount() const;
	int32 GetTunnelCount() const { return Tunnels.Num(); }
	void ClearClouds();
	FString DescribeClouds() const;
	/** The youngest cloud at least MinAge old: its centre and half side (tests). */
	bool GetYoungestCloud(float MinAge, FVector& OutCentre, float& OutHalf) const;
	/** The cost since the last call (os.Fx.SmokeCost). */
	FString TakeCostLine();

	/** The post-process that draws every cloud after the upscaler (v11 stage 3, Scripts/os_muzzle_fx.py). */
	UPROPERTY()
	TSoftObjectPtr<UMaterialInterface> PostMaterial = TSoftObjectPtr<UMaterialInterface>(FSoftObjectPath(TEXT("/Game/OSArt/FX/M_OS_GasPost.M_OS_GasPost")));

	/** Clouds the post-process draws at once (the nearest to the eye). */
	static constexpr int32 MaxDrawn = 6;

	static constexpr int32 MaxClouds = 8;
	static constexpr int32 MaxParcels = 220;
	/** The velocity grid (air): 20 cells of 6 cm; the render grid: 48 of 2 cm (3 cm in a vacuum), same centre. */
	static constexpr int32 VelocityCells = 20;
	static constexpr float VelocityCellCm = 6.0f;
	static constexpr int32 RenderCells = 48;
	static constexpr int32 MaxTunnels = 32;

private:
	struct FCommand
	{
		enum class EKind : uint8 { Carve, Burst } Kind;
		FVector A, B;
		float RadiusCm, SpeedCm;
	};

	struct FCloud
	{
		FOSGasLook Look;
		/** The parcels' frame origin, world (fixed for the cloud's life: floats stay small). */
		FVector Origin = FVector::ZeroVector;
		/** The render box's centre (origin frame, snapped to a render cell) of the last packed atlas, and the next one's. */
		FVector3f DrawnCentre = FVector3f::ZeroVector;
		FVector3f PackedCentre = FVector3f::ZeroVector;
		float RenderCellCm = 2.0f;
		float PackedCellCm = 2.0f;
		TArray<FOSGasParcel> Parcels;
		/** Parcels let out while a step was in flight: joined at the next launch. */
		TArray<FOSGasParcel> Incoming;
		TArray<FCommand> Commands;
		TArray<FOSSmokeSim::FMover> Movers;
		/** World planes (floor, wall) the gas slides on: traced at birth. */
		TArray<FPlane> Planes;
		TUniquePtr<FOSSmokeSim> Grid;
		bool bAir = true;
		FVector Gravity = FVector::ZeroVector;
		float Age = -1.0f;
		float Evict = 0.0f;
		float StepCarry = 0.0f;
		uint32 Seed = 0;
		TArray<FColor> Atlas;
		bool bPendingAtlas = false;
		bool bHasGas = false;
		/** A burst began this frame: its first parcels are splatted on the game thread and drawn on the shot's frame. */
		bool bStamp = false;
		/** The view's transmittance through the box's centre along X in the last atlas (os.Fx.SmokeCost calibration). */
		float CentreT = 1.0f;
		FVector3f KeyDir = FVector3f::UpVector;
		FLinearColor KeyColour = FLinearColor::White;
		TObjectPtr<UTexture2D> Texture = nullptr;
		/** An atlas has been uploaded (the post-process may draw it). */
		bool bDrawable = false;
		int32 TunnelCount = 0;
		mutable UE::Tasks::FTask Pending;
		bool IsBusy() const { return Pending.IsValid() && !Pending.IsCompleted(); }
		bool IsAlive() const { return Age >= 0.0f; }
	};

	struct FSource
	{
		int32 Id = 0;
		FOSGasRecipe Recipe;
		FOSGasLook Look;
		FVector Where, Direction, Carrier;
		TWeakObjectPtr<const USceneComponent> Attach;
		bool bAttached = false;
		/** A burst: parcels still owed and the time left to let them out. */
		float BurstLeft = 0.0f;
		float BurstOwed = 0.0f;
		float Rate = 0.0f;
		float Carry = 0.0f;
		int32 Cloud = INDEX_NONE;
	};

	struct FTunnel
	{
		FVector Start, End;
		float RadiusCm = 3.0f;
		float Age = 0.0f;
		bool bVacuum = false;
	};

	struct FAirFlow
	{
		FVector Location, Direction;
		float SpeedCm, ReachCm, Seconds;
	};

	TArray<FCloud> Clouds;
	TArray<FSource> Sources;
	TArray<FTunnel> Tunnels;
	TArray<FAirFlow> AirFlows;
	int32 NextSourceId = 1;
	TMap<TObjectKey<UPrimitiveComponent>, FVector> LastMoverAt;
	TArray<TWeakObjectPtr<ULightComponent>> KeyLights;
	float KeyLightsAge = 1e6f;
	/** The unbound post-process volume carrying PostMaterial, its instance, the clouds' data and the tunnels (float textures). */
	UPROPERTY()
	TObjectPtr<AActor> PostVolume = nullptr;
	UPROPERTY()
	TObjectPtr<UMaterialInstanceDynamic> Post = nullptr;
	UPROPERTY()
	TObjectPtr<UTexture2D> DataTexture = nullptr;
	UPROPERTY()
	TObjectPtr<UTexture2D> TunnelTexture = nullptr;
	TArray<TObjectPtr<UTexture2D>> BoundGrids;
	/** The post-process's parameters for this frame: the nearest clouds, the tunnels and bodies, the block size. */
	void UpdatePost();
	/** The room's light from above and below for the gas (display colours): by the map, the vacuum's starry dark. */
	void AmbientFor(bool bAir, FLinearColor& OutTop, FLinearColor& OutBottom) const;
	UPROPERTY()
	TArray<TObjectPtr<UObject>> Keep;

	/** A cloud of Look near Where (world) with room, or a new one. */
	/** A cloud of Look near Where no older than JoinAge (negative: always a new one), or a new one. */
	int32 CloudFor(const FOSGasLook& Look, const FVector& Where, float JoinAge = 1e9f);
	void Emit(FCloud& Cloud, const FOSGasRecipe& Recipe, const FVector& Where, const FVector& Direction, const FVector& Carrier, int32 Count);
	void TickSources(float DeltaTime);
	void TickCloud(FCloud& Cloud, float DeltaTime);
	void LaunchStep(FCloud& Cloud, int32 Steps, float Dt);
	void FindMovers(FCloud& Cloud, float DeltaTime, TSet<TObjectKey<UPrimitiveComponent>>& Seen);
	void PickKey(const FVector& Where, FVector3f& OutDir, FLinearColor& OutColour);
	void EnsureRender(FCloud& Cloud);
	void WaitAll() const;

	/** The step off the game thread: commands, the velocity grid (air), the parcels' motion, the splat and the atlas. */
	static void StepCloud(FCloud& Cloud, int32 Steps, float Dt, const TArray<FTunnel>& TunnelsNow);
	static void Splat(FCloud& Cloud);

	/** os.Fx.SmokeCost. */
	double CostStepUs = 0.0, CostMaxStepUs = 0.0, CostFrameUs = 0.0, CostMaxFrameUs = 0.0;
	int32 CostSteps = 0, CostFrames = 0;
	FCriticalSection CostLock;
};
