#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/FloatingPawnMovement.h"

struct SteeringOutput
{
    FVector2D LinearVelocity = FVector2D::ZeroVector;
    float AngularVelocity = 0.0f;
    bool IsValid = true;
};

struct FTargetData
{
    FVector2D Position = FVector2D::ZeroVector;
    FVector2D LinearVelocity = FVector2D::ZeroVector;
};

class ASteeringAgent
{
private:
    APawn* WrappedPawn = nullptr;
    UFloatingPawnMovement* MovementComp = nullptr;
    bool bAutoOrient = true;
    float MaxLinearSpeed = 400.f;
    float MaxAngularSpeed = 180.f;

public:
    ASteeringAgent(APawn* InPawn)
    {
        WrappedPawn = InPawn;
        if (WrappedPawn)
        {
            MovementComp = WrappedPawn->FindComponentByClass<UFloatingPawnMovement>();
        }
    }

    FVector2D GetPosition() const 
    { 
        if (!WrappedPawn) return FVector2D::ZeroVector;
        return FVector2D(WrappedPawn->GetActorLocation().X, WrappedPawn->GetActorLocation().Y); 
    }
    
    FVector GetVelocity() const 
    {
        if (!MovementComp) return FVector::ZeroVector;
        return MovementComp->Velocity;
    }
    
    FVector GetLinearVelocity() const
    {
        return GetVelocity();
    }

    FVector GetActorLocation() const { return WrappedPawn ? WrappedPawn->GetActorLocation() : FVector::ZeroVector; }
    FVector GetActorForwardVector() const { return WrappedPawn ? WrappedPawn->GetActorForwardVector() : FVector::ForwardVector; }
    UWorld* GetWorld() const { return WrappedPawn ? WrappedPawn->GetWorld() : nullptr; }
    
    float GetCapsuleRadius() const { return 50.f; } 
    float GetRotation() const { return WrappedPawn ? WrappedPawn->GetActorRotation().Yaw : 0.f; }
    
    float GetMaxLinearSpeed() const { return MovementComp ? MovementComp->MaxSpeed : MaxLinearSpeed; }
    void SetMaxLinearSpeed(float NewSpeed) { if (MovementComp) MovementComp->MaxSpeed = NewSpeed; }
    
    float GetMaxAngularSpeed() const { return MaxAngularSpeed; }
    void SetIsAutoOrienting(bool bOrient) { bAutoOrient = bOrient; }
    
    bool GetDebugRenderingEnabled() const { return true; } 
};