// jam.Move.Probe (Lvl_Jam_Ship): what moves the walking body from outside, and the
// view kick on entering gravity (Docs/Review-2026-10-19.md #24, #26, #37). Each
// check ends in PASS or FAIL and logs the frame time it ran at, so a run under
// -UseFixedTimeStep -FPS=30 and one under -FPS=144 show whether it depends on FPS.

#include "JamPickups.h"
#include "JamShip.h"
#include "JamGravity.h"
#include "JamWeapons.h"
#include "JamZeroGPawn.h"
#include "MindKore.h"

#include "Camera/CameraComponent.h"
#include "Components/BoxComponent.h"
#include "Components/SphereComponent.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/PlayerController.h"
#include "HAL/IConsoleManager.h"
#include "TimerManager.h"

// Named, not anonymous: in a unity build this shares a file with JamShip.cpp's probe helpers.
namespace JamMoveProbe
{
	void After(AJamZeroGPawn* Pawn, float Seconds, TFunction<void()> Step)
	{
		FTimerHandle Handle;
		Pawn->GetWorldTimerManager().SetTimer(Handle, FTimerDelegate::CreateWeakLambda(Pawn, MoveTemp(Step)), FMath::Max(Seconds, 0.001f), false);
	}

	/** Step every frame until it returns true. */
	void EveryFrame(AJamZeroGPawn* Pawn, TFunction<bool()> Step)
	{
		const TSharedRef<FTimerHandle> Handle = MakeShared<FTimerHandle>();
		Pawn->GetWorldTimerManager().SetTimer(*Handle, FTimerDelegate::CreateWeakLambda(Pawn, [Pawn, Handle, Step = MoveTemp(Step)]()
		{
			if (Handle->IsValid() && Step())
			{
				Pawn->GetWorldTimerManager().ClearTimer(*Handle);
			}
		}), 0.0005f, true);
	}

	struct FCabin
	{
		FVector Centre = FVector::ZeroVector;
		FVector Back = FVector::ZeroVector;
		FVector Outside = FVector::ZeroVector;
		double FloorZ = 0.0;
		double CeilingZ = 0.0;
	};

	/** Frames and game time since a mark: the mean frame time a check really ran at. */
	struct FClock
	{
		uint64 Frame = GFrameCounter;
		double Time = 0.0;
		explicit FClock(const UWorld* World) : Time(World->GetTimeSeconds()) {}
		double MeanDt(const UWorld* World) const
		{
			return (World->GetTimeSeconds() - Time) / FMath::Max<double>(1.0, double(GFrameCounter - Frame));
		}
	};

	/** The body at rest at Location, the view at Rotation (null: the view is left alone). */
	void Place(AJamZeroGPawn* Pawn, const FVector& Location, const FRotator* Rotation)
	{
		Pawn->Body->SetWorldLocation(Location, false, nullptr, ETeleportType::TeleportPhysics);
		Pawn->Body->SetPhysicsLinearVelocity(FVector::ZeroVector);
		if (Rotation)
		{
			Pawn->GetController()->SetControlRotation(*Rotation);
		}
	}

	void HoldKeys(AJamZeroGPawn* Pawn, const FVector& Input, float Seconds)
	{
		const double End = Pawn->GetWorld()->GetTimeSeconds() + Seconds;
		EveryFrame(Pawn, [Pawn, Input, End]()
		{
			const bool bOver = Pawn->GetWorld()->GetTimeSeconds() > End;
			Pawn->ProbeThrust(bOver ? FVector::ZeroVector : Input, false);
			return bOver;
		});
	}

	/** The highest the body gets over its start within Seconds, cm; then Done(rise, frame time). */
	void TrackRise(AJamZeroGPawn* Pawn, float Seconds, TFunction<void(float, double)> Done)
	{
		UWorld* World = Pawn->GetWorld();
		const double Start = Pawn->Body->GetComponentLocation().Z;
		const TSharedRef<double> Peak = MakeShared<double>(0.0);
		const TSharedRef<FClock> Clock = MakeShared<FClock>(World);
		const double End = World->GetTimeSeconds() + Seconds;
		EveryFrame(Pawn, [Pawn, World, Start, Peak, Clock, End, Done = MoveTemp(Done)]()
		{
			*Peak = FMath::Max(*Peak, Pawn->Body->GetComponentLocation().Z - Start);
			if (World->GetTimeSeconds() < End)
			{
				return false;
			}
			Done(float(*Peak), Clock->MeanDt(World));
			return true;
		});
	}

