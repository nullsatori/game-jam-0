// jam.Debug.Probe: the debug menu's checks (Docs/DebugMenu.md, Docs/Probes.md). Headless (NullRHI)
// on Lvl_Jam_Ship (everything) and Lvl_Jam_ZeroG (no gravity zones: that part says SKIP).
// Every check is one line "Debug probe [<name>]: PASS|FAIL ..."; the last line is "Debug probe: done".

#include "Components/BoxComponent.h"
#include "Components/SphereComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/WorldSettings.h"
#include "HAL/IConsoleManager.h"
#include "JamDebug.h"
#include "JamFlyer.h"
#include "JamGravity.h"
#include "JamOxygen.h"
#include "JamShip.h"
#include "JamWeapons.h"
#include "JamZeroGPawn.h"
#include "MKPhysicsProp.h"
#include "Misc/App.h"
#include "Misc/CommandLine.h"
#include "MindKore.h"
#include "TimerManager.h"

namespace
{
	struct FDebugProbeState
	{
		int32 Checks = 0;
		int32 Failed = 0;
		TMap<FString, FString> Defaults;
		TWeakObjectPtr<AJamGravityZone> Zone;
		TMap<float, FVector2D> Gravity; // scale -> (pawn, box) measured acceleration along the zone's down
		FVector PawnStart = FVector::ZeroVector;
	};
	TSharedPtr<FDebugProbeState> GDebugProbe;

	void DebugProbeCheck(const FString& Name, bool bPass, const FString& Details = FString())
	{
		++GDebugProbe->Checks;
		GDebugProbe->Failed += bPass ? 0 : 1;
		UE_LOG(LogMindKore, Log, TEXT("Debug probe [%s]: %s%s%s"), *Name, bPass ? TEXT("PASS") : TEXT("FAIL"), Details.IsEmpty() ? TEXT("") : TEXT(" - "), *Details);
	}

	void DebugProbeAfter(UWorld* World, float Seconds, TFunction<void()> Step)
	{
		FTimerHandle Handle;
		World->GetTimerManager().SetTimer(Handle, FTimerDelegate::CreateWeakLambda(World, MoveTemp(Step)), Seconds, false);
	}

	AJamZeroGPawn* DebugProbePawn(UWorld* World)
	{
		for (TActorIterator<AJamZeroGPawn> It(World); It; ++It)
		{
			return *It;
		}
		return nullptr;
	}

	FString DebugProbeGet(UJamDebugSubsystem* Debug, const FString& Key)
	{
		FString Value;
		return Debug->GetValue(Key, Value) ? Value : FString(TEXT("<none>"));
	}

	/** Every editable property of the class defaults the menu can reach (the pawn, its parts, the gun list, flyers, zones, habitats). */
	TMap<FString, FString> DebugProbeDefaults()
	{
		TMap<FString, FString> Out;
		TArray<UObject*> Objects = { GetMutableDefault<AJamZeroGPawn>(), GetMutableDefault<AJamFlyer>(), GetMutableDefault<AJamGravityZone>(), GetMutableDefault<AJamFlyerHabitat>() };
		TArray<UObject*> Parts;
		GetMutableDefault<AJamZeroGPawn>()->GetDefaultSubobjects(Parts);
		Objects.Append(Parts);
		for (UObject* Object : Objects)
		{
			for (TFieldIterator<FProperty> It(Object->GetClass()); It; ++It)
			{
				if (It->HasAnyPropertyFlags(CPF_Edit))
				{
					FString Text;
					It->ExportTextItem_InContainer(Text, Object, nullptr, Object, PPF_None);
					Out.Add(Object->GetPathName() + TEXT(".") + It->GetName(), Text);
				}
			}
		}
		return Out;
	}

	void DebugProbeCompareDefaults(const FString& Name)
	{
		const TMap<FString, FString> Now = DebugProbeDefaults();
		TArray<FString> Changed;
		for (const TPair<FString, FString>& Before : GDebugProbe->Defaults)
		{
			const FString* After = Now.Find(Before.Key);
			if (!After || *After != Before.Value)
			{
				Changed.Add(Before.Key);
			}
		}
		DebugProbeCheck(Name, Changed.IsEmpty(), FString::Printf(TEXT("%d default properties compared, %d changed%s%s"), GDebugProbe->Defaults.Num(), Changed.Num(),
			Changed.IsEmpty() ? TEXT("") : TEXT(": "), *FString::Join(Changed, TEXT(", "))));
	}

