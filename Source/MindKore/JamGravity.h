#pragma once

#include "CoreMinimal.h"

class AJamGravityZone;
class UWorld;
struct FCollisionQueryParams;
struct FHitResult;

// --- the zones' gravity (Docs/ZeroG.md, «Гравитация для тел») ----------------------------

/** Gravity at a point: the acceleration, cm/s^2 (zero outside walking zones), and the zone giving it. */
struct FJamGravity
{
	FVector Acceleration = FVector::ZeroVector;
	const AJamGravityZone* Zone = nullptr;
};

/**
 * The one gravity rule, for the pawn, loose bodies, corpses and the prediction:
 * of the walking zones (bWalk) containing Location, the higher Priority wins,
 * on a tie the smaller zone. The acceleration is the zone's GravityCm times
 * the global scale (JamSetGravityScale).
 */
MINDKORE_API FJamGravity JamGravityAt(const UWorld* World, const FVector& Location);

/**
 * The debug menu's global multiplier on every zone's gravity (World.GravityScale,
 * Docs/DebugMenu.md): 1 as built, 0 zero-G everywhere. Written by UJamDebugSubsystem only.
 */
MINDKORE_API void JamSetGravityScale(float Scale);
MINDKORE_API float JamGetGravityScale();

/** The same rule over every zone, walking or air-only: whose air is breathed at Location (null: none). */
MINDKORE_API const AJamGravityZone* JamAirZoneAt(const UWorld* World, const FVector& Location);

/**
 * Once per frame before physics (AJamShipGameMode::Tick): every simulated body
 * on the physics-body channel inside a walking zone gets that zone's gravity
 * (AddForce, acceleration; minus the world gravity if the map has one). Skipped:
 * bodies with gravity off (the pawn, a living flyer), tagged Jam.NoGravity, or
 * asleep. A body at rest on something (under 3 cm/s and 10 deg/s, ground within
 * 2 cm under its bounds) two frames running is put to sleep: a push would wake it every frame, and a
 * pushed body never gets slow enough for Chaos to sleep it. A hit or a grab wakes it.
 */
MINDKORE_API void JamPullBodies(UWorld* World);

/** A point of a predicted flight. */
struct FJamFlightPoint
{
	FVector Location = FVector::ZeroVector;
	float Seconds = 0.0f;
};

/**
 * A sphere of RadiusCm thrown from Start at Velocity, for Seconds, under
 * JamGravityAt: stepped as the physics steps a body (velocity, then position)
 * StepSeconds at a time, swept on ECC_PhysicsBody every ~0.1 s. At a contact
 * the normal speed comes back times Restitution, up to Bounces times.
 * OutPath gets the points; returns the first contact (bBlockingHit false: none)
 * and its time in OutHitSeconds.
 */
MINDKORE_API FHitResult JamPredictFlight(const UWorld* World, const FVector& Start, const FVector& Velocity, float RadiusCm,
	float Seconds, float StepSeconds, float Restitution, int32 Bounces, const FCollisionQueryParams& Params,
	TArray<FJamFlightPoint>& OutPath, float& OutHitSeconds);

// --- rotations in a gravity frame ------------------------------------------------------------

// Rotations relative to a gravity direction, as in Epic's custom-gravity
// character example for UE 5.4+ (CharacterMovementComponent::SetGravityDirection).
// With gravity straight down both are the identity, so ordinary walking is unchanged.

/** A world rotation seen from a frame whose "down" is GravityDirection. */
inline FRotator JamGravityRelativeRotation(const FRotator& Rotation, const FVector& GravityDirection)
{
	if (!GravityDirection.Equals(FVector::DownVector))
	{
		const FQuat GravityRotation = FQuat::FindBetweenNormals(GravityDirection, FVector::DownVector);
		return (GravityRotation * Rotation.Quaternion()).Rotator();
	}
	return Rotation;
}

/** Back from that frame to the world. */
inline FRotator JamGravityWorldRotation(const FRotator& Rotation, const FVector& GravityDirection)
{
	if (!GravityDirection.Equals(FVector::DownVector))
	{
		const FQuat GravityRotation = FQuat::FindBetweenNormals(FVector::DownVector, GravityDirection);
		return (GravityRotation * Rotation.Quaternion()).Rotator();
	}
	return Rotation;
}
