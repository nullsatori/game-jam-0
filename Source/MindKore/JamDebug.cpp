#include "JamDebug.h"

#include "AssetRegistry/IAssetRegistry.h"
#include "Dom/JsonObject.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/GameViewportClient.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Framework/Application/IInputProcessor.h"
#include "Framework/Application/SlateApplication.h"
#include "GameFramework/GameUserSettings.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/WorldSettings.h"
#include "HAL/FileManager.h"
#include "HAL/IConsoleManager.h"
#include "HAL/PlatformApplicationMisc.h"
#include "JamDebugMenu.h"
#include "JamFlyer.h"
#include "JamGravity.h"
#include "JamShip.h"
#include "JamWeapons.h"
#include "JamZeroGPawn.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/App.h"
#include "Misc/CommandLine.h"
#include "Misc/CoreDelegates.h"
#include "Misc/FileHelper.h"
#include "Misc/OutputDeviceNull.h"
#include "Misc/PackageName.h"
#include "Misc/StringOutputDevice.h"
#include "Misc/Paths.h"
#include "MindKore.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#if WITH_EDITOR
#include "UObject/Package.h"
#include "UObject/SavePackage.h"
#endif

namespace
{
	const FString JamDebugAssetPath = TEXT("/Game/Jam/Debug/Presets");

	FString JamDebugDir()
	{
		return FPaths::ProjectSavedDir() / TEXT("JamDebug");
	}

	FString JamDebugPresetsDir()
	{
		return JamDebugDir() / TEXT("Presets");
	}

	FString JamDebugLastFile()
	{
		return JamDebugDir() / TEXT("Last.json");
	}

	/**
	 * What the menu shows and takes: an editable (not VisibleAnywhere) single value of this module's
	 * classes - numbers, bools, enums, vectors, rotators, colours. Asset references, arrays, curves
	 * and the engine's own properties stay out.
	 */
	bool JamDebugSupported(const FProperty* Property)
	{
		if (!Property || Property->ArrayDim != 1 || !Property->HasAnyPropertyFlags(CPF_Edit) || Property->HasAnyPropertyFlags(CPF_EditConst | CPF_Deprecated))
		{
			return false;
		}
		const UStruct* Owner = Property->GetOwnerStruct();
		if (!Owner || Owner->GetPackage()->GetFName() != FName(TEXT("/Script/MindKore")))
		{
			return false;
		}
		if (Property->IsA<FNumericProperty>() || Property->IsA<FBoolProperty>() || Property->IsA<FEnumProperty>())
		{
			return true;
		}
		if (const FStructProperty* Struct = CastField<FStructProperty>(Property))
		{
			const UScriptStruct* Type = Struct->Struct;
			return Type == TBaseStructure<FVector>::Get() || Type == TBaseStructure<FRotator>::Get() || Type == TBaseStructure<FLinearColor>::Get()
				|| Type == TBaseStructure<FColor>::Get() || Type == TBaseStructure<FVector2D>::Get();
		}
		return false;
	}

	/** Text as the property itself would export it (so "72" and "72.0" compare equal); false: not a value of this type. */
	bool JamDebugCanonical(const FProperty* Property, const FString& In, FString& Out)
	{
		const FString Text = In.TrimStartAndEnd();
		if (const FNumericProperty* Numeric = CastField<FNumericProperty>(Property); Numeric && !Numeric->IsEnum())
		{
			double Number = 0.0;
			if (!LexTryParseString(Number, *Text) || (Numeric->IsInteger() && Number != FMath::RoundToDouble(Number)))
			{
				return false;
			}
		}
		if (Property->IsA<FBoolProperty>() && !(Text.Equals(TEXT("true"), ESearchCase::IgnoreCase) || Text.Equals(TEXT("false"), ESearchCase::IgnoreCase)
			|| Text == TEXT("1") || Text == TEXT("0")))
		{
			return false;
		}
		void* Temp = FMemory::Malloc(Property->GetSize(), Property->GetMinAlignment());
		Property->InitializeValue(Temp);
		FOutputDeviceNull Quiet;
		const bool bOk = Property->ImportText_Direct(*Text, Temp, nullptr, PPF_None, &Quiet) != nullptr;
		if (bOk)
		{
			// ExportText_* with no delta skips a value equal to zero ("identical"): ExportTextItem always writes.
			Out.Reset();
			Property->ExportTextItem_Direct(Out, Temp, nullptr, nullptr, PPF_None);
		}
		Property->DestroyValue(Temp);
		FMemory::Free(Temp);
		return bOk;
	}

	FString JamDebugRead(const FJamDebugTarget& Target, const FProperty* Property)
	{
		FString Out;
		Property->ExportTextItem_InContainer(Out, Target.Container, nullptr, Target.Object, PPF_None);
		return Out;
	}

	/** The one place a value is written. Never a template: in PIE the CDO is the editor's, and a write leaks into saved assets. */
	bool JamDebugWrite(const FJamDebugTarget& Target, const FProperty* Property, const FString& Text)
	{
		if (!Target.Object || Target.Object->IsTemplate())
		{
			UE_LOG(LogMindKore, Error, TEXT("Debug menu: refused to write %s on a template (%s)"), *Property->GetName(), *GetPathNameSafe(Target.Object));
			return false;
		}
		FOutputDeviceNull Quiet;
		return Property->ImportText_InContainer(*Text, Target.Container, Target.Object, PPF_None, &Quiet) != nullptr;
	}

	/** "Weapon[Пистолет]" -> "Weapon", "Пистолет". */
	void JamDebugSplitOwner(const FString& Owner, FString& OutHead, FString& OutArg, FString& OutSub)
	{
		OutHead = Owner;
		OutArg.Reset();
		OutSub.Reset();
		int32 Bracket = INDEX_NONE;
		const int32 Close = Owner.Find(TEXT("]"));
		if (Owner.FindChar(TEXT('['), Bracket) && Close > Bracket)
		{
			OutHead = Owner.Left(Bracket);
			OutArg = Owner.Mid(Bracket + 1, Close - Bracket - 1);
			OutSub = Owner.Mid(Close + 1);
			OutSub.RemoveFromStart(TEXT("."));
			return;
		}
		int32 Dot = INDEX_NONE;
		if (Owner.FindChar(TEXT('.'), Dot))
		{
			OutHead = Owner.Left(Dot);
			OutSub = Owner.Mid(Dot + 1);
		}
	}

	FString JamDebugJsonKey(const FString& Property)
	{
		return Property.Left(1).ToLower() + Property.Mid(1);
	}

	FString JamDebugToJson(const TSharedRef<FJsonObject>& Object)
	{
		FString Out;
		const TSharedRef<TJsonWriter<TCHAR, TPrettyJsonPrintPolicy<TCHAR>>> Writer = TJsonWriterFactory<TCHAR, TPrettyJsonPrintPolicy<TCHAR>>::Create(&Out);
		FJsonSerializer::Serialize(Object, Writer);
		return Out;
	}

	TSharedPtr<FJsonObject> JamDebugParse(const FString& Text)
	{
		TSharedPtr<FJsonObject> Root;
		const TSharedRef<TJsonReader<TCHAR>> Reader = TJsonReaderFactory<TCHAR>::Create(Text);
		return FJsonSerializer::Deserialize(Reader, Root) ? Root : nullptr;
	}

	/** F6 and the menu's keys before anything else sees them: they work whatever the pawn's input does. */
	class FJamDebugInput : public IInputProcessor
	{
	public:
		explicit FJamDebugInput(UJamDebugSubsystem* InOwner) : Owner(InOwner) {}
		virtual void Tick(const float DeltaTime, FSlateApplication& SlateApp, TSharedRef<ICursor> Cursor) override {}
		virtual bool HandleKeyDownEvent(FSlateApplication& SlateApp, const FKeyEvent& InKeyEvent) override
		{
			UJamDebugSubsystem* Debug = Owner.Get();
			return Debug && Debug->HandleKey(InKeyEvent);
		}
		virtual const TCHAR* GetDebugName() const override { return TEXT("JamDebugMenu"); }

	private:
		TWeakObjectPtr<UJamDebugSubsystem> Owner;
	};
}

// --- lifetime ---------------------------------------------------------------------------

UJamDebugSubsystem* UJamDebugSubsystem::Get(const UObject* WorldContext)
{
	const UWorld* World = GEngine ? GEngine->GetWorldFromContextObject(WorldContext, EGetWorldErrorMode::ReturnNull) : nullptr;
	const UGameInstance* GameInstance = World ? World->GetGameInstance() : nullptr;
	return GameInstance ? GameInstance->GetSubsystem<UJamDebugSubsystem>() : nullptr;
}

