// jam.Head.Probe: the head against geometry (review #44) and the hold at rest.
// Lvl_Jam_TestRoom and Lvl_Jam_Ship: hold on to the ceiling, pull up with Space
// and W, swing, logging the camera's distance to the nearest geometry every
// tick; walk under a low slab spawned for the run. Lvl_Jam_Ship also: hold on to
// a cabin wall, each door's leaf (both faces, closed and open) and the
// planetoid's ground (flat and a slope), rest, pull, rest - the hold must sit
// still (speed, contacts, shakes and velocity reversals logged per tick).
// Run at -UseFixedTimeStep -FPS=60; "shots" adds HighResShot frames of the pull-ups.

#include "JamShip.h"
#include "JamZeroGPawn.h"
#include "MKDoor.h"
#include "MKVitals.h"
#include "MindKore.h"

#include "Camera/CameraComponent.h"
#include "Components/BoxComponent.h"
#include "Components/SphereComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/OverlapResult.h"
#include "Engine/StaticMesh.h"
#include "Engine/StaticMeshActor.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/PlayerController.h"
#include "HAL/IConsoleManager.h"
#include "TimerManager.h"

// Named, not anonymous: in a unity build this shares a file with other probes.
namespace JamHeadProbe
{
	bool bShots = false;

	void After(AJamZeroGPawn* Pawn, float Seconds, TFunction<void()> Step)
	{
		FTimerHandle Handle;
		Pawn->GetWorldTimerManager().SetTimer(Handle, FTimerDelegate::CreateWeakLambda(Pawn, MoveTemp(Step)), FMath::Max(Seconds, 0.001f), false);
	}

	/** Step once a frame until it returns true (a looping timer shorter than the frame fires several times in one). */
	void EveryFrame(AJamZeroGPawn* Pawn, TFunction<bool()> Step)
	{
		const TSharedRef<FTimerHandle> Handle = MakeShared<FTimerHandle>();
		const TSharedRef<uint64> Last = MakeShared<uint64>(0);
		Pawn->GetWorldTimerManager().SetTimer(*Handle, FTimerDelegate::CreateWeakLambda(Pawn, [Pawn, Handle, Last, Step = MoveTemp(Step)]()
		{
			if (*Last == GFrameCounter)
			{
				return;
			}
			*Last = GFrameCounter;
			if (Handle->IsValid() && Step())
			{
				Pawn->GetWorldTimerManager().ClearTimer(*Handle);
			}
		}), 0.0005f, true);
	}

	const TCHAR* Verdict(bool bPass)
	{
		return bPass ? TEXT("PASS") : TEXT("FAIL");
	}

	void Look(AJamZeroGPawn* Pawn, const FRotator& Rotation)
	{
		if (AController* Controller = Pawn->GetController())
		{
			Controller->SetControlRotation(Rotation);
		}
	}

	/** The body at rest at Location - raised in 10 cm steps until its sphere is clear (a body put into the floor
	 *  is thrown out at ~16000 cm/s and dies of the crash). */
	void Place(AJamZeroGPawn* Pawn, const FVector& Location)
	{
		FCollisionQueryParams Params(SCENE_QUERY_STAT(JamHeadProbe), false, Pawn);
		FVector Spot = Location;
		for (int32 Step = 0; Step < 20 && Pawn->GetWorld()->OverlapBlockingTestByChannel(Spot, FQuat::Identity, ECC_Pawn,
			FCollisionShape::MakeSphere(Pawn->Body->GetScaledSphereRadius() + 1.0f), Params); ++Step)
		{
			Spot.Z += 10.0;
		}
		if (Spot != Location)
		{
			UE_LOG(LogMindKore, Log, TEXT("Head probe: %s is in something, the body put %.0f cm higher"), *Location.ToCompactString(), Spot.Z - Location.Z);
		}
		Pawn->Body->SetWorldLocation(Spot, false, nullptr, ETeleportType::TeleportPhysics);
		Pawn->Body->SetPhysicsLinearVelocity(FVector::ZeroVector);
	}

	void Shot(AJamZeroGPawn* Pawn, const FString& Label)
	{
		if (APlayerController* Controller = Cast<APlayerController>(Pawn->GetController()); Controller && bShots)
		{
			UE_LOG(LogMindKore, Log, TEXT("Head probe: HighResShot [%s]"), *Label);
			Controller->ConsoleCommand(TEXT("HighResShot 1"));
		}
	}

	/** The camera against geometry: the nearest blocking surface within 100 cm (26 rays and the closest point of
	 *  each simple collision around it), and whether the segment from the body's centre to the camera crosses one. */
	struct FClear
	{
		float Cm = 100.0f;
		bool bCrossed = false;
		FString What;
	};

