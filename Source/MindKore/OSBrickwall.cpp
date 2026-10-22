#include "OSBrickwall.h"

#include <atomic>

namespace
{
	std::atomic<float> GLeastGain{ 1.0f };
}

float OSBrickwallTakeLeastGain()
{
	return GLeastGain.exchange(1.0f);
}

void FOSBrickwall::Init(const FSoundEffectSubmixInitData& InitData)
{
	SampleRate = InitData.SampleRate > 0.0f ? InitData.SampleRate : 48000.0f;
}

void FOSBrickwall::OnPresetChanged()
{
	GET_EFFECT_SETTINGS(OSBrickwall);
	Ceiling = FMath::Pow(10.0f, FMath::Min(Settings.CeilingDb, 0.0f) / 20.0f);
	ReleaseCoef = FMath::Exp(-1.0f / (FMath::Max(Settings.ReleaseMsec, 1.0f) * 0.001f * SampleRate));
	LookaheadMsec = Settings.LookaheadMsec;
	Lookahead = 0; // resized on the next buffer
}

void FOSBrickwall::Resize(int32 Frames, int32 Channels)
{
	Lookahead = Frames;
	NumChannels = Channels;
	Delay.Init(0.0f, (Lookahead + 1) * Channels);
	Need.Init(1.0f, Lookahead + 1);
	Head = 0;
	Gain = 1.0f;
}

void FOSBrickwall::OnProcessAudio(const FSoundEffectSubmixInputData& InData, FSoundEffectSubmixOutputData& OutData)
{
	const int32 Channels = InData.NumChannels;
	const int32 Wanted = FMath::Max(1, FMath::RoundToInt(LookaheadMsec * 0.001f * SampleRate));
	if (Lookahead != Wanted || NumChannels != Channels)
	{
		Resize(Wanted, Channels);
	}
	const float* In = InData.AudioBuffer->GetData();
	float* Out = OutData.AudioBuffer->GetData();
	const int32 Window = Lookahead + 1;
	float Least = 1.0f;
	for (int32 Frame = 0; Frame < InData.NumFrames; ++Frame)
	{
		const float* Frame_In = In + Frame * Channels;
		// What the newest frame needs to stay under the ceiling.
		float Peak = 0.0f;
		for (int32 Channel = 0; Channel < Channels; ++Channel)
		{
			Peak = FMath::Max(Peak, FMath::Abs(Frame_In[Channel]));
		}
		// Two rings of Window frames: Head is the oldest (it leaves now, Lookahead frames late),
		// the newest goes in Lookahead slots after it.
		const int32 Newest = (Head + Lookahead) % Window;
		Need[Newest] = Peak > Ceiling ? Ceiling / Peak : 1.0f;
		FMemory::Memcpy(Delay.GetData() + Newest * Channels, Frame_In, Channels * sizeof(float));
		// The gain allowed now: every frame in the window, its need eased in over its distance.
		float Allowed = 1.0f;
		for (int32 Age = 0; Age < Window; ++Age)
		{
			const float Frame_Need = Need[(Head + Age) % Window];
			if (Frame_Need < 1.0f)
			{
				Allowed = FMath::Min(Allowed, 1.0f - (1.0f - Frame_Need) * (1.0f - float(Age) / Window));
			}
		}
		Gain = FMath::Min(Allowed, 1.0f - (1.0f - Gain) * ReleaseCoef);
		Least = FMath::Min(Least, Gain);
		// The delayed frame goes out (the clamp only guards float rounding: Gain <= its need).
		const float* Oldest = Delay.GetData() + Head * Channels;
		float* Frame_Out = Out + Frame * Channels;
		for (int32 Channel = 0; Channel < Channels; ++Channel)
		{
			Frame_Out[Channel] = FMath::Clamp(Oldest[Channel] * Gain, -Ceiling, Ceiling);
		}
		Head = (Head + 1) % Window;
	}
	for (float Seen = GLeastGain.load(std::memory_order_relaxed); Least < Seen && !GLeastGain.compare_exchange_weak(Seen, Least);)
	{
	}
}