bool UJamDebugSubsystem::ShouldCreateSubsystem(UObject* Outer) const
{
	// Shipping has no menu at all (Docs/DebugMenu.md).
	return !UE_BUILD_SHIPPING && Super::ShouldCreateSubsystem(Outer);
}

void UJamDebugSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	// What is set starts as what runs.
	if (const IConsoleVariable* MaxFps = IConsoleManager::Get().FindConsoleVariable(TEXT("t.MaxFPS")))
	{
		System.FpsLimit = MaxFps->GetFloat();
	}
	if (const IConsoleVariable* VSync = IConsoleManager::Get().FindConsoleVariable(TEXT("r.VSync")))
	{
		System.Vsync = VSync->GetInt() != 0;
	}
	bCommandLineFixedStep = FApp::UseFixedTimeStep();
	System.FixedStep = FApp::UseFixedTimeStep() && FApp::GetFixedDeltaTime() > 0.0 ? float(FMath::RoundToDouble(1.0 / FApp::GetFixedDeltaTime())) : 0.0f;
	InitialSystem = System;
	AppliedFpsLimit = System.FpsLimit;
	AppliedVsync = System.Vsync ? 1 : 0;
	AppliedFixedStep = System.FixedStep;
	JamSetGravityScale(WorldConfig.GravityScale);
	LoadPins();
	FString Last;
	if (FFileHelper::LoadFileToString(Last, *JamDebugLastFile()))
	{
		const TSharedPtr<FJsonObject> Root = JamDebugParse(Last);
		double Changed = 0.0;
		if (Root && Root->TryGetNumberField(TEXT("changed"), Changed))
		{
			LastSessionChanges = int32(Changed);
		}
	}
	FParse::Value(FCommandLine::Get(), TEXT("JamConfig="), PendingConfigFile);
	WorldInitHandle = FWorldDelegates::OnWorldInitializedActors.AddUObject(this, &UJamDebugSubsystem::OnWorldInitializedActors);
	BeginFrameHandle = FCoreDelegates::OnBeginFrame.AddUObject(this, &UJamDebugSubsystem::OnBeginFrame);
	EndFrameHandle = FCoreDelegates::OnEndFrame.AddUObject(this, &UJamDebugSubsystem::OnEndFrame);
	if (FSlateApplication::IsInitialized())
	{
		Input = MakeShared<FJamDebugInput>(this);
		FSlateApplication::Get().RegisterInputPreProcessor(Input);
	}
}

void UJamDebugSubsystem::Deinitialize()
{
	FWorldDelegates::OnWorldInitializedActors.Remove(WorldInitHandle);
	FCoreDelegates::OnBeginFrame.Remove(BeginFrameHandle);
	FCoreDelegates::OnEndFrame.Remove(EndFrameHandle);
	if (UWorld* World = HookedWorld.Get())
	{
		World->RemoveOnActorPreSpawnInitialization(PreSpawnHandle);
	}
	if (Input.IsValid() && FSlateApplication::IsInitialized())
	{
		FSlateApplication::Get().UnregisterInputPreProcessor(Input);
	}
	if (Menu.IsValid())
	{
		if (UGameViewportClient* Viewport = GetGameInstance()->GetGameViewportClient())
		{
			Viewport->RemoveViewportWidgetContent(Menu.ToSharedRef());
		}
		Menu.Reset();
	}
	// The globals outlive a PIE session: the editor goes on with its own frame rate, step and gravity.
	if (GIsEditor)
	{
		System = InitialSystem;
		ApplySettings();
	}
	JamSetGravityScale(1.0f);
	Super::Deinitialize();
}

UWorld* UJamDebugSubsystem::GetGameWorld() const
{
	return GetGameInstance() ? GetGameInstance()->GetWorld() : nullptr;
}

void UJamDebugSubsystem::OnWorldInitializedActors(const FActorsInitializedParams& Params)
{
	UWorld* World = Params.World;
	if (!World || World->GetGameInstance() != GetGameInstance())
	{
		return;
	}
	if (UWorld* Old = HookedWorld.Get())
	{
		Old->RemoveOnActorPreSpawnInitialization(PreSpawnHandle);
	}
	HookedWorld = World;
	PreSpawnHandle = World->AddOnActorPreSpawnInitialization(FOnActorSpawned::FDelegate::CreateUObject(this, &UJamDebugSubsystem::OnActorPreSpawn));
	// Map actors' originals belong to their map; the pawn's and the flyers' are the class's, the same on every map.
	for (auto It = Originals.CreateIterator(); It; ++It)
	{
		if (It.Key().StartsWith(TEXT("Zone[")) || It.Key().StartsWith(TEXT("Habitat[")))
		{
			It.RemoveCurrent();
		}
	}
	// The config goes on in the new map: its zones and habitats now, the pawn and the flyers as they spawn.
	for (const TPair<FString, FString>& Value : Values)
	{
		Write(Value.Key, Value.Value, nullptr);
	}
	ApplySettings();
	++Generation;
	EnsureWidgets();
	if (!PendingConfigFile.IsEmpty())
	{
		const FString File = PendingConfigFile;
		PendingConfigFile.Reset();
		const FJamDebugImport Result = Import(File);
		UE_LOG(LogMindKore, Log, TEXT("Debug menu: -JamConfig=%s - %d applied, %d skipped%s"), *File, Result.Applied, Result.Skipped.Num(),
			Result.Warning.IsEmpty() ? TEXT("") : *(TEXT(", ") + Result.Warning));
	}
}

void UJamDebugSubsystem::OnActorPreSpawn(AActor* Actor)
{
	// Before the actor's BeginPlay: a flyer is born with the config, as is the pawn.
	if (Values.IsEmpty() || !Actor || !(Actor->IsA<AJamZeroGPawn>() || Actor->IsA<AJamFlyer>()))
	{
		return;
	}
	for (const TPair<FString, FString>& Value : Values)
	{
		const FProperty* Property = nullptr;
		TArray<FJamDebugTarget> Live;
		FJamDebugTarget Schema;
		if (!Resolve(Value.Key, Actor, Property, Live, Schema, nullptr))
		{
			continue;
		}
		for (const FJamDebugTarget& Target : Live)
		{
			if (!Originals.Contains(Value.Key))
			{
				Originals.Add(Value.Key, JamDebugRead(Target, Property));
			}
			JamDebugWrite(Target, Property, Value.Value);
		}
	}
}

void UJamDebugSubsystem::EnsureWidgets()
{
	UGameViewportClient* Viewport = GetGameInstance() ? GetGameInstance()->GetGameViewportClient() : nullptr;
	if (!Viewport)
	{
		return;
	}
	if (!Menu.IsValid())
	{
		Menu = SNew(SJamDebugMenu).Debug(this);
	}
	// A map change may clear the viewport's widgets: add it again (removing first keeps it single).
	Viewport->RemoveViewportWidgetContent(Menu.ToSharedRef());
	Viewport->AddViewportWidgetContent(Menu.ToSharedRef(), 1000);
}

// --- frames, hitch, fixed step, pause ---------------------------------------------------

void UJamDebugSubsystem::OnBeginFrame()
{
	// The hitch: a long frame every N s of real time, caught by crossing the moment (the frame-rate rule).
	if (System.HitchMs > 0.0f && System.HitchEverySeconds > 0.0f)
	{
		const double Now = FPlatformTime::Seconds();
		if (NextHitch <= 0.0)
		{
			NextHitch = Now + System.HitchEverySeconds;
		}
		else if (Now >= NextHitch)
		{
			FPlatformProcess::SleepNoStats(System.HitchMs / 1000.0f);
			NextHitch = Now + System.HitchEverySeconds;
		}
	}
	else
	{
		NextHitch = 0.0;
	}
	// FApp's fixed step does not wait for real time: on a fast machine the game would run fast. The
	// menu's step keeps real-time pace; a machine that cannot keep it falls behind and the game slows.
	// A step from the command line (-UseFixedTimeStep, the probes) runs free as before.
	if (FApp::UseFixedTimeStep() && !bCommandLineFixedStep && System.FixedStep > 0.0f)
	{
		const double Step = 1.0 / System.FixedStep;
		const double Now = FPlatformTime::Seconds();
		if (LastPace > 0.0 && Now < LastPace + Step)
		{
			FPlatformProcess::SleepNoStats(float(LastPace + Step - Now));
		}
		LastPace = FPlatformTime::Seconds();
	}
	if (StepFramesLeft > 0 && --StepFramesLeft == 0)
	{
		SetPaused(true);
	}
}

void UJamDebugSubsystem::OnEndFrame()
{
	const double Now = FPlatformTime::Seconds();
	if (LastEndFrame > 0.0)
	{
		Frames.Add({ Now, float(Now - LastEndFrame), float(FApp::GetDeltaTime()) });
	}
	LastEndFrame = Now;
	// Trim in batches: the last 5 s are kept, at most a second more.
	if (!Frames.IsEmpty() && Frames[0].At < Now - 6.0)
	{
		int32 Old = 0;
		while (Old < Frames.Num() && Frames[Old].At < Now - 5.0)
		{
			++Old;
		}
		Frames.RemoveAt(0, Old, EAllowShrinking::No);
	}
}

