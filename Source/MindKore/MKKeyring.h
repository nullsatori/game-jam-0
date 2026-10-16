#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "MKKeyring.generated.h"

/**
 * Which keys the holder carries (PLAN_ADMIN_ENTRANCE_KEY; three or four per
 * mission: "yard", "K1", "K2"; "lab" is never handed to the player). Not an
 * inventory: a set of names and nothing else.
 *
 * One owner. In game, AMKKeyPickup::CompleteInteraction is the only caller of
 * AddKey; doors, the lift and switches only read through MKHolderHasKey.
 * AddKey is public C++ (the tests give keys directly) and deliberately not
 * BlueprintCallable, so no Blueprint or level script becomes a second writer.
 * Keys are never removed: a turned lock stays turned, so there is nothing to
 * spend.
 *
 * Transient: there is no save game and no respawn in C++ today. If a respawn
 * path appears, the set has to move to something that outlives the pawn
 * (PlayerState), or a taken key is lost with a hidden pickup.
 */
UCLASS(ClassGroup = (MindKore), meta = (BlueprintSpawnableComponent))
class MINDKORE_API UMKKeyringComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UMKKeyringComponent();

	/** Adds KeyId. False for NAME_None or a key already held: nothing is duplicated. */
	bool AddKey(FName KeyId);

	UFUNCTION(BlueprintPure, Category = "MindKore|Keys")
	bool HasKey(FName KeyId) const;

	/** For probes and logs; the order is not meaningful. */
	UFUNCTION(BlueprintPure, Category = "MindKore|Keys")
	TArray<FName> GetKeyIds() const { return Keys.Array(); }

	int32 Num() const { return Keys.Num(); }

private:
	UPROPERTY(VisibleInstanceOnly, Transient, Category = "MindKore|Keys")
	TSet<FName> Keys;
};

/** The holder's keyring, or null when Holder carries none. */
MINDKORE_API UMKKeyringComponent* MKFindKeyring(const AActor* Holder);

/** True when Holder carries a keyring with KeyId on it. False for a null holder or NAME_None. */
MINDKORE_API bool MKHolderHasKey(const AActor* Holder, FName KeyId);
