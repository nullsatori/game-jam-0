#pragma once

#include "CoreMinimal.h"
// Jam lift: PhysicsControlData.h include and AngularDrive cut - the PhysicsControl plugin is not enabled; only ApplyPointImpulse is used (AMKPhysicsProp).

struct FBodyInstance;

// Unit and backend boundary. Calibration lives in MKPhysicsCalibration.cpp.
// The acceleration-drive cap is NOT a torque in N m on UE 5.8.2.
namespace MKPhysicsBridge
{
	MINDKORE_API bool ApplyPointImpulse(FBodyInstance& Body, const FVector& NewtonSeconds, const FVector& WorldPointCm);
}