void UJamDebugSubsystem::Measure(float Seconds, float& OutFps, float& OutMs, float& OutMaxMs) const
{
	OutFps = OutMs = OutMaxMs = 0.0f;
	if (Frames.IsEmpty())
	{
		return;
	}
	const double From = Frames.Last().At - Seconds;
	double Sum = 0.0;
	int32 Count = 0;
	for (int32 Index = Frames.Num() - 1; Index >= 0 && Frames[Index].At >= From; --Index)
	{
		Sum += Frames[Index].RealSeconds;
		OutMaxMs = FMath::Max(OutMaxMs, Frames[Index].RealSeconds * 1000.0f);
		++Count;
	}
	if (Count > 0 && Sum > 0.0)
	{
		OutFps = float(Count / Sum);
		OutMs = float(Sum / Count * 1000.0);
	}
}

void UJamDebugSubsystem::SetPaused(bool bPause)
{
	if (UWorld* World = GetGameWorld())
	{
		UGameplayStatics::SetGamePaused(World, bPause);
	}
}

bool UJamDebugSubsystem::IsPaused() const
{
	const UWorld* World = GetGameWorld();
	return World && UGameplayStatics::IsGamePaused(World);
}

void UJamDebugSubsystem::StepFrame()
{
	if (IsPaused())
	{
		// The next frame begins unpaused and ticks; the one after pauses again (OnBeginFrame).
		SetPaused(false);
		StepFramesLeft = 2;
	}
}

void UJamDebugSubsystem::ApplySettings()
{
	JamSetGravityScale(WorldConfig.GravityScale);
	// The time scale: DemoPlayTimeDilation, which nothing else here uses - TimeDilation belongs to the
	// bullet time (UMKTimeDomainSubsystem); the effective dilation is their product.
	if (UWorld* World = GetGameWorld())
	{
		if (AWorldSettings* Settings = World->GetWorldSettings())
		{
			Settings->DemoPlayTimeDilation = System.TimeScale;
		}
	}
	UGameUserSettings* User = GEngine ? GEngine->GetGameUserSettings() : nullptr;
	// FPS and vsync go to UGameUserSettings (saved: the next run starts with them) and straight into
	// their CVars. Not ApplySettings(): it also applies the resolution and scalability, in PIE the editor's.
	if (System.FpsLimit != AppliedFpsLimit)
	{
		AppliedFpsLimit = System.FpsLimit;
		if (User)
		{
			User->SetFrameRateLimit(System.FpsLimit);
			User->SaveSettings();
		}
		if (IConsoleVariable* MaxFps = IConsoleManager::Get().FindConsoleVariable(TEXT("t.MaxFPS")))
		{
			MaxFps->Set(System.FpsLimit, ECVF_SetByConsole);
		}
	}
	if ((System.Vsync ? 1 : 0) != AppliedVsync)
	{
		AppliedVsync = System.Vsync ? 1 : 0;
		if (User)
		{
			User->SetVSyncEnabled(System.Vsync);
			User->SaveSettings();
		}
		if (IConsoleVariable* VSync = IConsoleManager::Get().FindConsoleVariable(TEXT("r.VSync")))
		{
			VSync->Set(AppliedVsync, ECVF_SetByConsole);
		}
	}
	if (System.FixedStep != AppliedFixedStep)
	{
		AppliedFixedStep = System.FixedStep;
		FApp::SetUseFixedTimeStep(System.FixedStep > 0.0f);
		if (System.FixedStep > 0.0f)
		{
			FApp::SetFixedDeltaTime(1.0 / System.FixedStep);
		}
		LastPace = 0.0;
	}
}

// --- parameters -------------------------------------------------------------------------

bool UJamDebugSubsystem::ResolveOwner(const FString& Owner, AActor* OnlyActor, TArray<FJamDebugTarget>& OutLive, FJamDebugTarget& OutSchema) const
{
	UJamDebugSubsystem* Self = const_cast<UJamDebugSubsystem*>(this);
	if (Owner == TEXT("System") || Owner == TEXT("World"))
	{
		OutSchema = Owner == TEXT("System") ? FJamDebugTarget{ Self, &Self->System, FJamDebugSystem::StaticStruct() }
			: FJamDebugTarget{ Self, &Self->WorldConfig, FJamDebugWorld::StaticStruct() };
		if (!OnlyActor)
		{
			OutLive.Add(OutSchema);
		}
		return true;
	}
	UWorld* World = GetGameWorld();
	FString Head, Arg, Sub;
	JamDebugSplitOwner(Owner, Head, Arg, Sub);
	const auto ForEachPawn = [World, OnlyActor](TFunctionRef<void(AJamZeroGPawn*)> Fn)
	{
		if (OnlyActor)
		{
			if (AJamZeroGPawn* Pawn = Cast<AJamZeroGPawn>(OnlyActor))
			{
				Fn(Pawn);
			}
		}
		else if (World)
		{
			for (TActorIterator<AJamZeroGPawn> It(World); It; ++It)
			{
				Fn(*It);
			}
		}
	};
	AJamZeroGPawn* PawnDefaults = GetMutableDefault<AJamZeroGPawn>(); // read only: never written (JamDebugWrite refuses templates)
	if (Head == TEXT("Pawn") && Arg.IsEmpty())
	{
		// Pawn, or Pawn.<component> by its subobject name (Pawn.Oxygen, Pawn.Guns).
		const auto Part = [&Sub](AJamZeroGPawn* Pawn) -> UObject*
		{
			return Sub.IsEmpty() ? static_cast<UObject*>(Pawn) : Cast<UActorComponent>(Pawn->GetDefaultSubobjectByName(*Sub));
		};
		UObject* Schema = Part(PawnDefaults);
		if (!Schema)
		{
			return false;
		}
		OutSchema = { Schema, Schema, Schema->GetClass() };
		ForEachPawn([&](AJamZeroGPawn* Pawn)
		{
			if (UObject* Object = Part(Pawn))
			{
				OutLive.Add({ Object, Object, Object->GetClass() });
			}
		});
		return true;
	}
	if (Head == TEXT("Weapon") && !Arg.IsEmpty() && Sub.IsEmpty())
	{
		// Weapon[<Name>]: an element of the pawn's gun list (UJamWeaponsComponent::Weapons) by its name.
		const auto Find = [&Arg](AJamZeroGPawn* Pawn) -> FJamWeapon*
		{
			if (Pawn->Guns)
			{
				for (FJamWeapon& Weapon : Pawn->Guns->Weapons)
				{
					if (Weapon.Name.ToString() == Arg)
					{
						return &Weapon;
					}
				}
			}
			return nullptr;
		};
		ForEachPawn([&](AJamZeroGPawn* Pawn)
		{
			if (FJamWeapon* Weapon = Find(Pawn))
			{
				OutLive.Add({ Pawn->Guns.Get(), Weapon, FJamWeapon::StaticStruct() });
			}
		});
		if (FJamWeapon* Weapon = Find(PawnDefaults))
		{
			OutSchema = { PawnDefaults->Guns.Get(), Weapon, FJamWeapon::StaticStruct() };
			return true;
		}
		OutSchema = OutLive.IsEmpty() ? FJamDebugTarget() : OutLive[0];
		return !OutLive.IsEmpty();
	}
	if (Head == TEXT("Flyer") && Arg.IsEmpty() && Sub.IsEmpty())
	{
		// Every flyer, living and to come (OnActorPreSpawn); the class's defaults when none is alive.
		AJamFlyer* Defaults = GetMutableDefault<AJamFlyer>();
		OutSchema = { Defaults, Defaults, AJamFlyer::StaticClass() };
		if (OnlyActor)
		{
			if (AJamFlyer* Flyer = Cast<AJamFlyer>(OnlyActor))
			{
				OutLive.Add({ Flyer, Flyer, Flyer->GetClass() });
			}
		}
		else if (World)
		{
			for (TActorIterator<AJamFlyer> It(World); It; ++It)
			{
				OutLive.Add({ *It, *It, It->GetClass() });
			}
		}
		return true;
	}
	if ((Head == TEXT("Habitat") || Head == TEXT("Zone")) && !Arg.IsEmpty() && Sub.IsEmpty())
	{
		// Map actors by label (by object name in a packaged build, which has no labels).
		if (OnlyActor || !World)
		{
			return false;
		}
		UClass* Class = Head == TEXT("Zone") ? AJamGravityZone::StaticClass() : AJamFlyerHabitat::StaticClass();
		for (TActorIterator<AActor> It(World, Class); It; ++It)
		{
			if (It->GetActorNameOrLabel() == Arg)
			{
				OutLive.Add({ *It, *It, It->GetClass() });
			}
		}
		OutSchema = OutLive.IsEmpty() ? FJamDebugTarget() : OutLive[0];
		return !OutLive.IsEmpty();
	}
	return false;
}