	FClear Measure(const AJamZeroGPawn* Pawn)
	{
		const UWorld* World = Pawn->GetWorld();
		const FVector Camera = Pawn->Camera->GetComponentLocation();
		FCollisionQueryParams Params(SCENE_QUERY_STAT(JamHeadProbe), false, Pawn);
		if (const UPrimitiveComponent* Held = Pawn->GetGrabbed())
		{
			Params.AddIgnoredComponent(Held);
		}
		FClear Out;
		FHitResult Hit;
		if (World->LineTraceSingleByChannel(Hit, Pawn->Body->GetComponentLocation(), Camera, ECC_Visibility, Params))
		{
			Out.bCrossed = true;
			Out.Cm = 0.0f;
			Out.What = GetNameSafe(Hit.GetActor());
			return Out;
		}
		for (int32 X = -1; X <= 1; ++X)
		{
			for (int32 Y = -1; Y <= 1; ++Y)
			{
				for (int32 Z = -1; Z <= 1; ++Z)
				{
					const FVector Direction = FVector(X, Y, Z).GetSafeNormal();
					if (!Direction.IsZero() && World->LineTraceSingleByChannel(Hit, Camera, Camera + Direction * 100.0f, ECC_Visibility, Params) && Hit.Distance < Out.Cm)
					{
						Out.Cm = Hit.Distance;
						Out.What = GetNameSafe(Hit.GetActor());
					}
				}
			}
		}
		TArray<FOverlapResult> Near;
		World->OverlapMultiByChannel(Near, Camera, FQuat::Identity, ECC_Visibility, FCollisionShape::MakeSphere(100.0f), Params);
		for (const FOverlapResult& Overlap : Near)
		{
			UPrimitiveComponent* Component = Overlap.GetComponent();
			FVector Closest;
			const float Distance = Component && !Component->IsSimulatingPhysics() ? Component->GetDistanceToCollision(Camera, Closest) : -1.0f;
			if (Distance >= 0.0f && Distance < Out.Cm)
			{
				Out.Cm = Distance;
				Out.What = GetNameSafe(Component->GetOwner());
			}
		}
		return Out;
	}

	/** The hold under the view (as RMB): RMB itself when it would hold on, else the same trace and Cling. */
	bool HoldOn(AJamZeroGPawn* Pawn, FHitResult& Hit)
	{
		FCollisionQueryParams Params(SCENE_QUERY_STAT(JamHeadProbe), false, Pawn);
		const FVector Eye = Pawn->ShoulderLocation();
		if (!Pawn->GetWorld()->LineTraceSingleByChannel(Hit, Eye, Eye + Pawn->GetControlRotation().Vector() * Pawn->GrabReachCm, ECC_Visibility, Params))
		{
			return false;
		}
		if (Pawn->GetRmbAction() == EJamRmbAction::Cling)
		{
			Pawn->Use();
		}
		else
		{
			Pawn->Cling(Hit.GetComponent(), Hit.ImpactPoint);
		}
		return Pawn->IsClinging();
	}

