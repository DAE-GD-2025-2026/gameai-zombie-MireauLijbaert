#include "StudentPerceptorLijbaertMireau.h"

UStudentPerceptor::UStudentPerceptor()
{
    PrimaryComponentTick.bCanEverTick = true;

    // Instantiate your ported classes
    MyWanderBehavior = new Wander();
    MyPathFollowBehavior = new PathFollow();
}

void UStudentPerceptor::BeginPlay()
{
    Super::BeginPlay();
    
    if (auto PerceptionComp = GetOwner()->GetComponentByClass<UAIPerceptionComponent>())
    {
       PerceptionComp->OnTargetPerceptionUpdated.AddDynamic(this, &UStudentPerceptor::OnPerceptionUpdated);
    }

    // TEMP
    ActivateWanderMode(); 
}

void UStudentPerceptor::ActivateWanderMode()
{
    CurrentState = EMovementState::Wander;
}

void UStudentPerceptor::ActivateNavigationTarget(const FVector& TargetWorldPos)
{
    APawn* GenericPawn = Cast<APawn>(GetOwner());
    if (!GenericPawn) return;
    
    struct FCalculatePathArgs
    {
        FVector TargetLocation;      // Input parameter matching her function signature
        TArray<FVector> ReturnValue; // Output/Return value matching her function signature
    };

    FCalculatePathArgs Args;
    Args.TargetLocation = TargetWorldPos;

    // Find the function by its exact text name inside her blueprint/pawn class
    UFunction* Func = GenericPawn->FindFunction(FName("CalculatePath"));
    
    TArray<FVector> UnrealPoints;
    if (Func)
    {
        // Execute the function safely on her pawn from the outside
        GenericPawn->ProcessEvent(Func, &Args);
        UnrealPoints = Args.ReturnValue;
    }
    else
    {
        // Fallback: If the reflection lookup fails, just go straight to the target in a straight line
        UnrealPoints.Add(GenericPawn->GetActorLocation());
        UnrealPoints.Add(TargetWorldPos);
    }

    // Convert the results to your custom 2D vector path format
    std::vector<FVector2D> CustomPath;
    for (const FVector& Pt : UnrealPoints)
    {
        CustomPath.push_back(FVector2D(Pt.X, Pt.Y));
    }

    // 4. Feed your ported PathFollow algorithm
    MyPathFollowBehavior->SetPath(CustomPath);
    CurrentState = EMovementState::PathFollowing;
}

void UStudentPerceptor::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
    Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

    APawn* PawnOwner = Cast<APawn>(GetOwner());
    if (!PawnOwner || CurrentState == EMovementState::None) return;

    // Instantiate Agent (logic part to send to the survivor)
    ASteeringAgent Agent(PawnOwner);
    SteeringOutput MathOutput{};
    
    if (CurrentState == EMovementState::Wander)
    {
        MathOutput = MyWanderBehavior->CalculateSteering(DeltaTime, Agent);
    }
    else if (CurrentState == EMovementState::PathFollowing)
    {
        MathOutput = MyPathFollowBehavior->CalculateSteering(DeltaTime, Agent);
    }

    // Translate 2D Steering output velocity back to 3D Unreal movement space
    FVector MovementVector = FVector(MathOutput.LinearVelocity.X, MathOutput.LinearVelocity.Y, 0.f);

    if (MovementVector.SizeSquared() > 1.f)
    {
        PawnOwner->AddMovementInput(MovementVector.GetSafeNormal(), 1.0f);
        
        FRotator TargetRotation = FRotationMatrix::MakeFromX(MovementVector).Rotator();
        PawnOwner->SetActorRotation(FRotator(0.f, TargetRotation.Yaw, 0.f));
    }
}

void UStudentPerceptor::OnPerceptionUpdated(AActor* Actor, FAIStimulus Stimulus)
{
    
    // Keeping your existing debug print
    GEngine->AddOnScreenDebugMessage(5, 1.f, FColor::Green, FString::Printf(TEXT("Saw Something!")));
    
    if (!Actor) return;

    // Detect if the actor is a zombie or loot based on its class name or tags
    bool bIsZombie = Actor->ActorHasTag(FName("Zombie")) || Actor->GetName().Contains(TEXT("Zombie"));
    bool bIsLoot = Actor->ActorHasTag(FName("Loot")) || Actor->GetName().Contains(TEXT("Item")) || Actor->GetName().Contains(TEXT("Pickup"));

    if (Stimulus.WasSuccessfullySensed())
    {
        if (bIsZombie) PerceivedZombies.AddUnique(Actor);
        if (bIsLoot) PerceivedLoot.AddUnique(Actor);
    }
    else
    {
        // Lost sight of them
        if (bIsZombie) PerceivedZombies.Remove(Actor);
        if (bIsLoot) PerceivedLoot.Remove(Actor);
    }
}

AActor* UStudentPerceptor::GetHighestThreatZombie()
{
    if (PerceivedZombies.Num() == 0) return nullptr;

    AActor* ClosestZombie = nullptr;
    float ClosestDistanceSq = MAX_FLT;
    FVector MyLoc = GetOwner()->GetActorLocation();

    for (AActor* Zombie : PerceivedZombies)
    {
        float DistSq = FVector::DistSquared(MyLoc, Zombie->GetActorLocation());
        if (DistSq < ClosestDistanceSq)
        {
            ClosestDistanceSq = DistSq;
            ClosestZombie = Zombie;
        }
    }
    return ClosestZombie; // This is the zombie your steering framework should Flee from or Shoot at!
}