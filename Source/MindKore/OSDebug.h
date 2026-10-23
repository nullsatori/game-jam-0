#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "OSDebug.generated.h"

class AActor;
class SOSDebugMenu;
class UWorld;
struct FActorsInitializedParams;

// The debug menu (Docs/DebugMenu.md): one F6 panel over the game, its console
// commands (os.Debug.*) and test configs. UOSDebugSubsystem is the only
// writer of the values; SOSDebugMenu (OSDebugMenu.cpp) only draws them.

/** The System tab (keys System.<property>); UOSDebugSubsystem::ApplySettings puts them into the engine. */
USTRUCT()
struct FOSDebugSystem
{
	GENERATED_BODY()

	/** Frame-rate cap, FPS; 0 = none. UGameUserSettings (kept between runs) and t.MaxFPS. */
	UPROPERTY(EditAnywhere, Category = "System", meta = (ClampMin = "0.0", ClampMax = "1000.0", UIMax = "240.0"))
	float FpsLimit = 0.0f;

	/** r.VSync, kept in UGameUserSettings. */
	UPROPERTY(EditAnywhere, Category = "System")
	bool Vsync = false;

	/** Fixed simulation step, Hz; 0 = off. Each frame advances the game 1/FixedStep s: for checks and repeats, not for play. */
	UPROPERTY(EditAnywhere, Category = "System", meta = (ClampMin = "0.0", ClampMax = "1000.0", UIMax = "240.0"))
	float FixedStep = 0.0f;

	/** Game speed on top of everything else (the bullet time's dilation multiplies it). */
	UPROPERTY(EditAnywhere, Category = "System", meta = (ClampMin = "0.1", ClampMax = "2.0"))
	float TimeScale = 1.0f;

	/** A long frame of this many ms... */
	UPROPERTY(EditAnywhere, Category = "System", meta = (ClampMin = "0.0", ClampMax = "2000.0", UIMax = "500.0"))
	float HitchMs = 0.0f;

	/** ...every this many seconds of real time. */
	UPROPERTY(EditAnywhere, Category = "System", meta = (ClampMin = "0.1", ClampMax = "60.0", UIMax = "10.0"))
	float HitchEverySeconds = 2.0f;
};

/** World multipliers (keys World.<property>). */
USTRUCT()
struct FOSDebugWorld
{
	GENERATED_BODY()

	/** Every zone's gravity times this (OSGravityAt): 0 is zero-G everywhere. */
	UPROPERTY(EditAnywhere, Category = "World", meta = (ClampMin = "0.0", ClampMax = "3.0"))
	float GravityScale = 1.0f;
};

/** A built-in test config: the JSON of an export (Docs/DebugMenu.md, «Формат»). Lives in /Game/OS/Debug/Presets. */
UCLASS(BlueprintType)
class MINDKORE_API UOSTestPreset : public UDataAsset
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere, Category = "OS|Debug", meta = (MultiLine = true))
	FString Json;
};

/** Where a parameter's value lives: an object, or a struct inside it (a gun of the list). */
struct FOSDebugTarget
{
	UObject* Object = nullptr;
	void* Container = nullptr;
	const UStruct* Struct = nullptr;
};

/** One parameter as the menu lists it. */
struct FOSDebugParam
{
	FString Key;
	FString Owner;
	FString Group;
	FString Tooltip;
	FString Units;
	const FProperty* Property = nullptr;
	TOptional<double> Min, Max, SliderMin, SliderMax;
};

/** What an import did. */
struct FOSDebugImport
{
	int32 Applied = 0;
	/** "<key>: <why>" for each unknown key or value of the wrong type. */
	TArray<FString> Skipped;
	/** Another build, an unreadable file. */
	FString Warning;
	bool bOk = false;
};

/** A preset the menu lists: an asset or a JSON file in Saved/OSDebug/Presets. */
struct FOSDebugPreset
{
	FString Name;
	FString Source;
	bool bAsset = false;
};

enum class EOSDebugMode : uint8
{
	Closed,
	Open,
	Pinned,
};

UCLASS()
class MINDKORE_API UOSDebugSubsystem : public UGameInstanceSubsystem
{
	GENERATED_BODY()

public:
	static UOSDebugSubsystem* Get(const UObject* WorldContext);

