#pragma once

#include "CoreMinimal.h"
#include "UObject/Interface.h"
#include "MKInteractable.generated.h"

/**
 * What an object offers to the hand that is looking at it. One offer at a
 * time, per DESIGN_INTERACTION_AND_PHYSICS §1.1: no menus. The executor
 * (UMKInteractionComponent) reads only this; the result of the action is the
 * object's business.
 */
USTRUCT(BlueprintType)
struct MINDKORE_API FMKInteractionOffer
{
	GENERATED_BODY()

	/** Short verb shown to the player, e.g. "Открыть". Empty = nothing offered. */
	UPROPERTY(BlueprintReadOnly, Category = "MindKore|Interaction")
	FText Action;

	/** False = the object is recognised but refuses; Reason says why. */
	UPROPERTY(BlueprintReadOnly, Category = "MindKore|Interaction")
	bool bAvailable = false;

	/** Physical reason for refusal ("заперто изнутри"). Shown, not hidden. */
	UPROPERTY(BlueprintReadOnly, Category = "MindKore|Interaction")
	FText Reason;

	/**
	 * The verb names a part or a state, not a kind of action ("Поставить на
	 * ШС-3", "Нажать: вниз"): the device spells it every time, also after the
	 * operator has done it once on this class of object (MKDeviceFrameLook).
	 */
	UPROPERTY(BlueprintReadOnly, Category = "MindKore|Interaction")
	bool bAlwaysSpelled = false;

	bool IsOffered() const { return !Action.IsEmpty(); }

	static FMKInteractionOffer None() { return FMKInteractionOffer(); }
	static FMKInteractionOffer Available(const FText& InAction)
	{
		FMKInteractionOffer Offer;
		Offer.Action = InAction;
		Offer.bAvailable = true;
		return Offer;
	}
	static FMKInteractionOffer Refused(const FText& InAction, const FText& InReason)
	{
		FMKInteractionOffer Offer;
		Offer.Action = InAction;
		Offer.bAvailable = false;
		Offer.Reason = InReason;
		return Offer;
	}
};

UINTERFACE(MinimalAPI, meta = (CannotImplementInterfaceInBlueprint))
class UMKInteractable : public UInterface
{
	GENERATED_BODY()
};

/**
 * Narrow contract from DESIGN_INTERACTION_AND_PHYSICS §10.2. This slice
 * implements the instant path only: Query, then Complete. Hold-to-drag
 * (quiet door, stiff valve) is not offered yet and no object here needs it;
 * adding it means a Begin/Update/Cancel triple, not a flag on Complete.
 */
class MINDKORE_API IMKInteractable
{
	GENERATED_BODY()

public:
	/** What this object offers to Hand right now. Must be cheap: called every frame while looked at. */
	virtual FMKInteractionOffer QueryInteraction(const AActor* Hand) const = 0;

	/** Perform the offered action. Called only when the last query was available. */
	virtual void CompleteInteraction(AActor* Hand) = 0;
};

/**
 * One noise for the AI per mechanism event, tagged so the log can tell a door
 * from a shutter. Same call the glass uses (MKGlassPane.cpp): the mechanism
 * itself is the instigator, not the hand that pulled it — the AI is told
 * where the sound came from, not who is standing there.
 */
MINDKORE_API void MKReportMechanismNoise(AActor* Source, const FVector& Location, float Loudness, FName Tag);
