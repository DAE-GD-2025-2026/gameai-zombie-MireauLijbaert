#include "StudentPerceptorLijbaertMireau.h"

#include "Kismet/GameplayStatics.h"
#include "Zombies/BaseZombie.h"
#include "Items/BaseItem.h"
#include "Items/ItemType.h"
#include "Village/House/House.h"
#include "Survivor/SurvivorPawn.h"

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

    // Cast directly to SurvivorPawn — CalculatePath is a plain C++ function, not a UFUNCTION,
    // so reflection (FindFunction/ProcessEvent) can never find it. Direct call is the only option.
    ASurvivorPawn* SurvivorPawn = Cast<ASurvivorPawn>(GetOwner());
    if (!SurvivorPawn) return;

    TArray<FVector> UnrealPoints = SurvivorPawn->CalculatePath(TargetWorldPos);

    if (UnrealPoints.Num() == 0)
    {
        // NavMesh couldn't find a path — fall back to straight line
        UE_LOG(LogTemp, Warning, TEXT("StudentPerceptor: CalculatePath returned empty — falling back to straight line"));
        UnrealPoints.Add(SurvivorPawn->GetActorLocation());
        UnrealPoints.Add(TargetWorldPos);
    }

    // Convert to the 2D path format the PathFollow steering behavior expects
    std::vector<FVector2D> CustomPath;
    for (const FVector& Pt : UnrealPoints)
    {
        CustomPath.push_back(FVector2D(Pt.X, Pt.Y));
    }

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

AActor* UStudentPerceptor::GetNearestUsefulLoot()
{
    AActor* Nearest = nullptr;
    float NearestDistSq = MAX_FLT;
    FVector MyLoc = GetOwner()->GetActorLocation();

    for (AActor* Loot : PerceivedLoot)
    {
        if (!Loot) continue;
        ABaseItem* Item = Cast<ABaseItem>(Loot);
        if (!Item || Item->GetItemType() == EItemType::Garbage) continue;

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