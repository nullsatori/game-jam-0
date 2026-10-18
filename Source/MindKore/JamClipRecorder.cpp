#include "JamClipRecorder.h"

#include "AudioMixerBlueprintLibrary.h"
#include "Engine/GameViewportClient.h"
#include "Engine/World.h"
#include "FrameGrabber.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformProcess.h"
#include "HAL/PlatformTime.h"
#include "HAL/Runnable.h"
#include "HAL/RunnableThread.h"
#include "MindKore.h"
#include "Misc/Paths.h"
#include "Misc/ScopeLock.h"
#include "Slate/SceneViewport.h"
#include "Widgets/SViewport.h"

#include <atomic>

/** Owns the encoder's stdin: frames are moved in on the game thread and written here. */
class FJamClipWriter : public FRunnable
{
public:
	FJamClipWriter(void* InPipe, FProcHandle InProcess) : Pipe(InPipe), Process(InProcess)
	{
		Wake = FPlatformProcess::GetSynchEventFromPool(false);
		Thread = FRunnableThread::Create(this, TEXT("JamClipWriter"));
	}

	virtual ~FJamClipWriter() override
	{
		Finish();
		if (Thread)
		{
			Thread->WaitForCompletion();
			delete Thread;
		}
		FPlatformProcess::ReturnSynchEventToPool(Wake);
	}

	void Push(TArray<FColor>&& Pixels)
	{
		{
			FScopeLock Lock(&Mutex);
			Pending.Add(MoveTemp(Pixels));
		}
		Wake->Trigger();
	}

	void Finish()
	{
		bFinish = true;
		Wake->Trigger();
	}

	bool IsDone() const { return bDone; }

	virtual uint32 Run() override
	{
		for (;;)
		{
			TArray<TArray<FColor>> Batch;
			{
				FScopeLock Lock(&Mutex);
				Batch = MoveTemp(Pending);
				Pending.Reset();
			}
			for (const TArray<FColor>& Frame : Batch)
			{
				const uint8* Data = reinterpret_cast<const uint8*>(Frame.GetData());
				int32 Left = Frame.Num() * sizeof(FColor);
				while (Left > 0 && Pipe)
				{
					int32 Done = 0;
					if (!FPlatformProcess::WritePipe(Pipe, Data, Left, &Done) || Done <= 0)
					{
						Pipe = nullptr; // the encoder is gone
						break;
					}
					Data += Done;
					Left -= Done;
				}
			}
			if (bFinish)
			{
				FScopeLock Lock(&Mutex);
				if (Pending.Num() == 0)
				{
					break;
				}
				continue;
			}
			Wake->Wait(50);
		}
		if (Pipe)
		{
			FPlatformProcess::ClosePipe(nullptr, Pipe); // EOF: the encoder finishes the file
		}
		const double Deadline = FPlatformTime::Seconds() + 60.0;
		while (FPlatformProcess::IsProcRunning(Process) && FPlatformTime::Seconds() < Deadline)
		{
			FPlatformProcess::Sleep(0.05f);
		}
		if (FPlatformProcess::IsProcRunning(Process))
		{
			FPlatformProcess::TerminateProc(Process);
		}
		FPlatformProcess::CloseProc(Process);
		bDone = true;
		return 0;
	}

private:
	void* Pipe = nullptr;
	FProcHandle Process;
	FEvent* Wake = nullptr;
	FRunnableThread* Thread = nullptr;
	FCriticalSection Mutex;
	TArray<TArray<FColor>> Pending;
	std::atomic<bool> bFinish{ false };
	std::atomic<bool> bDone{ false };
};

FJamClipRecorder::FJamClipRecorder() = default;

FJamClipRecorder::~FJamClipRecorder()
{
	if (Grabber.IsValid())
	{
		Grabber->StopCapturingFrames();
		Grabber->Shutdown();
	}
	Writer.Reset();
}

