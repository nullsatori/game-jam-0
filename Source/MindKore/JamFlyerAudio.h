#pragma once

#include "CoreMinimal.h"
#include "Sound/SoundAttenuation.h"
#include "JamFlyerAudio.generated.h"

class AActor;
class UAudioComponent;
class USceneComponent;
class USoundBase;
class USoundSubmixBase;

/** One stem of a layered sound: its wave, its level in the mix and how much that level wanders per play. */
USTRUCT(BlueprintType)
struct FJamSoundLayer
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Jam|Audio")
	TObjectPtr<USoundBase> Sound = nullptr;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Jam|Audio")
	float GainDb = 0.0f;

	/** Each play draws the gain from GainDb +- this, dB. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Jam|Audio", meta = (ClampMin = "0.0"))
	float GainJitterDb = 1.5f;

	/** The suit's reverb tail: heard only in vacuum (in air the zone's reverb send replaces it). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Jam|Audio")
	bool bVacuumOnly = false;
};

/**
 * A layered sound event (Scripts/jam_flyer_sounds.py renders the stems: Attack, Body,
 * Character, Sub, Tail). Every play draws one pitch, one start offset and one low-pass
 * cutoff for all its layers and a gain per layer, so no two plays are the same.
 */
USTRUCT(BlueprintType)
struct FJamSoundEvent
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Jam|Audio")
	TArray<FJamSoundLayer> Layers;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Jam|Audio")
	float Volume = 1.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Jam|Audio")
	FVector2D Pitch = FVector2D(0.95f, 1.05f);

	/** All layers start this far into their waves, random from 0, s. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Jam|Audio", meta = (ClampMin = "0.0"))
	float StartJitterSeconds = 0.015f;

	/** Low-pass cutoff per play when the listener is in vacuum (heard through the suit), Hz. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Jam|Audio")
	FVector2D LowPassVacuumHz = FVector2D(3500.0f, 5000.0f);

	/** ... in air, Hz. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Jam|Audio")
	FVector2D LowPassAirHz = FVector2D(9000.0f, 14000.0f);

	/** In air, the send level of every layer to the air reverb submix. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Jam|Audio", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float AirReverbSend = 0.3f;
};

/** How one play came out (for the log and for Doppler updates). */
struct FJamSoundPlay
{
	TArray<TWeakObjectPtr<UAudioComponent>> Components;
	float Pitch = 1.0f;
	float LowPassHz = 0.0f;
	float StartSeconds = 0.0f;
	bool bAir = false;
	/** Rock or hull between it and the player (JamSoundOccluded): 900 Hz and x0.35 while so. */
	bool bOccluded = false;

	bool IsPlaying() const;
	/** For a sound that moves (attached): re-check the line to the player, ease into or out of occlusion. */
	void UpdateOcclusion(const AActor* Source);
	void SetPitch(float NewPitch);
	void Stop(float FadeSeconds);
	FString Describe() const;
};

/** Settings shared by the flyer's sounds; editable on AJamFlyer. */
USTRUCT(BlueprintType)
struct FJamFlyerMix
{
	GENERATED_BODY()

	/** Screeches (aggro, dive) heard at once; a new one takes the place of the quietest (farthest) or is dropped. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Jam|Audio", meta = (ClampMin = "1"))
	int32 MaxScreeches = 2;

	/** Flyers whose wings (beats and flutter) are heard: the nearest this many. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Jam|Audio", meta = (ClampMin = "0"))
	int32 MaxWingSets = 4;

	/** Doppler strength on the dive shriek and the fly-by (1 = physical, speed of sound 343 m/s). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Jam|Audio", meta = (ClampMin = "0.0"))
	float DopplerScale = 2.5f;
};

/**
 * Plays an event as one audio component per layer, attached to AttachTo or at
 * Location, with the attenuation given (natural falloff, air absorption,
 * occlusion, the wide blend of close passes), the medium's low-pass and, in air,
 * the reverb send. Owner keeps the components (auto-destroyed when done).
 */
FJamSoundPlay JamPlaySoundEvent(AActor* Owner, const FJamSoundEvent& Event, const FVector& Location, USceneComponent* AttachTo,
	const FSoundAttenuationSettings& Attenuation, bool bAir, USoundSubmixBase* AirReverb, float VolumeScale = 1.0f,
	float PitchScale = 1.0f, float LowPassScale = 1.0f);

/**
 * Is the line from the player's ear to Source blocked (Visibility)? The player's own body and what it
 * carries are ignored - the engine's occlusion trace starts inside them and muffled every flyer sound.
 */
bool JamSoundOccluded(const UWorld* World, const FVector& Source, const AActor* SourceActor);

/** The attenuation every flyer sound starts from: FalloffCm of natural falloff, air absorption, occlusion, close passes wide. */
FSoundAttenuationSettings JamFlyerAttenuation(float FalloffCm, float AbsorptionStartCm);