	const TCHAR* Verdict(bool bPass)
	{
		return bPass ? TEXT("PASS") : TEXT("FAIL");
	}

	FRotator GravityView(const AJamZeroGPawn* Pawn)
	{
		return JamGravityRelativeRotation(Pawn->GetControlRotation(), Pawn->IsInGravity() ? Pawn->GetGravityDirection() : FVector::DownVector);
	}

	void LetGo(AJamZeroGPawn* Pawn)
	{
		if (Pawn->GetGrabbed())
		{
			Pawn->Release();
		}
		Pawn->LetGoCling();
		if (Pawn->GetGrappled())
		{
			Pawn->ToggleGrapple();
		}
	}

	void Done(AJamZeroGPawn*)
	{
		UE_LOG(LogMindKore, Log, TEXT("Move probe: done"));
	}

	/**
	 * The view kick right after entering gravity from a rolled zero-G view: the
	 * same entry twice, without and with an SV-98 shot on the first frame in
	 * gravity. Once the kick has settled the two views may differ only by the
	 * kick's climb (1 - RecoilSettleFraction of it): pitch up, yaw either way.
	 */
	void CheckViewKick(AJamZeroGPawn* Pawn, const FCabin Cabin)
	{
		Pawn->Guns->SelectWeapon(2);
		Pawn->Guns->FinishSwitch();
		const FRotator Rolled(0.0f, 0.0f, 60.0f);
		const auto Enter = [Pawn, Cabin, Rolled](bool bShoot, TFunction<void(FRotator)> Then)
		{
			Place(Pawn, Cabin.Outside, &Rolled);
			After(Pawn, 0.5f, [Pawn, Cabin, bShoot, Then]()
			{
				UE_LOG(LogMindKore, Log, TEXT("Move probe [view kick, %s]: zero-G %s, view %s"), bShoot ? TEXT("shot") : TEXT("no shot"),
					Pawn->IsInGravity() ? TEXT("NO") : TEXT("yes"), *Pawn->GetControlRotation().ToCompactString());
				Place(Pawn, Cabin.Centre, nullptr);
				EveryFrame(Pawn, [Pawn, bShoot, Then]()
				{
					if (!Pawn->IsInGravity())
					{
						return false;
					}
					const FRotator Before = GravityView(Pawn);
					const bool bFired = bShoot && Pawn->Fire();
					UE_LOG(LogMindKore, Log, TEXT("Move probe [view kick, %s]: in gravity, view %s in gravity space%s"), bShoot ? TEXT("shot") : TEXT("no shot"),
						*Before.ToCompactString(), bShoot ? (bFired ? TEXT(", SV-98 fired") : TEXT(", SV-98 did NOT fire")) : TEXT(""));
					After(Pawn, 2.0f, [Pawn, Then]() { Then(GravityView(Pawn)); });
					return true;
				});
			});
		};
		Enter(false, [Pawn, Enter](FRotator Calm)
		{
			UE_LOG(LogMindKore, Log, TEXT("Move probe [view kick, no shot]: settled view %s"), *Calm.ToCompactString());
			Enter(true, [Pawn, Calm](FRotator Kicked)
			{
				const FJamWeapon* Sniper = Pawn->Guns->GetWeapon();
				const float Climb = Sniper ? 1.0f - Pawn->Guns->RecoilSettleFraction : 0.0f;
				const float Pitch = Sniper ? Sniper->KickPitchDeg * Climb : 0.0f;
				const float Yaw = Sniper ? Sniper->KickYawDeg * Climb : 0.0f;
				const float PitchDrift = float(FRotator::NormalizeAxis(Kicked.Pitch - Calm.Pitch)) - Pitch;
				const float YawDrift = FMath::Abs(float(FRotator::NormalizeAxis(Kicked.Yaw - Calm.Yaw))) - Yaw;
				const bool bPass = FMath::Abs(PitchDrift) < 0.25f && FMath::Abs(YawDrift) < 0.25f && FMath::Abs(Kicked.Roll) < 0.25f;
				UE_LOG(LogMindKore, Log, TEXT("Move probe [view kick]: settled view %s, without the shot %s; the climb should be pitch +%.2f, yaw +-%.2f; drift pitch %.2f yaw %.2f deg, roll %.2f, dt %.4f s - %s"),
					*Kicked.ToCompactString(), *Calm.ToCompactString(), Pitch, Yaw, PitchDrift, YawDrift, Kicked.Roll, Pawn->GetWorld()->GetDeltaSeconds(), Verdict(bPass));
				Done(Pawn);
			});
		});
	}

