#include "StudentPerceptorLijbaertMireau.h"

#include "Kismet/GameplayStatics.h"
#include "Zombies/BaseZombie.h"
#include "Items/BaseItem.h"
#include "Village/House/House.h"

UStudentPerceptor::UStudentPerceptor()
{
    PrimaryComponentTick.bCanEverTick = true;

    // Instantiate your ported classes
    MyWanderBehavior = new Wander();
    MyPathFollowBehavior = new PathFollow();
    MyFleeBehavior = new Flee();
}

void UStudentPerceptor::BeginPlay()
{
    Super::BeginPlay();
    
    if (auto PerceptionComp = GetOwner()->GetComponentByClass<UAIPerceptionComponent>())
    {
       PerceptionComp->OnTargetPerceptionUpdated.AddDynamic(this, &UStudentPerceptor::OnPerceptionUpdated);
    }
}

void UStudentPerceptor::ActivateWanderMode()
{
    bActionFinished = false;
    CurrentState = EMovementState::Wander;
}

void UStudentPerceptor::ActivateNavigationTarget(const FVector& TargetWorldPos)
{
    bActionFinished = false;
    
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

    // Feed ported PathFollow algorithm
    MyPathFollowBehavior->SetPath(CustomPath);
    CurrentState = EMovementState::PathFollowing;
}

void UStudentPerceptor::ActivateFleeFrom(const FVector& ThreatWorldPos)
{
    bActionFinished = false;
    FTargetData T;
    T.Position = FVector2D(ThreatWorldPos.X, ThreatWorldPos.Y);
    MyFleeBehavior->SetTarget(T);
    CurrentState = EMovementState::Fleeing;
}

AActor* UStudentPerceptor::GetNearestLoot()
{
    if (PerceivedLoot.Num() == 0) return nullptr;

    AActor* Nearest = nullptr;
    float NearestDistSq = MAX_FLT;
    FVector MyLoc = GetOwner()->GetActorLocation();

    for (AActor* Loot : PerceivedLoot)
    {
        float DistSq = FVector::DistSquared(MyLoc, Loot->GetActorLocation());
        if (DistSq < NearestDistSq)
        {
            NearestDistSq = DistSq;
            Nearest = Loot;
        }
    }
    return Nearest;
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
        
        // Detect when we've arrived (steering goes near-zero at destination)
        if (MathOutput.LinearVelocity.SizeSquared() < 1.f)
        {
            bActionFinished = true;
            CurrentState = EMovementState::None;
        }
    }
    else if (CurrentState == EMovementState::Fleeing)
    {
        // Keep updating the threat position every tick so flee stays accurate
        AActor* Threat = GetHighestThreatZombie();
        if (Threat)
        {
            FTargetData T;
            T.Position = FVector2D(Threat->GetActorLocation().X, Threat->GetActorLocation().Y);
            T.LinearVelocity = FVector2D(Threat->GetVelocity().X, Threat->GetVelocity().Y);
            MyFleeBehavior->SetTarget(T);
        }
        else
        {
            // Lost the zombie — done fleeing
            bActionFinished = true;
            CurrentState = EMovementState::None;
        }
        MathOutput = MyFleeBehavior->CalculateSteering(DeltaTime, Agent);
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
    
    GEngine->AddOnScreenDebugMessage(5, 1.f, FColor::Green, FString::Printf(TEXT("Saw Something!")));
    
    if (!Actor) return;

    // Use proper type checks via Cast — no string hacking needed since we depend on GameAI_Zombie
    const bool bIsZombie = Cast<ABaseZombie>(Actor) != nullptr;
    const bool bIsLoot   = Cast<ABaseItem>(Actor)   != nullptr;
    const bool bIsHouse  = Cast<AHouse>(Actor)      != nullptr;

    if (Stimulus.WasSuccessfullySensed())
    {
        if (bIsZombie) PerceivedZombies.AddUnique(Actor);
        if (bIsLoot)   PerceivedLoot.AddUnique(Actor);
        if (bIsHouse)  PerceivedHouses.AddUnique(Actor);
    }
    else
    {
        if (bIsZombie) PerceivedZombies.Remove(Actor);
        if (bIsLoot)   PerceivedLoot.Remove(Actor);
        // Houses stay in memory even out of sight — once you see a house, you remember where it is
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
    return ClosestZombie;
}

AActor* UStudentPerceptor::GetNearestUnexploredHouse()
{
    // Only consider houses we have actually spotted via AIPerception (limited information)
    AActor* Nearest = nullptr;
    float NearestDistSq = MAX_FLT;
    FVector MyLoc = GetOwner()->GetActorLocation();

    for (AActor* House : PerceivedHouses)
    {
        if (!House || ExploredHouses.Contains(House)) continue;
        float DistSq = FVector::DistSquared(MyLoc, House->GetActorLocation());
        if (DistSq < NearestDistSq)
        {
            NearestDistSq = DistSq;
            Nearest = House;
        }
    }

    // Remember which house we're heading to so MarkCurrentHouseExplored knows what to mark
    if (Nearest)
    {
        CurrentTargetHouse = Nearest;
    }

    return Nearest;
}

void UStudentPerceptor::MarkCurrentHouseExplored()
{
    if (CurrentTargetHouse)
        ExploredHouses.AddUnique(CurrentTargetHouse);
    CurrentTargetHouse = nullptr;
}