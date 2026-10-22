#include "OSFlyerAudio.h"

#include "Components/AudioComponent.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "Sound/SoundBase.h"
#include "Sound/SoundSubmix.h"

bool FOSSoundPlay::IsPlaying() const
{
	for (const TWeakObjectPtr<UAudioComponent>& Each : Components)
	{
		if (Each.IsValid() && Each->IsPlaying())
		{
			return true;
		}
	}
	return false;
}

void FOSSoundPlay::SetPitch(float NewPitch)
{
	for (const TWeakObjectPtr<UAudioComponent>& Each : Components)
	{
		if (Each.IsValid())
		{
			Each->SetPitchMultiplier(NewPitch);
		}
	}
}

void FOSSoundPlay::Stop(float FadeSeconds)
{
	for (const TWeakObjectPtr<UAudioComponent>& Each : Components)
	{
		if (Each.IsValid())
		{
			Each->FadeOut(FadeSeconds, 0.0f);
		}
	}
}

FString FOSSoundPlay::Describe() const
{
	return FString::Printf(TEXT("%d layer(s), pitch %.2f, low-pass %.0f Hz, start +%.3f s, %s%s"), Components.Num(), Pitch, LowPassHz,
		StartSeconds, bAir ? TEXT("air (reverb send, no suit tail)") : TEXT("vacuum (suit tail)"), bOccluded ? TEXT(", occluded") : TEXT(""));
}

namespace
{
	constexpr float OccludedLowPassHz = 900.0f;
	constexpr float OccludedVolume = 0.35f;

	void ApplyOcclusion(UAudioComponent& Component, bool bOccluded, float OpenLowPassHz, float Seconds)
	{
		Component.SetLowPassFilterFrequency(bOccluded ? FMath::Min(OpenLowPassHz, OccludedLowPassHz) : OpenLowPassHz);
		Component.AdjustVolume(Seconds, bOccluded ? OccludedVolume : 1.0f);
	}
}

bool OSSoundOccluded(const UWorld* World, const FVector& Source, const AActor* SourceActor)
{
	const APlayerController* Controller = World ? World->GetFirstPlayerController() : nullptr;
	if (!Controller)
	{
		return false;
	}
	FVector Ear, Front, Right;
	Controller->GetAudioListenerPosition(Ear, Front, Right);
	FCollisionQueryParams Params(SCENE_QUERY_STAT(OSSoundOcclusion), /*bTraceComplex=*/false, SourceActor);
	if (APawn* Pawn = Controller->GetPawn())
	{
		Params.AddIgnoredActor(Pawn);
		TArray<AActor*> Carried;
		Pawn->GetAttachedActors(Carried, true, true);
		Params.AddIgnoredActors(Carried);
	}
	return World->LineTraceTestByChannel(Ear, Source, ECC_Visibility, Params);
}

void FOSSoundPlay::UpdateOcclusion(const AActor* Source)
{
	if (!Source || !IsPlaying())
	{
		return;
	}
	const bool bNow = OSSoundOccluded(Source->GetWorld(), Source->GetActorLocation(), Source);
	if (bNow == bOccluded)
	{
		return;
	}
	bOccluded = bNow;
	for (const TWeakObjectPtr<UAudioComponent>& Each : Components)
	{
		if (Each.IsValid())
		{
			ApplyOcclusion(*Each, bOccluded, LowPassHz, 0.15f);
		}
	}
}

FOSSoundPlay OSPlaySoundEvent(AActor* Owner, const FOSSoundEvent& Event, const FVector& Location, USceneComponent* AttachTo,
	const FSoundAttenuationSettings& Attenuation, bool bAir, USoundSubmixBase* AirReverb, float VolumeScale, float PitchScale,
	float LowPassScale)
{
	FOSSoundPlay Play;
	if (!Owner)
	{
		return Play;
	}
	Play.bAir = bAir;
	Play.Pitch = FMath::FRandRange(Event.Pitch.X, Event.Pitch.Y) * PitchScale;
	Play.StartSeconds = FMath::FRandRange(0.0f, Event.StartJitterSeconds);
	const FVector2D Range = bAir ? Event.LowPassAirHz : Event.LowPassVacuumHz;
	Play.LowPassHz = FMath::Clamp(FMath::FRandRange(Range.X, Range.Y) * LowPassScale, 200.0f, 20000.0f);
	Play.bOccluded = OSSoundOccluded(Owner->GetWorld(), AttachTo ? AttachTo->GetComponentLocation() : Location, Owner);
	for (const FOSSoundLayer& Layer : Event.Layers)
	{
		if (!Layer.Sound || (Layer.bVacuumOnly && bAir))
		{
			continue;
		}
		UAudioComponent* Component = NewObject<UAudioComponent>(Owner);
		Component->bAutoActivate = false;
		Component->bAutoDestroy = true;
		Component->bAllowSpatialization = true;
		Component->bOverrideAttenuation = true;
		Component->AttenuationOverrides = Attenuation;
		Component->SetSound(Layer.Sound);
		Component->SetVolumeMultiplier(Event.Volume * VolumeScale
			* FMath::Pow(10.0f, (Layer.GainDb + FMath::FRandRange(-Layer.GainJitterDb, Layer.GainJitterDb)) / 20.0f));
		Component->SetPitchMultiplier(Play.Pitch);
		Component->SetLowPassFilterEnabled(true);
		Component->SetLowPassFilterFrequency(Play.LowPassHz);
		if (AttachTo)
		{
			Component->SetupAttachment(AttachTo);
		}
		else
		{
			Component->SetUsingAbsoluteLocation(true);
			Component->SetWorldLocation(Location);
		}
		Component->RegisterComponent();
		if (bAir && AirReverb)
		{
			Component->SetSubmixSend(AirReverb, Event.AirReverbSend);
		}
		Component->Play(Play.StartSeconds);
		if (Play.bOccluded)
		{
			ApplyOcclusion(*Component, true, Play.LowPassHz, 0.0f);
		}
		Play.Components.Add(Component);
	}
	return Play;
}

FSoundAttenuationSettings OSFlyerAttenuation(float FalloffCm, float AbsorptionStartCm)
{
	FSoundAttenuationSettings S;
	S.bAttenuate = true;
	S.bSpatialize = true;
	// Natural falloff (-60 dB at the end), from a 2 m sphere of full volume.
	S.DistanceAlgorithm = EAttenuationDistanceModel::NaturalSound;
	S.dBAttenuationAtMax = -60.0f;
	S.AttenuationShape = EAttenuationShape::Sphere;
	S.AttenuationShapeExtents = FVector(200.0f, 0.0f, 0.0f);
	S.FalloffDistance = FalloffCm;
	// Air absorption: the highs go first with distance.
	S.bAttenuateWithLPF = true;
	S.LPFRadiusMin = AbsorptionStartCm;
	S.LPFRadiusMax = FalloffCm;
	S.LPFFrequencyAtMin = 20000.0f;
	S.LPFFrequencyAtMax = 1500.0f;
	// A close pass is felt all around: inside 4 m the source blends to omni, wholly at 1.5 m.
	S.NonSpatializedRadiusStart = 400.0f;
	S.NonSpatializedRadiusEnd = 150.0f;
	S.NonSpatializedRadiusMode = ENonSpatializedRadiusSpeakerMapMode::OmniDirectional;
	// Occlusion is OSSoundOccluded's, not the engine's: its trace starts at the listener, inside the
	// player's own body and gun, and muffled every flyer sound with a clear line (os.Flyer.MixCheck:
	// -12 dB at 10 m, on the Visibility and on the Camera channel alike).
	S.bEnableOcclusion = false;
	return S;
}