	/**
	 * Holding on with the feet on the floor: standing in the cabin, the hand on
	 * the ceiling overhead (the arm's rest set to that reach for the check: the
	 * gravity zone stops ~2.5 m short of the hull's walls, so no wall is in
	 * reach while standing in gravity). D across the arm pumps a swing along
	 * the floor (SwingAccelCm, less the floor's friction); the walk braking the
	 * body under the arm at 4000 cm/s^2 kept it near rest.
	 */
	void CheckFloorHang(AJamZeroGPawn* Pawn, const FCabin Cabin)
	{
		const FRotator Up(89.0f, 0.0f, 0.0f);
		Place(Pawn, Cabin.Centre, &Up);
		After(Pawn, 1.5f, [Pawn, Cabin]()
		{
			FHitResult Hold;
			FCollisionQueryParams Params(SCENE_QUERY_STAT(JamMoveProbe), false, Pawn);
			Pawn->GetWorld()->LineTraceSingleByChannel(Hold, Pawn->ShoulderLocation(), Pawn->ShoulderLocation() + Pawn->GetControlRotation().Vector() * Pawn->GrabReachCm,
				ECC_Visibility, Params);
			const float Arm = Pawn->ArmLengthCm;
			Pawn->ArmLengthCm = Hold.bBlockingHit ? float(Hold.Distance) : Arm;
			const bool bStanding = Pawn->IsGrounded();
			const bool bHeld = Hold.bBlockingHit && Pawn->Cling(Hold.GetComponent(), Hold.ImpactPoint);
			UE_LOG(LogMindKore, Log, TEXT("Move probe [floor hang]: %s %s %.0f cm over the eye, %s, %s, mode %s"), bHeld ? TEXT("holding") : TEXT("NOT holding"),
				*GetNameSafe(Hold.GetActor()), Hold.Distance, bStanding ? TEXT("was standing") : TEXT("was NOT standing"),
				Pawn->IsInGravity() ? TEXT("in gravity") : TEXT("NOT in gravity"), *UEnum::GetDisplayValueAsText(Pawn->GetMoveMode()).ToString());
			HoldKeys(Pawn, FVector(0.0f, 1.0f, 0.0f), 0.5f);
			const TSharedRef<FClock> Clock = MakeShared<FClock>(Pawn->GetWorld());
			After(Pawn, 0.5f, [Pawn, Cabin, Clock, Arm, bStanding]()
			{
				const FVector Velocity = Pawn->GetBodyVelocity();
				const float Across = float(FVector(Velocity.X, Velocity.Y, 0.0).Size());
				const float Height = float(Pawn->Body->GetComponentLocation().Z - Cabin.FloorZ);
				const bool bPass = bStanding && Pawn->IsClinging() && Pawn->IsInGravity() && Across > 40.0f;
				UE_LOG(LogMindKore, Log, TEXT("Move probe [floor hang]: D held 0.5 s - along the floor %.0f cm/s (pump %.0f cm/s^2 less friction), arm %.1f cm, body centre %.1f cm over the floor, dt %.4f s - %s"),
					Across, Pawn->SwingAccelCm, Pawn->GetArmLengthCm(), Height, Clock->MeanDt(Pawn->GetWorld()), Verdict(bPass));
				After(Pawn, 1.5f, [Pawn, Cabin, Arm]()
				{
					UE_LOG(LogMindKore, Log, TEXT("Move probe [floor hang]: hands still 1.5 s - %.0f cm/s, arm %.1f cm, body centre %.1f cm over the floor, %s"), Pawn->GetBodyVelocity().Size(),
						Pawn->GetArmLengthCm(), Pawn->Body->GetComponentLocation().Z - Cabin.FloorZ, Pawn->IsClinging() ? TEXT("holding") : TEXT("NOT holding"));
					LetGo(Pawn);
					Pawn->ArmLengthCm = Arm;
					CheckViewKick(Pawn, Cabin);
				});
			});
		});
	}

