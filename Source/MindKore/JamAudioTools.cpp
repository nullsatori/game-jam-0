#include "JamAudioTools.h"

#include "EffectConvolutionReverb.h"

void UJamAudioTools::SetImpulseResponse(UAudioImpulseResponse* Response, const TArray<float>& Interleaved, int32 Channels, int32 SampleRate)
{
	if (!Response || Channels <= 0 || SampleRate <= 0)
	{
		return;
	}
	Response->Modify();
	Response->ImpulseResponse = Interleaved;
	Response->NumChannels = Channels;
	Response->SampleRate = SampleRate;
	Response->bTrueStereo = false;
#if WITH_EDITORONLY_DATA
	Response->bIsEvenChannelCount = Channels % 2 == 0;
#endif
	Response->MarkPackageDirty();
}