	void DebugProbeFinish(UWorld* World)
	{
		UJamDebugSubsystem* Debug = UJamDebugSubsystem::Get(World);
		Debug->Reset(TEXT("all"));
		DebugProbeCheck(TEXT("reset all at the end"), Debug->GetChangedCount() == 0, FString::Printf(TEXT("%d changed"), Debug->GetChangedCount()));
		DebugProbeCompareDefaults(TEXT("class defaults untouched at the end"));
		UE_LOG(LogMindKore, Log, TEXT("Debug probe: %d checks, %d failed"), GDebugProbe->Checks, GDebugProbe->Failed);
		UE_LOG(LogMindKore, Log, TEXT("Debug probe: done"));
		GDebugProbe.Reset();
	}

	/** The frames whose end fell in the last Seconds: the game's dt and the real frame time, min and max. */
	void DebugProbeFrames(UJamDebugSubsystem* Debug, double Since, int32& OutCount, float& OutMinDt, float& OutMaxDt, float& OutMinReal, float& OutMeanReal)
	{
		OutCount = 0;
		OutMinDt = OutMinReal = 1e9f;
		OutMaxDt = OutMeanReal = 0.0f;
		for (const UJamDebugSubsystem::FFrameSample& Frame : Debug->GetFrames())
		{
			if (Frame.At > Since)
			{
				++OutCount;
				OutMinDt = FMath::Min(OutMinDt, Frame.DeltaSeconds);
				OutMaxDt = FMath::Max(OutMaxDt, Frame.DeltaSeconds);
				OutMinReal = FMath::Min(OutMinReal, Frame.RealSeconds);
				OutMeanReal += Frame.RealSeconds;
			}
		}
		OutMeanReal = OutCount > 0 ? OutMeanReal / OutCount : 0.0f;
	}

	/** The FPS cap: 30 must hold every frame's dt at >= 1/30 s (0.5 ms for the wait's grain). */
	void DebugProbeFpsLimit(UWorld* World)
	{
		UJamDebugSubsystem* Debug = UJamDebugSubsystem::Get(World);
		Debug->SetValue(TEXT("System.FpsLimit"), TEXT("30"));
		DebugProbeAfter(World, 0.5f, [World]()
		{
			const double Since = FPlatformTime::Seconds();
			DebugProbeAfter(World, 1.0f, [World, Since]()
			{
				UJamDebugSubsystem* Debug = UJamDebugSubsystem::Get(World);
				int32 Count = 0;
				float MinDt = 0.0f, MaxDt = 0.0f, MinReal = 0.0f, MeanReal = 0.0f;
				DebugProbeFrames(Debug, Since, Count, MinDt, MaxDt, MinReal, MeanReal);
				const IConsoleVariable* MaxFps = IConsoleManager::Get().FindConsoleVariable(TEXT("t.MaxFPS"));
				// The cap holds the engine's dt (wall clock from one frame's start to the next, where the limiter
				// waits); the interval between frame ends also carries the frame's own varying work, so it is only logged.
				DebugProbeCheck(TEXT("fps limit 30: dt >= 1/30"), Count > 5 && MinDt >= 1.0f / 30.0f - 0.0005f,
					FString::Printf(TEXT("t.MaxFPS %g, %d frames in 1 s, game dt %.2f..%.2f ms, real frame min %.2f mean %.2f ms (%.1f FPS)"),
						MaxFps ? MaxFps->GetFloat() : -1.0f, Count, MinDt * 1000.0f, MaxDt * 1000.0f, MinReal * 1000.0f, MeanReal * 1000.0f, MeanReal > 0.0f ? 1.0f / MeanReal : 0.0f));
				Debug->Reset(TEXT("System.FpsLimit"));
				DebugProbeAfter(World, 0.3f, [World]() { DebugProbeFinish(World); });
			});
		});
	}

