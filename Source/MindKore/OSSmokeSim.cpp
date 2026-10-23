#include "OSSmokeSim.h"

#include "Async/ParallelFor.h"

void FOSSmokeSim::Reset(int32 InN, float InCellCm, bool bInAir, const FVector3f& InDown)
{
	N = InN;
	CellCm = InCellCm;
	bAir = bInAir;
	Down = InDown;
	Planes.Reset();
	NoiseSeed = FMath::Rand();
	Time = 0.0f;
	const int32 Count = N * N * N;
	for (TArray<float>* F : { &Rho, &Heat, &Ux, &Uy, &Uz, &Curl, &T0, &T1, &T2, &T3, &T4, &P, &Div, &Wx, &Wy, &Wz })
	{
		F->SetNumZeroed(Count);
		FMemory::Memzero(F->GetData(), Count * sizeof(float));
	}
	Solid.SetNumZeroed(Count);
	FMemory::Memzero(Solid.GetData(), Count);
	Lo = FIntVector(0);
	Hi = FIntVector(-1);
}

void FOSSmokeSim::UpdateBox()
{
	// The cells with gas or heat, by slice; then their bounds grown by Margin cells: every step works only there.
	constexpr int32 Margin = 4;
	TArray<FIntVector4, TInlineAllocator<64>> SliceLo, SliceHi;
	SliceLo.Init(FIntVector4(N, N, N, 0), N);
	SliceHi.Init(FIntVector4(-1, -1, -1, 0), N);
	ParallelFor(N, [&](int32 Z)
	{
		for (int32 Y = 0; Y < N; ++Y)
		{
			for (int32 X = 0; X < N; ++X)
			{
				const int32 I = Index(X, Y, Z);
				if (Rho[I] > 0.002f || Heat[I] > 0.005f)
				{
					SliceLo[Z] = FIntVector4(FMath::Min(SliceLo[Z].X, X), FMath::Min(SliceLo[Z].Y, Y), FMath::Min(SliceLo[Z].Z, Z), 0);
					SliceHi[Z] = FIntVector4(FMath::Max(SliceHi[Z].X, X), FMath::Max(SliceHi[Z].Y, Y), FMath::Max(SliceHi[Z].Z, Z), 0);
				}
			}
		}
	});
	FIntVector L(N), H(-1);
	for (int32 Z = 0; Z < N; ++Z)
	{
		L = FIntVector(FMath::Min(L.X, SliceLo[Z].X), FMath::Min(L.Y, SliceLo[Z].Y), FMath::Min(L.Z, SliceLo[Z].Z));
		H = FIntVector(FMath::Max(H.X, SliceHi[Z].X), FMath::Max(H.Y, SliceHi[Z].Y), FMath::Max(H.Z, SliceHi[Z].Z));
	}
	if (H.X < 0)
	{
		Lo = FIntVector(0);
		Hi = FIntVector(-1);
		return;
	}
	Lo = FIntVector(FMath::Max(L.X - Margin, 0), FMath::Max(L.Y - Margin, 0), FMath::Max(L.Z - Margin, 0));
	Hi = FIntVector(FMath::Min(H.X + Margin, N - 1), FMath::Min(H.Y + Margin, N - 1), FMath::Min(H.Z + Margin, N - 1));
}

void FOSSmokeSim::RebuildSolids()
{
	ParallelFor(N, [this](int32 Z)
	{
		for (int32 Y = 0; Y < N; ++Y)
		{
			for (int32 X = 0; X < N; ++X)
			{
				const FVector3f C = CellCentre(X, Y, Z);
				bool bSolid = false;
				for (const FPlane4f& Plane : Planes)
				{
					bSolid |= Plane.PlaneDot(C) < 0.0f;
				}
				const int32 I = Index(X, Y, Z);
				Solid[I] = bSolid ? 1 : 0;
				if (bSolid)
				{
					Rho[I] = Heat[I] = Ux[I] = Uy[I] = Uz[I] = 0.0f;
				}
			}
		}
	});
}