	/**
	 * Standing at Spot facing Yaw: look up, hold on to the ceiling, then the view ahead: hang 1 s, Space 2 s,
	 * W 2 s with the view 60 degrees up, D 1.5 s (the swing), rest 1.5 s, let go. Every tick: the camera's
	 * clearance and its height under the ceiling's plane.
	 */
	void PullUp(AJamZeroGPawn* Pawn, const FString& Label, const FVector& Spot, float Yaw, TFunction<void()> Then)
	{
		Place(Pawn, Spot);
		Look(Pawn, FRotator(89.0f, Yaw, 0.0f));
		After(Pawn, 1.5f, [Pawn, Label, Yaw, Then]()
		{
			FHitResult Ceiling;
			const bool bHeld = HoldOn(Pawn, Ceiling);
			UE_LOG(LogMindKore, Log, TEXT("Head probe [%s]: %s %s %.0f cm over the eye, normal %s, %s, mode %s"), *Label, bHeld ? TEXT("holding") : TEXT("NOT holding"),
				*GetNameSafe(Ceiling.GetActor()), Ceiling.Distance, *FVector(Ceiling.ImpactNormal).ToCompactString(),
				Pawn->IsInGravity() ? TEXT("gravity") : TEXT("zero-G"), *UEnum::GetDisplayValueAsText(Pawn->GetMoveMode()).ToString());
			if (!bHeld)
			{
				UE_LOG(LogMindKore, Log, TEXT("Head probe [%s]: no hold - FAIL"), *Label);
				Then();
				return;
			}
			Look(Pawn, FRotator(0.0f, Yaw, 0.0f));
			const double Start = Pawn->GetWorld()->GetTimeSeconds();
			const FVector Point = Ceiling.ImpactPoint;
			const FVector Normal = Ceiling.ImpactNormal;
			struct FMin
			{
				float Clear = 1000.0f;
				float Under = 1000.0f;
				bool bCrossed = false;
				int32 NextShot = 0;
			};
			const TSharedRef<TMap<FString, FMin>> Mins = MakeShared<TMap<FString, FMin>>();
			EveryFrame(Pawn, [Pawn, Label, Yaw, Then, Start, Point, Normal, Mins]()
			{
				const float T = float(Pawn->GetWorld()->GetTimeSeconds() - Start);
				const TCHAR* Phase = T < 1.0f ? TEXT("hang") : T < 3.0f ? TEXT("Space") : T < 5.0f ? TEXT("W up") : T < 6.5f ? TEXT("D") : TEXT("rest");
				if (T < 1.0f || T >= 6.5f)
				{
					Pawn->ProbeThrust(FVector::ZeroVector, false);
				}
				else if (T < 3.0f)
				{
					Pawn->ProbeThrust(FVector(0.0f, 0.0f, 1.0f), false);
				}
				else
				{
					Look(Pawn, FRotator(60.0f, Yaw, 0.0f));
					Pawn->ProbeThrust(T < 5.0f ? FVector(1.0f, 0.0f, 0.0f) : FVector(0.0f, 1.0f, 0.0f), false);
				}
				const FClear Clear = Measure(Pawn);
				const float Under = float(FVector::DotProduct(Pawn->Camera->GetComponentLocation() - Point, Normal));
				FMin& Min = Mins->FindOrAdd(Phase);
				Min.Clear = FMath::Min(Min.Clear, Clear.Cm);
				Min.Under = FMath::Min(Min.Under, Under);
				Min.bCrossed |= Clear.bCrossed;
				UE_LOG(LogMindKore, Log, TEXT("Head probe [%s %s +%.3f]: camera %.1f cm off %s%s, %.1f cm under the hold's plane, arm %.1f (reach %.1f), %.0f cm/s, %s"),
					*Label, Phase, T, Clear.Cm, *Clear.What, Clear.bCrossed ? TEXT(" CROSSED") : TEXT(""), Under, Pawn->GetArmLengthCm(), Pawn->GetArmRestCm(),
					Pawn->GetBodyVelocity().Size(), Pawn->IsClinging() ? TEXT("holding") : TEXT("NOT holding"));
				// Frames: one in the hang, then every 0.25 s of the pull-ups.
				FMin& Frames = Mins->FindOrAdd(TEXT("frames"));
				const float NextShotAt = Frames.NextShot == 0 ? 0.5f : 1.0f + 0.25f * float(Frames.NextShot - 1);
				if (T >= NextShotAt && NextShotAt <= 5.0f)
				{
					Shot(Pawn, FString::Printf(TEXT("%s %s +%.2f, camera %.1f cm off geometry, %.1f under the plane"), *Label, Phase, T, Clear.Cm, Under));
					++Frames.NextShot;
				}
				if (T < 8.0f)
				{
					return false;
				}
				Pawn->LetGoCling();
				bool bPass = true;
				for (const TPair<FString, FMin>& Pair : *Mins)
				{
					if (Pair.Key == TEXT("frames"))
					{
						continue;
					}
					const bool bPhase = !Pair.Value.bCrossed && Pair.Value.Clear >= 0.8f * Pawn->EyeRadiusCm && Pair.Value.Under >= Pawn->HeadClearanceCm - 2.0f;
					bPass &= bPhase;
					UE_LOG(LogMindKore, Log, TEXT("Head probe [%s %s]: the camera at least %.1f cm off geometry (%.1f needed)%s, at least %.1f cm under the hold's plane (%.1f needed) - %s"),
						*Label, *Pair.Key, Pair.Value.Clear, 0.8f * Pawn->EyeRadiusCm, Pair.Value.bCrossed ? TEXT(", CROSSED a surface") : TEXT(""), Pair.Value.Under,
						Pawn->HeadClearanceCm - 2.0f, Verdict(bPhase));
				}
				UE_LOG(LogMindKore, Log, TEXT("Head probe [%s]: dt %.4f s - %s"), *Label, Pawn->GetWorld()->GetDeltaSeconds(), Verdict(bPass));
				After(Pawn, 0.5f, Then);
				return true;
			});
		});
	}

	/** The horizontal direction from Spot with the most free room (8 tried), and that room, cm. */
	float FreeYaw(const AJamZeroGPawn* Pawn, const FVector& Spot, float& OutRoom)
	{
		FCollisionQueryParams Params(SCENE_QUERY_STAT(JamHeadProbe), false, Pawn);
		float Best = 0.0f;
		OutRoom = 0.0f;
		for (int32 Index = 0; Index < 8; ++Index)
		{
			const float Yaw = 45.0f * Index;
			const FVector Direction = FRotator(0.0f, Yaw, 0.0f).Vector();
			FHitResult Hit;
			const bool bHit = Pawn->GetWorld()->SweepSingleByChannel(Hit, Spot, Spot + Direction * 1500.0f, FQuat::Identity, ECC_Pawn,
				FCollisionShape::MakeSphere(Pawn->Body->GetScaledSphereRadius()), Params);
			const float Room = bHit ? Hit.Distance : 1500.0f;
			if (Room > OutRoom)
			{
				OutRoom = Room;
				Best = Yaw;
			}
		}
		return Best;
	}