	virtual bool ShouldCreateSubsystem(UObject* Outer) const override;
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

	// --- parameters ---------------------------------------------------------------------

	/** Every parameter there is now: System, World, the pawn and its components, guns, flyers, habitats, zones. */
	void ListParams(TArray<FOSDebugParam>& Out) const;
	/** The value as UE property text; false: no such key. */
	bool GetValue(const FString& Key, FString& Out) const;
	/** How many live owners Key writes to now (flyers: 0 is fine, they get it as they spawn); -1: no such key. */
	int32 GetLiveCount(const FString& Key) const;
	/** A numeric parameter's value; false: not numeric or no such key. */
	bool GetNumber(const FString& Key, double& Out) const;
	/**
	 * Write Value (UE property text) to every live owner of Key; flyers also get it as they spawn.
	 * bRecord: one undo step, Before overriding the "before" (a drag's start). False and OutError: refused.
	 */
	bool SetValue(const FString& Key, const FString& Value, FString* OutError = nullptr, bool bRecord = true, const FString* Before = nullptr);
	bool IsChanged(const FString& Key) const { return Values.Contains(Key); }
	/** The value before the first edit this session; null: never edited. */
	const FString* GetOriginal(const FString& Key) const { return Originals.Find(Key); }
	int32 GetChangedCount() const { return Values.Num(); }
	const TMap<FString, FString>& GetChanged() const { return Values; }

	/** Back to the original: a key, every key of an owner ("Pawn.Oxygen", "Weapon[Дробовик]"), or everything ("all"). One undo step. */
	int32 Reset(const FString& KeyOrOwner);
	bool Undo();
	bool Redo();
	bool CanUndo() const { return !UndoSteps.IsEmpty(); }
	bool CanRedo() const { return !RedoSteps.IsEmpty(); }

	// --- pins ---------------------------------------------------------------------------

	const TArray<FString>& GetPins() const { return Pins; }
	bool IsPinned(const FString& Key) const { return Pins.Contains(Key); }
	void SetPinned(const FString& Key, bool bPinned);
	void RestoreDefaultPins();
	static const TArray<FString>& GetDefaultPins();

	// --- configs ------------------------------------------------------------------------

	FString ExportJson() const;
	/** Saved/OSDebug/Presets/<Name>.json, and the clipboard unless unattended. Names the config. Returns the file. */
	FString ExportToFile(const FString& Name);
	/** A file (path, or a name in Saved/OSDebug/Presets), "clipboard", or an asset/JSON preset's name. bReplace: reset everything first. */
	FOSDebugImport Import(const FString& Source, bool bReplace = true);
	FOSDebugImport ImportJson(const FString& Json, bool bReplace = true);
	/** Editor builds: /Game/OS/Debug/Presets/DA_Test_<Name> (default: the config's name). A refused save is an error. Elsewhere: an export, saying why. */
	bool SaveToAsset(const FString& Name, FString& OutMessage);
	void ListPresets(TArray<FOSDebugPreset>& Out) const;
	/** Changes in Saved/OSDebug/Last.json from the last session (0: none). Never applied by itself. */
	int32 GetLastSessionChanges() const { return LastSessionChanges; }
	FOSDebugImport RestoreLastSession();
	const FString& GetConfigName() const { return ConfigName; }
	void SetConfigName(const FString& Name) { ConfigName = Name; }
	/** The checkout's commit (.git), "?" without one (a packaged build). */
	static const FString& GetBuildHash();

	// --- system -------------------------------------------------------------------------

	const FOSDebugSystem& GetSystem() const { return System; }
	const FOSDebugWorld& GetWorldConfig() const { return WorldConfig; }
	void SetPaused(bool bPause);
	bool IsPaused() const;
	/** Paused: run one frame, then pause again. */
	void StepFrame();

	/**
	 * Gives the player a gun on any map (owner 23.10: «на этой карте в дебаг меню нет выдачи пушек»): "pistol",
	 * "shotgun" (the pump gun back in slot 2), "sv98", "rmb93" (the Рысь in slot 2, as its pickup does), "all"
	 * (pistol, slot 2 as it is, SV-98). Unlocked and drawn. False: no player with guns, or an unknown name.
	 */
	bool GiveGun(const FString& Which);