float FOSSmokeSim::Sample(const TArray<float>& F, const FVector3f& Cells) const
{
	const float Max = float(N - 1);
	const float X = FMath::Clamp(Cells.X, 0.0f, Max), Y = FMath::Clamp(Cells.Y, 0.0f, Max), Z = FMath::Clamp(Cells.Z, 0.0f, Max);
	const int32 X0 = FMath::Min(int32(X), N - 2), Y0 = FMath::Min(int32(Y), N - 2), Z0 = FMath::Min(int32(Z), N - 2);
	const float FX = X - X0, FY = Y - Y0, FZ = Z - Z0;
	const float* D = F.GetData();
	const int32 I = Index(X0, Y0, Z0), SY = N, SZ = N * N;
	const float C00 = FMath::Lerp(D[I], D[I + 1], FX);
	const float C10 = FMath::Lerp(D[I + SY], D[I + SY + 1], FX);
	const float C01 = FMath::Lerp(D[I + SZ], D[I + SZ + 1], FX);
	const float C11 = FMath::Lerp(D[I + SZ + SY], D[I + SZ + SY + 1], FX);
	return FMath::Lerp(FMath::Lerp(C00, C10, FY), FMath::Lerp(C01, C11, FY), FZ);
}

void FOSSmokeSim::Inject(const FVector3f& Centre, const FVector3f& Axis, float LengthCm, float RadiusCm, float InRho, float InHeat, const FVector3f& Velocity, float Radial,
	const FVector3f& RadialPlane)
{
	const FVector3f PlaneN = RadialPlane.GetSafeNormal();
	const float InvL = 1.0f / FMath::Max(LengthCm, 0.1f);
	const float InvR = 1.0f / FMath::Max(RadiusCm, 0.1f);
	ParallelFor(N, [&](int32 Z)
	{
		for (int32 Y = 0; Y < N; ++Y)
		{
			for (int32 X = 0; X < N; ++X)
			{
				const int32 I = Index(X, Y, Z);
				if (Solid[I])
				{
					continue;
				}
				const FVector3f Q = CellCentre(X, Y, Z) - Centre;
				const float A = Q | Axis;
				const float S2 = FMath::Square(A * InvL) + (Q - Axis * A).SizeSquared() * InvR * InvR;
				if (S2 >= 2.25f)
				{
					continue;
				}
				// A flat top with a short soft rim (a gaussian left only its very core in the dense band), broken up
				// per cell by a hash: a lumpy slug, not a perfect ball that would roll into a perfect ring.
				const float W = FMath::Clamp((1.25f - FMath::Sqrt(S2)) * 2.0f, 0.0f, 1.0f);
				const uint32 H = (uint32(I) ^ NoiseSeed) * 2654435761u;
				const float Lump = 0.65f + 0.7f * float((H >> 8) & 0xFFFF) / 65535.0f;
				const float Jitter = float((H >> 4) & 0xFF) / 255.0f - 0.5f;
				Rho[I] = FMath::Max(Rho[I], InRho * W * Lump);
				Heat[I] = FMath::Max(Heat[I], InHeat * W);
				const FVector3f Out = PlaneN.IsNearlyZero() ? Q.GetSafeNormal() : (Q - PlaneN * (Q | PlaneN)).GetSafeNormal();
				const FVector3f V = Velocity * (1.0f + 0.3f * Jitter) + Out * Radial;
				const float B = FMath::Min(W * 2.0f, 1.0f);
				Ux[I] = FMath::Lerp(Ux[I], V.X, B);
				Uy[I] = FMath::Lerp(Uy[I], V.Y, B);
				Uz[I] = FMath::Lerp(Uz[I], V.Z, B);
			}
		}
	});
}

bool FOSSmokeSim::Impulse(const FVector3f& Origin, const FVector3f& Dir, float RadiusCm, float Along, float Push, float Clear, float LengthCm)
{
	const float InvR2 = 1.0f / FMath::Max(RadiusCm * RadiusCm, 1.0f);
	std::atomic<int32> Touched(0);
	ParallelFor(N, [&](int32 Z)
	{
		int32 Here = 0;
		for (int32 Y = 0; Y < N; ++Y)
		{
			for (int32 X = 0; X < N; ++X)
			{
				const int32 I = Index(X, Y, Z);
				if (Solid[I])
				{
					continue;
				}
				const FVector3f Rel = CellCentre(X, Y, Z) - Origin;
				const float T = Rel | Dir;
				if (T < 0.0f || T > LengthCm)
				{
					continue;
				}
				const FVector3f Perp = Rel - Dir * T;
				const float W = FMath::Exp(-Perp.SizeSquared() * InvR2);
				if (W < 0.02f)
				{
					continue;
				}
				const FVector3f V = Dir * (Along * W) + Perp.GetSafeNormal() * (Push * W);
				Ux[I] += V.X;
				Uy[I] += V.Y;
				Uz[I] += V.Z;
				Here += (W > 0.1f && Rho[I] > 0.02f) ? 1 : 0;
				Rho[I] *= 1.0f - Clear * W;
			}
		}
		Touched += Here;
	});
	return Touched.load() > 0;
}

