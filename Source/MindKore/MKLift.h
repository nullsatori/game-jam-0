#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "MKInteractable.h"
#include "MKLift.generated.h"

class AMKGate;
class UBoxComponent;
class UPointLightComponent;
class UStaticMeshComponent;

UENUM(BlueprintType)
enum class EMKLiftState : uint8
{
	Idle,
	Called,
	Arrived,
	Departed
};

DECLARE_DYNAMIC_MULTICAST_DELEGATE(FMKLiftDepartedSignature);

/**
 * The J lift (DESIGN_LEVEL §4.4 J, DESIGN_LEVEL_ADMIN_REWORK §5.4). It was the
 * level goal; in the current mission it is dead for the player: with
 * RequiredKeyId = "lab" set on the level instance, the call buttons refuse a
 * caller without that clearance, and the player is never given it
 * (DESIGN_LEVEL_ADMIN_STRUCTURE §8). The lift owns that rule
 * (AcceptsCaller); AMKControlSwitch only asks. With RequiredKeyId empty, or
 * with the key, the cabin behaves exactly as below.
 *
 * Call is one action, departure is another, and nothing in between is a
 * timer the player can lose to: the cabin travels for TravelSeconds, opens
 * its door (an AMKGate that slides sideways) and then WAITS. It leaves when
 * a hand inside the cabin says so. Arrival does not depend on who is alive.
 *
 * While travelling the shaft emits one tagged noise per second from the
 * portal. That is the only thing the AI is told: a sound with a place, not
 * the player's position and not the fact that a button was pressed.
 *
 * The amber indicator is the lift's colour (DESIGN_LEVEL §9.6) and the one
 * landmark that reads through the north grille and the east vitrine alike.
 */
UCLASS()
class MINDKORE_API AMKLift : public AActor, public IMKInteractable
{
	GENERATED_BODY()

public:
	AMKLift();

	/** Standing here inside the arrived cabin is the condition for departure. No collision. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "MindKore|Lift")
	TObjectPtr<UBoxComponent> CabinVolume = nullptr;

	/** What the eye finds on the cabin's back wall: the thing the offer hangs on. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "MindKore|Lift")
	TObjectPtr<UStaticMeshComponent> DepartButton = nullptr;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "MindKore|Lift")
	TObjectPtr<UPointLightComponent> Indicator = nullptr;

	/** The portal panel. Opens on arrival, closes on departure. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MindKore|Lift")
	TObjectPtr<AMKGate> Door = nullptr;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MindKore|Lift", meta = (ClampMin = "0.0"))
	float TravelSeconds = 12.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MindKore|Lift|Noise", meta = (ClampMin = "0.0"))
	float TravelNoiseLoudness = 0.5f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MindKore|Lift|Noise", meta = (ClampMin = "0.0"))
	float ArriveNoiseLoudness = 0.8f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MindKore|Lift|Indicator")
	float IndicatorIdleIntensity = 300.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MindKore|Lift|Indicator")
	float IndicatorActiveIntensity = 2500.0f;

	/** Clearance a caller must carry (UMKKeyringComponent). NAME_None = anyone may call. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MindKore|Lift|Access")
	FName RequiredKeyId = NAME_None;

	/** Shown on the call button when AcceptsCaller refuses. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MindKore|Lift|Access")
	FText NoClearanceReason;

	/**
	 * True when Caller may call the cabin. A null caller (level script) is
	 * refused when a key is required. Call() itself does not check: in game it
	 * is reached only through AMKControlSwitch, which asks this first.
	 */
	UFUNCTION(BlueprintPure, Category = "MindKore|Lift|Access")
	bool AcceptsCaller(const AActor* Caller) const;

	UPROPERTY(BlueprintReadOnly, Transient, Category = "MindKore|Lift")
	EMKLiftState State = EMKLiftState::Idle;

	UPROPERTY(BlueprintReadOnly, Transient, Category = "MindKore|Lift")
	float TravelElapsed = 0.0f;

	UPROPERTY(BlueprintAssignable, Category = "MindKore|Lift")
	FMKLiftDepartedSignature OnDeparted;

	UFUNCTION(BlueprintCallable, Category = "MindKore|Lift")
	void Call();

	UFUNCTION(BlueprintCallable, Category = "MindKore|Lift")
	void Depart();

	UFUNCTION(BlueprintPure, Category = "MindKore|Lift")
	bool IsInsideCabin(const AActor* Actor) const;

	// IMKInteractable
	virtual FMKInteractionOffer QueryInteraction(const AActor* Hand) const override;
	virtual void CompleteInteraction(AActor* Hand) override;

	virtual void Tick(float DeltaSeconds) override;
	virtual void BeginPlay() override;

private:
	float NoiseAccumulator = 0.0f;
	float BlinkAccumulator = 0.0f;

	void SetState(EMKLiftState NewState);
	void UpdateIndicator(float DeltaSeconds);
};
