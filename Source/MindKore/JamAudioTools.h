#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "JamAudioTools.generated.h"

class UAudioImpulseResponse;

/** Helpers for the editor scripts that build the jam's audio assets (Scripts/jam_air_audio.py). */
UCLASS()
class MINDKORE_API UJamAudioTools : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	/**
	 * Fills an impulse response asset with interleaved samples: UAudioImpulseResponse keeps them in plain
	 * UPROPERTYs that Python cannot write, and its factory only reads a staged wave from the editor's menu.
	 * Independent channels (not true stereo), as a stereo IR of a convolution submix effect.
	 */
	UFUNCTION(BlueprintCallable, Category = "Jam|Audio")
	static void SetImpulseResponse(UAudioImpulseResponse* Response, const TArray<float>& Interleaved, int32 Channels, int32 SampleRate);
};
