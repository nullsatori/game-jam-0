#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "MKInteractable.h"
#include "MKLockRules.h"
#include "MKDoor.generated.h"

class ANavLinkProxy;
class UBoxComponent;
class UStaticMeshComponent;

UENUM(BlueprintType)
enum class EMKDoorState : uint8
{
	Closed,
	Opening,
	Open,
	Closing,
	/** Stopped part-way by a body. Passable, not a stable design state: the next use closes or opens it. */
	Ajar
};

/**
 * A hinged leaf (DESIGN_INTERACTION_AND_PHYSICS §4.1), deterministic: the
 * actor drives the angle, the sweep stops it on a body, no physics owns it.
 *
 * Frame. Actor origin is the HINGE at floor level; the leaf extends along
 * local +Y from the hinge and faces local +X when closed. "Inside" is the
 * -X side: that is where a bolt is reached from. Place the actor with its
 * X axis across the doorway and its Y axis along the wall.
 *
 * Three locks, three owners. bBoltedInside is the player's: only a hand on
 * the inside lifts it, and it never re-engages by itself. bLocked is the
 * building's: an AMKControlSwitch releases it, and bCloseAndLockBehind sets
 * it once the player has gone through; a key never releases it. The key lock
 * (RequiredKeyId) is turned once by a hand at the door that carries that key
 * (UMKKeyringComponent) and never re-engages; picking the key up does not
 * touch the door. The order they are read in is MKResolveDoorVerdict
 * (MKLockRules.h). Any one of them makes the leaf
 * cut the navmesh; an unlocked door is transparent to navigation and an
 * enemy in AiOpenVolume WHOSE PATH GOES THROUGH THE DOORWAY swings it open
 * without a hand animation; one walking past along the wall does not (§4.1: "враг способен открыть её теми же состояниями"). A closed unlocked
 * door therefore breaks LINE OF SIGHT, not the enemy's path — that is the
 * trade the offices are built on.
 */