void FOSSmokeSim::Confine(float Dt, float Epsilon)
{
	const float Inv2 = 0.5f / CellCm;
	const auto At = [this](int32 X, int32 Y, int32 Z) { return Index(FMath::Clamp(X, 0, N - 1), FMath::Clamp(Y, 0, N - 1), FMath::Clamp(Z, 0, N - 1)); };
	// omega = curl U (central differences), and its magnitude: the churn the material shows.
	ForBox([&](int32 X, int32 Y, int32 Z, int32 I)
	{
		const int32 XP = At(X + 1, Y, Z), XM = At(X - 1, Y, Z), YP = At(X, Y + 1, Z), YM = At(X, Y - 1, Z), ZP = At(X, Y, Z + 1), ZM = At(X, Y, Z - 1);
		Wx[I] = ((Uz[YP] - Uz[YM]) - (Uy[ZP] - Uy[ZM])) * Inv2;
		Wy[I] = ((Ux[ZP] - Ux[ZM]) - (Uz[XP] - Uz[XM])) * Inv2;
		Wz[I] = ((Uy[XP] - Uy[XM]) - (Ux[YP] - Ux[YM])) * Inv2;
		Curl[I] = FMath::Sqrt(Wx[I] * Wx[I] + Wy[I] * Wy[I] + Wz[I] * Wz[I]);
	});
	if (Epsilon <= 0.0f)
	{
		return;
	}
	// Fedkiw: f = eps h (normalize(grad |omega|) x omega).
	const float Scale = Epsilon * CellCm * Dt;
	ForBox([&](int32 X, int32 Y, int32 Z, int32 I)
	{
		if (Solid[I])
		{
			return;
		}
		FVector3f Eta((Curl[At(X + 1, Y, Z)] - Curl[At(X - 1, Y, Z)]), (Curl[At(X, Y + 1, Z)] - Curl[At(X, Y - 1, Z)]), (Curl[At(X, Y, Z + 1)] - Curl[At(X, Y, Z - 1)]));
		const float Len = Eta.Size();
		if (Len < 1e-5f)
		{
			return;
		}
		Eta /= Len;
		const FVector3f F = (Eta ^ FVector3f(Wx[I], Wy[I], Wz[I])) * Scale;
		Ux[I] += F.X;
		Uy[I] += F.Y;
		Uz[I] += F.Z;
	});
}

void FOSSmokeSim::Advect(float Dt)
{
	const float Cells = Dt / CellCm;
	ForBox([&](int32 X, int32 Y, int32 Z, int32 I)
	{
		if (Solid[I])
		{
			T0[I] = T1[I] = T2[I] = T3[I] = T4[I] = 0.0f;
			return;
		}
		const FVector3f From = FVector3f(X, Y, Z) - FVector3f(Ux[I], Uy[I], Uz[I]) * Cells;
		T0[I] = Sample(Ux, From);
		T1[I] = Sample(Uy, From);
		T2[I] = Sample(Uz, From);
		T3[I] = Sample(Rho, From);
		T4[I] = Sample(Heat, From);
		// The density's limits where it came from (MacCormack's clamp below).
		const int32 X0 = FMath::Clamp(int32(From.X), 0, N - 2), Y0 = FMath::Clamp(int32(From.Y), 0, N - 2), Z0 = FMath::Clamp(int32(From.Z), 0, N - 2);
		float L = Rho[Index(X0, Y0, Z0)], H = L;
		for (int32 C = 1; C < 8; ++C)
		{
			const float V = Rho[Index(X0 + (C & 1), Y0 + ((C >> 1) & 1), Z0 + (C >> 2))];
			L = FMath::Min(L, V);
			H = FMath::Max(H, V);
		}
		Wx[I] = L;
		Wy[I] = H;
	});
	// MacCormack for the density only: trace the advected field back, take half the round trip's error off, clamp
	// to where it came from. Semi-Lagrangian alone smears a 4 cm grid into a ball within a second; this keeps rims.
	// (Outside the working box T3 is stale, but the trace back stays within its margin: at most ~3 cells a step.)
	ForBox([&](int32 X, int32 Y, int32 Z, int32 I)
	{
		Wz[I] = Solid[I] ? 0.0f : Sample(T3, FVector3f(X, Y, Z) + FVector3f(Ux[I], Uy[I], Uz[I]) * Cells);
	});
	// Back into the fields, the working box only (outside it nothing moved).
	ForBox([&](int32 X, int32 Y, int32 Z, int32 I)
	{
		Ux[I] = T0[I];
		Uy[I] = T1[I];
		Uz[I] = T2[I];
		Rho[I] = Solid[I] ? 0.0f : FMath::Clamp(T3[I] + 0.5f * (Rho[I] - Wz[I]), Wx[I], Wy[I]);
		Heat[I] = T4[I];
	});
}

