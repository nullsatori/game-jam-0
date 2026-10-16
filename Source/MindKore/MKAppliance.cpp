#include "MKAppliance.h"

#include "Components/AudioComponent.h"
#include "Components/LightComponent.h"
#include "Components/MeshComponent.h"
#include "Kismet/GameplayStatics.h"
#include "Materials/MaterialInterface.h"
#include "MindKore.h"
#include "Sound/SoundAttenuation.h"
#include "Sound/SoundBase.h"

FMKInteractionOffer MKApplianceOffer(const FMKApplianceState& State)
{
	if (State.Action.IsEmpty())
	{
		return FMKInteractionOffer::None();
	}
	return State.BusyReason.IsEmpty()
		? FMKInteractionOffer::Available(State.Action)
		: FMKInteractionOffer::Refused(State.Action, State.BusyReason);
}

FName MKApplianceNext(const FMKApplianceState& State, EMKApplianceEvent Event)
{
	if (Event == EMKApplianceEvent::Use)
	{
		return MKApplianceOffer(State).bAvailable ? State.NextOnUse : NAME_None;
	}
	return State.Seconds > 0.0f ? State.NextOnTimer : NAME_None;
}

AMKAppliance::AMKAppliance()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.bStartWithTickEnabled = false;

	Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	SetRootComponent(Root);

	LoopAudio = CreateDefaultSubobject<UAudioComponent>(TEXT("LoopAudio"));
	LoopAudio->SetupAttachment(Root);
	LoopAudio->bAutoActivate = false;
}

void AMKAppliance::BeginPlay()
{
	Super::BeginPlay();
	LoopAudio->AttenuationSettings = Attenuation;
	LoopAudio->SetVolumeMultiplier(Volume);
	const FMKApplianceState* Start = InitialState.IsNone() ? (States.Num() > 0 ? &States[0] : nullptr) : Find(InitialState);
	if (!Start)
	{
		UE_LOG(LogMindKore, Warning, TEXT("Appliance %s: no initial state '%s' among %d states"),
			*GetName(), *InitialState.ToString(), States.Num());
		return;
	}
	Enter(*Start, /*bAtRest=*/true);
}

const FMKApplianceState* AMKAppliance::Find(FName Name) const
{
	return States.FindByPredicate([Name](const FMKApplianceState& State) { return State.Name == Name; });
}

FMKInteractionOffer AMKAppliance::QueryInteraction(const AActor* Hand) const
{
	const FMKApplianceState* State = Hand ? Find(CurrentState) : nullptr;
	return State ? MKApplianceOffer(*State) : FMKInteractionOffer::None();
}

void AMKAppliance::CompleteInteraction(AActor* Hand)
{
	Use(Hand);
}

void AMKAppliance::Use(AActor* User)
{
	const FMKApplianceState* State = Find(CurrentState);
	const FName NextName = State ? MKApplianceNext(*State, EMKApplianceEvent::Use) : NAME_None;
	if (NextName.IsNone())
	{
		return;
	}
	if (const FMKApplianceState* Next = Find(NextName))
	{
		Enter(*Next, /*bAtRest=*/false);
	}
	else
	{
		UE_LOG(LogMindKore, Warning, TEXT("Appliance %s: state '%s' names a missing state '%s'"),
			*GetName(), *CurrentState.ToString(), *NextName.ToString());
	}
}

void AMKAppliance::ReportNoise(const FMKApplianceState& State)
{
	MKReportMechanismNoise(this, GetActorLocation(), State.NoiseLoudness, State.NoiseTag);
	++NoiseCount;
}

