#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "MKGlassFracture.h"
#include "MKGlassShardDebris.generated.h"

class UMaterialInterface;
class UProceduralMeshComponent;
class USoundBase;

/**
 * One piece of glass that has left the frame and is on its way to the floor.
 *
 * It has the pane's thickness and rim, and one convex outline the pane was
 * drawing a moment ago - or a piece of one. Glass leaving a frame breaks again
 * on the way out, so AMKGlassPane cuts what it releases down to a size that
 * belongs on a floor (MKShatterShard) before handing it here; the pane's own
 * shards stay the size that makes its hole read.
 *
 * What does not change is the KIND of shape: still convex, still the pane's
 * thickness, still a piece of the outline that was in the frame. A piece that
 * changed shape as it came loose would read as a swap, and a swap is exactly
 * what the partition looked like before this existed: the hole appeared in one
 * frame and the glass that used to be there was simply gone.
 *
 * It is deliberately not a Niagara burst. DESIGN_LEVEL section 1.3 point 3 asks
 * broken glass to leave a NOISY FLOOR behind - a standing, weak stimulus - and
 * particles leave nothing. These land, settle, and stay.
 *
 * Lifetime. After FreezeAfterSeconds, physics is switched off once Chaos
 * reports the piece asleep, so a corridor full of broken glass costs draw calls
 * and not a solver. Its collision stays on: pieces land on each other, and a
 * frozen piece is still floor for the next one. The pane, not the debris, owns
 * how many pieces are allowed to exist.
 */
UCLASS()
class MINDKORE_API AMKGlassShardDebris : public AActor
{
	GENERATED_BODY()

public:
	AMKGlassShardDebris();

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "MindKore|Glass")
	TObjectPtr<UProceduralMeshComponent> ShardMesh = nullptr;

	/**
	 * First settlement check, in seconds. Moving pieces keep simulating and
	 * are checked again every half second; this never freezes a falling piece.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MindKore|Glass")
	float FreezeAfterSeconds = 4.0f;

	/**
	 * What this piece says when it reaches the floor, or nothing if the pane
	 * spent its landing budget before this piece left the frame. It is the tail
	 * of the break: the round is a bang, the collapse is a rustle, and this is
	 * the glass telling the player where it went - and what the floor he is
	 * about to cross now sounds like (DESIGN_LEVEL 4.2, object 1).
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MindKore|Glass")
	TObjectPtr<USoundBase> LandingSound = nullptr;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MindKore|Glass", meta = (ClampMin = "0"))
	float LandingSoundVolume = 0.22f;

	/**
	 * Below this impact speed the piece is settling, not landing. Glass sliding
	 * the last centimetre off a chair would otherwise chime like it fell three
	 * metres, and the tail of the break would never end.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MindKore|Glass", meta = (ClampMin = "0"))
	float LandingSoundMinSpeedCmS = 120.0f;

	/**
	 * Build the piece and throw it. Shard is in the pane's own frame; the actor
	 * is placed at the shard's centroid in world space, so it turns about its
	 * middle rather than about the pane's origin.
	 */
	void LaunchShard(
		const FMKGlassShard& Shard,
		const FTransform& PaneTransform,
		float ThicknessCm,
		float UvTilingCm,
		UMaterialInterface* Material,
		UMaterialInterface* EdgeMaterial,
		const FVector& LaunchVelocity);

protected:
	virtual void BeginPlay() override;

	/** First real contact: the piece is heard once and then never again. */
	UFUNCTION()
	void OnShardHit(
		UPrimitiveComponent* HitComponent,
		AActor* OtherActor,
		UPrimitiveComponent* OtherComponent,
		FVector NormalImpulse,
		const FHitResult& Hit);

private:
	void FreezeInPlace();

	FTimerHandle FreezeTimer;

	/** A piece of glass lands once. Anything after that is it settling. */
	bool bHasLanded = false;
};