void FOSSmokeSim::Project(float Swell)
{
	const float H = CellCm;
	const float Inv2 = 0.5f / H;
	// A neighbour's velocity for the divergence: a solid's is 0, outside the grid the cell's own (the gas leaves freely).
	const auto Vel = [this](const TArray<float>& U, int32 I, int32 X, int32 Y, int32 Z)
	{
		if (X < 0 || Y < 0 || Z < 0 || X >= N || Y >= N || Z >= N)
		{
			return U[I];
		}
		const int32 J = Index(X, Y, Z);
		return Solid[J] ? 0.0f : U[J];
	};
	// Hot gas swells: a volume source in the divergence (Lap P = div U - S), so the slug blooms instead of sliding as a blob.
	ForBox([&](int32 X, int32 Y, int32 Z, int32 I)
	{
		Div[I] = Solid[I] ? 0.0f
			: (Vel(Ux, I, X + 1, Y, Z) - Vel(Ux, I, X - 1, Y, Z) + Vel(Uy, I, X, Y + 1, Z) - Vel(Uy, I, X, Y - 1, Z) + Vel(Uz, I, X, Y, Z + 1) - Vel(Uz, I, X, Y, Z - 1)) * Inv2
				- Swell * Heat[I];
	});
	// Red-black Gauss-Seidel over the working box, warm-started from the last step's pressure. A solid neighbour
	// mirrors the cell (no flow through it), outside the grid the pressure is 0 (open faces); past the working box
	// the last pressure there stands as the boundary.
	for (int32 Sweep = 0; Sweep < 2 * Jacobi; ++Sweep)
	{
		const int32 Colour = Sweep & 1;
		ParallelFor(Hi.Z - Lo.Z + 1, [&](int32 K)
		{
			const int32 Z = Lo.Z + K;
			for (int32 Y = Lo.Y; Y <= Hi.Y; ++Y)
			{
				for (int32 X = Lo.X + ((Lo.X + Y + Z + Colour) & 1); X <= Hi.X; X += 2)
				{
					const int32 I = Index(X, Y, Z);
					if (Solid[I])
					{
						continue;
					}
					float Sum = 0.0f;
					int32 Walls = 0;
					const auto Add = [&](int32 NX, int32 NY, int32 NZ)
					{
						if (NX < 0 || NY < 0 || NZ < 0 || NX >= N || NY >= N || NZ >= N)
						{
							return;
						}
						const int32 J = Index(NX, NY, NZ);
						Solid[J] ? void(++Walls) : void(Sum += P[J]);
					};
					Add(X + 1, Y, Z); Add(X - 1, Y, Z); Add(X, Y + 1, Z); Add(X, Y - 1, Z); Add(X, Y, Z + 1); Add(X, Y, Z - 1);
					P[I] = (Sum - Div[I] * H * H) / float(6 - Walls);
				}
			}
		});
	}
	const auto Pr = [this](int32 I, int32 X, int32 Y, int32 Z)
	{
		if (X < 0 || Y < 0 || Z < 0 || X >= N || Y >= N || Z >= N)
		{
			return 0.0f;
		}
		const int32 J = Index(X, Y, Z);
		return Solid[J] ? P[I] : P[J];
	};
	ForBox([&](int32 X, int32 Y, int32 Z, int32 I)
	{
		if (Solid[I])
		{
			return;
		}
		Ux[I] -= (Pr(I, X + 1, Y, Z) - Pr(I, X - 1, Y, Z)) * Inv2;
		Uy[I] -= (Pr(I, X, Y + 1, Z) - Pr(I, X, Y - 1, Z)) * Inv2;
		Uz[I] -= (Pr(I, X, Y, Z + 1) - Pr(I, X, Y, Z - 1)) * Inv2;
		// Nothing flows into a solid (it may leave it: the gas lifts off the floor freely).
		if (X + 1 < N && IsSolid(X + 1, Y, Z)) { Ux[I] = FMath::Min(Ux[I], 0.0f); }
		if (X > 0 && IsSolid(X - 1, Y, Z)) { Ux[I] = FMath::Max(Ux[I], 0.0f); }
		if (Y + 1 < N && IsSolid(X, Y + 1, Z)) { Uy[I] = FMath::Min(Uy[I], 0.0f); }
		if (Y > 0 && IsSolid(X, Y - 1, Z)) { Uy[I] = FMath::Max(Uy[I], 0.0f); }
		if (Z + 1 < N && IsSolid(X, Y, Z + 1)) { Uz[I] = FMath::Min(Uz[I], 0.0f); }
		if (Z > 0 && IsSolid(X, Y, Z - 1)) { Uz[I] = FMath::Max(Uz[I], 0.0f); }
	});
}

