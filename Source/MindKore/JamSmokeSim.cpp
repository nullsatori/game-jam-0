#include "JamSmokeSim.h"

#include "Async/ParallelFor.h"

void FJamSmokeSim::Reset(int32 InN, float InCellCm, bool bInAir, const FVector3f& InDown)
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
}

void FJamSmokeSim::RebuildSolids()
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

float FJamSmokeSim::Sample(const TArray<float>& F, const FVector3f& Cells) const
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

void FJamSmokeSim::Inject(const FVector3f& Centre, const FVector3f& Axis, float LengthCm, float RadiusCm, float InRho, float InHeat, const FVector3f& Velocity, float Radial)
{
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
				const FVector3f V = Velocity * (1.0f + 0.3f * Jitter) + Q.GetSafeNormal() * Radial;
				const float B = FMath::Min(W * 2.0f, 1.0f);
				Ux[I] = FMath::Lerp(Ux[I], V.X, B);
				Uy[I] = FMath::Lerp(Uy[I], V.Y, B);
				Uz[I] = FMath::Lerp(Uz[I], V.Z, B);
			}
		}
	});
}

bool FJamSmokeSim::Impulse(const FVector3f& Origin, const FVector3f& Dir, float RadiusCm, float Along, float Push, float Clear)
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
				if (T < 0.0f)
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

void FJamSmokeSim::Confine(float Dt, float Epsilon)
{
	const float Inv2 = 0.5f / CellCm;
	const auto At = [this](int32 X, int32 Y, int32 Z) { return Index(FMath::Clamp(X, 0, N - 1), FMath::Clamp(Y, 0, N - 1), FMath::Clamp(Z, 0, N - 1)); };
	// omega = curl U (central differences), and its magnitude: the churn the material shows.
	ParallelFor(N, [&](int32 Z)
	{
		for (int32 Y = 0; Y < N; ++Y)
		{
			for (int32 X = 0; X < N; ++X)
			{
				const int32 I = Index(X, Y, Z);
				const int32 XP = At(X + 1, Y, Z), XM = At(X - 1, Y, Z), YP = At(X, Y + 1, Z), YM = At(X, Y - 1, Z), ZP = At(X, Y, Z + 1), ZM = At(X, Y, Z - 1);
				Wx[I] = ((Uz[YP] - Uz[YM]) - (Uy[ZP] - Uy[ZM])) * Inv2;
				Wy[I] = ((Ux[ZP] - Ux[ZM]) - (Uz[XP] - Uz[XM])) * Inv2;
				Wz[I] = ((Uy[XP] - Uy[XM]) - (Ux[YP] - Ux[YM])) * Inv2;
				Curl[I] = FMath::Sqrt(Wx[I] * Wx[I] + Wy[I] * Wy[I] + Wz[I] * Wz[I]);
			}
		}
	});
	if (Epsilon <= 0.0f)
	{
		return;
	}
	// Fedkiw: f = eps h (normalize(grad |omega|) x omega).
	const float Scale = Epsilon * CellCm * Dt;
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
				FVector3f Eta((Curl[At(X + 1, Y, Z)] - Curl[At(X - 1, Y, Z)]), (Curl[At(X, Y + 1, Z)] - Curl[At(X, Y - 1, Z)]), (Curl[At(X, Y, Z + 1)] - Curl[At(X, Y, Z - 1)]));
				const float Len = Eta.Size();
				if (Len < 1e-5f)
				{
					continue;
				}
				Eta /= Len;
				const FVector3f F = (Eta ^ FVector3f(Wx[I], Wy[I], Wz[I])) * Scale;
				Ux[I] += F.X;
				Uy[I] += F.Y;
				Uz[I] += F.Z;
			}
		}
	});
}