	/**
	 * Walking under a low ceiling: a 3 x 3 m slab spawned for the run with its underside 135 cm over the floor
	 * (the standing eye is at ~150), 2.5 m ahead; W 1.6 s under it and out, then 1.5 s still. Every tick: the
	 * camera's clearance and its height over the floor.
	 */
	void LowCeiling(AJamZeroGPawn* Pawn, const FString& Label, const FVector& Spot, double FloorZ, TFunction<void()> Then)
	{
		float Room = 0.0f;
		const float Yaw = FreeYaw(Pawn, Spot, Room);
		const FVector Ahead = FRotator(0.0f, Yaw, 0.0f).Vector();
		UStaticMesh* Cube = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube"));
		const FVector SlabAt(Spot.X + Ahead.X * 400.0, Spot.Y + Ahead.Y * 400.0, FloorZ + 145.0);
		AStaticMeshActor* Slab = Pawn->GetWorld()->SpawnActor<AStaticMeshActor>(SlabAt, FRotator(0.0f, Yaw, 0.0f));
		if (!Slab || !Cube)
		{
			UE_LOG(LogMindKore, Log, TEXT("Head probe [%s]: no slab - FAIL"), *Label);
			Then();
			return;
		}
		Slab->GetStaticMeshComponent()->SetMobility(EComponentMobility::Movable);
		Slab->GetStaticMeshComponent()->SetStaticMesh(Cube);
		Slab->GetStaticMeshComponent()->SetCollisionProfileName(TEXT("BlockAll"));
		Slab->SetActorScale3D(FVector(3.0f, 3.0f, 0.2f));
		UE_LOG(LogMindKore, Log, TEXT("Head probe [%s]: a slab 300 x 300 x 20 cm, its underside %.0f cm over the floor, 2.5-5.5 m ahead along yaw %.0f (%.0f cm free)"),
			*Label, 135.0f, Yaw, Room);
		Place(Pawn, Spot);
		Look(Pawn, FRotator(0.0f, Yaw, 0.0f));
		After(Pawn, 1.5f, [Pawn, Label, FloorZ, Slab, Then]()
		{
			const double Start = Pawn->GetWorld()->GetTimeSeconds();
			const TSharedRef<FVector2f> Min = MakeShared<FVector2f>(1000.0f, 1000.0f); // clearance, eye over the floor
			const TSharedRef<bool> bCrossed = MakeShared<bool>(false);
			EveryFrame(Pawn, [Pawn, Label, FloorZ, Slab, Then, Start, Min, bCrossed]()
			{
				const float T = float(Pawn->GetWorld()->GetTimeSeconds() - Start);
				Pawn->ProbeThrust(T < 1.6f ? FVector(1.0f, 0.0f, 0.0f) : FVector::ZeroVector, false);
				const FClear Clear = Measure(Pawn);
				const float Eye = float(Pawn->Camera->GetComponentLocation().Z - FloorZ);
				Min->X = FMath::Min(Min->X, Clear.Cm);
				*bCrossed |= Clear.bCrossed;
				UE_LOG(LogMindKore, Log, TEXT("Head probe [%s +%.3f]: camera %.1f cm off %s%s, %.1f cm over the floor, body %s, %.0f cm/s, %s"),
					*Label, T, Clear.Cm, *Clear.What, Clear.bCrossed ? TEXT(" CROSSED") : TEXT(""), Eye, *Pawn->Body->GetComponentLocation().ToCompactString(),
					Pawn->GetBodyVelocity().Size(), *UEnum::GetDisplayValueAsText(Pawn->GetMoveMode()).ToString());
				if (T < 3.1f)
				{
					return false;
				}
				const bool bPass = !*bCrossed && Min->X >= 0.8f * Pawn->EyeRadiusCm;
				UE_LOG(LogMindKore, Log, TEXT("Head probe [%s]: the camera at least %.1f cm off geometry (%.1f needed)%s, dt %.4f s - %s"), *Label, Min->X,
					0.8f * Pawn->EyeRadiusCm, *bCrossed ? TEXT(", CROSSED a surface") : TEXT(""), Pawn->GetWorld()->GetDeltaSeconds(), Verdict(bPass));
				Slab->Destroy();
				After(Pawn, 0.5f, Then);
				return true;
			});
		});
	}

