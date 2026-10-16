#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "MKInteractable.h"
#include "MKControlSwitch.generated.h"

class AMKDoor;
class AMKGate;
class AMKLift;
class AMKShelfBlock;
class UMaterialInterface;
class USoundBase;
class UStaticMeshComponent;

/**
 * A lever, crank or button on a wall that acts on named actors elsewhere.
 * Targets are explicit references set in the level (DESIGN_INTERACTION_AND
 * _PHYSICS §10.4: "явные зависимости между несколькими акторами", no
 * dispatcher). One switch, one action; what that action is follows from
 * which target lists are filled:
 *
 *   Gates         -> open (or toggle, see bToggleGates)
 *   DoorsToUnlock -> release the building-side lock
 *   ShelfBlocks   -> crank to the other position
 *   Lights        -> all off / all on (the room's wall switch)
 *   Lift          -> call the cabin, if the lift accepts the caller
 *                    (AMKLift::AcceptsCaller). A refused caller gets the
 *                    lift's reason and the whole switch does nothing: a
 *                    one-shot is not spent, no other target is touched.
 *
 * bOneShot switches (the reception shutter lever, "общий доступ") refuse
 * after the first use; the state they changed stays changed. Repeat presses
 * therefore cannot re-lock anything - the level only ever opens up.
 *
 * State is read off the object, not off the prompt: the Handle is thrown
 * between two pitches and the Lamp is lit while the switch still has
 * something to give. A spent one-shot is handle down, lamp dark. A toggle
 * (the shelf drive) alternates its handle and stays lit. bMomentary (a call
 * button) dips and springs back.
 */
UCLASS()
class MINDKORE_API AMKControlSwitch : public AActor, public IMKInteractable
{
	GENERATED_BODY()

public:
	AMKControlSwitch();

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "MindKore|Switch")
	TObjectPtr<UStaticMeshComponent> Body = nullptr;

	/** Thrown part. Its origin is the pivot; pitch runs HandleRestPitch -> HandleThrownPitch. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "MindKore|Switch")
	TObjectPtr<UStaticMeshComponent> Handle = nullptr;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "MindKore|Switch")
	TObjectPtr<UStaticMeshComponent> Lamp = nullptr;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MindKore|Switch|Handle")
	float HandleRestPitch = 40.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MindKore|Switch|Handle")
	float HandleThrownPitch = -40.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MindKore|Switch|Handle", meta = (ClampMin = "0.0"))
	float HandleSeconds = 0.22f;

	/** Springs back to rest after the throw (call button). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MindKore|Switch|Handle")
	bool bMomentary = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MindKore|Switch|Lamp")
	TObjectPtr<UMaterialInterface> LampLitMaterial = nullptr;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MindKore|Switch|Lamp")
	TObjectPtr<UMaterialInterface> LampDarkMaterial = nullptr;

	/** Verb offered to the hand, e.g. "Поднять роллету". */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MindKore|Switch")
	FText ActionLabel;

	/** The verb names the position it turns to ("Повернуть: 0", a packet switch): the device spells it every time. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MindKore|Switch")
	bool bAlwaysSpelled = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MindKore|Switch")
	bool bOneShot = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MindKore|Switch|Targets")
	TArray<TObjectPtr<AMKGate>> Gates;

	/** False: every use opens. True: alternate open/close (a crank, not a release). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MindKore|Switch|Targets")
	bool bToggleGates = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MindKore|Switch|Targets")
	TArray<TObjectPtr<AMKDoor>> DoorsToUnlock;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MindKore|Switch|Targets")
	TArray<TObjectPtr<AMKShelfBlock>> ShelfBlocks;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MindKore|Switch|Targets")
	TObjectPtr<AMKLift> Lift = nullptr;

	/**
	 * Room lights (AMKFlickerLightActor, or any actor with light components):
	 * every use turns them all off or all on. Such a switch is a toggle
	 * (bOneShot off) and needs no Lamp.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MindKore|Switch|Targets")
	TArray<TObjectPtr<AActor>> Lights;

	/** Verb offered while Lights are on. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MindKore|Switch")
	FText LightsOnActionLabel;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MindKore|Switch|Noise", meta = (ClampMin = "0.0"))
	float UseNoiseLoudness = 0.3f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MindKore|Switch|Noise")
	FName UseNoiseTag = TEXT("MK_Switch");

	/** The click the player hears. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MindKore|Switch")
	TObjectPtr<USoundBase> UseSound = nullptr;

	/** What the next use does to Lights; read off the first light at BeginPlay. */
	UPROPERTY(BlueprintReadOnly, Transient, Category = "MindKore|Switch")
	bool bLightsOn = true;

	UPROPERTY(BlueprintReadOnly, Transient, Category = "MindKore|Switch")
	bool bUsed = false;

	UPROPERTY(BlueprintReadOnly, Transient, Category = "MindKore|Switch")
	int32 UseCount = 0;

	/** Where the handle is heading: false = rest, true = thrown. */
	UPROPERTY(BlueprintReadOnly, Transient, Category = "MindKore|Switch")
	bool bHandleThrown = false;

	/** Still has something to give: drives the lamp. */
	UFUNCTION(BlueprintPure, Category = "MindKore|Switch")
	bool IsLive() const { return !(bOneShot && bUsed); }

	/**
	 * The circuit is closed: the Lights are on. A toggle without Lights (a
	 * packet switch, «Ф3 резерв») keeps it on its handle - closed at rest,
	 * open thrown - and offers LightsOnActionLabel while closed, when it has one.
	 * A one-shot or a button without Lights opens nothing: always closed.
	 */
	UFUNCTION(BlueprintPure, Category = "MindKore|Switch")
	bool IsOn() const;

	/** Same as the hand: for level script and the PIE probe. A null User is refused by a lift that requires a key. */
	UFUNCTION(BlueprintCallable, Category = "MindKore|Switch")
	void Activate(AActor* User);

	// IMKInteractable
	virtual FMKInteractionOffer QueryInteraction(const AActor* Hand) const override;
	virtual void CompleteInteraction(AActor* Hand) override;

	virtual void Tick(float DeltaSeconds) override;

protected:
	virtual void BeginPlay() override;

private:
	float HandleAlpha = 0.0f;
	static bool IsLightLit(const AActor* Light);
	static void SetLightLit(AActor* Light, bool bLit);
	void ApplyHandle();
	void ApplyLamp();
};

/**
 * A current that runs through every one of Switches flows while each of them
 * is on (AMKControlSwitch::IsOn): a breaker whose Lights are the section's
 * lamps, a packet switch by its handle. Empty entries are skipped; none at all
 * and it always flows.
 */
MINDKORE_API bool MKCurrentThrough(TConstArrayView<TObjectPtr<AMKControlSwitch>> Switches);
