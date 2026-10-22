#pragma once

#include "CoreMinimal.h"

class FFrameGrabber;
class FOSClipWriter;
class UWorld;

/**
 * A take of the game viewport with its sound, for clips the owner listens to
 * (MindKore's MKHandsFeelVideo, trimmed): the viewport's own back buffer through
 * FFrameGrabber, piped to ffmpeg on a worker thread, so nothing else on the
 * desktop gets into the file; the master submix recorded alongside
 * (UAudioMixerBlueprintLibrary). The video is written at a nominal 60 fps, one
 * frame per rendered frame; Stop logs the real seconds so a mux can retime it.
 */
class FOSClipRecorder
{
public:
	FOSClipRecorder();
	~FOSClipRecorder();

	/** Starts both; Width is the encoded width (the viewport's aspect kept). */
	bool Start(UWorld* World, const FString& FfmpegPath, const FString& VideoPath, int32 Width, FString& OutError);

	/** Game thread, once per frame. */
	void Tick();

	/** Stops both: the sound goes to Dir/WavName.wav at once, the video when its frames are in. */
	void Stop(UWorld* World, const FString& WavName, const FString& Dir);

	/** True once the encoder has the last frame and has exited. */
	bool IsFinished();

	int32 GetFrames() const { return Received; }
	/** Frames asked of the grabber so far: the video frame the current game frame becomes. */
	int32 GetRequested() const { return Requested; }
	double GetSeconds() const { return StopSeconds - StartSeconds; }

private:
	TUniquePtr<FFrameGrabber> Grabber;
	TSharedPtr<FOSClipWriter, ESPMode::ThreadSafe> Writer;
	FIntPoint Size = FIntPoint::ZeroValue;
	bool bStopping = false;
	int32 TicksAfterStop = 0;
	int32 Received = 0;
	int32 Requested = 0;
	double StartSeconds = 0.0;
	double StopSeconds = 0.0;
	void Drain();
};