	/**
	 * Holding on at rest: the body put at BodyAt, after 1.2 s the hold taken on Target (the view at it), 2 s
	 * still, W 0.6 s (a pull toward the hand), 2 s still, let go. The last second of each rest must be still:
	 * under 2 cm/s, no camera shake, no reversal of the velocity along the arm. Contacts are logged
	 * (resting on the ground in gravity has them; holding in zero-G should not).
	 */
	void GripRest(AJamZeroGPawn* Pawn, const FString& Label, const FVector& BodyAt, const FVector& Target, TFunction<void(bool)> Then)
	{
		Place(Pawn, BodyAt);
		Look(Pawn, (Target - Pawn->ShoulderLocation()).Rotation());
		After(Pawn, 1.2f, [Pawn, Label, Target, Then]()
		{
			Look(Pawn, (Target - Pawn->ShoulderLocation()).Rotation());
			FHitResult Hit;
			const bool bHeld = HoldOn(Pawn, Hit);
			UE_LOG(LogMindKore, Log, TEXT("Head probe [%s]: %s %s (%s) at %s, %.0f cm from the eye, normal %s, %s, mode %s"), *Label,
				bHeld ? TEXT("holding") : TEXT("NOT holding"), *GetNameSafe(Hit.GetActor()), *GetNameSafe(Hit.GetComponent()), *FVector(Hit.ImpactPoint).ToCompactString(),
				Hit.Distance, *FVector(Hit.ImpactNormal).ToCompactString(), Pawn->IsInGravity() ? TEXT("gravity") : TEXT("zero-G"),
				*UEnum::GetDisplayValueAsText(Pawn->GetMoveMode()).ToString());
			if (!bHeld)
			{
				UE_LOG(LogMindKore, Log, TEXT("Head probe [%s]: no hold - FAIL"), *Label);
				Then(false);
				return;
			}
			struct FWindow
			{
				float MaxSpeed = 0.0f;
				int32 Contacts = 0;
				float Impulse = 0.0f;
				int32 Shakes = 0;
				int32 Reversals = 0;
				float LastRadial = 0.0f;
				int32 Frames = 0;
				int32 GravityFlips = 0;
			};
			const double Start = Pawn->GetWorld()->GetTimeSeconds();
			const TSharedRef<FWindow> First = MakeShared<FWindow>();
			const TSharedRef<FWindow> Second = MakeShared<FWindow>();
			const TSharedRef<FIntVector> Last = MakeShared<FIntVector>(Pawn->ProbeContacts, Pawn->ProbeShakes, Pawn->IsInGravity() ? 1 : 0);
			const TSharedRef<float> LastImpulse = MakeShared<float>(Pawn->ProbeContactImpulse);
			EveryFrame(Pawn, [Pawn, Label, Then, Start, First, Second, Last, LastImpulse]()
			{
				const float T = float(Pawn->GetWorld()->GetTimeSeconds() - Start);
				const bool bPull = T >= 2.0f && T < 2.6f;
				if (bPull)
				{
					Look(Pawn, (Pawn->GetHandLocation() - Pawn->ShoulderLocation()).Rotation());
				}
				Pawn->ProbeThrust(bPull ? FVector(1.0f, 0.0f, 0.0f) : FVector::ZeroVector, false);
				const FVector Velocity = Pawn->GetBodyVelocity();
				const FVector Arm = (Pawn->ShoulderLocation() - Pawn->GetHandLocation()).GetSafeNormal();
				const float Radial = float(FVector::DotProduct(Velocity, Arm));
				const int32 Contacts = Pawn->ProbeContacts - Last->X;
				const int32 Shakes = Pawn->ProbeShakes - Last->Y;
				const float Impulse = Pawn->ProbeContactImpulse - *LastImpulse;
				const int32 Gravity = Pawn->IsInGravity() ? 1 : 0;
				const bool bFlip = Gravity != Last->Z;
				*Last = FIntVector(Pawn->ProbeContacts, Pawn->ProbeShakes, Gravity);
				*LastImpulse = Pawn->ProbeContactImpulse;
				UE_LOG(LogMindKore, Log, TEXT("Head probe [%s +%.3f]%s: v %s = %.2f cm/s (along the arm %.2f), arm %.1f (reach %.1f), contacts %d (%.0f kg cm/s), shakes %d, %s%s, %s, health %.0f"),
					*Label, T, bPull ? TEXT(" W") : TEXT(""), *Velocity.ToCompactString(), Velocity.Size(), Radial, Pawn->GetArmLengthCm(), Pawn->GetArmRestCm(),
					Contacts, Impulse, Shakes, Gravity ? TEXT("gravity") : TEXT("zero-G"), bFlip ? TEXT(" (FLIPPED)") : TEXT(""),
					*UEnum::GetDisplayValueAsText(Pawn->GetMoveMode()).ToString(), Pawn->Vitals->Health);
				FWindow* Window = T >= 1.0f && T < 2.0f ? &*First : T >= 3.6f && T < 4.6f ? &*Second : nullptr;
				if (Window)
				{
					Window->MaxSpeed = FMath::Max(Window->MaxSpeed, float(Velocity.Size()));
					Window->Contacts += Contacts;
					Window->Impulse += Impulse;
					Window->Shakes += Shakes;
					Window->GravityFlips += bFlip ? 1 : 0;
					if (Window->Frames > 0 && FMath::Abs(Radial) > 0.5f && FMath::Abs(Window->LastRadial) > 0.5f && FMath::Sign(Radial) != FMath::Sign(Window->LastRadial))
					{
						++Window->Reversals;
					}
					if (FMath::Abs(Radial) > 0.5f)
					{
						Window->LastRadial = Radial;
					}
					++Window->Frames;
				}
				if (T < 4.6f)
				{
					return false;
				}
				Pawn->LetGoCling();
				bool bPass = true;
				for (int32 Index = 0; Index < 2; ++Index)
				{
					const FWindow& W = Index == 0 ? *First : *Second;
					const bool bStill = W.MaxSpeed < 2.0f && W.Shakes == 0 && W.Reversals == 0 && W.GravityFlips == 0;
					bPass &= bStill;
					UE_LOG(LogMindKore, Log, TEXT("Head probe [%s %s]: the last second of rest (%d frames) - max %.2f cm/s, %d contacts (%.0f kg cm/s; resting weight alone %.0f), %d shakes, %d reversals along the arm, %d gravity flips - %s"),
						*Label, Index == 0 ? TEXT("rest") : TEXT("after the pull"), W.Frames, W.MaxSpeed, W.Contacts, W.Impulse,
						Pawn->Body->GetMass() * (Pawn->IsInGravity() ? 980.0f : 0.0f) * 1.0f, W.Shakes, W.Reversals, W.GravityFlips, Verdict(bStill));
				}
				After(Pawn, 0.3f, [Then, bPass]() { Then(bPass); });
				return true;
			});
		});
	}