namespace
{
	/** Smooth value noise in -1..1 (a hash per lattice point, smoothstepped). */
	float SmokeNoise(const FVector3f& P, uint32 Seed)
	{
		const auto Hash = [Seed](int32 X, int32 Y, int32 Z)
		{
			uint32 H = uint32(X) * 73856093u ^ uint32(Y) * 19349663u ^ uint32(Z) * 83492791u ^ Seed;
			H ^= H >> 13;
			H *= 0x5bd1e995u;
			H ^= H >> 15;
			return float(H & 0xFFFF) / 32767.5f - 1.0f;
		};
		const int32 X = FMath::FloorToInt(P.X), Y = FMath::FloorToInt(P.Y), Z = FMath::FloorToInt(P.Z);
		FVector3f F = P - FVector3f(X, Y, Z);
		F = F * F * (FVector3f(3.0f) - 2.0f * F);
		const float A = FMath::Lerp(Hash(X, Y, Z), Hash(X + 1, Y, Z), F.X), B = FMath::Lerp(Hash(X, Y + 1, Z), Hash(X + 1, Y + 1, Z), F.X);
		const float C = FMath::Lerp(Hash(X, Y, Z + 1), Hash(X + 1, Y, Z + 1), F.X), D = FMath::Lerp(Hash(X, Y + 1, Z + 1), Hash(X + 1, Y + 1, Z + 1), F.X);
		return FMath::Lerp(FMath::Lerp(A, B, F.Y), FMath::Lerp(C, D, F.Y), F.Z);
	}
}

void FOSSmokeSim::Churn(float Dt, float Amount, float Ambient)
{
	// Where there is heat, a lattice of ~3 cells drifting with time (the fresh gas); and wherever there is gas, a slow
	// coarse one of ~8 cells (the room's air never still: the cloud keeps folding after it has cooled).
	const FVector3f Drift(Time * 0.9f, Time * -0.7f, Time * 0.5f);
	const FVector3f Slow(Time * 0.15f, Time * 0.11f, Time * -0.13f);
	ForBox([&](int32 X, int32 Y, int32 Z, int32 I)
	{
		if (Solid[I])
		{
			return;
		}
		if (Heat[I] >= 0.02f && Amount > 0.0f)
		{
			const FVector3f Pos = FVector3f(X, Y, Z) / 2.5f + Drift;
			const float S = Amount * Heat[I] * Dt;
			Ux[I] += S * SmokeNoise(Pos, NoiseSeed);
			Uy[I] += S * SmokeNoise(Pos, NoiseSeed ^ 0x9E3779B9u);
			Uz[I] += S * SmokeNoise(Pos, NoiseSeed ^ 0x85EBCA6Bu);
		}
		if (Rho[I] > 0.004f && Ambient > 0.0f)
		{
			const FVector3f Pos = FVector3f(X, Y, Z) / 8.0f + Slow;
			const float S = Ambient * Dt;
			Ux[I] += S * SmokeNoise(Pos, NoiseSeed ^ 0x27D4EB2Fu);
			Uy[I] += S * SmokeNoise(Pos, NoiseSeed ^ 0x165667B1u);
			Uz[I] += S * SmokeNoise(Pos, NoiseSeed ^ 0xD3A2646Cu);
		}
	});
}

