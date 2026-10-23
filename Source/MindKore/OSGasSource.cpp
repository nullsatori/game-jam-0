#include "OSGasSource.h"

#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "GameFramework/PlayerController.h"
#include "HAL/IConsoleManager.h"
#include "MindKore.h"
#include "OSGravity.h"

FOSGasPresetDef OSGasPreset(EOSGasPreset Preset)
{
	FOSGasPresetDef D;
	FOSGasRecipe& R = D.Recipe;
	FOSGasLook& L = D.Look;
	switch (Preset)
	{
	case EOSGasPreset::Muzzle:
		// As a gun's muzzle (UOSMuzzleFxComponent's jet): a quick blast with a ring.
		R.SpeedCm = 350.0f;
		R.ConeDeg = 8.0f;
		R.RingCm = 8.0f;
		R.RingTurnsPerSecond = 2.0f;
		R.RadiusMinCm = 2.0f;
		R.RadiusMaxCm = 4.0f;
		R.Heat = 1.0f;
		R.bInstant = true;
		R.LifeSeconds = 6.0f;
		D.Count = 60;
		break;
	case EOSGasPreset::Port:
		// A vent hole's small puff, stopped by the air within ~15 cm.
		R.SpeedCm = 300.0f;
		R.SpeedMin = 0.5f;
		R.ConeDeg = 20.0f;
		R.DragSeconds = 0.05f;
		R.RadiusMinCm = 1.5f;
		R.RadiusMaxCm = 3.0f;
		R.MaxRadiusCm = 6.0f;
		R.Heat = 0.6f;
		R.LifeSeconds = 2.5f;
		D.Count = 12;
		break;
	case EOSGasPreset::Weld:
		// Welding fumes (as UOSHullToolsComponent's): thin, warm, rising.
		R.SpeedCm = 40.0f;
		R.SpeedMin = 0.5f;
		R.ConeDeg = 20.0f;
		R.RadiusMinCm = 1.0f;
		R.RadiusMaxCm = 2.0f;
		R.Mass = 0.35f;
		R.Heat = 1.0f;
		R.LifeSeconds = 2.5f;
		L.Colour = FLinearColor(0.62f, 0.66f, 0.72f);
		L.WarmColour = FLinearColor(1.0f, 0.75f, 0.45f);
		L.Extinction = 0.02f;
		L.LifeSeconds = 4.0f;
		D.Mode = EOSGasMode::Continuous;
		D.Rate = 25.0f;
		break;
	case EOSGasPreset::Breach:
		// Air leaving through a hole along the component's X: a straight jet (in air the room's air also drawn to it).
		R.SpeedCm = 450.0f;
		R.SpeedMin = 0.6f;
		R.ConeDeg = 10.0f;
		R.RadiusMinCm = 2.0f;
		R.RadiusMaxCm = 3.0f;
		R.Mass = 0.5f;
		R.Heat = 0.0f;
		R.LifeSeconds = 2.0f;
		L.Colour = FLinearColor(0.70f, 0.74f, 0.80f);
		L.bEmbers = false;
		L.Extinction = 0.025f;
		L.LifeSeconds = 6.0f;
		D.Mode = EOSGasMode::Continuous;
		D.Rate = 60.0f;
		break;
	case EOSGasPreset::Rcs:
		// A thruster's short cold cone: fast, narrow, stopped within half a metre, gone in a second.
		R.SpeedCm = 600.0f;
		R.SpeedMin = 0.6f;
		R.ConeDeg = 6.0f;
		R.DragSeconds = 0.12f;
		R.RadiusMinCm = 1.0f;
		R.RadiusMaxCm = 2.0f;
		R.MaxRadiusCm = 6.0f;
		R.Mass = 0.6f;
		R.Heat = 0.0f;
		R.LifeSeconds = 1.2f;
		L.Colour = FLinearColor(0.78f, 0.82f, 0.88f);
		L.bEmbers = false;
		L.LifeSeconds = 3.0f;
		D.Count = 20;
		break;
	case EOSGasPreset::Dust:
		// Dust: a slow wide cloud of heavy grains - it settles in gravity, hangs in zero-G.
		R.SpeedCm = 60.0f;
		R.SpeedMin = 0.2f;
		R.ConeDeg = 70.0f;
		R.RadiusMinCm = 3.0f;
		R.RadiusMaxCm = 5.0f;
		R.MaxRadiusCm = 10.0f;
		R.Mass = 0.8f;
		R.Heat = 0.0f;
		R.FallCmPerS2 = 90.0f;
		R.LifeSeconds = 5.0f;
		L.Colour = FLinearColor(0.55f, 0.50f, 0.44f);
		L.bEmbers = false;
		L.Extinction = 0.035f;
		L.LifeSeconds = 6.0f;
		D.Count = 40;
		break;
	default:
		break;
	}
	return D;
}

