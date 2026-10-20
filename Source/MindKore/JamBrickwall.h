#pragma once

#include "CoreMinimal.h"
#include "Sound/SoundEffectSubmix.h"
#include "JamBrickwall.generated.h"

USTRUCT(BlueprintType)
struct FJamBrickwallSettings
{
	GENERATED_BODY()

	/** No sample of the output goes above this, dBFS. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Limiter", meta = (ClampMin = "-24.0", ClampMax = "0.0"))
	float CeilingDb = -1.0f;

	/** The gain starts falling this long before a peak (the output is delayed by it), ms. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Limiter", meta = (ClampMin = "0.5", ClampMax = "20.0"))
	float LookaheadMsec = 5.0f;

	/** Time constant of the gain's return to 1 after a peak, ms. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Limiter", meta = (ClampMin = "5.0", ClampMax = "1000.0"))
	float ReleaseMsec = 80.0f;
};

/**
 * A lookahead brickwall for the master submix. UE's dynamics processor in
 * limiter mode follows an envelope and let peaks through: with threshold -1 dB,
 * look-ahead 5 ms and attack 1 ms (its minimum) the pistol's and the SV-98's
 * sums still reached 0 dBFS (1816 samples over -1 dBFS in jam.Audio.LimiterProbe),
 * and at threshold -12 dB the output sat 1-3 dB above it. Here the gain for each
 * delayed frame is the smallest any frame in the look-ahead window asks for, that
 * frame's need eased in linearly over the window, so the output never exceeds the
 * ceiling; after the peaks it returns with ReleaseMsec. No makeup gain.
 */
class FJamBrickwall : public FSoundEffectSubmix
{
public:
	virtual void Init(const FSoundEffectSubmixInitData& InitData) override;
	virtual void OnPresetChanged() override;
	virtual void OnProcessAudio(const FSoundEffectSubmixInputData& InData, FSoundEffectSubmixOutputData& OutData) override;

private:
	void Resize(int32 Frames, int32 Channels);

	float SampleRate = 48000.0f;
	float Ceiling = 0.891f;
	float ReleaseCoef = 0.9997f;
	float LookaheadMsec = 5.0f;
	int32 Lookahead = 0;
	int32 NumChannels = 0;
	TArray<float> Delay; // Lookahead + 1 frames, interleaved
	TArray<float> Need;  // Lookahead + 1 gains; Head is the frame going out
	int32 Head = 0;
	float Gain = 1.0f;
};

/**
 * The least gain the brickwall applied since the last call (1: it never acted), and starts over: how hard it
 * held what came through, for probes (jam.Weapons.AirTake). Measures only; the effect is the same without it.
 */
MINDKORE_API float JamBrickwallTakeLeastGain();

UCLASS(ClassGroup = AudioSourceEffect, meta = (BlueprintSpawnableComponent))
class MINDKORE_API UJamBrickwallPreset : public USoundEffectSubmixPreset
{
	GENERATED_BODY()

public:
	EFFECT_PRESET_METHODS(JamBrickwall)

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = SubmixEffectPreset, meta = (ShowOnlyInnerProperties))
	FJamBrickwallSettings Settings;
};