void FOSSmokeSim::Step(float Dt, float Age, const FOSSmokeParams& K)
{
	Jacobi = K.Jacobi;
	Time += Dt;
	// Every pass works in the box around the gas and its heat (plus a margin), not the whole grid.
	UpdateBox();
	if (Hi.X < Lo.X)
	{
		return;
	}
	// 1. Forces. Air: hot gas rises against Down and the air takes the gas's own motion. A vacuum: nothing acts.
	if (bAir)
	{
		const float Drag = FMath::Exp(-Dt / K.DragSeconds);
		const FVector3f Lift = -Down * (K.Buoyancy * Dt);
		ForBox([&](int32 X, int32 Y, int32 Z, int32 I)
		{
			const float Warm = Heat[I] + K.ResidualLift * FMath::Min(Rho[I], 1.0f);
			Ux[I] = (Ux[I] + Lift.X * Warm) * Drag;
			Uy[I] = (Uy[I] + Lift.Y * Warm) * Drag;
			Uz[I] = (Uz[I] + Lift.Z * Warm) * Drag;
		});
		// The fresh gas churns (air only: without the pressure solve the push is divergent, and the advected
		// density would swell with it).
		Churn(Dt, K.Turbulence, K.AmbientChurn);
	}
	// 2. The curls the coarse grid would smear (no pressure feeds them in a vacuum: half).
	Confine(Dt, bAir ? K.Vorticity : K.Vorticity * 0.5f);
	// 2b. Bodies moving through: inside each the flow is the body's and the gas goes (out within ~20 ms); a cell's
	// width round it is dragged along at half its speed. The pressure solve then turns the rest round it.
	for (const FMover& M : Movers)
	{
		const float Out = FMath::Exp(-Dt / 0.02f);
		const float Reach = M.RadiusCm + CellCm;
		ForBox([&](int32 X, int32 Y, int32 Z, int32 I)
		{
			float D;
			if (M.Extent.IsZero())
			{
				D = FVector3f::Dist(CellCentre(X, Y, Z), M.Centre);
			}
			else
			{
				// The box's distance outside it (0 inside), as a sphere of radius 0 would see it.
				const FVector3f P = M.Rotation.UnrotateVector(CellCentre(X, Y, Z) - M.Centre);
				const FVector3f Q(FMath::Max(FMath::Abs(P.X) - M.Extent.X, 0.0f), FMath::Max(FMath::Abs(P.Y) - M.Extent.Y, 0.0f), FMath::Max(FMath::Abs(P.Z) - M.Extent.Z, 0.0f));
				D = Q.IsZero() ? -1.0f : Q.Size() + M.RadiusCm;
			}
			if (D < M.RadiusCm)
			{
				Ux[I] = M.Velocity.X;
				Uy[I] = M.Velocity.Y;
				Uz[I] = M.Velocity.Z;
				Rho[I] *= Out;
				Heat[I] *= Out;
			}
			else if (D < Reach)
			{
				Ux[I] += (M.Velocity.X - Ux[I]) * 0.5f;
				Uy[I] += (M.Velocity.Y - Uy[I]) * 0.5f;
				Uz[I] += (M.Velocity.Z - Uz[I]) * 0.5f;
			}
		});
	}
	// 3. Everything rides the velocity (the velocity itself too: free-streaming in a vacuum).
	Advect(Dt);
	// 4. Incompressible in air, the floor and walls turning the flow.
	if (bAir)
	{
		Project(K.SwellPerSecond * FMath::Exp(-Age / K.SwellSeconds));
	}
	// 5. Dissipation; the outer two cells bleed (a soft open boundary: nothing drawn at the box faces).
	const float Decay = FMath::Exp(-Dt * K.DecayPerSecond);
	const float Cool = FMath::Exp(-Dt / K.HeatSeconds);
	ForBox([&](int32 X, int32 Y, int32 Z, int32 I)
	{
		const bool bEdge = FMath::Min3(FMath::Min(X, N - 1 - X), FMath::Min(Y, N - 1 - Y), FMath::Min(Z, N - 1 - Z)) < 2;
		Rho[I] *= bEdge ? Decay * 0.5f : Decay;
		Heat[I] *= Cool;
	});
}

