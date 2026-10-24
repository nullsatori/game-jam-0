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
class AOSGravityZone;
/** A spot's cone (axis, cos of outer and inner) or a rect's cosine to its normal (0, 1); false for others. */
bool OSGasLampCone(const ULightComponent* L, FVector& OutAxis, float& OutCosOuter, float& OutCosInner);
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
	/** The gas's albedo: neutral grey, darker than the walls round it unlit (powder smoke; stage 6). */
	FLinearColor Colour = FLinearColor(0.40f, 0.40f, 0.40f);

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

	/** In air a parcel grows to this radius at most, cm (a thin wisp stays thin). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OS|Gas", meta = (ClampMin = "0.5"))
	float MaxRadiusCm = 14.0f;

	/** Its own air drag's time constant in air, s (0: none) - a hole's pop stops within a few cm. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OS|Gas", meta = (ClampMin = "0.0"))
	float DragSeconds = 0.0f;

	/** Seen at full weight from its first step (the muzzle's blast); otherwise a parcel fades in over 0.12 s. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OS|Gas")
	bool bInstant = false;

	/** Joins the cloud a source started this very frame (one shot's several streamers share one cloud). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OS|Gas")
	bool bJoinThisFrame = false;

	/** Always a cloud of its own and the shot's (as a burst of 20 or more), whatever its count. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OS|Gas")
	bool bShotCloud = false;

	/** Heavy matter (dust): it falls along gravity at this, cm/s^2 (the air's drag gives it a terminal speed); 0 gas. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OS|Gas", meta = (ClampMin = "0.0"))
	float FallCmPerS2 = 0.0f;
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
	float MaxRadiusCm = 14.0f;
	float DragSeconds = 0.0f;
	float Fall = 0.0f;
	bool bInstant = false;
	uint32 Seed = 0;
	/** The haze (v12 7a): its strand's axis and stretch (1 a ball, 5 at most; volume kept), its residual warmth (lifts it). */
	FVector3f StretchAxis = FVector3f(1.0f, 0.0f, 0.0f);
	float Stretch = 1.0f;
	float Warm = 0.0f;
	/**
	 * The hand-over to the zone's haze (world s): in a shot's cloud the moment it starts to leave (over 0.5 s; -1 never);
	 * in the zone's cloud the moment it starts to come in. bHanded: its copy is in the zone's cloud.
	 */
	float HandOut = -1.0f;
	float HandIn = -1.0f;
	bool bHanded = false;
	/** Air (1) or vacuum (0) where it is (v12 7c.4: checked on the game thread, eased over 0.2 s): rest and the grid, or ballistic. */
	float Air = 1.0f;
	float AirTarget = 1.0f;
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
	void EmitNow(const FOSGasRecipe& Recipe, const FOSGasLook& Look, const FVector& Where, const FVector& Direction, const FVector& CarrierVelocity, int32 Count,
		bool bMayStartCloud = false);

	/**
	 * Heat without smoke (v12 7b.1): Count warm puffs out of Where along Direction over 50 ms, rising 20-30 cm/s against
	 * gravity, living 1-2 s - seen only as the air's shimmer behind them, never as gas.
	 */
	void AddHeat(const FVector& Where, const FVector& Direction, int32 Count, float Heat = 1.0f, float Seconds = 0.08f, float LifeMax = 2.0f);

	/**
	 * How much light gets through the gas from A to B (v12 7c.1): every cloud's parcels' kernels along the segment, from
	 * their last launched step; 1 clear. Kept 0.25 s for each Caller (a flyer's sight, the laser's dot).
	 */
	float Transmittance(const FVector& A, const FVector& B, const UObject* Caller);
	static constexpr int32 MaxHeat = 64;

	/** A projectile's tunnel (as UOSMuzzleFxComponent::CarveRay): drawn at once, healing; the parcels in it lose mass and are pushed out. */
	void CarveRay(const FVector& Start, const FVector& End, float RadiusCm);
	/** A radial blow (as UOSMuzzleFxComponent::Burst), half again in a vacuum. */
	void Burst(const FVector& Location, const FVector& Normal, float RadiusCm, float SpeedCm);
	/** Air leaving through a hole (as UOSMuzzleFxComponent::AddAirFlow). */
	void AddAirFlow(const FVector& Location, const FVector& Direction, float SpeedCm, float ReachCm, float Seconds);
	/**
	 * A level source's parcels (UOSGasSourceComponent): only into its own cloud (InOutCloud and InOutSerial remember it;
	 * a new one when it is gone), never a shot's. Within the world's budget for sources - continuous ones 120 parcels
	 * a second in all, a burst 90 - the rest is refused with a log line. Returns how many went out.
	 */
	int32 EmitForSource(const FOSGasRecipe& Recipe, const FOSGasLook& Look, const FVector& Where, const FVector& Direction, const FVector& Carrier, int32 Count,
		bool bContinuous, int32& InOutCloud, uint32& InOutSerial, float FeedWindow = 0.5f);
	static constexpr int32 SourceRatePerSecond = 120;
	static constexpr int32 SourceBurstMax = 90;

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
	/**
	 * The cloud with most parcels (os.Gas.WalkProbe): its parcels' mass centroid (world), their count, how many are behind one of
	 * its wall/floor planes, its drawn box's centre (world). Waits for the steps in flight.
	 */
	bool ProbeYoungest(FVector& OutCentroid, int32& OutParcels, int32& OutBehind, FVector& OutBox, float& OutSpread, float& OutHalf, float& OutGridCell, float& OutOldSpeed) const;
	/** os.Gas.Debug 1: the clouds' boxes, parcels, bodies, tunnels, planes and sources drawn in the world. */
	void DrawDebug() const;

	/** The post-process that draws every cloud after the upscaler (v11 stage 3, Scripts/os_muzzle_fx.py). */
	UPROPERTY()
	TSoftObjectPtr<UMaterialInterface> PostMaterial = TSoftObjectPtr<UMaterialInterface>(FSoftObjectPath(TEXT("/Game/OSArt/FX/M_OS_GasPost.M_OS_GasPost")));

	static constexpr int32 MaxClouds = 10;
	/** The zones' haze clouds (v12 7a): their own pool after the shots' slots. */
	static constexpr int32 MaxZones = 2;
	static constexpr int32 MaxZoneParcels = 400;
	/** Clouds the post-process draws: all of them (a set picked by distance swapped clouds in and out whole). */
	static constexpr int32 MaxDrawn = MaxClouds + MaxZones;
	/** A cloud's parcels: the Рысь's shot is 160 from the muzzle, 80 from its 16 shroud holes, its thread and seep. */
	static constexpr int32 MaxParcels = 300;
	/** GasData's rows a cloud (Scripts/os_muzzle_fx.py, GAS_POST_HLSL). */
	/** GasData's rows: 0-12 a cloud's atlases and look, 13-28 its four lamps (4 rows each, os.Gas.Light 1). */
	static constexpr int32 MaxLamps = 4;
	static constexpr int32 DataRows = 13 + 4 * MaxLamps;
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
		/** Applied over this many steps (0.1 s; set at its first step): the mass goes in a ramp, the push over 3. */
		int32 StepsTotal = 0, StepsLeft = 0;
	};

	struct FCloud
	{
		FOSGasLook Look;
		/**
		 * A zone's haze (v12 7a, slots MaxClouds..): no velocity grid, 5 Hz, a coarse cell, the zone's drift, its box's
		 * faces and the shots' planes for walls, thinned by the zone's ventilation.
		 */
		bool bZone = false;
		TWeakObjectPtr<const AOSGravityZone> Zone;
		float HazeHalfLife = 45.0f;
		/** The drift's field (by the zone the gas is in) and its speed, cm/s. */
		uint32 DriftSeed = 0;
		float DriftCm = 6.0f;
		/** World time = SimTime + this (set at each launch: the hand-over's ramps go by world time in both clouds). */
		double SimToWorld = 0.0;
		/** A shot's cloud hands its gas to its zone's haze (from FadeAge 3 s); when it began (world s, -1 not yet). */
		bool bHands = false;
		double HandStart = -1.0;
		/** Its parcels as they were at the last launch (world centre + radius, a density): Transmittance reads these. */
		TArray<FVector4f> Snap;
		TArray<float> SnapDensity;
		FBox SnapBox = FBox(ForceInit);
		/** Its density's gain as drawn last frame (the life's fade, the eviction). */
		float DrawGain = 1.0f;
		/** The next parcel whose air or vacuum is checked (an eighth at each launch). */
		int32 AirCursor = 0;
		/** The haze's densest drawn cell and how long it has been unseen (it ends after 5 s). */
		float MaxDensity = 1.0f;
		float FaintFor = 0.0f;
		/** The haze's gas's mean age (its phase's g ages with it), s. */
		float HazeAge = 0.0f;
		/** The parcels' frame origin, world (fixed for the cloud's life: floats stay small). */
		FVector Origin = FVector::ZeroVector;
		/** The render box's centre (origin frame, snapped to a render cell) of the last packed atlas, and the next one's. */
		FVector3f DrawnCentre = FVector3f::ZeroVector;
		FVector3f PackedCentre = FVector3f::ZeroVector;
		float RenderCellCm = 2.0f;
		float PackedCellCm = 2.0f;
		/**
		 * The two atlases drawn, blended by the phase between their times (the gas's own clock, SimTime): the current
		 * (DrawnCentre, RenderCellCm, CurTime) and the one before (PrevCentre, PrevCellCm, PrevTime). Each half of the
		 * texture holds one; CurHalf is the current's. ShowTime is the moment shown, a step behind, never backward.
		 */
		FVector3f PrevCentre = FVector3f::ZeroVector;
		float PrevCellCm = 2.0f;
		/** The cells each atlas holds gas in (origin frame): the drawing marches only through them. */
		FVector3f CurOccLo = FVector3f::ZeroVector, CurOccHi = FVector3f::ZeroVector, PrevOccLo = FVector3f::ZeroVector, PrevOccHi = FVector3f::ZeroVector;
		float CurTime = 0.0f, PrevTime = 0.0f, ShowTime = 0.0f, PackedTime = 0.0f, SimTime = 0.0f, StepDt = 1.0f / 60.0f;
		int32 CurHalf = 0;
		/** The atlas being packed is a burst's stamp: shown at once (both halves). */
		bool bStampAtlas = false;
		/** The box: when the gas began to fill less than 60% of it (SimTime, negative: it fills), the last re-box. */
		float LooseSince = -1.0f, LastRebox = -1.0f;
		/** This cloud's life (a slot is reused: the serial tells them apart) and its birth (world s: tunnels older cut it). */
		uint32 Serial = 0;
		float BirthWorld = 0.0f;
		TArray<FOSGasParcel> Parcels;
		/** Parcels let out while a step was in flight: joined at the next launch. */
		TArray<FOSGasParcel> Incoming;
		/** The step's commands and bodies: the game thread fills Next*, they move in at a launch (never while a step runs). */
		TArray<FCommand> Commands;
		TArray<FOSSmokeSim::FMover> Movers;
		TArray<FCommand> NextCommands;
		TArray<FOSSmokeSim::FMover> NextMovers;
		/** World planes (floor, walls, ceiling, glass) the gas slides on: traced round the birth point, 18 ways, 4 m. */
		TArray<FPlane> Planes;
		/** Each plane's surface's bounds (+10 cm): the plane holds only inside them. */
		TArray<FBox> PlaneBoxes;
		TUniquePtr<FOSSmokeSim> Grid;
		/** The velocity grid's centre (origin frame): kept while the gas's mass centre is within 3 cells of it. */
		FVector3f GridAnchor = FVector3f::ZeroVector;
		bool bGridAnchored = false;
		bool bAir = true;
		FVector Gravity = FVector::ZeroVector;
		float Age = -1.0f;
		/**
		 * The age its fade and end go by: held at 0.3 of its life while it is fed (a level source's steady flow, a
		 * weld - it pulsed: faded and ended by its own age while fed), then aging on from there.
		 */
		float FadeAge = 0.0f;
		/** When gas was last let into it (world s). */
		double LastFed = -1e9;
		/** How long after its last gas it counts as fed: 0.5 s, or 1.5 periods of a steady source's rate. */
		float FedWindow = 0.5f;
		bool IsFed(double Now) const { return Now - LastFed < FedWindow; }
		/**
		 * Takes new gas only while it has not faded far: a fade age past 0.3 of its life + 1 s and fresh gas would
		 * bring the whole old cloud back up (a weld let go and pressed again, a thruster's bursts) - a new cloud then.
		 */
		bool TakesGas() const { return FadeAge <= 0.3f * Look.LifeSeconds + 1.0f; }
		float Evict = 0.0f;
		float StepCarry = 0.0f;
		uint32 Seed = 0;
		TArray<FColor> Atlas;
		/** The cells the last atlas wrote (cleared before the next: no 440 KB re-zeroed each step). */
		FIntVector AtlasLo = FIntVector(0), AtlasHi = FIntVector(-1);
		bool bPendingAtlas = false;
		bool bHasGas = false;
		/** A burst began this frame: its first parcels are splatted on the game thread and drawn on the shot's frame. */
		bool bStamp = false;
		/** The view's transmittance through the box's centre along X in the last atlas (os.Fx.SmokeCost calibration). */
		float CentreT = 1.0f;
		FVector3f KeyDir = FVector3f::UpVector;
		FLinearColor KeyColour = FLinearColor::White;
		/**
		 * Its lamps (os.Gas.Light 1): the four strongest at the box's point nearest each, crossfaded over 0.25 s as the set
		 * changes (the player's own - the muzzle's flash, the torch - at once); Key the one whose baked self-shadow it takes.
		 */
		struct FLampSlot
		{
			TWeakObjectPtr<const ULightComponent> Lamp;
			float Weight = 0.0f;
			float Score = 0.0f;
		};
		FLampSlot Lamps[MaxLamps];
		int32 KeyLamp = INDEX_NONE;
		bool bLampsFresh = true;
		/** Whether each lamp near the cloud sees it (os.Gas.Light 1): a trace from the centre every 0.25 s, eased 0.15 s. */
		struct FLampSeen
		{
			float Seen = 0.0f;
			float Target = 0.0f;
			double Traced = -1e9;
		};
		TMap<TWeakObjectPtr<const ULightComponent>, FLampSeen> LampsSeen;
		TObjectPtr<UTexture2D> Texture = nullptr;
		/** An atlas has been uploaded (the post-process may draw it). */
		bool bDrawable = false;
		int32 TunnelCount = 0;
		/** Facts for the log at its end: re-boxes (the largest move, the cells seen), carves and blows it took. */
		int32 Reboxes = 0, Carves = 0, Blows = 0;
		float MaxReboxCm = 0.0f, MinCellCm = 1e9f, MaxCellCm = 0.0f;
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
		uint32 CloudSerial = 0;
	};

	struct FTunnel
	{
		FVector Start, End;
		float RadiusCm = 3.0f;
		float Age = 0.0f;
		bool bVacuum = false;
		/** World time it was cut (it cuts only clouds born before it). */
		float Birth = 0.0f;
	};

	struct FAirFlow
	{
		FVector Location, Direction;
		float SpeedCm, ReachCm, Seconds;
	};

	TArray<FCloud> Clouds;
	/** Heat puffs (world): where, velocity, radius, heat, age, life; a negative age waits (the shot's 50 ms). */
	struct FHeatPuff
	{
		FVector P, V;
		float RadiusCm, Heat, Age, Life;
	};
	TArray<FHeatPuff> HeatPuffs;
	struct FSeenThrough
	{
		double At = -1e9;
		float T = 1.0f;
	};
	TMap<TObjectKey<UObject>, FSeenThrough> SeenThrough;
	TArray<FSource> Sources;
	TArray<FTunnel> Tunnels;
	TArray<FAirFlow> AirFlows;
	int32 NextSourceId = 1;
	uint32 NextSerial = 1;
	/** The last shot's cloud (a seep or a case joins only it) and the barrel's own small cloud (its thread), by serial. */
	int32 ShotCloud = INDEX_NONE, SmallCloud = INDEX_NONE;
	/** The level sources' continuous parcels in the last second (their budget): when, how many. */
	TArray<TPair<double, int32>> SourceSpent;
	uint32 ShotSerial = 0, SmallSerial = 0;
	TMap<TObjectKey<UPrimitiveComponent>, FVector> LastMoverAt;
	TArray<TWeakObjectPtr<ULightComponent>> KeyLights;
	/** The player's own lamps (the torch, the muzzle's flash, the Nixie's glow): gathered every frame, never waiting for KeyLights' second. */
	TArray<TWeakObjectPtr<ULightComponent>> PlayerLights;
	/**
	 * The torch's shadow map for the gas (v13 13a): a square of traces round its outer cone from the lamp (distance to
	 * the first hit, eased), a few texels a frame in turn, re-sampled into the lamp's new frame as it turns.
	 */
	struct FTorchShadow
	{
		TArray<float> Depth, Target;
		int32 Res = 0;
		int32 Cursor = 0;
		FVector Pos = FVector::ZeroVector, Axis = FVector::ForwardVector, U = FVector::RightVector, V = FVector::UpVector;
		float TanOuter = 0.3f, Range = 2500.0f;
		bool bOn = false;
	};
	FTorchShadow Torch;
	UPROPERTY()
	TObjectPtr<UTexture2D> TorchTexture = nullptr;
	static constexpr int32 MaxTorchRes = 32;
	/** The torch's map for this frame (Tick, before UpdatePost). */
	void UpdateTorchShadow(float DeltaTime);
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
	/** The heat puffs for the shimmer: two rows - position + radius, heat. */
	UPROPERTY()
	TObjectPtr<UTexture2D> HeatTexture = nullptr;
	TArray<TObjectPtr<UTexture2D>> BoundGrids;
	/** The screen-block pass (v11 stage 5, os.Gas.LowRes 1): fed each frame with what M_OS_GasPost would get. */
	TSharedPtr<class FOSGasViewExtension, ESPMode::ThreadSafe> GasView;
	/** The drawn set (slots) last frame and how often it changed (the log, os.Gas.WalkProbe). */
	TArray<int32> LastDrawnSet;