	/** The fixed step: 60 Hz gives every frame dt 1/60 exactly, then off again. */
	void DebugProbeFixedStep(UWorld* World)
	{
		UJamDebugSubsystem* Debug = UJamDebugSubsystem::Get(World);
		if (FParse::Param(FCommandLine::Get(), TEXT("UseFixedTimeStep")))
		{
			// A command-line fixed step also ignores t.MaxFPS (the engine does not wait), so the cap cannot be checked either.
			UE_LOG(LogMindKore, Log, TEXT("Debug probe [fixed step]: SKIP - the command line already fixes the step (run without -UseFixedTimeStep)"));
			UE_LOG(LogMindKore, Log, TEXT("Debug probe [fps limit]: SKIP - a command-line fixed step ignores t.MaxFPS (run without -UseFixedTimeStep)"));
			DebugProbeFinish(World);
			return;
		}
		Debug->SetValue(TEXT("System.FixedStep"), TEXT("60"));
		DebugProbeAfter(World, 0.3f, [World]()
		{
			const double Since = FPlatformTime::Seconds();
			DebugProbeAfter(World, 0.5f, [World, Since]()
			{
				UJamDebugSubsystem* Debug = UJamDebugSubsystem::Get(World);
				int32 Count = 0;
				float MinDt = 0.0f, MaxDt = 0.0f, MinReal = 0.0f, MeanReal = 0.0f;
				DebugProbeFrames(Debug, Since, Count, MinDt, MaxDt, MinReal, MeanReal);
				DebugProbeCheck(TEXT("fixed step 60: constant dt"), Count > 5 && FMath::Abs(MinDt - 1.0f / 60.0f) < 1e-5f && FMath::Abs(MaxDt - 1.0f / 60.0f) < 1e-5f,
					FString::Printf(TEXT("%d frames, dt %.4f..%.4f ms, real frame mean %.2f ms (paced to real time)"), Count, MinDt * 1000.0f, MaxDt * 1000.0f, MeanReal * 1000.0f));
				Debug->SetValue(TEXT("System.FixedStep"), TEXT("0"));
				DebugProbeCheck(TEXT("fixed step off"), !FApp::UseFixedTimeStep());
				DebugProbeAfter(World, 0.3f, [World]() { DebugProbeFpsLimit(World); });
			});
		});
	}