void FOSSmokeSim::StepGrid(float Dt, const FOSSmokeParams& K, float GravityScale)
{
	Jacobi = K.Jacobi;
	Time += Dt;
	UpdateBox();
	if (Hi.X < Lo.X)
	{
		return;
	}
	if (bAir)
	{
		const float Drag = FMath::Exp(-Dt / K.DragSeconds);
		const FVector3f Lift = -Down * (K.Buoyancy * GravityScale * Dt);
		ForBox([&](int32 X, int32 Y, int32 Z, int32 I)
		{
			const float Warm = Heat[I] + K.ResidualLift * FMath::Min(Rho[I], 1.0f);
			Ux[I] = (Ux[I] + Lift.X * Warm) * Drag;
			Uy[I] = (Uy[I] + Lift.Y * Warm) * Drag;
			Uz[I] = (Uz[I] + Lift.Z * Warm) * Drag;
		});
		Churn(Dt, K.Turbulence, K.AmbientChurn);
	}
	Confine(Dt, bAir ? K.Vorticity : K.Vorticity * 0.5f);
	for (const FMover& M : Movers)
	{
		ForBox([&](int32 X, int32 Y, int32 Z, int32 I)
		{
			float D;
			if (M.Extent.IsZero())
			{
				D = FVector3f::Dist(CellCentre(X, Y, Z), M.Centre);
			}
			else
			{
				const FVector3f Q3 = M.Rotation.UnrotateVector(CellCentre(X, Y, Z) - M.Centre);
				const FVector3f Q(FMath::Max(FMath::Abs(Q3.X) - M.Extent.X, 0.0f), FMath::Max(FMath::Abs(Q3.Y) - M.Extent.Y, 0.0f), FMath::Max(FMath::Abs(Q3.Z) - M.Extent.Z, 0.0f));
				D = Q.IsZero() ? -1.0f : Q.Size() + M.RadiusCm;
			}
			if (D < M.RadiusCm + CellCm)
			{
				const float Share = D < M.RadiusCm ? 1.0f : 0.5f;
				Ux[I] += (M.Velocity.X - Ux[I]) * Share;
				Uy[I] += (M.Velocity.Y - Uy[I]) * Share;
				Uz[I] += (M.Velocity.Z - Uz[I]) * Share;
			}
		});
	}
	if (bAir)
	{
		Project(0.0f);
	}
}

void FOSSmokeSim::Shift(const FIntVector& D)
{
	if (D == FIntVector::ZeroValue)
	{
		return;
	}
	for (TArray<float>* F : { &Rho, &Heat, &Ux, &Uy, &Uz, &P, &Curl })
	{
		ParallelFor(N, [&](int32 Z)
		{
			for (int32 Y = 0; Y < N; ++Y)
			{
				for (int32 X = 0; X < N; ++X)
				{
					const int32 SX = X + D.X, SY = Y + D.Y, SZ = Z + D.Z;
					T0[Index(X, Y, Z)] = (SX < 0 || SY < 0 || SZ < 0 || SX >= N || SY >= N || SZ >= N) ? 0.0f : (*F)[Index(SX, SY, SZ)];
				}
			}
		});
		Swap(*F, T0);
	}
	// The box moved by D cells: the planes stay where they are in the world.
	const FVector3f Moved = FVector3f(D) * CellCm;
	for (FPlane4f& Plane : Planes)
	{
		Plane.W -= Plane.GetSafeNormal() | Moved;
	}
	RebuildSolids();
}

FVector3f FOSSmokeSim::Centroid(float& OutMass) const
{
	double Mass = 0.0, X = 0.0, Y = 0.0, Z = 0.0;
	for (int32 K = 0; K < N; ++K)
	{
		for (int32 J = 0; J < N; ++J)
		{
			for (int32 I = 0; I < N; ++I)
			{
				const float R = Rho[Index(I, J, K)];
				Mass += R;
				X += R * I;
				Y += R * J;
				Z += R * K;
			}
		}
	}
	OutMass = float(Mass);
	if (Mass < 1e-4)
	{
		return FVector3f::ZeroVector;
	}
	const float C = 0.5f * (N - 1);
	return FVector3f(float(X / Mass) - C, float(Y / Mass) - C, float(Z / Mass) - C);
}

float FOSSmokeSim::MeanRadialSpeed() const
{
	float Mass;
	const FVector3f C = Centroid(Mass) + 0.5f * (N - 1);
	if (Mass < 1e-4f)
	{
		return 0.0f;
	}
	double Sum = 0.0;
	for (int32 K = 0; K < N; ++K)
	{
		for (int32 J = 0; J < N; ++J)
		{
			for (int32 I = 0; I < N; ++I)
			{
				const int32 Idx = Index(I, J, K);
				Sum += Rho[Idx] * (FVector3f(Ux[Idx], Uy[Idx], Uz[Idx]) | (FVector3f(I, J, K) - C).GetSafeNormal());
			}
		}
	}
	return float(Sum / Mass);
}

void FOSSmokeSim::Stats(float& OutMeanSpeed, float& OutMaxHeat) const
{
	double Mass = 0.0, Speed = 0.0;
	OutMaxHeat = 0.0f;
	for (int32 I = 0; I < Rho.Num(); ++I)
	{
		Mass += Rho[I];
		Speed += Rho[I] * FMath::Sqrt(Ux[I] * Ux[I] + Uy[I] * Uy[I] + Uz[I] * Uz[I]);
		OutMaxHeat = FMath::Max(OutMaxHeat, Heat[I]);
	}
	OutMeanSpeed = Mass > 1e-4 ? float(Speed / Mass) : 0.0f;
}