bool UJamDebugSubsystem::Resolve(const FString& Key, AActor* OnlyActor, const FProperty*& OutProperty, TArray<FJamDebugTarget>& OutLive,
	FJamDebugTarget& OutSchema, FString* OutError) const
{
	OutProperty = nullptr;
	int32 Dot = INDEX_NONE;
	if (!Key.FindLastChar(TEXT('.'), Dot) || Dot == 0)
	{
		if (OutError)
		{
			*OutError = TEXT("ключ не вида <владелец>.<свойство>");
		}
		return false;
	}
	const FString Owner = Key.Left(Dot);
	if (!ResolveOwner(Owner, OnlyActor, OutLive, OutSchema))
	{
		if (OutError)
		{
			*OutError = FString::Printf(TEXT("нет владельца %s"), *Owner);
		}
		return false;
	}
	const UStruct* Struct = OutSchema.Struct ? OutSchema.Struct : (OutLive.IsEmpty() ? nullptr : OutLive[0].Struct);
	const FProperty* Property = Struct ? FindFProperty<FProperty>(Struct, *Key.Mid(Dot + 1)) : nullptr;
	if (!JamDebugSupported(Property))
	{
		if (OutError)
		{
			*OutError = Property ? TEXT("свойство не правится из меню") : FString::Printf(TEXT("у %s нет такого свойства"), *Owner);
		}
		return false;
	}
	OutProperty = Property;
	return true;
}

void UJamDebugSubsystem::ListOwners(TArray<FString>& Out) const
{
	Out.Add(TEXT("System"));
	Out.Add(TEXT("World"));
	Out.Add(TEXT("Pawn"));
	const auto HasParams = [](const UStruct* Struct)
	{
		for (TFieldIterator<FProperty> It(Struct); It; ++It)
		{
			if (JamDebugSupported(*It))
			{
				return true;
			}
		}
		return false;
	};
	TArray<UObject*> Parts;
	GetMutableDefault<AJamZeroGPawn>()->GetDefaultSubobjects(Parts);
	for (const UObject* Part : Parts)
	{
		if (Part && Part->IsA<UActorComponent>() && HasParams(Part->GetClass()))
		{
			Out.Add(TEXT("Pawn.") + Part->GetName());
		}
	}
	const UWorld* World = GetGameWorld();
	const AJamZeroGPawn* Pawn = nullptr;
	if (World)
	{
		for (TActorIterator<AJamZeroGPawn> It(World); It && !Pawn; ++It)
		{
			Pawn = *It;
		}
	}
	const AJamZeroGPawn* GunsOf = Pawn ? Pawn : GetDefault<AJamZeroGPawn>();
	if (GunsOf->Guns)
	{
		for (const FJamWeapon& Weapon : GunsOf->Guns->Weapons)
		{
			Out.Add(FString::Printf(TEXT("Weapon[%s]"), *Weapon.Name.ToString()));
		}
	}
	Out.Add(TEXT("Flyer"));
	if (World)
	{
		for (TActorIterator<AJamFlyerHabitat> It(World); It; ++It)
		{
			Out.AddUnique(FString::Printf(TEXT("Habitat[%s]"), *It->GetActorNameOrLabel()));
		}
		for (TActorIterator<AJamGravityZone> It(World); It; ++It)
		{
			Out.AddUnique(FString::Printf(TEXT("Zone[%s]"), *It->GetActorNameOrLabel()));
		}
	}
}

void UJamDebugSubsystem::ListParams(TArray<FJamDebugParam>& Out) const
{
	TArray<FString> Owners;
	ListOwners(Owners);
	for (const FString& Owner : Owners)
	{
		TArray<FJamDebugTarget> Live;
		FJamDebugTarget Schema;
		if (!ResolveOwner(Owner, nullptr, Live, Schema) || !Schema.Struct)
		{
			continue;
		}
		for (TFieldIterator<FProperty> It(Schema.Struct); It; ++It)
		{
			if (!JamDebugSupported(*It))
			{
				continue;
			}
			FJamDebugParam& Param = Out.AddDefaulted_GetRef();
			Param.Key = Owner + TEXT(".") + It->GetName();
			Param.Owner = Owner;
			Param.Group = Owner;
			Param.Property = *It;
#if WITH_METADATA
			// Editor metadata: in PIE and editor runs only (a packaged build has none: plain fields by owner).
			FString Category = It->GetMetaData(TEXT("Category"));
			Category.RemoveFromStart(TEXT("Jam|"));
			if (!Category.IsEmpty() && Category != Owner)
			{
				Param.Group = Owner + TEXT(" · ") + Category;
			}
			Param.Tooltip = It->GetMetaData(TEXT("ToolTip"));
			Param.Units = It->GetMetaData(TEXT("Units"));
			const auto Number = [&It](const TCHAR* Name) -> TOptional<double>
			{
				return It->HasMetaData(Name) ? TOptional<double>(FCString::Atod(*It->GetMetaData(Name))) : TOptional<double>();
			};
			Param.Min = Number(TEXT("ClampMin"));
			Param.Max = Number(TEXT("ClampMax"));
			Param.SliderMin = Number(TEXT("UIMin")).IsSet() ? Number(TEXT("UIMin")) : Param.Min;
			Param.SliderMax = Number(TEXT("UIMax")).IsSet() ? Number(TEXT("UIMax")) : Param.Max;
#endif
		}
	}
}

bool UJamDebugSubsystem::GetValue(const FString& Key, FString& Out) const
{
	const FProperty* Property = nullptr;
	TArray<FJamDebugTarget> Live;
	FJamDebugTarget Schema;
	if (!Resolve(Key, nullptr, Property, Live, Schema, nullptr))
	{
		return false;
	}
	if (!Live.IsEmpty())
	{
		Out = JamDebugRead(Live[0], Property);
	}
	else if (const FString* Value = Values.Find(Key))
	{
		Out = *Value; // no live owner (no flyer yet): what the next one gets
	}
	else
	{
		Out = JamDebugRead(Schema, Property);
	}
	return true;
}

int32 UJamDebugSubsystem::GetLiveCount(const FString& Key) const
{
	const FProperty* Property = nullptr;
	TArray<FJamDebugTarget> Live;
	FJamDebugTarget Schema;
	return Resolve(Key, nullptr, Property, Live, Schema, nullptr) ? Live.Num() : -1;
}

bool UJamDebugSubsystem::GetNumber(const FString& Key, double& Out) const
{
	FString Text;
	const FProperty* Property = nullptr;
	TArray<FJamDebugTarget> Live;
	FJamDebugTarget Schema;
	if (!Resolve(Key, nullptr, Property, Live, Schema, nullptr))
	{
		return false;
	}
	const FNumericProperty* Numeric = CastField<FNumericProperty>(Property);
	if (!Numeric || Numeric->IsEnum())
	{
		return false;
	}
	if (Live.IsEmpty() && Values.Contains(Key))
	{
		return LexTryParseString(Out, *Values[Key]);
	}
	const FJamDebugTarget& Target = Live.IsEmpty() ? Schema : Live[0];
	const void* Value = Numeric->ContainerPtrToValuePtr<void>(Target.Container);
	Out = Numeric->IsFloatingPoint() ? Numeric->GetFloatingPointPropertyValue(Value) : double(Numeric->GetSignedIntPropertyValue(Value));
	return true;
}

bool UJamDebugSubsystem::Write(const FString& Key, const FString& Canonical, FString* OutError)
{
	const FProperty* Property = nullptr;
	TArray<FJamDebugTarget> Live;
	FJamDebugTarget Schema;
	if (!Resolve(Key, nullptr, Property, Live, Schema, OutError))
	{
		return false;
	}
	if (!Originals.Contains(Key))
	{
		Originals.Add(Key, JamDebugRead(Live.IsEmpty() ? Schema : Live[0], Property));
	}
	for (const FJamDebugTarget& Target : Live)
	{
		if (!JamDebugWrite(Target, Property, Canonical))
		{
			if (OutError)
			{
				*OutError = TEXT("запись не удалась");
			}
			return false;
		}
	}
	if (Schema.Object == this)
	{
		ApplySettings();
	}
	return true;
}