	/** Forward from the back of the cabin for 0.4 s; the speed then. */
	void WalkSpeed(AJamZeroGPawn* Pawn, const FCabin Cabin, TFunction<void(float, double)> Then)
	{
		HoldKeys(Pawn, FVector(1.0f, 0.0f, 0.0f), 0.4f);
		const TSharedRef<FClock> Clock = MakeShared<FClock>(Pawn->GetWorld());
		After(Pawn, 0.4f, [Pawn, Clock, Then]() { Then(float(FVector(Pawn->GetBodyVelocity().X, Pawn->GetBodyVelocity().Y, 0.0).Size()), Clock->MeanDt(Pawn->GetWorld())); });
	}

	/** Walking empty-handed, then holding an 800 kg body: the load must slow the start. */
	void CheckHeavyWalk(AJamZeroGPawn* Pawn, const FCabin Cabin)
	{
		const FRotator Ahead(0.0f, 0.0f, 0.0f);
		Place(Pawn, Cabin.Back, &Ahead);
		After(Pawn, 1.0f, [Pawn, Cabin, Ahead]()
		{
			WalkSpeed(Pawn, Cabin, [Pawn, Cabin, Ahead](float Empty, double EmptyDt)
			{
				UPrimitiveComponent* Load = nullptr;
				for (TActorIterator<AActor> It(Pawn->GetWorld()); It; ++It)
				{
					UPrimitiveComponent* Root = Cast<UPrimitiveComponent>(It->GetRootComponent());
					if (Root && Root != Pawn->Body && Root->IsSimulatingPhysics() && Root->Bounds.SphereRadius < 60.0f)
					{
						Load = Root;
						break;
					}
				}
				if (!Load)
				{
					UE_LOG(LogMindKore, Warning, TEXT("Move probe [heavy walk]: no small loose body - FAIL"));
					CheckFloorHang(Pawn, Cabin);
					return;
				}
				const float Mass = Load->GetMass();
				const FVector Home = Load->GetComponentLocation();
				Load->SetMassOverrideInKg(NAME_None, 800.0f, true);
				Place(Pawn, Cabin.Back, &Ahead);
				After(Pawn, 0.5f, [Pawn, Cabin, Load, Mass, Home, Empty, EmptyDt]()
				{
					Load->SetWorldLocation(Pawn->ShoulderLocation() + FVector::ForwardVector * Pawn->HoldDistanceCm, false, nullptr, ETeleportType::TeleportPhysics);
					Load->SetPhysicsLinearVelocity(FVector::ZeroVector);
					Load->SetPhysicsAngularVelocityInDegrees(FVector::ZeroVector);
					const bool bHeld = Pawn->Grab(Load, Load->GetComponentLocation());
					After(Pawn, 1.0f, [Pawn, Cabin, Load, Mass, Home, Empty, EmptyDt, bHeld]()
					{
						WalkSpeed(Pawn, Cabin, [Pawn, Cabin, Load, Mass, Home, Empty, EmptyDt, bHeld](float Loaded, double LoadedDt)
						{
							const float Expected = Pawn->WalkAccelCm * Pawn->Body->GetMass() / (Pawn->Body->GetMass() + Load->GetMass()) * 0.4f;
							const bool bPass = bHeld && Pawn->GetGrabbed() == Load && Loaded < 0.6f * Empty;
							UE_LOG(LogMindKore, Log, TEXT("Move probe [heavy walk]: 0.4 s of W - empty %.0f cm/s (dt %.4f s), holding %s %.0f kg %.0f cm/s (dt %.4f s, about %.0f expected) - %s"),
								Empty, EmptyDt, *GetNameSafe(Load->GetOwner()), Load->GetMass(), Loaded, LoadedDt, Expected, Verdict(bPass));
							LetGo(Pawn);
							Load->SetMassOverrideInKg(NAME_None, Mass, true);
							Load->SetWorldLocation(Home, false, nullptr, ETeleportType::TeleportPhysics);
							Load->SetPhysicsLinearVelocity(FVector::ZeroVector);
							CheckFloorHang(Pawn, Cabin);
						});
					});
				});
			});
		});
	}

