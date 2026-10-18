#include "JamFlyerAudio.h"

#include "Components/AudioComponent.h"
#include "GameFramework/Actor.h"
#include "Sound/SoundBase.h"
#include "Sound/SoundSubmix.h"

bool FJamSoundPlay::IsPlaying() const
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

void FJamSoundPlay::SetPitch(float NewPitch)
{
	for (const TWeakObjectPtr<UAudioComponent>& Each : Components)
	{
		if (Each.IsValid())
		{
			Each->SetPitchMultiplier(NewPitch);
		}
	}
}

void FJamSoundPlay::Stop(float FadeSeconds)
{
	for (const TWeakObjectPtr<UAudioComponent>& Each : Components)
	{
		if (Each.IsValid())
		{
			Each->FadeOut(FadeSeconds, 0.0f);
		}
	}
}

FString FJamSoundPlay::Describe() const
{
	return FString::Printf(TEXT("%d layer(s), pitch %.2f, low-pass %.0f Hz, start +%.3f s, %s"), Components.Num(), Pitch, LowPassHz,
		StartSeconds, bAir ? TEXT("air (reverb send, no suit tail)") : TEXT("vacuum (suit tail)"));
}

FJamSoundPlay JamPlaySoundEvent(AActor* Owner, const FJamSoundEvent& Event, const FVector& Location, USceneComponent* AttachTo,
	const FSoundAttenuationSettings& Attenuation, bool bAir, USoundSubmixBase* AirReverb, float VolumeScale, float PitchScale,
	float LowPassScale)
{
	FJamSoundPlay Play;
	if (!Owner)
	{
		return Play;
	}
	Play.bAir = bAir;
	Play.Pitch = FMath::FRandRange(Event.Pitch.X, Event.Pitch.Y) * PitchScale;
	Play.StartSeconds = FMath::FRandRange(0.0f, Event.StartJitterSeconds);
	const FVector2D Range = bAir ? Event.LowPassAirHz : Event.LowPassVacuumHz;
	Play.LowPassHz = FMath::Clamp(FMath::FRandRange(Range.X, Range.Y) * LowPassScale, 200.0f, 20000.0f);
	for (const FJamSoundLayer& Layer : Event.Layers)
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
		Play.Components.Add(Component);
	}
	return Play;
}

FSoundAttenuationSettings JamFlyerAttenuation(float FalloffCm, float AbsorptionStartCm)
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
	// Behind the planetoid or the hull: dull and quieter.
	S.bEnableOcclusion = true;
	S.OcclusionTraceChannel = ECC_Visibility;
	S.OcclusionLowPassFilterFrequency = 900.0f;
	S.OcclusionVolumeAttenuation = 0.35f;
	S.OcclusionInterpolationTime = 0.15f;
	return S;
}