void FJamSmokeSim::Advect(float Dt)
{
	const float Cells = Dt / CellCm;
	ParallelFor(N, [&](int32 Z)
	{
		for (int32 Y = 0; Y < N; ++Y)
		{
			for (int32 X = 0; X < N; ++X)
			{
				const int32 I = Index(X, Y, Z);
				if (Solid[I])
				{
					T0[I] = T1[I] = T2[I] = T3[I] = T4[I] = 0.0f;
					continue;
				}
				const FVector3f From = FVector3f(X, Y, Z) - FVector3f(Ux[I], Uy[I], Uz[I]) * Cells;
				T0[I] = Sample(Ux, From);
				T1[I] = Sample(Uy, From);
				T2[I] = Sample(Uz, From);
				T3[I] = Sample(Rho, From);
				T4[I] = Sample(Heat, From);
				// The density's limits where it came from (MacCormack's clamp below).
				const int32 X0 = FMath::Clamp(int32(From.X), 0, N - 2), Y0 = FMath::Clamp(int32(From.Y), 0, N - 2), Z0 = FMath::Clamp(int32(From.Z), 0, N - 2);
				float Lo = Rho[Index(X0, Y0, Z0)], Hi = Lo;
				for (int32 C = 1; C < 8; ++C)
				{
					const float V = Rho[Index(X0 + (C & 1), Y0 + ((C >> 1) & 1), Z0 + (C >> 2))];
					Lo = FMath::Min(Lo, V);
					Hi = FMath::Max(Hi, V);
				}
				Wx[I] = Lo;
				Wy[I] = Hi;
			}
		}
	});
	// MacCormack for the density only: trace the advected field back, take half the round trip's error off, clamp
	// to where it came from. Semi-Lagrangian alone smears a 4 cm grid into a ball within a second; this keeps rims.
	ParallelFor(N, [&](int32 Z)
	{
		for (int32 Y = 0; Y < N; ++Y)
		{
			for (int32 X = 0; X < N; ++X)
			{
				const int32 I = Index(X, Y, Z);
				Wz[I] = Solid[I] ? 0.0f : Sample(T3, FVector3f(X, Y, Z) + FVector3f(Ux[I], Uy[I], Uz[I]) * Cells);
			}
		}
	});
	ParallelFor(N, [&](int32 Z)
	{
		for (int32 I = Z * N * N, End = I + N * N; I < End; ++I)
		{
			T3[I] = Solid[I] ? 0.0f : FMath::Clamp(T3[I] + 0.5f * (Rho[I] - Wz[I]), Wx[I], Wy[I]);
		}
	});
	Swap(Ux, T0);
	Swap(Uy, T1);
	Swap(Uz, T2);
	Swap(Rho, T3);
	Swap(Heat, T4);
}