bool UJamDebugSubsystem::SetValue(const FString& Key, const FString& Value, FString* OutError, bool bRecord, const FString* Before)
{
	const FProperty* Property = nullptr;
	TArray<FJamDebugTarget> Live;
	FJamDebugTarget Schema;
	if (!Resolve(Key, nullptr, Property, Live, Schema, OutError))
	{
		return false;
	}
	FString Canonical;
	if (!JamDebugCanonical(Property, Value, Canonical))
	{
		if (OutError)
		{
			*OutError = FString::Printf(TEXT("«%s» не значение типа %s"), *Value, *Property->GetCPPType());
		}
		return false;
	}
	FString Current;
	GetValue(Key, Current);
	if (!Write(Key, Canonical, OutError))
	{
		return false;
	}
	if (Canonical == Originals[Key])
	{
		Values.Remove(Key);
	}
	else
	{
		Values.Add(Key, Canonical);
	}
	const FString& From = Before ? *Before : Current;
	if (bRecord && From != Canonical)
	{
		Record({ { Key, From, Canonical } });
	}
	return true;
}

void UJamDebugSubsystem::Record(FStep&& Step)
{
	UndoSteps.Add(MoveTemp(Step));
	if (UndoSteps.Num() > 200)
	{
		UndoSteps.RemoveAt(0);
	}
	RedoSteps.Reset();
	Autosave();
}

bool UJamDebugSubsystem::Undo()
{
	if (UndoSteps.IsEmpty())
	{
		return false;
	}
	FStep Step = UndoSteps.Pop();
	for (int32 Index = Step.Num() - 1; Index >= 0; --Index)
	{
		SetValue(Step[Index].Key, Step[Index].Before, nullptr, false);
	}
	RedoSteps.Add(MoveTemp(Step));
	Autosave();
	return true;
}

bool UJamDebugSubsystem::Redo()
{
	if (RedoSteps.IsEmpty())
	{
		return false;
	}
	FStep Step = RedoSteps.Pop();
	for (const FChange& Change : Step)
	{
		SetValue(Change.Key, Change.After, nullptr, false);
	}
	UndoSteps.Add(MoveTemp(Step));
	Autosave();
	return true;
}

int32 UJamDebugSubsystem::Reset(const FString& KeyOrOwner)
{
	TArray<FString> Keys;
	for (const TPair<FString, FString>& Value : Values)
	{
		if (KeyOrOwner == TEXT("all") || Value.Key == KeyOrOwner || Value.Key.StartsWith(KeyOrOwner + TEXT(".")))
		{
			Keys.Add(Value.Key);
		}
	}
	FStep Step;
	for (const FString& Key : Keys)
	{
		FString Current = Values[Key];
		GetValue(Key, Current);
		const FString* Original = Originals.Find(Key);
		// No original (its map actor is gone): just forget the change.
		if (!Original || !SetValue(Key, *Original, nullptr, false))
		{
			Values.Remove(Key);
		}
		Step.Add({ Key, Current, Original ? *Original : Current });
	}
	if (!Step.IsEmpty())
	{
		Record(MoveTemp(Step));
	}
	return Keys.Num();
}

// --- pins -------------------------------------------------------------------------------

const TArray<FString>& UJamDebugSubsystem::GetDefaultPins()
{
	// Docs/DebugMenu.md, «Закреплено по умолчанию»; jam.Debug.Probe checks that each one resolves.
	static const TArray<FString> Defaults = {
		TEXT("World.GravityScale"),
		TEXT("Pawn.LookSensitivity"),
		TEXT("Pawn.CameraFOV"),
		TEXT("Pawn.ThrustAccelCm"),
		TEXT("Pawn.WalkSpeedCm"),
		TEXT("Pawn.JumpSpeedCm"),
		TEXT("Pawn.ArmLengthCm"),
		TEXT("Pawn.ArmPullCmS"),
		TEXT("Pawn.SwingAccelCm"),
		TEXT("Weapon[Пистолет].RecoilImpulse"),
		TEXT("Weapon[Дробовик].RecoilImpulse"),
		TEXT("Weapon[Снапа].RecoilImpulse"),
		TEXT("Pawn.Oxygen.CapacitySeconds"),
		TEXT("Flyer.DiveSpeedCm"),
		TEXT("Flyer.BiteDamage"),
	};
	return Defaults;
}

void UJamDebugSubsystem::LoadPins()
{
	Pins = GetDefaultPins();
	FString Text;
	if (FFileHelper::LoadFileToString(Text, *(JamDebugDir() / TEXT("Pins.json"))))
	{
		const TSharedPtr<FJsonObject> Root = JamDebugParse(Text);
		const TArray<TSharedPtr<FJsonValue>>* List = nullptr;
		if (Root && Root->TryGetArrayField(TEXT("pins"), List))
		{
			Pins.Reset();
			for (const TSharedPtr<FJsonValue>& Value : *List)
			{
				Pins.AddUnique(Value->AsString());
			}
		}
	}
}