bool FJamClipRecorder::Start(UWorld* World, const FString& FfmpegPath, const FString& VideoPath, int32 Width, FString& OutError)
{
	UGameViewportClient* Client = World ? World->GetGameViewport() : nullptr;
	TSharedPtr<SViewport> Widget = Client ? Client->GetGameViewportWidget() : nullptr;
	TSharedPtr<ISlateViewport> Interface = Widget.IsValid() ? Widget->GetViewportInterface().Pin() : nullptr;
	FSceneViewport* Raw = Client ? Client->GetGameViewport() : nullptr;
	if (!Interface.IsValid() || !Raw || static_cast<ISlateViewport*>(Raw) != Interface.Get())
	{
		OutError = TEXT("the game viewport is not a shared scene viewport (needs a real RHI and a window)");
		return false;
	}
	TSharedRef<FSceneViewport> Scene = StaticCastSharedRef<FSceneViewport>(Interface.ToSharedRef());
	const FIntPoint ViewportSize = Scene->GetSizeXY();
	if (ViewportSize.X <= 0 || ViewportSize.Y <= 0)
	{
		OutError = TEXT("the viewport has no size");
		return false;
	}
	Size.X = Width - Width % 2;
	Size.Y = FMath::RoundToInt(double(Width) * ViewportSize.Y / ViewportSize.X);
	Size.Y -= Size.Y % 2;
	IFileManager::Get().MakeDirectory(*FPaths::GetPath(VideoPath), true);
	void* PipeRead = nullptr;
	void* PipeWrite = nullptr;
	if (!FPlatformProcess::CreatePipe(PipeRead, PipeWrite, /*bWritePipeLocal=*/true))
	{
		OutError = TEXT("no pipe for the encoder");
		return false;
	}
	const FString Params = FString::Printf(TEXT("-hide_banner -loglevel error -y -f rawvideo -pix_fmt bgra -s %dx%d -framerate 60 -i - ")
		TEXT("-c:v libx264 -preset veryfast -crf 18 -pix_fmt yuv420p \"%s\""), Size.X, Size.Y, *VideoPath);
	FProcHandle Process = FPlatformProcess::CreateProc(*FfmpegPath, *Params, false, true, true, nullptr, 0, nullptr, nullptr, PipeRead);
	FPlatformProcess::ClosePipe(PipeRead, nullptr);
	if (!Process.IsValid())
	{
		FPlatformProcess::ClosePipe(nullptr, PipeWrite);
		OutError = FString::Printf(TEXT("could not start '%s'"), *FfmpegPath);
		return false;
	}
	Writer = MakeShared<FJamClipWriter, ESPMode::ThreadSafe>(PipeWrite, Process);
	Grabber = MakeUnique<FFrameGrabber>(Scene, Size, PF_B8G8R8A8, 3);
	Grabber->StartCapturingFrames();
	UAudioMixerBlueprintLibrary::StartRecordingOutput(World, 600.0f);
	StartSeconds = FPlatformTime::Seconds();
	UE_LOG(LogMindKore, Log, TEXT("Clip: recording %dx%d to %s, and the master submix"), Size.X, Size.Y, *VideoPath);
	return true;
}

void FJamClipRecorder::Drain()
{
	if (!Grabber.IsValid() || !Writer.IsValid())
	{
		return;
	}
	for (FCapturedFrameData& Frame : Grabber->GetCapturedFrames())
	{
		if (Frame.BufferSize == Size)
		{
			Writer->Push(MoveTemp(Frame.ColorBuffer));
			++Received;
		}
	}
}

void FJamClipRecorder::Tick()
{
	if (!Grabber.IsValid())
	{
		return;
	}
	if (!bStopping)
	{
		Grabber->CaptureThisFrame(FFramePayloadPtr());
	}
	else
	{
		++TicksAfterStop;
	}
	Drain();
}

void FJamClipRecorder::Stop(UWorld* World, const FString& WavName, const FString& Dir)
{
	if (!Grabber.IsValid() || bStopping)
	{
		return;
	}
	bStopping = true;
	StopSeconds = FPlatformTime::Seconds();
	Grabber->StopCapturingFrames();
	UAudioMixerBlueprintLibrary::StopRecordingOutput(World, EAudioRecordingExportType::WavFile, WavName, Dir);
}

bool FJamClipRecorder::IsFinished()
{
	if (Grabber.IsValid())
	{
		Drain();
		if (Grabber->HasOutstandingFrames() && TicksAfterStop < 30)
		{
			return false;
		}
		Grabber->Shutdown();
		Grabber.Reset();
		Writer->Finish();
	}
	return !Writer.IsValid() || Writer->IsDone();
}