	/** A frame as measured at its end: real seconds since the last, and the game's dt. */
	struct FFrameSample
	{
		double At = 0.0;
		float RealSeconds = 0.0f;
		float DeltaSeconds = 0.0f;
	};
	/** The last 5 s of frames, oldest first. */
	const TArray<FFrameSample>& GetFrames() const { return Frames; }
	/** Real frames per second and mean real frame ms over the last Seconds. */
	void Measure(float Seconds, float& OutFps, float& OutMs, float& OutMaxMs) const;

	// --- the panel ----------------------------------------------------------------------

	EOSDebugMode GetMode() const { return Mode; }
	void SetMode(EOSDebugMode NewMode);
	/** Bumped when the set of parameters changes (a new map): the panel rebuilds its list. */
	int32 GetGeneration() const { return Generation; }
	/** The panel's status line: the last action's result. */
	FString Status;
	bool bStatusError = false;
	TArray<FString> LastSkipped;
	void Say(const FString& Message, bool bError = false);
	TSharedPtr<SOSDebugMenu> GetMenu() const { return Menu; }

	/** F6 and friends, from the Slate input preprocessor: true when used. */
	bool HandleKey(const struct FKeyEvent& Event);

private:
	struct FChange
	{
		FString Key;
		FString Before;
		FString After;
	};
	using FStep = TArray<FChange>;

	/** Resolve a key to its property, the live targets (only inside OnlyActor when given) and a read-only schema target. */
	bool Resolve(const FString& Key, AActor* OnlyActor, const FProperty*& OutProperty, TArray<FOSDebugTarget>& OutLive, FOSDebugTarget& OutSchema, FString* OutError) const;
	bool ResolveOwner(const FString& Owner, AActor* OnlyActor, TArray<FOSDebugTarget>& OutLive, FOSDebugTarget& OutSchema) const;
	void ListOwners(TArray<FString>& Out) const;
	bool Write(const FString& Key, const FString& Canonical, FString* OutError);
	void Record(FStep&& Step);
	void ApplySettings();
	void Autosave();
	void LoadPins();
	void SavePins() const;
	void EnsureWidgets();
	UWorld* GetGameWorld() const;

	void OnWorldInitializedActors(const FActorsInitializedParams& Params);
	void OnActorPreSpawn(AActor* Actor);
	void OnBeginFrame();
	void OnEndFrame();

	UPROPERTY()
	FOSDebugSystem System;

	UPROPERTY()
	FOSDebugWorld WorldConfig;

	/** The engine's settings when the session began: PIE puts them back at its end (the editor keeps running). */
	FOSDebugSystem InitialSystem;

	/** Key -> the value written (canonical text); only keys that differ from their original. */
	TMap<FString, FString> Values;
	/** Key -> the value before the first edit (in this map, for map actors). */
	TMap<FString, FString> Originals;
	TArray<FStep> UndoSteps;
	TArray<FStep> RedoSteps;
	TArray<FString> Pins;
	FString ConfigName;
	int32 LastSessionChanges = 0;
	bool bAutosaveArmed = false;
	FString PendingConfigFile;

	EOSDebugMode Mode = EOSDebugMode::Closed;
	int32 Generation = 0;
	TSharedPtr<SOSDebugMenu> Menu;
	TSharedPtr<class IInputProcessor> Input;
	TWeakObjectPtr<UWorld> HookedWorld;
	FDelegateHandle PreSpawnHandle;
	FDelegateHandle WorldInitHandle;
	FDelegateHandle BeginFrameHandle;
	FDelegateHandle EndFrameHandle;

	// What ApplySettings last put into the engine, so it touches only what changed.
	float AppliedFpsLimit = -1.0f;
	int32 AppliedVsync = -1;
	float AppliedFixedStep = -1.0f;
	bool bCommandLineFixedStep = false;

	TArray<FFrameSample> Frames;
	double LastEndFrame = 0.0;
	double NextHitch = 0.0;
	double LastPace = 0.0;
	int32 StepFramesLeft = 0;
};