void AMKAppliance::Enter(const FMKApplianceState& State, bool bAtRest)
{
	UE_LOG(LogMindKore, Log, TEXT("Appliance %s: %s -> %s"), *GetName(), *CurrentState.ToString(), *State.Name.ToString());
	CurrentState = State.Name;
	StateElapsed = 0.0f;
	NoisePeriods = 0;
	Spins.Reset();

	for (const FMKAppliancePart& Part : State.Parts)
	{
		USceneComponent* Component = nullptr;
		for (UActorComponent* Candidate : GetComponents())
		{
			if (Candidate && Candidate->GetFName() == Part.Component)
			{
				Component = Cast<USceneComponent>(Candidate);
				break;
			}
		}
		if (!Component)
		{
			UE_LOG(LogMindKore, Warning, TEXT("Appliance %s: state '%s' names a missing part '%s'"),
				*GetName(), *State.Name.ToString(), *Part.Component.ToString());
			continue;
		}
		if (Part.bMove)
		{
			Moves.RemoveAll([Component](const FMove& Move) { return Move.Part == Component; });
			if (bAtRest || Part.MoveSeconds <= 0.0f)
			{
				Component->SetRelativeTransform(Part.Pose);
			}
			else
			{
				Moves.Add({Component, Component->GetRelativeTransform(), Part.Pose, Part.MoveSeconds, 0.0f});
			}
		}
		if (!Part.SpinPerSecond.IsZero())
		{
			Spins.Add({Component, Part.SpinPerSecond});
		}
		if (ULightComponent* Light = Cast<ULightComponent>(Component); Light && Part.LightIntensity >= 0.0f)
		{
			Light->SetIntensity(Part.LightIntensity);
			Light->SetVisibility(Part.LightIntensity > 0.0f);
		}
		if (UPrimitiveComponent* Primitive = Cast<UPrimitiveComponent>(Component); Primitive && Part.Material)
		{
			Primitive->SetMaterial(0, Part.Material);
		}
		if (UMeshComponent* Mesh = Cast<UMeshComponent>(Component); Mesh && !Part.ScalarParameter.IsNone())
		{
			Mesh->SetScalarParameterValueOnMaterials(Part.ScalarParameter, Part.ScalarValue);
		}
	}

	if (LoopAudio->Sound != State.LoopSound || !LoopAudio->IsPlaying())
	{
		LoopAudio->Stop();
		LoopAudio->SetSound(State.LoopSound);
		if (State.LoopSound)
		{
			LoopAudio->Play();
		}
	}
	if (!bAtRest)
	{
		if (State.EnterSound)
		{
			UGameplayStatics::PlaySoundAtLocation(this, State.EnterSound, GetActorLocation(), Volume, 1.0f, 0.0f, Attenuation);
		}
		if (State.NoiseLoudness > 0.0f)
		{
			ReportNoise(State);
		}
	}

	const bool bLastingNoise = State.NoiseLoudness > 0.0f && State.NoisePeriod > 0.0f;
	SetActorTickEnabled(State.Seconds > 0.0f || bLastingNoise || Moves.Num() > 0 || Spins.Num() > 0);
}

void AMKAppliance::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	for (int32 Index = Moves.Num() - 1; Index >= 0; --Index)
	{
		FMove& Move = Moves[Index];
		Move.Elapsed += DeltaSeconds;
		const float Alpha = FMath::Clamp(Move.Elapsed / Move.Seconds, 0.0f, 1.0f);
		if (USceneComponent* Part = Move.Part.Get())
		{
			FTransform Pose;
			Pose.Blend(Move.From, Move.To, FMath::SmoothStep(0.0f, 1.0f, Alpha));
			Part->SetRelativeTransform(Pose);
		}
		if (Alpha >= 1.0f)
		{
			Moves.RemoveAtSwap(Index);
		}
	}
	for (const FSpin& Spin : Spins)
	{
		if (USceneComponent* Part = Spin.Part.Get())
		{
			Part->AddLocalRotation(Spin.PerSecond * DeltaSeconds);
		}
	}

	const FMKApplianceState* State = Find(CurrentState);
	if (!State)
	{
		SetActorTickEnabled(false);
		return;
	}
	// The timer first: a state that has ended makes no more of its own noise.
	StateElapsed += DeltaSeconds;
	if (State->Seconds > 0.0f && StateElapsed >= State->Seconds)
	{
		if (const FMKApplianceState* Next = Find(MKApplianceNext(*State, EMKApplianceEvent::Timer)))
		{
			Enter(*Next, /*bAtRest=*/false);
		}
		else
		{
			UE_LOG(LogMindKore, Warning, TEXT("Appliance %s: timed state '%s' names a missing state '%s'"),
				*GetName(), *State->Name.ToString(), *State->NextOnTimer.ToString());
			SetActorTickEnabled(false);
		}
		return;
	}
	const bool bLastingNoise = State->NoiseLoudness > 0.0f && State->NoisePeriod > 0.0f;
	if (bLastingNoise)
	{
		// Counted off the state's own clock, so the last period and the timer cannot disagree.
		while (State->NoisePeriod * (NoisePeriods + 1) <= StateElapsed)
		{
			++NoisePeriods;
			ReportNoise(*State);
		}
	}
	if (State->Seconds <= 0.0f && !bLastingNoise && Moves.Num() == 0 && Spins.Num() == 0)
	{
		SetActorTickEnabled(false);
	}
}