EOSGasPreset OSGasPresetByName(const FString& Name)
{
	static const TPair<const TCHAR*, EOSGasPreset> Names[] = { { TEXT("muzzle"), EOSGasPreset::Muzzle }, { TEXT("port"), EOSGasPreset::Port },
		{ TEXT("weld"), EOSGasPreset::Weld }, { TEXT("breach"), EOSGasPreset::Breach }, { TEXT("rcs"), EOSGasPreset::Rcs }, { TEXT("dust"), EOSGasPreset::Dust } };
	for (const TPair<const TCHAR*, EOSGasPreset>& Each : Names)
	{
		if (Name.Equals(Each.Key, ESearchCase::IgnoreCase))
		{
			return Each.Value;
		}
	}
	return EOSGasPreset::Custom;
}

UOSGasSourceComponent::UOSGasSourceComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.bStartWithTickEnabled = true;
}

void UOSGasSourceComponent::ApplyPreset(EOSGasPreset NewPreset)
{
	Preset = NewPreset;
	if (Preset == EOSGasPreset::Custom)
	{
		return;
	}
	const FOSGasPresetDef D = OSGasPreset(Preset);
	Recipe = D.Recipe;
	Look = D.Look;
	Mode = D.Mode;
	Rate = D.Rate > 0.0f ? D.Rate : Rate;
	Count = D.Count > 0 ? D.Count : Count;
}

void UOSGasSourceComponent::BeginPlay()
{
	Super::BeginPlay();
	LastLocation = GetComponentLocation();
	if (bAutoStart)
	{
		Mode == EOSGasMode::Burst ? Burst() : SetRate(Rate);
	}
}

#if WITH_EDITOR
void UOSGasSourceComponent::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);
	// The preset fills the fields once, when picked; a designer's later tweaks stand.
	if (PropertyChangedEvent.GetPropertyName() == GET_MEMBER_NAME_CHECKED(UOSGasSourceComponent, Preset))
	{
		ApplyPreset(Preset);
	}
}
#endif

void UOSGasSourceComponent::Burst()
{
	Let(Count, false);
}

void UOSGasSourceComponent::SetRate(float PerSecond)
{
	Rate = FMath::Max(PerSecond, 0.0f);
	bFlowing = Rate > 0.0f;
}

void UOSGasSourceComponent::Stop()
{
	bFlowing = false;
	Carry = 0.0f;
}

FVector UOSGasSourceComponent::PointOnShape() const
{
	FVector Local = FVector::ZeroVector;
	switch (Shape)
	{
	case EOSGasShape::Segment:
		Local.X = FMath::FRandRange(0.0f, Size.X);
		break;
	case EOSGasShape::Ring:
	{
		const float A = FMath::FRandRange(0.0f, UE_TWO_PI);
		Local = FVector(0.0f, FMath::Cos(A), FMath::Sin(A)) * Size.Y;
		break;
	}
	case EOSGasShape::Disc:
	{
		const float A = FMath::FRandRange(0.0f, UE_TWO_PI);
		Local = FVector(0.0f, FMath::Cos(A), FMath::Sin(A)) * (Size.Y * FMath::Sqrt(FMath::FRand()));
		break;
	}
	case EOSGasShape::Box:
		Local = FVector(FMath::FRandRange(-Size.X, Size.X), FMath::FRandRange(-Size.Y, Size.Y), FMath::FRandRange(-Size.Z, Size.Z));
		break;
	default:
		break;
	}
	return GetComponentTransform().TransformPosition(Local);
}

void UOSGasSourceComponent::Let(int32 Number, bool bContinuous)
{
	UOSGasSubsystem* Gas = UOSGasSubsystem::Get(this);
	if (!Gas || Number <= 0)
	{
		return;
	}
	const FVector Direction = GetForwardVector();
	const float Dt = GetWorld()->GetDeltaSeconds();
	const FVector Carrier = bCarried && Dt > 0.0f ? (GetComponentLocation() - LastLocation) / Dt : FVector::ZeroVector;
	// A point lets them out together (one emit); a shape a parcel at a time at its own point.
	if (Shape == EOSGasShape::Point)
	{
		Gas->EmitForSource(Recipe, Look, GetComponentLocation(), Direction, Carrier, Number, bContinuous, Cloud, CloudSerial);
		return;
	}
	for (int32 Index = 0; Index < Number; ++Index)
	{
		if (Gas->EmitForSource(Recipe, Look, PointOnShape(), Direction, Carrier, 1, bContinuous, Cloud, CloudSerial) == 0)
		{
			break;
		}
	}
}

void UOSGasSourceComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
	if (bFlowing)
	{
		Carry += Rate * DeltaTime;
		const int32 Number = FMath::FloorToInt(Carry);
		if (Number > 0)
		{
			Carry -= Number;
			Let(Number, true);
		}
		// A breach in air also draws the room's air to the hole (UOSGasSubsystem::AddAirFlow, refreshed every 0.5 s).
		if (Preset == EOSGasPreset::Breach && (BreachFlowAge -= DeltaTime) <= 0.0f && OSAirZoneAt(GetWorld(), GetComponentLocation()))
		{
			BreachFlowAge = 0.5f;
			if (UOSGasSubsystem* Gas = UOSGasSubsystem::Get(this))
			{
				Gas->AddAirFlow(GetComponentLocation(), GetForwardVector(), 150.0f, 200.0f, 0.6f);
			}
		}
	}
	LastLocation = GetComponentLocation();
}

#if !UE_BUILD_SHIPPING
namespace
{
	TAutoConsoleVariable<float> CVarGasSpawnSeconds(TEXT("os.Gas.SpawnSeconds"), 4.0f, TEXT("How long an os.Gas.Spawn source lives, s (4)."));

	/** os.Gas.Spawn <preset> [x y z]: a source of a preset at a point (1.5 m along the view by default), os.Gas.SpawnSeconds. */
	FAutoConsoleCommandWithWorldAndArgs GGasSpawn(
		TEXT("os.Gas.Spawn"),
		TEXT("<muzzle|port|weld|breach|rcs|dust|list> [x y z]: a gas source of the preset at the point (1.5 m along the view by default) for 4 s, along the view."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
		{
			if (Args.IsEmpty() || Args[0].Equals(TEXT("list"), ESearchCase::IgnoreCase))
			{
				UE_LOG(LogMindKore, Log, TEXT("os.Gas.Spawn presets: muzzle (burst 60, ring), port (burst 12, a puff), weld (25/s, warm, rising), breach (60/s, a jet; in air a draw to the hole), rcs (burst 20, cold, short), dust (burst 40, heavy, settles)"));
				return;
			}
			const EOSGasPreset Preset = OSGasPresetByName(Args[0]);
			if (Preset == EOSGasPreset::Custom)
			{
				UE_LOG(LogMindKore, Warning, TEXT("os.Gas.Spawn: no preset '%s' (os.Gas.Spawn list)"), *Args[0]);
				return;
			}
			FVector Eye = FVector::ZeroVector;
			FRotator View = FRotator::ZeroRotator;
			if (const APlayerController* Controller = World->GetFirstPlayerController())
			{
				Controller->GetPlayerViewPoint(Eye, View);
			}
			FVector Where = Eye + View.Vector() * 150.0f;
			if (Args.Num() >= 4)
			{
				Where = FVector(FCString::Atof(*Args[1]), FCString::Atof(*Args[2]), FCString::Atof(*Args[3]));
			}
			FActorSpawnParameters Params;
			Params.ObjectFlags |= RF_Transient;
			AActor* Holder = World->SpawnActor<AActor>(AActor::StaticClass(), FTransform(View, Where), Params);
			if (!Holder)
			{
				return;
			}
			UOSGasSourceComponent* Source = NewObject<UOSGasSourceComponent>(Holder);
			Source->ApplyPreset(Preset);
			Holder->SetRootComponent(Source);
			Source->SetWorldTransform(FTransform(View, Where));
			Source->RegisterComponent();
			// os.Gas.SpawnSeconds: how long (a flow's whole run; a burst is let out at once anyway).
			Holder->SetLifeSpan(FMath::Max(IConsoleManager::Get().FindConsoleVariable(TEXT("os.Gas.SpawnSeconds"))->GetFloat(), 0.1f));
			UE_LOG(LogMindKore, Log, TEXT("os.Gas.Spawn: %s at %s"), *Args[0], *Where.ToCompactString());
		}));
}
#endif