	/** One scale: pawn and a 10 kg box at rest in the probe's zone, their velocity change over 0.3 s. */
	void DebugProbeGravityAt(UWorld* World, const TArray<float> Scales, int32 Index)
	{
		UJamDebugSubsystem* Debug = UJamDebugSubsystem::Get(World);
		AJamZeroGPawn* Pawn = DebugProbePawn(World);
		AJamGravityZone* Zone = GDebugProbe->Zone.Get();
		if (!Scales.IsValidIndex(Index) || !Zone || !Pawn)
		{
			const FVector2D* One = GDebugProbe->Gravity.Find(1.0f);
			const FVector2D* Half = GDebugProbe->Gravity.Find(0.5f);
			const FVector2D* None = GDebugProbe->Gravity.Find(0.0f);
			DebugProbeCheck(TEXT("gravity measured at x1, x0.5, x0"), One && Half && None);
			if (One && Half && None)
			{
				const double Expected = Zone ? Zone->GravityCm : 980.0;
				DebugProbeCheck(TEXT("gravity x1: pawn and body fall at the zone's gravity"), FMath::Abs(One->X - Expected) < 0.03 * Expected && FMath::Abs(One->Y - Expected) < 0.03 * Expected,
					FString::Printf(TEXT("pawn %.1f, body %.1f cm/s^2, zone %.0f"), One->X, One->Y, Expected));
				DebugProbeCheck(TEXT("gravity x0.5: half"), FMath::Abs(Half->X / One->X - 0.5) < 0.03 && FMath::Abs(Half->Y / One->Y - 0.5) < 0.03,
					FString::Printf(TEXT("pawn %.1f (%.3f of x1), body %.1f (%.3f of x1) cm/s^2"), Half->X, Half->X / One->X, Half->Y, Half->Y / One->Y));
				DebugProbeCheck(TEXT("gravity x0: none"), FMath::Abs(None->X) < 5.0 && FMath::Abs(None->Y) < 5.0,
					FString::Printf(TEXT("pawn %.2f, body %.2f cm/s^2"), None->X, None->Y));
			}
			Debug->Reset(TEXT("World.GravityScale"));
			if (Zone)
			{
				Zone->Destroy();
			}
			if (Pawn)
			{
				Pawn->Body->SetWorldLocation(GDebugProbe->PawnStart, false, nullptr, ETeleportType::TeleportPhysics);
				Pawn->Body->SetPhysicsLinearVelocity(FVector::ZeroVector);
			}
			DebugProbeAfter(World, 0.5f, [World]() { DebugProbeFixedStep(World); });
			return;
		}
		const float Scale = Scales[Index];
		Debug->SetValue(TEXT("World.GravityScale"), FString::SanitizeFloat(Scale));
		const FVector Centre = Zone->GetActorLocation();
		Pawn->Body->SetWorldLocation(Centre, false, nullptr, ETeleportType::TeleportPhysics);
		Pawn->Body->SetPhysicsLinearVelocity(FVector::ZeroVector);
		const FTransform BoxAt(FRotator::ZeroRotator, Centre + FVector(400.0f, 0.0f, 0.0f), FVector(0.3f));
		AMKPhysicsProp* Box = World->SpawnActorDeferred<AMKPhysicsProp>(AMKPhysicsProp::StaticClass(), BoxAt, nullptr, nullptr, ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
		Box->Body->SetStaticMesh(LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube")));
		Box->MassKg = 10.0f;
		Box->FinishSpawning(BoxAt);
		const TWeakObjectPtr<AMKPhysicsProp> WeakBox = Box;
		DebugProbeAfter(World, 0.15f, [World, Scales, Index, Scale, WeakBox]()
		{
			AJamZeroGPawn* Pawn = DebugProbePawn(World);
			AMKPhysicsProp* Box = WeakBox.Get();
			if (!Pawn || !Box)
			{
				DebugProbeCheck(TEXT("gravity: stand"), false, TEXT("pawn or box gone"));
				DebugProbeGravityAt(World, {}, 0);
				return;
			}
			const FVector PawnV = Pawn->GetBodyVelocity();
			const FVector BoxV = Box->Body->GetPhysicsLinearVelocity();
			const double Start = World->GetTimeSeconds();
			DebugProbeAfter(World, 0.3f, [World, Scales, Index, Scale, WeakBox, PawnV, BoxV, Start]()
			{
				AJamZeroGPawn* Pawn = DebugProbePawn(World);
				AMKPhysicsProp* Box = WeakBox.Get();
				const double Seconds = World->GetTimeSeconds() - Start;
				const FVector Down = FVector::DownVector;
				const double PawnA = Pawn ? FVector::DotProduct(Pawn->GetBodyVelocity() - PawnV, Down) / Seconds : 0.0;
				const double BoxA = Box ? FVector::DotProduct(Box->Body->GetPhysicsLinearVelocity() - BoxV, Down) / Seconds : 0.0;
				UE_LOG(LogMindKore, Log, TEXT("Debug probe [gravity x%.2f]: pawn %.1f cm/s^2 (mode %s), body %.1f cm/s^2 over %.3f s; JamGravityAt %s"), Scale, PawnA,
					Pawn ? *UEnum::GetValueAsString(Pawn->GetMoveMode()) : TEXT("-"), BoxA, Seconds,
					Pawn ? *JamGravityAt(World, Pawn->GetActorLocation()).Acceleration.ToCompactString() : TEXT("-"));
				GDebugProbe->Gravity.Add(Scale, FVector2D(PawnA, BoxA));
				if (Box)
				{
					Box->Destroy();
				}
				DebugProbeGravityAt(World, Scales, Index + 1);
			});
		});
	}

	/** The global multiplier: a probe zone 2 km over the map, so nothing but its gravity acts. */
	void DebugProbeGravity(UWorld* World)
	{
		AJamZeroGPawn* Pawn = DebugProbePawn(World);
		if (!World->GetAuthGameMode<AJamShipGameMode>() || !Pawn)
		{
			UE_LOG(LogMindKore, Log, TEXT("Debug probe [gravity]: SKIP - needs AJamShipGameMode (Lvl_Jam_Ship): it feeds the pawn and the bodies"));
			DebugProbeFixedStep(World);
			return;
		}
		GDebugProbe->PawnStart = Pawn->GetActorLocation();
		FActorSpawnParameters Params;
		Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		AJamGravityZone* Zone = World->SpawnActor<AJamGravityZone>(GDebugProbe->PawnStart + FVector(0.0f, 0.0f, 200000.0f), FRotator::ZeroRotator, Params);
		Zone->Zone->SetBoxExtent(FVector(3000.0f));
		Zone->GravityCm = 980.0f;
		Zone->Priority = 1000;
		GDebugProbe->Zone = Zone;
		DebugProbeGravityAt(World, { 1.0f, 0.5f, 0.0f }, 0);
	}

	void RunDebugProbe(UWorld* World)
	{
		UJamDebugSubsystem* Debug = UJamDebugSubsystem::Get(World);
		AJamZeroGPawn* Pawn = DebugProbePawn(World);
		GDebugProbe = MakeShared<FDebugProbeState>();
		if (!Debug || !Pawn)
		{
			DebugProbeCheck(TEXT("setup"), false, FString::Printf(TEXT("subsystem %s, pawn %s"), Debug ? TEXT("yes") : TEXT("no"), Pawn ? TEXT("yes") : TEXT("no")));
			UE_LOG(LogMindKore, Log, TEXT("Debug probe: done"));
			return;
		}
		UE_LOG(LogMindKore, Log, TEXT("Debug probe: %s, build %s, fixed step %s"), *World->GetMapName(), *UJamDebugSubsystem::GetBuildHash(),
			FApp::UseFixedTimeStep() ? TEXT("on") : TEXT("off"));
		Debug->Reset(TEXT("all"));
		GDebugProbe->Defaults = DebugProbeDefaults();

		// Every default pin resolves to a parameter, and to a live owner (flyers may be none yet).
		for (const FString& Pin : UJamDebugSubsystem::GetDefaultPins())
		{
			const int32 Live = Debug->GetLiveCount(Pin);
			DebugProbeCheck(TEXT("pin ") + Pin, Live > 0 || (Live == 0 && Pin.StartsWith(TEXT("Flyer."))),
				FString::Printf(TEXT("= %s, live owners %d"), *DebugProbeGet(Debug, Pin), Live));
		}
		TArray<FJamDebugParam> Params;
		Debug->ListParams(Params);
		UE_LOG(LogMindKore, Log, TEXT("Debug probe: %d parameters listed"), Params.Num());

		// Set, get, reset, undo, redo on one pawn field, checked on the pawn itself.
		const FString Arm = TEXT("Pawn.ArmLengthCm");
		const float ArmOriginal = Pawn->ArmLengthCm;
		const FString ArmText = DebugProbeGet(Debug, Arm);
		Debug->SetValue(Arm, TEXT("123.5"));
		DebugProbeCheck(TEXT("set"), Pawn->ArmLengthCm == 123.5f && DebugProbeGet(Debug, Arm) == TEXT("123.500000") && Debug->IsChanged(Arm),
			FString::Printf(TEXT("pawn %.2f, get %s, changed %d"), Pawn->ArmLengthCm, *DebugProbeGet(Debug, Arm), Debug->GetChangedCount()));
		Debug->Reset(Arm);
		DebugProbeCheck(TEXT("reset"), Pawn->ArmLengthCm == ArmOriginal && DebugProbeGet(Debug, Arm) == ArmText && !Debug->IsChanged(Arm),
			FString::Printf(TEXT("pawn %.2f, original %s"), Pawn->ArmLengthCm, *ArmText));
		Debug->SetValue(Arm, TEXT("123.5"));
		Debug->Undo();
		DebugProbeCheck(TEXT("undo"), Pawn->ArmLengthCm == ArmOriginal && !Debug->IsChanged(Arm), FString::Printf(TEXT("pawn %.2f"), Pawn->ArmLengthCm));
		Debug->Redo();
		DebugProbeCheck(TEXT("redo"), Pawn->ArmLengthCm == 123.5f && Debug->IsChanged(Arm), FString::Printf(TEXT("pawn %.2f"), Pawn->ArmLengthCm));
		Debug->Undo();
		Debug->Undo(); // the reset step: back to 123.5
		Debug->Undo(); // the first set: the original
		DebugProbeCheck(TEXT("undo through reset"), Pawn->ArmLengthCm == ArmOriginal && Debug->GetChangedCount() == 0, FString::Printf(TEXT("pawn %.2f"), Pawn->ArmLengthCm));
		FString Error;
		DebugProbeCheck(TEXT("type mismatch refused"), !Debug->SetValue(Arm, TEXT("abc"), &Error) && Pawn->ArmLengthCm == ArmOriginal, Error);

		// A gun of the list, a component, flyers (live and newborn), a zone, the system.
		Debug->SetValue(TEXT("Weapon[Пистолет].RecoilImpulse"), TEXT("5000"));
		const FJamWeapon* Pistol = Pawn->Guns->Weapons.FindByPredicate([](const FJamWeapon& Weapon) { return Weapon.Name.ToString() == TEXT("Пистолет"); });
		DebugProbeCheck(TEXT("weapon element"), Pistol && Pistol->RecoilImpulse == 5000.0f, FString::Printf(TEXT("pistol recoil %.0f"), Pistol ? Pistol->RecoilImpulse : -1.0f));
		Debug->SetValue(TEXT("Pawn.Oxygen.CapacitySeconds"), TEXT("99"));
		DebugProbeCheck(TEXT("component"), Pawn->Oxygen->CapacitySeconds == 99.0f, FString::Printf(TEXT("oxygen capacity %.0f"), Pawn->Oxygen->CapacitySeconds));
		Debug->SetValue(TEXT("Flyer.BiteDamage"), TEXT("77"));
		int32 Flyers = 0, FlyersSet = 0;
		for (TActorIterator<AJamFlyer> It(World); It; ++It)
		{
			++Flyers;
			FlyersSet += It->BiteDamage == 77.0f ? 1 : 0;
		}
		DebugProbeCheck(TEXT("live flyers"), FlyersSet == Flyers, FString::Printf(TEXT("%d of %d"), FlyersSet, Flyers));
		FActorSpawnParameters SpawnParams;
		SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		AJamFlyer* Newborn = World->SpawnActor<AJamFlyer>(Pawn->GetActorLocation() + FVector(0.0f, 0.0f, 100000.0f), FRotator::ZeroRotator, SpawnParams);
		DebugProbeCheck(TEXT("newborn flyer"), Newborn && Newborn->BiteDamage == 77.0f, FString::Printf(TEXT("bite %.0f"), Newborn ? Newborn->BiteDamage : -1.0f));
		if (Newborn)
		{
			Newborn->Destroy();
		}
		AJamGravityZone* MapZone = nullptr;
		for (TActorIterator<AJamGravityZone> It(World); It && !MapZone; ++It)
		{
			MapZone = *It;
		}
		if (MapZone)
		{
			const FString Key = FString::Printf(TEXT("Zone[%s].GravityCm"), *MapZone->GetActorNameOrLabel());
			Debug->SetValue(Key, TEXT("321"));
			DebugProbeCheck(TEXT("map zone"), MapZone->GravityCm == 321.0f, Key);
		}
		Debug->SetValue(TEXT("System.TimeScale"), TEXT("0.5"));
		DebugProbeCheck(TEXT("time scale"), FMath::IsNearlyEqual(World->GetWorldSettings()->DemoPlayTimeDilation, 0.5f),
			FString::Printf(TEXT("effective dilation %.2f"), World->GetWorldSettings()->GetEffectiveTimeDilation()));
		Debug->SetValue(TEXT("World.GravityScale"), TEXT("0.35"));
		DebugProbeCheck(TEXT("gravity scale"), FMath::IsNearlyEqual(JamGetGravityScale(), 0.35f), FString::Printf(TEXT("JamGetGravityScale %.2f"), JamGetGravityScale()));

		// Export, reset all, import: the same values.
		TMap<FString, FString> Before = Debug->GetChanged();
		const FString File = Debug->ExportToFile(TEXT("debug-probe"));
		Debug->Reset(TEXT("all"));
		bool bOriginals = Debug->GetChangedCount() == 0 && Pawn->Oxygen->CapacitySeconds != 99.0f && JamGetGravityScale() == 1.0f;
		DebugProbeCheck(TEXT("reset all"), bOriginals, FString::Printf(TEXT("%d changed, oxygen %.0f, gravity scale %.2f"), Debug->GetChangedCount(),
			Pawn->Oxygen->CapacitySeconds, JamGetGravityScale()));
		const FJamDebugImport Imported = Debug->Import(File);
		TArray<FString> Different;
		for (const TPair<FString, FString>& Value : Before)
		{
			if (DebugProbeGet(Debug, Value.Key) != Value.Value)
			{
				Different.Add(FString::Printf(TEXT("%s %s != %s"), *Value.Key, *DebugProbeGet(Debug, Value.Key), *Value.Value));
			}
		}
		DebugProbeCheck(TEXT("export, reset, import: identical"), Imported.bOk && Imported.Skipped.IsEmpty() && Different.IsEmpty() && Debug->GetChangedCount() == Before.Num(),
			FString::Printf(TEXT("%d keys, %d applied, %d skipped, %d differ%s%s; %s"), Before.Num(), Imported.Applied, Imported.Skipped.Num(), Different.Num(),
				Different.IsEmpty() ? TEXT("") : TEXT(": "), *FString::Join(Different, TEXT("; ")), *File));
		DebugProbeCheck(TEXT("imported values live"), Pawn->Oxygen->CapacitySeconds == 99.0f && FMath::IsNearlyEqual(JamGetGravityScale(), 0.35f));
		Debug->Undo();
		DebugProbeCheck(TEXT("undo the import"), Debug->GetChangedCount() == 0, FString::Printf(TEXT("%d changed"), Debug->GetChangedCount()));

		// Unknown keys and values of the wrong type are skipped and listed; the rest applies; another build warns.
		const FJamDebugImport Mixed = Debug->ImportJson(TEXT("{\"format\":1,\"name\":\"probe-mixed\",\"build\":\"0000000\",\"system\":{\"noSuchSetting\":1},")
			TEXT("\"params\":{\"Pawn.NoSuchThing\":\"1\",\"Pawn.ArmLengthCm\":\"abc\",\"Weapon[Нет].Damage\":\"1\",\"Pawn.WalkSpeedCm\":\"500\"}}"));
		const auto Listed = [&Mixed](const TCHAR* Key) { return Mixed.Skipped.ContainsByPredicate([Key](const FString& Line) { return Line.StartsWith(Key); }); };
		DebugProbeCheck(TEXT("unknown keys skipped and listed"), Mixed.bOk && Mixed.Applied == 1 && Listed(TEXT("Pawn.NoSuchThing")) && Listed(TEXT("Pawn.ArmLengthCm"))
			&& Listed(TEXT("Weapon[Нет].Damage")) && Listed(TEXT("System.NoSuchSetting")) && Pawn->WalkSpeedCm == 500.0f && Pawn->ArmLengthCm == ArmOriginal,
			FString::Printf(TEXT("applied %d, skipped: %s"), Mixed.Applied, *FString::Join(Mixed.Skipped, TEXT(" | "))));
		DebugProbeCheck(TEXT("another build warns, not refuses"), !Mixed.Warning.IsEmpty() && Mixed.bOk, Mixed.Warning);

		// Built-in presets: listed, and one applies.
		TArray<FJamDebugPreset> Presets;
		Debug->ListPresets(Presets);
		int32 Assets = 0;
		for (const FJamDebugPreset& Preset : Presets)
		{
			Assets += Preset.bAsset ? 1 : 0;
			UE_LOG(LogMindKore, Log, TEXT("Debug probe: preset %s (%s)"), *Preset.Name, *Preset.Source);
		}
		DebugProbeCheck(TEXT("built-in presets listed"), Assets >= 4, FString::Printf(TEXT("%d assets, %d in all"), Assets, Presets.Num()));
		const FJamDebugImport Preset = Debug->Import(TEXT("Гравитация ×0.35"));
		DebugProbeCheck(TEXT("preset applies"), Preset.bOk && FMath::IsNearlyEqual(JamGetGravityScale(), 0.35f) && Debug->GetChangedCount() == 1,
			FString::Printf(TEXT("gravity scale %.2f, changed %d, %s"), JamGetGravityScale(), Debug->GetChangedCount(), *Preset.Warning));
		Debug->Reset(TEXT("all"));
		DebugProbeCompareDefaults(TEXT("class defaults untouched after edits and reset"));

		DebugProbeAfter(World, 0.2f, [World]() { DebugProbeGravity(World); });
	}

	FAutoConsoleCommandWithWorld GJamDebugProbe(TEXT("jam.Debug.Probe"),
		TEXT("Debug menu checks (headless, without -UseFixedTimeStep): set/get/reset/undo, export-reset-import, skipped keys, default pins, class defaults untouched, ")
		TEXT("the gravity multiplier on the pawn and a body (Lvl_Jam_Ship), the fixed step, the FPS cap. Ends with 'Debug probe: done'."),
		FConsoleCommandWithWorldDelegate::CreateStatic(&RunDebugProbe));

	/**
	 * jam.Debug.Shots (needs a real RHI): the menu as the player sees it, "Shot showui" (Slate included) of
	 * the System tab, the parameter list with a changed and a pinned row, an import's skipped list, and
	 * the pinned overlay over the game; the watermark is in every frame from the second on.
	 */
	void RunDebugShots(UWorld* World)
	{
		UJamDebugSubsystem* Debug = UJamDebugSubsystem::Get(World);
		if (!Debug)
		{
			return;
		}
		const auto Console = [World](const TCHAR* Command) { GEngine->Exec(World, Command); };
		const auto Shot = [World](const TCHAR* Label)
		{
			UE_LOG(LogMindKore, Log, TEXT("Debug shots: %s"), Label);
			World->GetFirstPlayerController()->ConsoleCommand(TEXT("Shot showui"));
		};
		Debug->Reset(TEXT("all"));
		Console(TEXT("DisableAllScreenMessages")); // the engine's on-screen warnings cover the panel
		Console(TEXT("jam.Debug.Menu Система"));
		DebugProbeAfter(World, 2.0f, [Shot]() { Shot(TEXT("system tab")); });
		DebugProbeAfter(World, 3.0f, [Console]()
		{
			Console(TEXT("jam.Debug.Set Pawn.ArmLengthCm 90"));
			Console(TEXT("jam.Debug.Menu Игрок"));
			Console(TEXT("jam.Debug.Search Arm"));
		});
		DebugProbeAfter(World, 4.5f, [Shot]() { Shot(TEXT("parameters: ArmLengthCm changed and pinned")); });
		DebugProbeAfter(World, 5.5f, [World, Console]()
		{
			Console(TEXT("jam.Debug.Menu Мир"));
			if (UJamDebugSubsystem* Debug = UJamDebugSubsystem::Get(World))
			{
				Debug->ImportJson(TEXT("{\"format\":1,\"name\":\"чужой конфиг\",\"build\":\"0000000\",\"world\":{\"gravityScale\":0.5,\"air\":\"vacuum\"},")
					TEXT("\"params\":{\"Pawn.ArmLengthCm\":\"90\",\"Pawn.NoSuchThing\":\"1\",\"Weapon[Пистолет].RecoilImpulse\":\"много\",\"Flyer.HearingRangeCm\":\"4000\"}}"));
				Debug->Say(TEXT("импорт «чужой конфиг»: см. пропущено"), true);
			}
		});
		DebugProbeAfter(World, 7.0f, [Shot]() { Shot(TEXT("import: skipped list, World tab")); });
		DebugProbeAfter(World, 8.0f, [Console]() { Console(TEXT("jam.Debug.Menu pin")); });
		DebugProbeAfter(World, 9.5f, [Shot]() { Shot(TEXT("pinned overlay over the game")); });
		DebugProbeAfter(World, 11.0f, [World]()
		{
			if (UJamDebugSubsystem* Debug = UJamDebugSubsystem::Get(World))
			{
				Debug->Reset(TEXT("all"));
				Debug->SetMode(EJamDebugMode::Closed);
			}
			UE_LOG(LogMindKore, Log, TEXT("Debug shots: done"));
		});
	}

	FAutoConsoleCommandWithWorld GJamDebugShots(TEXT("jam.Debug.Shots"),
		TEXT("Debug menu frames (real RHI): System tab, parameters with a changed and a pinned row, an import's skipped list, the pinned overlay; Saved/Screenshots."),
		FConsoleCommandWithWorldDelegate::CreateStatic(&RunDebugShots));
}