UCLASS()
class MINDKORE_API AMKDoor : public AActor, public IMKInteractable
{
	GENERATED_BODY()

public:
	AMKDoor();

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "MindKore|Door")
	TObjectPtr<USceneComponent> Hinge = nullptr;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "MindKore|Door")
	TObjectPtr<UStaticMeshComponent> Leaf = nullptr;

	// OS lift: HandleAnchor (UMKFrameAnchorComponent) cut - it only framed the handle for the bodycam device HUD, which is not lifted.

	/** Enemies in this box are asked where they are going (IsOnRouteOf). Sized to both sides of the leaf. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "MindKore|Door")
	TObjectPtr<UBoxComponent> AiOpenVolume = nullptr;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MindKore|Door", meta = (ClampMin = "10.0", ClampMax = "170.0"))
	float OpenAngleDeg = 95.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MindKore|Door", meta = (ClampMin = "0.05"))
	float SwingSeconds = 0.9f;

	/**
	 * Building-side lock (service mode). Released by AMKControlSwitch::DoorsToUnlock;
	 * also set by the door itself when bCloseAndLockBehind fires. A key does not release it.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MindKore|Door")
	bool bLocked = false;

	/**
	 * Key lock. NAME_None = the door has none (the default: behaves as before).
	 * Otherwise the door refuses with KeyRequiredReason until a hand carrying
	 * this key uses it; that use turns the key ("Отпереть"), the next one opens.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MindKore|Door|Key")
	FName RequiredKeyId = NAME_None;

	/** Shown when the key lock refuses a hand without the key. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MindKore|Door|Key")
	FText KeyRequiredReason;

	/** The key lock has been turned. Only CompleteInteraction writes it; it never turns back. */
	UPROPERTY(BlueprintReadOnly, Transient, Category = "MindKore|Door|Key")
	bool bKeyTurned = false;

	/** Player-side bolt on the -X face. Lifted once by a hand on that side; never re-engages. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MindKore|Door")
	bool bBoltedInside = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MindKore|Door")
	FText DisplayName;

	/**
	 * The way in that does not stay a way out (DESIGN_LEVEL §2.3, entrance A:
	 * "закрывается за игроком"). Once the player who opened it stands on the
	 * far side and LockBehindDistanceCm away, the leaf closes; when it has
	 * closed, the building-side lock engages. One shot. Nothing scripted is
	 * taken from the player: he walks through a door and it shuts.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MindKore|Door")
	bool bCloseAndLockBehind = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MindKore|Door", meta = (ClampMin = "60.0"))
	float LockBehindDistanceCm = 170.0f;

	/** Shown when bLocked refuses the hand. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MindKore|Door")
	FText LockedReason;

	/**
	 * The nav link bridging this doorway (the navmesh does not pass a 100 cm
	 * opening at agent radius 35). It must be a SMART link: a point link
	 * cannot be switched off, and a link that stays on would let an enemy
	 * path through a locked door. Disabled while the door is secured.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MindKore|Door")
	TObjectPtr<ANavLinkProxy> NavLink = nullptr;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MindKore|Door|Noise", meta = (ClampMin = "0.0"))
	float SwingNoiseLoudness = 0.4f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MindKore|Door|Noise", meta = (ClampMin = "0.0"))
	float LockNoiseLoudness = 0.3f;

	UPROPERTY(BlueprintReadOnly, Transient, Category = "MindKore|Door")
	EMKDoorState State = EMKDoorState::Closed;

	/** Signed target yaw the leaf is heading for (0 = closed). */
	UPROPERTY(BlueprintReadOnly, Transient, Category = "MindKore|Door")
	float TargetYaw = 0.0f;

	UFUNCTION(BlueprintCallable, Category = "MindKore|Door")
	void OpenAwayFrom(const FVector& PusherLocation);

	UFUNCTION(BlueprintCallable, Category = "MindKore|Door")
	void Close();

	/** Building-side release. Idempotent. */
	UFUNCTION(BlueprintCallable, Category = "MindKore|Door")
	void Unlock();

	UFUNCTION(BlueprintPure, Category = "MindKore|Door")
	bool IsPassable() const { return State == EMKDoorState::Open || State == EMKDoorState::Ajar; }

	UFUNCTION(BlueprintPure, Category = "MindKore|Door")
	bool IsKeyLockEngaged() const { return !RequiredKeyId.IsNone() && !bKeyTurned; }

	/** The key lock is what stops Hand now: locked on a key Hand does not carry, and nothing else refuses first. */
	bool RefusesForKey(const AActor* Hand) const;

	/** Any of the three locks: navmesh footprint, NavLink off, no enemy push, no OpenAwayFrom. */
	UFUNCTION(BlueprintPure, Category = "MindKore|Door")
	bool IsSecured() const { return bLocked || bBoltedInside || IsKeyLockEngaged(); }

	/** True when Location lies on the bolt (-X) side of the leaf's closed plane. */
	UFUNCTION(BlueprintPure, Category = "MindKore|Door")
	bool IsInside(const FVector& Location) const;

	// IMKInteractable
	virtual FMKInteractionOffer QueryInteraction(const AActor* Hand) const override;
	virtual void CompleteInteraction(AActor* Hand) override;

	virtual void Tick(float DeltaSeconds) override;
	virtual void BeginPlay() override;
#if WITH_EDITOR
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
#endif

private:
	float CurrentYaw = 0.0f;
	float SwingStartYaw = 0.0f;
	float SwingAlpha = 0.0f;
	float AiOpenRetryAccumulator = 0.0f;

	// Close-and-lock-behind bookkeeping.
	TWeakObjectPtr<AActor> LockBehindOpener;
	bool bOpenerWasInside = false;
	bool bLockBehindArmed = false;
	float LastLockBehindRetryTime = -10.0f;
	void TickLockBehind();

	/** True when Walker is an AI pawn whose remaining path goes through this doorway. */
	bool IsOnRouteOf(const AActor* Walker) const;
	/** Length of the leaf along the wall, whatever axis the mesh was authored on. */
	float GetLeafWidthCm() const;

	/** The one place Query and Complete read the locks from. */
	EMKDoorVerdict VerdictFor(const AActor* Hand) const;

	void ApplyNavigationFootprint();
	void SetState(EMKDoorState NewState);
	/** The pawn the leaf would overlap at CandidateYaw, or null. */
	AActor* FindPawnInLeafArc(float CandidateYaw) const;
};
