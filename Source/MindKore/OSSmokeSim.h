#pragma once

#include "CoreMinimal.h"
#include "Async/ParallelFor.h"
#include "OSSmokeSim.generated.h"

/** The gas's behaviour in a smoke grid (Docs/RecoilWeapons-SmokeSim.md). */
USTRUCT(BlueprintType)
struct FOSSmokeParams
{
	GENERATED_BODY()

	/** Hot gas rises against the local gravity, cm/s^2 per unit of heat (air only). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OS|Smoke")
	float Buoyancy = 260.0f;

	/** The air's hold on the gas's own motion, s (air only; a vacuum keeps every speed). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OS|Smoke", meta = (ClampMin = "0.01"))
	float DragSeconds = 0.35f;

	/** Vorticity confinement (dimensionless, times the cell): keeps the curls the coarse grid would smear. Halved in a vacuum. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OS|Smoke", meta = (ClampMin = "0.0"))
	float Vorticity = 0.35f;

	/** The heat dies with this time constant, s (the embers go out, the rise and the swelling stop). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OS|Smoke", meta = (ClampMin = "0.01"))
	float HeatSeconds = 0.5f;

	/**
	 * The blast swells (air): the pressure solve's volume source, 1/s per unit of heat, dying with SwellSeconds after
	 * the shot - the slug blooms into a cloud in its first tenth of a second, then only moves.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OS|Smoke", meta = (ClampMin = "0.0"))
	float SwellPerSecond = 30.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OS|Smoke", meta = (ClampMin = "0.01"))
	float SwellSeconds = 0.1f;

	/** Hot gas churns: a smooth random push, cm/s^2 per unit of heat (the pressure solve keeps only its swirl in air). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OS|Smoke", meta = (ClampMin = "0.0"))
	float Turbulence = 1400.0f;

	/**
	 * Air: the gas stays a little warmer than the room its whole life (v6, owner: «облака дыма слишком статичные»): a
	 * lift of ResidualLift x Buoyancy per unit of density on top of the heat's, and a slow ambient churn, cm/s^2 - a
	 * coarse noise drifting with time - that keeps folding it after the heat is gone.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OS|Smoke", meta = (ClampMin = "0.0"))
	float ResidualLift = 0.25f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OS|Smoke", meta = (ClampMin = "0.0"))
	float AmbientChurn = 40.0f;

	/** Density lost per second (the rest of the dying is the material's erosion). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OS|Smoke", meta = (ClampMin = "0.0"))
	float DecayPerSecond = 0.06f;

	/** Red-black Gauss-Seidel sweeps of the pressure solve per step. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OS|Smoke", meta = (ClampMin = "1", ClampMax = "64"))
	int32 Jacobi = 12;
};

/**
 * A small smoke grid in a cloud's space (the heterogeneous volume's box): incompressible in air - Stam's stable
 * fluids (semi-Lagrangian advection, a pressure projection) with buoyancy and vorticity confinement - and free-
 * streaming in a vacuum (no pressure, no drag: the slug flies apart and thins). Solids are up to two planes (the
 * floor, a wall). N cells a side of CellCm; positions are cm in the box frame, its centre at the origin. Every loop
 * runs over z-slices in a ParallelFor, only in the box around the gas; the fixed step is the caller's (UOSMuzzleFxComponent:
 * 60 Hz for the first half second, 30 Hz after, in a task off the game thread).
 */
struct FOSSmokeSim
{
	int32 N = 32;
	float CellCm = 4.0f;
	bool bAir = true;
	/** Unit direction of the local gravity, box frame (zero: none). */
	FVector3f Down = FVector3f::ZeroVector;
	/** Solids, box frame (cm): a cell whose centre is behind a plane (PlaneDot < 0) is solid. */
	TArray<FPlane4f> Planes;
	TArray<float> Rho, Heat, Ux, Uy, Uz;
	/** |curl U|, 1/s, of the last step (the material's churn). */
	TArray<float> Curl;
	TArray<uint8> Solid;
	/** Each cloud's own lumps (Inject's per-cell hash). */
	uint32 NoiseSeed = 0;

	int32 Index(int32 X, int32 Y, int32 Z) const { return (Z * N + Y) * N + X; }
	/** A cell's centre, cm, box frame. */
	FVector3f CellCentre(int32 X, int32 Y, int32 Z) const { return (FVector3f(X, Y, Z) + 0.5f) * CellCm - 0.5f * N * CellCm; }
	/** Trilinear in cell coordinates (a cell's centre at its index), clamped to the grid. */
	float Sample(const TArray<float>& F, const FVector3f& Cells) const;