void FOSSmokeSim::Pack(TArray<FColor>& Out, const FVector3f& KeyDir, float KeyShadow, float Occlusion, float Sharpen)
{
	const FIntPoint Size = AtlasSize(N);
	Out.SetNumZeroed(Size.X * Size.Y);
	const auto Byte = [](float V) { return uint8(FMath::Clamp(FMath::RoundToInt(V * 255.0f), 0, 255)); };
	const auto At = [this](const FIntVector& Q) { return Rho[Index(FMath::Clamp(Q.X, 0, N - 1), FMath::Clamp(Q.Y, 0, N - 1), FMath::Clamp(Q.Z, 0, N - 1))]; };
	// The blur of the density for the sharpening: a box of 5 cells, separable (T0 -> T1 -> T2).
	const auto BoxPass = [this](const TArray<float>& From, TArray<float>& To, const FIntVector& Axis)
	{
		ParallelFor(N, [&](int32 Z)
		{
			for (int32 Y = 0; Y < N; ++Y)
			{
				for (int32 X = 0; X < N; ++X)
				{
					float Sum = 0.0f;
					for (int32 K = -2; K <= 2; ++K)
					{
						const FIntVector Q = FIntVector(X, Y, Z) + Axis * K;
						Sum += From[Index(FMath::Clamp(Q.X, 0, N - 1), FMath::Clamp(Q.Y, 0, N - 1), FMath::Clamp(Q.Z, 0, N - 1))];
					}
					To[Index(X, Y, Z)] = Sum * 0.2f;
				}
			}
		});
	};
	BoxPass(Rho, T0, FIntVector(1, 0, 0));
	BoxPass(T0, T1, FIntVector(0, 1, 0));
	BoxPass(T1, T2, FIntVector(0, 0, 1));
	// The key light's shadow: the gas toward it, 1 to 10 cells (the steps rounded to cells once).
	constexpr int32 KeySteps = 6;
	const float Steps[KeySteps] = { 1.0f, 2.0f, 3.0f, 5.0f, 7.0f, 10.0f };
	const float Weights[KeySteps] = { 1.0f, 1.0f, 1.0f, 1.5f, 2.0f, 2.5f };
	FIntVector KeyCells[KeySteps];
	for (int32 S = 0; S < KeySteps; ++S)
	{
		const FVector3f U = KeyDir * Steps[S];
		KeyCells[S] = FIntVector(FMath::RoundToInt(U.X), FMath::RoundToInt(U.Y), FMath::RoundToInt(U.Z));
	}
	ParallelFor(N, [&](int32 Z)
	{
		const int32 OX = (Z % 8) * N, OY = (Z / 8) * N;
		for (int32 Y = 0; Y < N; ++Y)
		{
			for (int32 X = 0; X < N; ++X)
			{
				const int32 I = Index(X, Y, Z);
				float Key = 1.0f, Ambient = 1.0f;
				if (Rho[I] > 0.004f)
				{
					const FIntVector C(X, Y, Z);
					float Toward = 0.0f;
					for (int32 S = 0; S < KeySteps; ++S)
					{
						Toward += At(C + KeyCells[S]) * Weights[S];
					}
					const float Around = At(C + FIntVector(2, 0, 0)) + At(C - FIntVector(2, 0, 0)) + At(C + FIntVector(0, 2, 0))
						+ At(C - FIntVector(0, 2, 0)) + At(C + FIntVector(0, 0, 2)) + At(C - FIntVector(0, 0, 2));
					Key = FMath::Exp(-KeyShadow * Toward);
					Ambient = FMath::Exp(-Occlusion * Around);
				}
				// Never hollow: the gas less its blur alone kept only a blob's rim (a dark glass dome), so the inside keeps
				// at least 0.45 of its density, broken up by the cell hash into holes and strands.
				const uint32 Hash = (uint32(I) * 2654435761u) ^ NoiseSeed;
				const float Keep = 0.25f + 0.35f * float((Hash >> 9) & 0xFF) / 255.0f;
				const float Drawn = FMath::Max(FMath::Max(0.0f, Rho[I] - Sharpen * T2[I]) / FMath::Max(1.0f - Sharpen, 0.05f), Rho[I] * Keep);
				Out[(OY + Y) * Size.X + OX + X] = FColor(Byte(Drawn), Byte(Heat[I]), Byte(Key), Byte(Ambient));
			}
		}
	});
}