	/** The grapple hooked into the ceiling ahead and above: its pull beats gravity and lifts the body off the floor. */
	void CheckGrappleLift(AJamZeroGPawn* Pawn, const FCabin Cabin)
	{
		for (TActorIterator<AJamToolPickup> Tool(Pawn->GetWorld()); Tool; ++Tool)
		{
			Tool->CompleteInteraction(Pawn);
		}
		const FRotator Up(75.0f, 0.0f, 0.0f);
		Place(Pawn, Cabin.Centre, &Up);
		After(Pawn, 1.5f, [Pawn, Cabin]()
		{
			Pawn->ToggleGrapple();
			const bool bHooked = Pawn->GetGrappled() != nullptr;
			TrackRise(Pawn, 0.6f, [Pawn, Cabin, bHooked](float Rise, double Dt)
			{
				const bool bPass = bHooked && Rise > 5.0f;
				UE_LOG(LogMindKore, Log, TEXT("Move probe [grapple lift]: hooked %s, rose %.1f cm in 0.6 s, dt %.4f s - %s"), bHooked ? TEXT("yes") : TEXT("NO"), Rise, Dt, Verdict(bPass));
				LetGo(Pawn);
				CheckHeavyWalk(Pawn, Cabin);
			});
		});
	}

	/** A gun fired at the feet: its recoil up (Weapon / 100 kg) beats LaunchThresholdCm and the body leaves the floor. */
	void CheckLaunch(AJamZeroGPawn* Pawn, const FCabin Cabin, int32 Weapon, float MinRiseCm, TFunction<void()> Then)
	{
		Pawn->Guns->SelectWeapon(Weapon);
		Pawn->Guns->FinishSwitch();
		const FRotator Down(-89.0f, 0.0f, 0.0f);
		Place(Pawn, Cabin.Centre, &Down);
		After(Pawn, 1.5f, [Pawn, Weapon, MinRiseCm, Then]()
		{
			const bool bGrounded = Pawn->IsGrounded();
			const bool bFired = Pawn->Fire();
			const FJamWeapon* Gun = Pawn->Guns->GetWeapon();
			const FString Name = Gun ? Gun->Name.ToString() : TEXT("?");
			const float Kick = Gun ? Gun->RecoilImpulse / Pawn->Body->GetMass() : 0.0f;
			TrackRise(Pawn, 1.2f, [Name, Kick, bFired, bGrounded, MinRiseCm, Then](float Rise, double Dt)
			{
				const bool bPass = bFired && bGrounded && Rise > MinRiseCm;
				UE_LOG(LogMindKore, Log, TEXT("Move probe [launch %s]: %s, %s, recoil %.0f cm/s up, rose %.1f cm (over %.0f needed), dt %.4f s - %s"), *Name,
					bGrounded ? TEXT("standing") : TEXT("NOT standing"), bFired ? TEXT("fired at the feet") : TEXT("did NOT fire"), Kick, Rise, MinRiseCm, Dt, Verdict(bPass));
				Then();
			});
		});
	}