	/** Cases run one after another; each calls the next. */
	using FCase = TFunction<void(TFunction<void()>)>;

	void RunCases(TSharedRef<TArray<FCase>> Cases, int32 Index, AJamZeroGPawn* Pawn)
	{
		if (!Cases->IsValidIndex(Index))
		{
			UE_LOG(LogMindKore, Log, TEXT("Head probe: done"));
			return;
		}
		(*Cases)[Index]([Cases, Index, Pawn]() { RunCases(Cases, Index + 1, Pawn); });
	}

	/** The floor and the ceiling over Centre (null hit: none). */
	bool FloorAndCeiling(const AJamZeroGPawn* Pawn, const FVector& Centre, FHitResult& Floor, FHitResult& Ceiling)
	{
		FCollisionQueryParams Params(SCENE_QUERY_STAT(JamHeadProbe), false, Pawn);
		const UWorld* World = Pawn->GetWorld();
		return World->LineTraceSingleByChannel(Ceiling, Centre, Centre + FVector(0.0f, 0.0f, 1000.0f), ECC_Visibility, Params)
			&& World->LineTraceSingleByChannel(Floor, Centre, Centre - FVector(0.0f, 0.0f, 1000.0f), ECC_Visibility, Params);
	}

	void Grip(TArray<FCase>& Cases, AJamZeroGPawn* Pawn, const FString& Label, const FVector& BodyAt, const FVector& Target)
	{
		Cases.Add([Pawn, Label, BodyAt, Target](TFunction<void()> Next) { GripRest(Pawn, Label, BodyAt, Target, [Next](bool) { Next(); }); });
	}