public:
	int32 DrawnSetChanges = 0;
private:
	/** The post-process's parameters for this frame: the nearest clouds, the tunnels and bodies, the block size. */
	void UpdatePost();
	/** The room's light from above and below for the gas (display colours): by the map, the vacuum's starry dark. */
	void AmbientFor(bool bAir, FLinearColor& OutTop, FLinearColor& OutBottom) const;
	UPROPERTY()
	TArray<TObjectPtr<UObject>> Keep;

	/**
	 * A cloud of Look near Where no older than JoinAge (negative: always a new one), or a new one in a free slot; with
	 * none free, the youngest of the look within 1.5 m, else INDEX_NONE (a slot is never taken from a living cloud).
	 */
	int32 CloudFor(const FOSGasLook& Look, const FVector& Where, float JoinAge = 1e9f, bool bFreeSlotOnly = false);
	void Emit(FCloud& Cloud, const FOSGasRecipe& Recipe, const FVector& Where, const FVector& Direction, const FVector& Carrier, int32 Count);
	void TickSources(float DeltaTime);
	void TickCloud(FCloud& Cloud, float DeltaTime);
	void LaunchStep(FCloud& Cloud, int32 Steps, float Dt);
	void FindMovers(FCloud& Cloud, float DeltaTime, TSet<TObjectKey<UPrimitiveComponent>>& Seen);
	void PickKey(const FVector& Where, FVector3f& OutDir, FLinearColor& OutColour);
	/** os.Gas.Light 1: the cloud's four lamps for this frame (the set, the crossfade, the key's direction). */
	void UpdateLamps(FCloud& Cloud, const FVector& Centre, float DeltaTime, FVector3f& OutKeyDir);
	/** How much a lamp sees Where (0-1, traced every 0.25 s, eased 0.15 s), by the cloud's record. */
	float LampSees(FCloud& Cloud, const ULightComponent* Lamp, const FVector& Where, const FVector& Toward, bool bDirectional);
	void EnsureRender(FCloud& Cloud);
	/** A cloud made new in its slot: the look, the origin, the clocks, the lamps; bTrace: its walls traced round Where. */
	void InitCloud(FCloud& Cloud, const FOSGasLook& Look, const FVector& Where, bool bTrace);
	/** The zone's haze cloud (a slot of its pool), made at its first gas; INDEX_NONE: none free. */
	int32 ZoneCloudFor(const AOSGravityZone* Zone);
	/** A shot's cloud in air: its old gas handed to its zone's haze (v12 7a.2). */
	void HandOver(FCloud& Cloud, const FVector& Centre);
	void WaitAll() const;

	/** The step off the game thread: commands, the velocity grid (air), the parcels' motion, the splat and the atlas. */
	static void StepCloud(FCloud& Cloud, int32 Steps, float Dt, const TArray<FAirFlow>& FlowsNow);
	static void Splat(FCloud& Cloud);

	/** os.Fx.SmokeCost. */
	double CostStepUs = 0.0, CostMaxStepUs = 0.0, CostFrameUs = 0.0, CostMaxFrameUs = 0.0;
	int32 CostSteps = 0, CostFrames = 0;
	FCriticalSection CostLock;
};