void UJamDebugSubsystem::SavePins() const
{
	const TSharedRef<FJsonObject> Root = MakeShared<FJsonObject>();
	TArray<TSharedPtr<FJsonValue>> List;
	for (const FString& Pin : Pins)
	{
		List.Add(MakeShared<FJsonValueString>(Pin));
	}
	Root->SetArrayField(TEXT("pins"), List);
	FFileHelper::SaveStringToFile(JamDebugToJson(Root), *(JamDebugDir() / TEXT("Pins.json")), FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
}

void UJamDebugSubsystem::SetPinned(const FString& Key, bool bPinned)
{
	if (bPinned)
	{
		Pins.AddUnique(Key);
	}
	else
	{
		Pins.Remove(Key);
	}
	SavePins();
}

void UJamDebugSubsystem::RestoreDefaultPins()
{
	Pins = GetDefaultPins();
	SavePins();
}

// --- configs ----------------------------------------------------------------------------

const FString& UJamDebugSubsystem::GetBuildHash()
{
	// The checkout's HEAD, read from .git (a worktree's .git is a file pointing at its gitdir; refs
	// are loose files or packed-refs in the common dir).
	static const FString Hash = []() -> FString
	{
		const FString Project = FPaths::ConvertRelativePathToFull(FPaths::ProjectDir());
		FString GitDir = Project / TEXT(".git");
		FString Text;
		if (FPaths::FileExists(GitDir) && FFileHelper::LoadFileToString(Text, *GitDir) && Text.StartsWith(TEXT("gitdir:")))
		{
			GitDir = Text.Mid(7).TrimStartAndEnd();
			if (FPaths::IsRelative(GitDir))
			{
				GitDir = Project / GitDir;
			}
		}
		FString Head;
		if (!FFileHelper::LoadFileToString(Head, *(GitDir / TEXT("HEAD"))))
		{
			return TEXT("?");
		}
		Head.TrimStartAndEndInline();
		if (!Head.StartsWith(TEXT("ref: ")))
		{
			return Head.Left(7);
		}
		const FString Ref = Head.Mid(5);
		FString Common = GitDir;
		if (FFileHelper::LoadFileToString(Text, *(GitDir / TEXT("commondir"))))
		{
			Common = FPaths::ConvertRelativePathToFull(GitDir / Text.TrimStartAndEnd());
		}
		for (const FString& Dir : { GitDir, Common })
		{
			if (FFileHelper::LoadFileToString(Text, *(Dir / Ref)))
			{
				return Text.TrimStartAndEnd().Left(7);
			}
		}
		TArray<FString> Lines;
		if (FFileHelper::LoadFileToStringArray(Lines, *(Common / TEXT("packed-refs"))))
		{
			for (const FString& Line : Lines)
			{
				if (Line.EndsWith(TEXT(" ") + Ref))
				{
					return Line.Left(7);
				}
			}
		}
		return TEXT("?");
	}();
	return Hash;
}

FString UJamDebugSubsystem::ExportJson() const
{
	const TSharedRef<FJsonObject> Root = MakeShared<FJsonObject>();
	const TSharedRef<FJsonObject> SystemJson = MakeShared<FJsonObject>();
	const TSharedRef<FJsonObject> WorldJson = MakeShared<FJsonObject>();
	const TSharedRef<FJsonObject> Params = MakeShared<FJsonObject>();
	const UWorld* World = GetGameWorld();
	Root->SetNumberField(TEXT("format"), 1);
	Root->SetStringField(TEXT("name"), ConfigName);
	Root->SetStringField(TEXT("note"), TEXT(""));
	Root->SetStringField(TEXT("build"), GetBuildHash());
	Root->SetStringField(TEXT("map"), World ? UWorld::RemovePIEPrefix(World->GetMapName()) : FString());
	Root->SetStringField(TEXT("created"), FDateTime::Now().ToString(TEXT("%Y-%m-%dT%H:%M:%S")));
	Root->SetNumberField(TEXT("changed"), Values.Num());
	TArray<FString> Keys;
	Values.GetKeys(Keys);
	Keys.Sort();
	for (const FString& Key : Keys)
	{
		const FString& Value = Values[Key];
		FString Section, Name;
		Key.Split(TEXT("."), &Section, &Name, ESearchCase::CaseSensitive, ESearchDir::FromEnd);
		if (Section == TEXT("System") || Section == TEXT("World"))
		{
			// The doc's format: system and world as JSON numbers and bools, only what differs.
			const TSharedRef<FJsonObject>& Into = Section == TEXT("System") ? SystemJson : WorldJson;
			if (Value == TEXT("True") || Value == TEXT("False"))
			{
				Into->SetBoolField(JamDebugJsonKey(Name), Value == TEXT("True"));
			}
			else
			{
				Into->SetNumberField(JamDebugJsonKey(Name), FCString::Atod(*Value));
			}
			continue;
		}
		Params->SetStringField(Key, Value);
	}
	Root->SetObjectField(TEXT("system"), SystemJson);
	Root->SetObjectField(TEXT("world"), WorldJson);
	Root->SetObjectField(TEXT("params"), Params);
	return JamDebugToJson(Root);
}

FString UJamDebugSubsystem::ExportToFile(const FString& Name)
{
	if (!Name.IsEmpty())
	{
		ConfigName = Name;
	}
	const FString File = JamDebugPresetsDir() / FPaths::MakeValidFileName(ConfigName.IsEmpty() ? FString(TEXT("config")) : ConfigName, TEXT('_')) + TEXT(".json");
	const FString Json = ExportJson();
	if (!FFileHelper::SaveStringToFile(Json, *File, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM))
	{
		Say(FString::Printf(TEXT("экспорт не записан: %s"), *File), true);
		return FString();
	}
	// Unattended runs (probes) leave the user's clipboard alone.
	const bool bClipboard = !FApp::IsUnattended();
	if (bClipboard)
	{
		FPlatformApplicationMisc::ClipboardCopy(*Json);
	}
	Say(FString::Printf(TEXT("экспорт: %s%s"), *FPaths::ConvertRelativePathToFull(File), bClipboard ? TEXT(" и в буфер") : TEXT("")));
	return File;
}

FJamDebugImport UJamDebugSubsystem::ImportJson(const FString& Json, bool bReplace)
{
	FJamDebugImport Result;
	const TSharedPtr<FJsonObject> Root = JamDebugParse(Json);
	if (!Root)
	{
		Result.Warning = TEXT("не JSON");
		return Result;
	}
	FString Build;
	if (Root->TryGetStringField(TEXT("build"), Build) && !Build.IsEmpty() && Build != GetBuildHash())
	{
		Result.Warning = FString::Printf(TEXT("конфиг из сборки %s, сейчас %s"), *Build, *GetBuildHash());
	}
	TArray<TPair<FString, FString>> Pairs;
	for (const TCHAR* Section : { TEXT("system"), TEXT("world") })
	{
		const TSharedPtr<FJsonObject>* Object = nullptr;
		if (Root->TryGetObjectField(Section, Object))
		{
			const FString Owner = FString(Section).Left(1).ToUpper() + FString(Section).Mid(1);
			for (const TPair<FString, TSharedPtr<FJsonValue>>& Field : (*Object)->Values)
			{
				const FString Key = Owner + TEXT(".") + Field.Key.Left(1).ToUpper() + Field.Key.Mid(1);
				// By JSON type: a number also answers TryGetBool.
				Pairs.Emplace(Key, Field.Value->Type == EJson::Boolean ? FString(Field.Value->AsBool() ? TEXT("True") : TEXT("False"))
					: Field.Value->Type == EJson::Number ? FString::SanitizeFloat(Field.Value->AsNumber()) : Field.Value->AsString());
			}
		}
	}
	const TSharedPtr<FJsonObject>* Params = nullptr;
	if (Root->TryGetObjectField(TEXT("params"), Params))
	{
		for (const TPair<FString, TSharedPtr<FJsonValue>>& Field : (*Params)->Values)
		{
			Pairs.Emplace(Field.Key, Field.Value->AsString());
		}
	}
	for (const TCHAR* Later : { TEXT("cheats"), TEXT("start") })
	{
		if (Root->HasField(Later))
		{
			Result.Skipped.Add(FString::Printf(TEXT("%s: в этой сборке нет (фаза 4)"), Later));
		}
	}
	// One undo step for the whole import, the reset included.
	FStep Step;
	if (bReplace)
	{
		TArray<FString> Keys;
		Values.GetKeys(Keys);
		for (const FString& Key : Keys)
		{
			FString Current = Values[Key];
			GetValue(Key, Current);
			const FString* Original = Originals.Find(Key);
			if (!Original || !SetValue(Key, *Original, nullptr, false))
			{
				Values.Remove(Key);
			}
			Step.Add({ Key, Current, Original ? *Original : Current });
		}
	}
	for (const TPair<FString, FString>& Pair : Pairs)
	{
		FString Before, Error;
		GetValue(Pair.Key, Before);
		if (!SetValue(Pair.Key, Pair.Value, &Error, false))
		{
			Result.Skipped.Add(FString::Printf(TEXT("%s: %s"), *Pair.Key, *Error));
			continue;
		}
		FString After;
		GetValue(Pair.Key, After);
		Step.Add({ Pair.Key, Before, Values.Contains(Pair.Key) ? Values[Pair.Key] : After });
		++Result.Applied;
	}
	FString Name;
	if (Root->TryGetStringField(TEXT("name"), Name))
	{
		ConfigName = Name;
	}
	if (!Step.IsEmpty())
	{
		Record(MoveTemp(Step));
	}
	Result.bOk = true;
	LastSkipped = Result.Skipped;
	return Result;
}

FJamDebugImport UJamDebugSubsystem::Import(const FString& Source, bool bReplace)
{
	FString Json;
	if (Source.Equals(TEXT("clipboard"), ESearchCase::IgnoreCase))
	{
		FPlatformApplicationMisc::ClipboardPaste(Json);
	}
	else
	{
		for (const FString& File : { Source, JamDebugPresetsDir() / Source, JamDebugPresetsDir() / Source + TEXT(".json"), JamDebugDir() / Source })
		{
			if (FPaths::FileExists(File) && FFileHelper::LoadFileToString(Json, *File))
			{
				break;
			}
		}
		if (Json.IsEmpty())
		{
			// A preset by its name: the asset's or the config's.
			TArray<FJamDebugPreset> Presets;
			ListPresets(Presets);
			for (const FJamDebugPreset& Preset : Presets)
			{
				if (Preset.Name == Source || FPaths::GetBaseFilename(Preset.Source) == Source || FPaths::GetBaseFilename(Preset.Source) == TEXT("DA_Test_") + Source)
				{
					if (!Preset.bAsset)
					{
						FFileHelper::LoadFileToString(Json, *Preset.Source);
					}
					else if (const UJamTestPreset* Asset = LoadObject<UJamTestPreset>(nullptr, *Preset.Source))
					{
						Json = Asset->Json;
					}
					break;
				}
			}
		}
	}
	FJamDebugImport Result;
	if (Json.IsEmpty())
	{
		Result.Warning = FString::Printf(TEXT("нет конфига «%s»"), *Source);
	}
	else
	{
		Result = ImportJson(Json, bReplace);
	}
	Say(FString::Printf(TEXT("импорт «%s»: применено %d, пропущено %d%s%s"), *Source, Result.Applied, Result.Skipped.Num(),
		Result.Warning.IsEmpty() ? TEXT("") : TEXT(" - "), *Result.Warning), !Result.bOk);
	for (const FString& Skipped : Result.Skipped)
	{
		UE_LOG(LogMindKore, Log, TEXT("Debug menu: skipped %s"), *Skipped);
	}
	return Result;
}

FJamDebugImport UJamDebugSubsystem::RestoreLastSession()
{
	LastSessionChanges = 0;
	return Import(JamDebugLastFile());
}

void UJamDebugSubsystem::Autosave()
{
	// Never at start: Last.json keeps the previous session's config until this one changes something.
	bAutosaveArmed = true;
	FFileHelper::SaveStringToFile(ExportJson(), *JamDebugLastFile(), FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
}

void UJamDebugSubsystem::ListPresets(TArray<FJamDebugPreset>& Out) const
{
	IAssetRegistry& Registry = IAssetRegistry::GetChecked();
#if WITH_EDITOR
	Registry.ScanPathsSynchronous({ JamDebugAssetPath }, /*bForceRescan=*/true);
#endif
	TArray<FAssetData> Assets;
	Registry.GetAssetsByPath(FName(*JamDebugAssetPath), Assets, /*bRecursive=*/true);
	for (const FAssetData& Asset : Assets)
	{
		if (Asset.AssetClassPath != UJamTestPreset::StaticClass()->GetClassPathName())
		{
			continue;
		}
		FJamDebugPreset& Preset = Out.AddDefaulted_GetRef();
		Preset.bAsset = true;
		Preset.Source = Asset.GetObjectPathString();
		Preset.Name = Asset.AssetName.ToString();
		if (const UJamTestPreset* Loaded = Cast<UJamTestPreset>(Asset.GetAsset()))
		{
			const TSharedPtr<FJsonObject> Root = JamDebugParse(Loaded->Json);
			FString Name;
			if (Root && Root->TryGetStringField(TEXT("name"), Name) && !Name.IsEmpty())
			{
				Preset.Name = Name;
			}
		}
	}
	TArray<FString> Files;
	IFileManager::Get().FindFiles(Files, *(JamDebugPresetsDir() / TEXT("*.json")), true, false);
	for (const FString& File : Files)
	{
		Out.Add({ FPaths::GetBaseFilename(File), JamDebugPresetsDir() / File, false });
	}
}

bool UJamDebugSubsystem::SaveToAsset(const FString& Name, FString& OutMessage)
{
	// Name gives the asset its name; the config inside keeps its own (named after the asset if it has none).
	const FString Suffix = Name.IsEmpty() ? ConfigName : Name;
	if (ConfigName.IsEmpty())
	{
		ConfigName = Suffix;
	}
	FString Clean = Suffix.IsEmpty() ? FString(TEXT("config")) : Suffix;
	for (TCHAR& Char : Clean)
	{
		if (FCString::Strchr(INVALID_OBJECTNAME_CHARACTERS INVALID_LONGPACKAGE_CHARACTERS TEXT(" "), Char))
		{
			Char = TEXT('_');
		}
	}
#if WITH_EDITOR
	const FString AssetName = TEXT("DA_Test_") + Clean;
	const FString PackageName = JamDebugAssetPath / AssetName;
	FText Reason;
	if (!FPackageName::IsValidLongPackageName(PackageName, false, &Reason))
	{
		OutMessage = FString::Printf(TEXT("ассет не сохранён: имя %s - %s"), *PackageName, *Reason.ToString());
		Say(OutMessage, true);
		return false;
	}
	const FString File = FPackageName::LongPackageNameToFilename(PackageName, FPackageName::GetAssetPackageExtension());
	if (IFileManager::Get().IsReadOnly(*File))
	{
		OutMessage = FString::Printf(TEXT("ассет не сохранён: %s только для чтения (git lfs lock?)"), *File);
		Say(OutMessage, true);
		return false;
	}
	// An existing preset is loaded whole first: a partly loaded package refuses to save.
	UPackage* Package = FPackageName::DoesPackageExist(PackageName) ? LoadPackage(nullptr, *PackageName, LOAD_None) : nullptr;
	if (!Package)
	{
		Package = CreatePackage(*PackageName);
	}
	Package->FullyLoad();
	UJamTestPreset* Preset = FindObject<UJamTestPreset>(Package, *AssetName);
	const bool bNew = !Preset;
	if (bNew)
	{
		Preset = NewObject<UJamTestPreset>(Package, *AssetName, RF_Public | RF_Standalone);
	}
	Preset->Json = ExportJson();
	Package->MarkPackageDirty();
	if (bNew)
	{
		IAssetRegistry::GetChecked().AssetCreated(Preset);
	}
	FStringOutputDevice Errors;
	FSavePackageArgs Args;
	Args.TopLevelFlags = RF_Public | RF_Standalone;
	Args.Error = &Errors;
	const bool bSaved = UPackage::SavePackage(Package, Preset, *File, Args);
	// A refused save is an error, never silence (as jam_build.save()).
	if (!bSaved || !FPaths::FileExists(File))
	{
		OutMessage = FString::Printf(TEXT("ассет не сохранён: %s %s"), *File, *Errors);
		Say(OutMessage, true);
		return false;
	}
	OutMessage = FString::Printf(TEXT("сохранено в ассет %s"), *PackageName);
	Say(OutMessage);
	return true;
#else
	// A packaged build cannot write assets: the config goes to a JSON preset instead.
	const FString File = ExportToFile(FString());
	OutMessage = FString::Printf(TEXT("в сборке ассеты не сохраняются (только редактор) - сохранено в %s"), *File);
	Say(OutMessage);
	return !File.IsEmpty();
#endif
}

// --- the panel --------------------------------------------------------------------------

void UJamDebugSubsystem::Say(const FString& Message, bool bError)
{
	Status = Message;
	bStatusError = bError;
	if (bError)
	{
		UE_LOG(LogMindKore, Warning, TEXT("Debug menu: %s"), *Message);
	}
	else
	{
		UE_LOG(LogMindKore, Log, TEXT("Debug menu: %s"), *Message);
	}
}

void UJamDebugSubsystem::SetMode(EJamDebugMode NewMode)
{
	const EJamDebugMode Was = Mode;
	Mode = NewMode;
	EnsureWidgets();
	APlayerController* Controller = GetGameWorld() ? GetGameWorld()->GetFirstPlayerController() : nullptr;
	if (Controller && (Mode == EJamDebugMode::Open || Was == EJamDebugMode::Open))
	{
		if (Mode == EJamDebugMode::Open)
		{
			// The cursor and the keys go to the menu; the game keeps running.
			FInputModeUIOnly InputMode;
			if (Menu.IsValid())
			{
				InputMode.SetWidgetToFocus(Menu->GetFocusTarget());
			}
			InputMode.SetLockMouseToViewportBehavior(EMouseLockMode::DoNotLock);
			Controller->SetInputMode(InputMode);
			Controller->SetShowMouseCursor(true);
		}
		else
		{
			Controller->SetInputMode(FInputModeGameOnly());
			Controller->SetShowMouseCursor(false);
		}
	}
	if (Menu.IsValid())
	{
		Menu->OnModeChanged();
	}
}

bool UJamDebugSubsystem::HandleKey(const FKeyEvent& Event)
{
	const FKey Key = Event.GetKey();
	if (Key == EKeys::F6)
	{
		if (!Event.IsRepeat())
		{
			if (Event.IsShiftDown())
			{
				SetMode(Mode == EJamDebugMode::Pinned ? EJamDebugMode::Closed : EJamDebugMode::Pinned);
			}
			else
			{
				SetMode(Mode == EJamDebugMode::Open ? EJamDebugMode::Closed : EJamDebugMode::Open);
			}
		}
		return true;
	}
	if (Mode != EJamDebugMode::Open)
	{
		return false;
	}
	if (Key == EKeys::Escape)
	{
		SetMode(EJamDebugMode::Closed);
		return true;
	}
	if (Event.IsControlDown() && Key == EKeys::F && Menu.IsValid())
	{
		Menu->FocusSearch();
		return true;
	}
	if (Event.IsControlDown() && (Key == EKeys::Z || Key == EKeys::Y))
	{
		// Inside a text field Ctrl+Z is the field's own undo.
		const TSharedPtr<SWidget> Focused = FSlateApplication::Get().GetKeyboardFocusedWidget();
		if (Focused.IsValid() && Focused->GetType() == FName(TEXT("SEditableText")))
		{
			return false;
		}
		Key == EKeys::Z ? Undo() : Redo();
		return true;
	}
	return false;
}

// --- console ----------------------------------------------------------------------------

namespace
{
	UJamDebugSubsystem* JamDebugFor(UWorld* World)
	{
		UJamDebugSubsystem* Debug = UJamDebugSubsystem::Get(World);
		if (!Debug)
		{
			UE_LOG(LogMindKore, Warning, TEXT("Debug menu: no game instance (jam.Debug.* runs in a game or PIE)"));
		}
		return Debug;
	}

	void JamDebugSetLogged(UJamDebugSubsystem* Debug, const FString& Key, const FString& Value)
	{
		FString Error;
		if (Debug->SetValue(Key, Value, &Error))
		{
			FString Now;
			Debug->GetValue(Key, Now);
			Debug->Say(FString::Printf(TEXT("%s = %s"), *Key, *Now));
		}
		else
		{
			Debug->Say(FString::Printf(TEXT("%s: %s"), *Key, *Error), true);
		}
	}

	FAutoConsoleCommandWithWorldAndArgs GJamDebugMenu(TEXT("jam.Debug.Menu"),
		TEXT("Debug menu (F6): toggle; or open on a tab (Система, Мир, Игрок, Оружие, Враги, Пресеты, Закреплено), 'pin' (Shift+F6), 'close'."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
		{
			if (UJamDebugSubsystem* Debug = JamDebugFor(World))
			{
				const FString Arg = Args.IsEmpty() ? FString() : Args[0];
				if (Arg == TEXT("pin"))
				{
					Debug->SetMode(EJamDebugMode::Pinned);
				}
				else if (Arg == TEXT("close"))
				{
					Debug->SetMode(EJamDebugMode::Closed);
				}
				else if (Arg.IsEmpty())
				{
					Debug->SetMode(Debug->GetMode() == EJamDebugMode::Open ? EJamDebugMode::Closed : EJamDebugMode::Open);
				}
				else
				{
					Debug->SetMode(EJamDebugMode::Open);
					if (Debug->GetMenu().IsValid())
					{
						Debug->GetMenu()->ShowTab(Arg);
					}
				}
			}
		}));

	FAutoConsoleCommandWithWorldAndArgs GJamDebugSearch(TEXT("jam.Debug.Search"),
		TEXT("Debug menu: put this text into the search (empty: clear); 'changed' toggles «только изменённые»."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
		{
			UJamDebugSubsystem* Debug = JamDebugFor(World);
			if (Debug && Debug->GetMenu().IsValid())
			{
				if (Args.Num() == 1 && Args[0] == TEXT("changed"))
				{
					Debug->GetMenu()->ToggleChangedOnly();
				}
				else
				{
					Debug->GetMenu()->SetSearch(FString::Join(Args, TEXT(" ")));
				}
			}
		}));

	FAutoConsoleCommandWithWorldAndArgs GJamDebugSet(TEXT("jam.Debug.Set"),
		TEXT("jam.Debug.Set <key> <value>: a parameter, as UE property text (Pawn.ArmLengthCm 72, Weapon[Дробовик].RecoilImpulse 20000)."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
		{
			UJamDebugSubsystem* Debug = JamDebugFor(World);
			if (Debug && Args.Num() >= 2)
			{
				JamDebugSetLogged(Debug, Args[0], FString::Join(TArrayView<const FString>(Args).Slice(1, Args.Num() - 1), TEXT(" ")));
			}
		}));

	FAutoConsoleCommandWithWorldAndArgs GJamDebugGet(TEXT("jam.Debug.Get"),
		TEXT("jam.Debug.Get <key>: a parameter's value, and its original if changed."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
		{
			UJamDebugSubsystem* Debug = JamDebugFor(World);
			FString Value;
			if (Debug && Args.Num() == 1 && Debug->GetValue(Args[0], Value))
			{
				const FString* Original = Debug->GetOriginal(Args[0]);
				Debug->Say(FString::Printf(TEXT("%s = %s%s"), *Args[0], *Value,
					Debug->IsChanged(Args[0]) && Original ? *FString::Printf(TEXT(" (было %s)"), **Original) : TEXT("")));
			}
			else if (Debug)
			{
				Debug->Say(FString::Printf(TEXT("нет параметра %s"), Args.IsEmpty() ? TEXT("") : *Args[0]), true);
			}
		}));

	FAutoConsoleCommandWithWorldAndArgs GJamDebugReset(TEXT("jam.Debug.Reset"),
		TEXT("jam.Debug.Reset [key|owner|all]: back to the originals (default all)."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
		{
			if (UJamDebugSubsystem* Debug = JamDebugFor(World))
			{
				const FString What = Args.IsEmpty() ? FString(TEXT("all")) : Args[0];
				Debug->Say(FString::Printf(TEXT("сброс %s: %d"), *What, Debug->Reset(What)));
			}
		}));

	FAutoConsoleCommandWithWorld GJamDebugUndo(TEXT("jam.Debug.Undo"), TEXT("Debug menu: undo the last change (Ctrl+Z)."),
		FConsoleCommandWithWorldDelegate::CreateLambda([](UWorld* World)
		{
			if (UJamDebugSubsystem* Debug = JamDebugFor(World))
			{
				Debug->Undo();
			}
		}));

	FAutoConsoleCommandWithWorld GJamDebugRedo(TEXT("jam.Debug.Redo"), TEXT("Debug menu: redo (Ctrl+Y)."),
		FConsoleCommandWithWorldDelegate::CreateLambda([](UWorld* World)
		{
			if (UJamDebugSubsystem* Debug = JamDebugFor(World))
			{
				Debug->Redo();
			}
		}));

	FAutoConsoleCommandWithWorldAndArgs GJamDebugExport(TEXT("jam.Debug.Export"),
		TEXT("jam.Debug.Export [name]: the config to Saved/JamDebug/Presets/<name>.json and the clipboard."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
		{
			if (UJamDebugSubsystem* Debug = JamDebugFor(World))
			{
				Debug->ExportToFile(FString::Join(Args, TEXT(" ")));
			}
		}));

	FAutoConsoleCommandWithWorldAndArgs GJamDebugImport(TEXT("jam.Debug.Import"),
		TEXT("jam.Debug.Import <file|clipboard>: replace the config; unknown keys are skipped and listed."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
		{
			UJamDebugSubsystem* Debug = JamDebugFor(World);
			if (Debug && !Args.IsEmpty())
			{
				Debug->Import(FString::Join(Args, TEXT(" ")));
			}
		}));

	FAutoConsoleCommandWithWorldAndArgs GJamDebugPreset(TEXT("jam.Debug.Preset"),
		TEXT("jam.Debug.Preset <name>: apply a preset (asset or Saved JSON) instead of the current config."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
		{
			UJamDebugSubsystem* Debug = JamDebugFor(World);
			if (Debug && !Args.IsEmpty())
			{
				Debug->Import(FString::Join(Args, TEXT(" ")));
			}
		}));

	FAutoConsoleCommandWithWorldAndArgs GJamDebugSaveAsset(TEXT("jam.Debug.SaveAsset"),
		TEXT("jam.Debug.SaveAsset [name]: the config into /Game/Jam/Debug/Presets/DA_Test_<name> (editor builds only)."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
		{
			if (UJamDebugSubsystem* Debug = JamDebugFor(World))
			{
				FString Message;
				Debug->SaveToAsset(FString::Join(Args, TEXT(" ")), Message);
			}
		}));

	void JamDebugSystemCommand(const TCHAR* Name, const TCHAR* Key, const TCHAR* Help)
	{
		IConsoleManager::Get().RegisterConsoleCommand(Name, Help, FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([Key](const TArray<FString>& Args, UWorld* World)
		{
			UJamDebugSubsystem* Debug = JamDebugFor(World);
			if (Debug && !Args.IsEmpty())
			{
				JamDebugSetLogged(Debug, Key, Args[0]);
			}
		}));
	}

	struct FJamDebugSystemCommands
	{
		FJamDebugSystemCommands()
		{
			JamDebugSystemCommand(TEXT("jam.Debug.Fps"), TEXT("System.FpsLimit"), TEXT("jam.Debug.Fps <N|0>: frame-rate cap (UGameUserSettings + t.MaxFPS), 0 = none."));
			JamDebugSystemCommand(TEXT("jam.Debug.VSync"), TEXT("System.Vsync"), TEXT("jam.Debug.VSync 0|1"));
			JamDebugSystemCommand(TEXT("jam.Debug.FixedStep"), TEXT("System.FixedStep"), TEXT("jam.Debug.FixedStep <Hz|0>: fixed simulation step (FApp), 0 = off."));
			JamDebugSystemCommand(TEXT("jam.Debug.Gravity"), TEXT("World.GravityScale"), TEXT("jam.Debug.Gravity <scale>: every zone's gravity times this (0 = zero-G everywhere)."));
			JamDebugSystemCommand(TEXT("jam.Debug.TimeScale"), TEXT("System.TimeScale"), TEXT("jam.Debug.TimeScale <0.1..2>: game speed."));
		}
	};
	FJamDebugSystemCommands GJamDebugSystemCommands;

	FAutoConsoleCommandWithWorldAndArgs GJamDebugHitch(TEXT("jam.Debug.Hitch"),
		TEXT("jam.Debug.Hitch <ms> <s>: a frame of <ms> every <s> seconds of real time (0 ms = off)."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
		{
			UJamDebugSubsystem* Debug = JamDebugFor(World);
			if (Debug && !Args.IsEmpty())
			{
				JamDebugSetLogged(Debug, TEXT("System.HitchMs"), Args[0]);
				if (Args.Num() > 1)
				{
					JamDebugSetLogged(Debug, TEXT("System.HitchEverySeconds"), Args[1]);
				}
			}
		}));

	FAutoConsoleCommandWithWorldAndArgs GJamDebugPause(TEXT("jam.Debug.Pause"),
		TEXT("jam.Debug.Pause [0|1|step]: pause, resume, or one frame while paused."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
		{
			if (UJamDebugSubsystem* Debug = JamDebugFor(World))
			{
				if (!Args.IsEmpty() && Args[0] == TEXT("step"))
				{
					Debug->StepFrame();
				}
				else
				{
					Debug->SetPaused(Args.IsEmpty() ? !Debug->IsPaused() : Args[0] == TEXT("1"));
				}
			}
		}));
}