	/** Lvl_Jam_Ship: the walls, the doors and the planetoid held on to. */
	void AddGrips(TArray<FCase>& Cases, AJamZeroGPawn* Pawn, const FVector& CabinCentre, double FloorZ)
	{
		UWorld* World = Pawn->GetWorld();
		FCollisionQueryParams Params(SCENE_QUERY_STAT(JamHeadProbe), false, Pawn);
		// A cabin wall: the nearest of four, held at 1 m.
		const FVector Chest(CabinCentre.X, CabinCentre.Y, FloorZ + 120.0);
		for (const FVector& Direction : { FVector::RightVector, FVector::LeftVector, FVector::ForwardVector, FVector::BackwardVector })
		{
			FHitResult Wall;
			if (World->LineTraceSingleByChannel(Wall, Chest, Chest + Direction * 1500.0f, ECC_Visibility, Params))
			{
				const FVector Point = Wall.ImpactPoint;
				const FVector Normal = Wall.ImpactNormal;
				Grip(Cases, Pawn, FString::Printf(TEXT("wall %s"), *Direction.ToCompactString()), Point + Normal * 110.0f, Point);
				break;
			}
		}
		// Each door's leaf from both faces, closed; then opened (a key lock excepted) and the same.
		TArray<AMKDoor*> Doors;
		for (TActorIterator<AMKDoor> Door(World); Door; ++Door)
		{
			Doors.Add(*Door);
		}
		const auto DoorGrips = [Pawn](TArray<FCase>& List, AMKDoor* Door, const TCHAR* State)
		{
			for (const float Side : { 1.0f, -1.0f })
			{
				List.Add([Pawn, Door, State, Side](TFunction<void()> Next)
				{
					// The leaf's face: its thinnest horizontal extent (the mesh is not laid along the actor's +X); the hold at
					// the leaf's centre, the body 80 cm off that face (the airlock's doors are 136 cm apart; in gravity it drops to the floor first).
					const FVector Centre = Door->Leaf->Bounds.Origin;
					const FVector Extent = Door->Leaf->Bounds.BoxExtent;
					const FVector Face = (Extent.X < Extent.Y ? FVector::ForwardVector : FVector::RightVector) * Side;
					const FString Label = FString::Printf(TEXT("door %s %s %s"), *Door->GetName(), State, Side > 0.0f ? TEXT("+ face") : TEXT("- face"));
					UE_LOG(LogMindKore, Log, TEXT("Head probe [%s]: leaf at %s, face %s, door passable %s"), *Label, *Centre.ToCompactString(), *Face.ToCompactString(),
						Door->IsPassable() ? TEXT("yes") : TEXT("no"));
					GripRest(Pawn, Label, Centre + Face * 80.0f, Centre, [Next](bool) { Next(); });
				});
			}
		};
		for (AMKDoor* Door : Doors)
		{
			DoorGrips(Cases, Door, TEXT("closed"));
		}
		for (AMKDoor* Door : Doors)
		{
			if (Door->IsKeyLockEngaged())
			{
				continue;
			}
			Cases.Add([Pawn, Door](TFunction<void()> Next)
			{
				Place(Pawn, Door->GetActorLocation() + FVector(0.0f, 0.0f, 3000.0f)); // out of the leaf's arc
				Door->CompleteInteraction(Pawn);
				After(Pawn, 2.0f, [Door, Next]()
				{
					UE_LOG(LogMindKore, Log, TEXT("Head probe: %s opened - passable %s"), *Door->GetName(), Door->IsPassable() ? TEXT("yes") : TEXT("no"));
					Next();
				});
			});
			DoorGrips(Cases, Door, TEXT("open"));
		}
		// The planetoid: standing on its ground at the top and where the ground is steepest of 24 tried; the hold on the ground ahead.
		for (TActorIterator<AJamGravityZone> It(World); It; ++It)
		{
			if (It->SphereRadiusCm <= 0.0f || !It->bRadial)
			{
				continue;
			}
			const FVector Centre = It->GetActorLocation();
			const float Radius = It->SphereRadiusCm;
			struct FSpot
			{
				FVector Point;
				FVector Normal;
				FVector Radial;
				float Angle = -1.0f;
			};
			FSpot Top;
			FSpot Steep;
			for (int32 Index = 0; Index < 25; ++Index)
			{
				// Index 0 straight up; then 24 directions 35 / 60 degrees from up.
				const float Tilt = Index == 0 ? 0.0f : (Index <= 12 ? 35.0f : 60.0f);
				const float Around = 30.0f * float(Index % 12);
				const FVector Radial = FRotator(90.0f - Tilt, Around, 0.0f).Vector();
				FHitResult Ground;
				FCollisionQueryParams Query(SCENE_QUERY_STAT(JamHeadProbe), true, Pawn);
				if (!World->LineTraceSingleByChannel(Ground, Centre + Radial * Radius, Centre, ECC_Visibility, Query))
				{
					continue;
				}
				const FSpot Spot{ Ground.ImpactPoint, Ground.ImpactNormal, Radial,
					FMath::RadiansToDegrees(FMath::Acos(FMath::Clamp(float(FVector::DotProduct(Ground.ImpactNormal, Radial)), -1.0f, 1.0f))) };
				if (Index == 0)
				{
					Top = Spot;
				}
				else if (Spot.Angle < Pawn->MaxGroundAngleDeg - 5.0f && Spot.Angle > Steep.Angle)
				{
					Steep = Spot;
				}
			}
			for (const FSpot* Spot : { &Top, &Steep })
			{
				if (Spot->Angle < 0.0f)
				{
					continue;
				}
				// Standing on the spot; the hold on the ground 120 cm ahead along the slope's fall line (or any tangent at the top).
				FVector Along = FVector::VectorPlaneProject(-Spot->Radial, Spot->Normal).GetSafeNormal();
				if (Along.IsNearlyZero())
				{
					Along = FVector::VectorPlaneProject(FVector::ForwardVector, Spot->Normal).GetSafeNormal();
				}
				const FVector Body = Spot->Point + Spot->Normal * 45.0f;
				const FVector Target = Spot->Point + Along * 120.0f - Spot->Normal * 5.0f;
				Grip(Cases, Pawn, FString::Printf(TEXT("planetoid %s, ground %.0f deg"), Spot == &Top ? TEXT("top") : TEXT("slope"), Spot->Angle), Body, Target);
			}
			break;
		}
	}