	void Reset(int32 InN, float InCellCm, bool bInAir, const FVector3f& InDown);
	void RebuildSolids();
	/**
	 * Gas in an ellipsoid at Centre (cm): LengthCm along Axis, RadiusCm across (semi-axes; a flat top, lumpy); density,
	 * heat (each at most), moving at Velocity plus Radial cm/s out of its centre - within the plane across RadialPlane
	 * when that is not zero (a splash spreading along a surface).
	 */
	void Inject(const FVector3f& Centre, const FVector3f& Axis, float LengthCm, float RadiusCm, float InRho, float InHeat, const FVector3f& Velocity, float Radial,
		const FVector3f& RadialPlane = FVector3f::ZeroVector);
	/**
	 * A push along the line Origin + t Dir (0 < t < LengthCm, cm): Along cm/s down it, Push cm/s out of it, within RadiusCm
	 * (gaussian); Clear (0..1) of the density on the line is thrown aside at once (a round's tunnel). True if it touched gas.
	 */
	bool Impulse(const FVector3f& Origin, const FVector3f& Dir, float RadiusCm, float Along, float Push, float Clear, float LengthCm = 1e6f);
	/**
	 * A body moving through the gas (v9, owner: «дым не раскрывается вокруг бочки»): a sphere in the box frame (cm) with
	 * its velocity (cm/s). Each step the gas inside it is pushed out and the flow there takes its velocity - a moving
	 * boundary: the pressure solve parts the gas round it and leaves a wake behind. Set by the caller before Step.
	 */
	struct FMover
	{
		FVector3f Centre;
		float RadiusCm;
		FVector3f Velocity;
		/** Non-zero: an oriented box of these half extents (cm) turned by Rotation (box frame) instead of the sphere. */
		FVector3f Extent = FVector3f::ZeroVector;
		FQuat4f Rotation = FQuat4f::Identity;
	};
	TArray<FMover> Movers;
	/** One step; Age (s since the shot) times the blast's swell. */
	void Step(float Dt, float Age, const FOSSmokeParams& K);
	/**
	 * A velocity-only step for the gas parcels (v11, UOSGasSubsystem): the caller fills Rho (mass), Heat and U from the
	 * parcels; forces, churn, vorticity, bodies and the pressure solve - no advection (the parcels carry everything) and
	 * no dissipation. Gravity scales buoyancy and the residual lift (|g| / 980).
	 */
	void StepGrid(float Dt, const FOSSmokeParams& K, float GravityScale);
	/** Move the grid by whole cells (the box follows the gas): the fields shift, what leaves is lost, the planes stay put in the world. */
	void Shift(const FIntVector& Cells);
	/** Density centroid, cells from the grid's centre; OutMass the total density. */
	FVector3f Centroid(float& OutMass) const;
	/** Mean speed out of the centroid, cm/s, density-weighted (the vacuum's expansion). */
	float MeanRadialSpeed() const;
	/** Mean |U| (cm/s) and the hottest cell (log lines). */
	void Stats(float& OutMeanSpeed, float& OutMaxHeat) const;
	/**
	 * The atlas for the material: 8 tiles across, ceil(N / 8) down, each N x N (slice z at tile (z % 8, z / 8)).
	 * R the drawn density: the gas less Sharpen x its blur (5 cells a side) - the low, soft cushion goes, rags and
	 * strands stay. G heat. B the key light's shadow: exp(-KeyShadow x the gas toward KeyDir, box frame, 1 to 10
	 * cells). A the ambient's: exp(-Occlusion x the gas around, 2 cells each way). Solid cells hold no gas.
	 */
	void Pack(TArray<FColor>& Out, const FVector3f& KeyDir, float KeyShadow, float Occlusion, float Sharpen);
	static FIntPoint AtlasSize(int32 InN) { return FIntPoint(8 * InN, ((InN + 7) / 8) * InN); }

private:
	TArray<float> T0, T1, T2, T3, T4, P, Div, Wx, Wy, Wz;
	int32 Jacobi = 12;
	/** The working box of this step (cells, inclusive; empty when Hi < Lo): the gas and its heat grown by a margin. */
	FIntVector Lo = FIntVector(0);
	FIntVector Hi = FIntVector(-1);
	void UpdateBox();
	/** Body(X, Y, Z, Index) over the working box, z-slices in parallel. */
	template <typename FBody>
	void ForBox(const FBody& Body) const
	{
		ParallelFor(FMath::Max(Hi.Z - Lo.Z + 1, 0), [&](int32 K)
		{
			const int32 Z = Lo.Z + K;
			for (int32 Y = Lo.Y; Y <= Hi.Y; ++Y)
			{
				for (int32 X = Lo.X; X <= Hi.X; ++X)
				{
					Body(X, Y, Z, Index(X, Y, Z));
				}
			}
		}, EParallelForFlags::BackgroundPriority);
	}
	bool IsSolid(int32 X, int32 Y, int32 Z) const { return Solid[Index(X, Y, Z)] != 0; }
	void Advect(float Dt);
	void Churn(float Dt, float Amount, float Ambient);
	/** Seconds stepped (the churn's noise drifts with it). */
	float Time = 0.0f;
	void Confine(float Dt, float Epsilon);
	void Project(float Swell);
};