void FJamSmokeSim::Project(float Swell)
{
	const float H = CellCm;
	const float Inv2 = 0.5f / H;
	// A neighbour's velocity for the divergence: a solid's is 0, outside the box the cell's own (the gas leaves freely).
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
	ParallelFor(N, [&](int32 Z)
	{
		for (int32 Y = 0; Y < N; ++Y)
		{
			for (int32 X = 0; X < N; ++X)
			{
				const int32 I = Index(X, Y, Z);
				Div[I] = Solid[I] ? 0.0f
					: (Vel(Ux, I, X + 1, Y, Z) - Vel(Ux, I, X - 1, Y, Z) + Vel(Uy, I, X, Y + 1, Z) - Vel(Uy, I, X, Y - 1, Z) + Vel(Uz, I, X, Y, Z + 1) - Vel(Uz, I, X, Y, Z - 1)) * Inv2
						- Swell * Heat[I];
			}
		}
	});
	// Red-black Gauss-Seidel, warm-started from the last step's pressure. A solid neighbour mirrors the cell
	// (no flow through it), outside the box the pressure is 0 (open faces).
	for (int32 Sweep = 0; Sweep < 2 * Jacobi; ++Sweep)
	{
		const int32 Colour = Sweep & 1;
		ParallelFor(N, [&](int32 Z)
		{
			for (int32 Y = 0; Y < N; ++Y)
			{
				for (int32 X = (Y + Z + Colour) & 1; X < N; X += 2)
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
			}
		}
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

void FJamSmokeSim::Churn(float Dt, float Amount)
{
	if (Amount <= 0.0f)
	{
		return;
	}
	// Only where there is heat (the fresh gas; nothing once it has cooled), a lattice of ~3 cells drifting with time.
	const FVector3f Drift(Time * 0.9f, Time * -0.7f, Time * 0.5f);
	ParallelFor(N, [&](int32 Z)
	{
		for (int32 Y = 0; Y < N; ++Y)
		{
			for (int32 X = 0; X < N; ++X)
			{
				const int32 I = Index(X, Y, Z);
				if (Heat[I] < 0.02f || Solid[I])
				{
					continue;
				}
				const FVector3f P = FVector3f(X, Y, Z) / 2.5f + Drift;
				const float S = Amount * Heat[I] * Dt;
				Ux[I] += S * SmokeNoise(P, NoiseSeed);
				Uy[I] += S * SmokeNoise(P, NoiseSeed ^ 0x9E3779B9u);
				Uz[I] += S * SmokeNoise(P, NoiseSeed ^ 0x85EBCA6Bu);
			}
		}
	});
}

void FJamSmokeSim::Step(float Dt, float Age, const FJamSmokeParams& K)
{
	Jacobi = K.Jacobi;
	Time += Dt;
	// 1. Forces. Air: hot gas rises against Down and the air takes the gas's own motion. A vacuum: nothing acts.
	if (bAir)
	{
		const float Drag = FMath::Exp(-Dt / K.DragSeconds);
		const FVector3f Lift = -Down * (K.Buoyancy * Dt);
		ParallelFor(N, [&](int32 Z)
		{
			for (int32 I = Z * N * N, End = I + N * N; I < End; ++I)
			{
				Ux[I] = (Ux[I] + Lift.X * Heat[I]) * Drag;
				Uy[I] = (Uy[I] + Lift.Y * Heat[I]) * Drag;
				Uz[I] = (Uz[I] + Lift.Z * Heat[I]) * Drag;
			}
		});
	}
	// The fresh gas churns (in a vacuum too: its own turbulence, frozen in as it flies apart).
	Churn(Dt, K.Turbulence);
	// 2. The curls the coarse grid would smear (no pressure feeds them in a vacuum: half).
	Confine(Dt, bAir ? K.Vorticity : K.Vorticity * 0.5f);
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
	ParallelFor(N, [&](int32 Z)
	{
		for (int32 Y = 0; Y < N; ++Y)
		{
			for (int32 X = 0; X < N; ++X)
			{
				const int32 I = Index(X, Y, Z);
				const bool bEdge = FMath::Min3(FMath::Min(X, N - 1 - X), FMath::Min(Y, N - 1 - Y), FMath::Min(Z, N - 1 - Z)) < 2;
				Rho[I] *= bEdge ? Decay * 0.5f : Decay;
				Heat[I] *= Cool;
			}
		}
	});
}

void FJamSmokeSim::Shift(const FIntVector& D)
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

FVector3f FJamSmokeSim::Centroid(float& OutMass) const
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

float FJamSmokeSim::MeanRadialSpeed() const
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

void FJamSmokeSim::Stats(float& OutMeanSpeed, float& OutMaxHeat) const
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

void FJamSmokeSim::Pack(TArray<FColor>& Out) const
{
	const FIntPoint Size = AtlasSize(N);
	Out.SetNumZeroed(Size.X * Size.Y);
	const auto Byte = [](float V) { return uint8(FMath::Clamp(FMath::RoundToInt(V * 255.0f), 0, 255)); };
	ParallelFor(N, [&](int32 Z)
	{
		const int32 OX = (Z % 8) * N, OY = (Z / 8) * N;
		for (int32 Y = 0; Y < N; ++Y)
		{
			for (int32 X = 0; X < N; ++X)
			{
				const int32 I = Index(X, Y, Z);
				Out[(OY + Y) * Size.X + OX + X] = FColor(Byte(Rho[I]), Byte(Heat[I]), Byte(Curl[I] / 40.0f), Solid[I] ? 255 : 0);
			}
		}
	});
}