	void RunHeadProbe(const TArray<FString>& Args, UWorld* World)
	{
		bShots = Args.Contains(TEXT("shots"));
		const APlayerController* Controller = World->GetFirstPlayerController();
		AJamZeroGPawn* Pawn = Controller ? Cast<AJamZeroGPawn>(Controller->GetPawn()) : nullptr;
		if (!Pawn)
		{
			UE_LOG(LogMindKore, Warning, TEXT("Head probe: no AJamZeroGPawn - FAIL"));
			return;
		}
		if (Pawn->GetGrabbed())
		{
			Pawn->Release();
		}
		Pawn->LetGoCling();
		// The biggest box gravity zone: the cabin on the ship, the room's on the test room.
		const AJamGravityZone* Room = nullptr;
		for (TActorIterator<AJamGravityZone> It(World); It; ++It)
		{
			const FVector Extent = It->Zone->GetScaledBoxExtent();
			if (It->SphereRadiusCm <= 0.0f && (!Room || Extent.X * Extent.Y * Extent.Z > Room->Zone->GetScaledBoxExtent().X * Room->Zone->GetScaledBoxExtent().Y * Room->Zone->GetScaledBoxExtent().Z))
			{
				Room = *It;
			}
		}
		// The test room: where the player starts; the ship: the cabin zone's centre.
		const bool bShip = World->GetMapName().Contains(TEXT("Ship"));
		const FVector Centre = bShip && Room ? Room->GetActorLocation() : Pawn->GetActorLocation() + FVector(0.0f, 0.0f, 60.0f);
		FHitResult Floor;
		FHitResult Ceiling;
		if (!FloorAndCeiling(Pawn, Centre, Floor, Ceiling))
		{
			UE_LOG(LogMindKore, Warning, TEXT("Head probe: no floor or ceiling over %s - FAIL"), *Centre.ToCompactString());
			return;
		}
		const double FloorZ = Floor.ImpactPoint.Z;
		const FVector Spot(Centre.X, Centre.Y, FloorZ + 45.0);
		UE_LOG(LogMindKore, Log, TEXT("Head probe: %s, floor Z %.0f (%s), ceiling Z %.0f (%s), eye radius %.0f, head clearance %.0f, arm %.0f (pull-up to %.0f), frame time %.4f s"),
			*World->GetMapName(), FloorZ, *GetNameSafe(Floor.GetActor()), Ceiling.ImpactPoint.Z, *GetNameSafe(Ceiling.GetActor()), Pawn->EyeRadiusCm, Pawn->HeadClearanceCm,
			Pawn->ArmLengthCm, Pawn->ArmMinCm, World->GetDeltaSeconds());
		const TSharedRef<TArray<FCase>> Cases = MakeShared<TArray<FCase>>();
		const FString Where = bShip ? TEXT("cabin") : TEXT("test room");
		Cases->Add([Pawn, Where, Spot](TFunction<void()> Next) { PullUp(Pawn, Where + TEXT(" pull-up"), Spot, 0.0f, Next); });
		if (!bShots)
		{
			Cases->Add([Pawn, Where, Spot, FloorZ](TFunction<void()> Next) { LowCeiling(Pawn, Where + TEXT(" low ceiling"), Spot, FloorZ, Next); });
			if (bShip)
			{
				AddGrips(*Cases, Pawn, Centre, FloorZ);
			}
		}
		RunCases(Cases, 0, Pawn);
	}

	FAutoConsoleCommandWithWorldAndArgs GHeadProbe(
		TEXT("jam.Head.Probe"),
		TEXT("The head against geometry (review #44) and the hold at rest: on Lvl_Jam_TestRoom / Lvl_Jam_Ship hold on to the ceiling and pull up with Space and W, swing (the camera's clearance every tick), walk under a spawned low slab; on Lvl_Jam_Ship also hold on to a cabin wall, each door's leaf (both faces, closed and open) and the planetoid (flat and a slope) and rest (speed, contacts, shakes, reversals). PASS/FAIL each. \"shots\": the pull-up only, with HighResShot frames. Run at -UseFixedTimeStep -FPS=60."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&RunHeadProbe));
}
