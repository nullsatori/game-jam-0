#pragma once

#include "CoreMinimal.h"

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