	void RunMoveProbe(UWorld* World)
	{
		const APlayerController* Controller = World->GetFirstPlayerController();
		AJamZeroGPawn* Pawn = Controller ? Cast<AJamZeroGPawn>(Controller->GetPawn()) : nullptr;
		// The cabin is the biggest box zone (as jam.Ship.Probe picks it); outside is 2 m past the lowest X of them all.
		const AJamGravityZone* CabinZone = nullptr;
		double MinX = TNumericLimits<double>::Max();
		for (TActorIterator<AJamGravityZone> It(World); It; ++It)
		{
			if (It->SphereRadiusCm > 0.0f)
			{
				continue;
			}
			const FVector Extent = It->Zone->GetScaledBoxExtent();
			MinX = FMath::Min(MinX, It->GetActorLocation().X - Extent.X);
			if (!CabinZone || Extent.X * Extent.Y * Extent.Z > CabinZone->Zone->GetScaledBoxExtent().X * CabinZone->Zone->GetScaledBoxExtent().Y * CabinZone->Zone->GetScaledBoxExtent().Z)
			{
				CabinZone = *It;
			}
		}
		if (!Pawn || !CabinZone)
		{
			UE_LOG(LogMindKore, Warning, TEXT("Move probe: needs an AJamZeroGPawn and a box gravity zone (Lvl_Jam_Ship) - FAIL"));
			return;
		}
		FCabin Cabin;
		const FVector Centre = CabinZone->GetActorLocation();
		FCollisionQueryParams Params(SCENE_QUERY_STAT(JamMoveProbe), false, Pawn);
		FHitResult Up;
		FHitResult Down;
		if (!World->LineTraceSingleByChannel(Up, Centre, Centre + FVector(0.0f, 0.0f, 1000.0f), ECC_Visibility, Params)
			|| !World->LineTraceSingleByChannel(Down, Centre, Centre - FVector(0.0f, 0.0f, 1000.0f), ECC_Visibility, Params))
		{
			UE_LOG(LogMindKore, Warning, TEXT("Move probe: no cabin floor or ceiling over %s - FAIL"), *Centre.ToCompactString());
			return;
		}
		Cabin.FloorZ = Down.ImpactPoint.Z;
		Cabin.CeilingZ = Up.ImpactPoint.Z;
		Cabin.Centre = FVector(Centre.X, Centre.Y, Cabin.FloorZ + 45.0);
		Cabin.Back = FVector(Centre.X - CabinZone->Zone->GetScaledBoxExtent().X + 60.0, Centre.Y, Cabin.FloorZ + 45.0);
		Cabin.Outside = FVector(MinX - 200.0, Centre.Y, Centre.Z);
		UE_LOG(LogMindKore, Log, TEXT("Move probe: %s floor Z %.0f, ceiling Z %.0f, frame time %.4f s (max FPS %s)"), *CabinZone->GetName(), Cabin.FloorZ,
			Cabin.CeilingZ, World->GetDeltaSeconds(), *IConsoleManager::Get().FindConsoleVariable(TEXT("t.MaxFPS"))->GetString());
		LetGo(Pawn);
		Pawn->Guns->UnlockWeapon(1);
		Pawn->Guns->UnlockWeapon(2);
		CheckLaunch(Pawn, Cabin, 1, 15.0f, [Pawn, Cabin]()
		{
			CheckLaunch(Pawn, Cabin, 2, 30.0f, [Pawn, Cabin]() { CheckGrappleLift(Pawn, Cabin); });
		});
	}

	FAutoConsoleCommandWithWorld GMoveProbe(
		TEXT("jam.Move.Probe"),
		TEXT("Lvl_Jam_Ship: the shotgun and the SV-98 fired at the feet lift the walker, the grapple lifts it off the floor, an 800 kg body held slows the walk, holding a wall with the feet on the floor swings freely, and a view kick right after entering gravity from a rolled zero-G view does not drift (PASS/FAIL each, with the frame time)."),
		FConsoleCommandWithWorldDelegate::CreateStatic(&RunMoveProbe));
}
