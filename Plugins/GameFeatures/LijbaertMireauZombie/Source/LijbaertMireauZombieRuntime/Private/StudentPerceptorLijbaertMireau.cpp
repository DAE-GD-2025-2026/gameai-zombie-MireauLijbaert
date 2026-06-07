#include "StudentPerceptorLijbaertMireau.h"

#include "Kismet/GameplayStatics.h"
#include "NavigationSystem.h"
#include "Zombies/BaseZombie.h"
#include "Items/BaseItem.h"
#include "Items/ItemType.h"
#include "Village/House/House.h"
#include "Survivor/SurvivorPawn.h"

UStudentPerceptor::UStudentPerceptor()
{
    PrimaryComponentTick.bCanEverTick = true;

    MyWanderBehavior     = new Wander();
    MyDriftSeek          = new Seek();
    MyBlendedWander      = new BlendedSteering({
        { MyWanderBehavior, 0.75f },  // organic wandering
        { MyDriftSeek,      0.25f }   // gentle drift toward a distant target
    });
    MyPathFollowBehavior = new PathFollow();
    MyFleeBehavior       = new Flee();
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
    PickNewDriftTarget();
}

void UStudentPerceptor::PickNewDriftTarget()
{
    FVector MyLoc = GetOwner()->GetActorLocation();
    UNavigationSystemV1* NavSys = UNavigationSystemV1::GetCurrent(GetWorld());
    FNavLocation NavResult;

    if (NavSys && NavSys->GetRandomReachablePointInRadius(MyLoc, FMath::RandRange(3000.f, 6000.f), NavResult))
    {
        CurrentDriftTarget = FVector2D(NavResult.Location.X, NavResult.Location.Y);
    }
    else
    {
        // Fallback if NavMesh unavailable
        float Angle = FMath::RandRange(0.f, 2.f * PI);
        float Dist  = FMath::RandRange(3000.f, 6000.f);
        CurrentDriftTarget = FVector2D(MyLoc.X + FMath::Cos(Angle) * Dist,
                                       MyLoc.Y + FMath::Sin(Angle) * Dist);
    }
    UE_LOG(LogTemp, Log, TEXT("StudentPerceptor: New drift target (%.0f, %.0f)"), CurrentDriftTarget.X, CurrentDriftTarget.Y);
}

void UStudentPerceptor::ActivateRotationSearch()
{
    bActionFinished = false;
    bHouseRotationDone = false;
    TotalRotationDone = 0.0f;
    CurrentState = EMovementState::RotatingSearch;
    UE_LOG(LogTemp, Log, TEXT("StudentPerceptor: Starting 360 rotation scan inside house"));
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
    // Search persistent memory — KnownItems survives the FOV sweeping away after a house scan
    AActor* Nearest = nullptr;
    float NearestDistSq = MAX_FLT;
    FVector MyLoc = GetOwner()->GetActorLocation();

    for (AActor* Loot : KnownItems)
    {
        if (!IsValid(Loot)) continue; // Skip actors that have been destroyed/picked up by others
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
        FVector MyLoc = PawnOwner->GetActorLocation();
        FVector2D MyLoc2D(MyLoc.X, MyLoc.Y);

        // Refresh drift target when we've arrived
        if (FVector2D::DistSquared(MyLoc2D, CurrentDriftTarget) < 400.f * 400.f)
        {
            PickNewDriftTarget();
            WanderStuckTimer = 0.f;
        }

        // Stuck detection if barely moving for 1.5s, pathfind to drift target to route around the wall
        const float SpeedSq = PawnOwner->GetVelocity().SizeSquared2D();
        if (SpeedSq < 50.f * 50.f)
        {
            WanderStuckTimer += DeltaTime;
            if (WanderStuckTimer >= 1.5f)
            {
                WanderStuckTimer = 0.f;
                UE_LOG(LogTemp, Log, TEXT("StudentPerceptor: Wander stuck — pathfinding around wall to drift target"));
                FVector DriftTarget3D(CurrentDriftTarget.X, CurrentDriftTarget.Y, MyLoc.Z);
                ActivateNavigationTarget(DriftTarget3D);
                return;
            }
        }
        else
        {
            WanderStuckTimer = 0.f;
        }

        FTargetData DriftData;
        DriftData.Position = CurrentDriftTarget;
        MyDriftSeek->SetTarget(DriftData);

        MathOutput = MyBlendedWander->CalculateSteering(DeltaTime, Agent);
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
    else if (CurrentState == EMovementState::RotatingSearch)
    {
        // Rotate the pawn in place — perception fires as items come into the FOV cone
        TotalRotationDone += RotationSearchSpeed * DeltaTime;
        float NewYaw = PawnOwner->GetActorRotation().Yaw + RotationSearchSpeed * DeltaTime;
        PawnOwner->SetActorRotation(FRotator(0.f, NewYaw, 0.f));

        if (TotalRotationDone >= 360.0f)
        {
            bHouseRotationDone = true;
            bActionFinished = true;
            CurrentState = EMovementState::None;
            UE_LOG(LogTemp, Log, TEXT("StudentPerceptor: 360 scan complete, %d loot items spotted"), PerceivedLoot.Num());
        }
        return; // No movement output needed — rotation is applied directly above
    }
    else if (CurrentState == EMovementState::Fleeing)
    {
        // Keep updating the threat position every tick so flee stays accurate
        AActor* Threat = GetHighestThreatZombie();
        if (Threat)
        {
            // Stop fleeing once we're a safe distance away — GOAP replans and starts a fresh
            // flee cycle if the zombie is still perceived, preventing an endless sprint to the
            // map edge. Tune SafeFleeDistSq to match your perception radius.
            const float SafeFleeDistSq = 2000.f * 2000.f;
            if (FVector::DistSquared(GetOwner()->GetActorLocation(), Threat->GetActorLocation()) > SafeFleeDistSq)
            {
                bActionFinished = true;
                CurrentState = EMovementState::None;
            }
            else
            {
                FTargetData T;
                T.Position = FVector2D(Threat->GetActorLocation().X, Threat->GetActorLocation().Y);
                T.LinearVelocity = FVector2D(Threat->GetVelocity().X, Threat->GetVelocity().Y);
                MyFleeBehavior->SetTarget(T);
            }
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

        // Only override rotation if the BT task isn't controlling it externally (e.g. during kiting)
        if (!bExternalRotationControl)
        {
            FRotator TargetRotation = FRotationMatrix::MakeFromX(MovementVector).Rotator();
            PawnOwner->SetActorRotation(FRotator(0.f, TargetRotation.Yaw, 0.f));
        }
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
        if (bIsHouse)  PerceivedHouses.AddUnique(Actor);
        if (bIsLoot)
        {
            PerceivedLoot.AddUnique(Actor);
            // Also store in persistent memory — survives leaving the FOV
            KnownItems.AddUnique(Actor);
        }
    }
    else
    {
        // Lost sight — remove from "currently visible" lists
        if (bIsZombie) PerceivedZombies.Remove(Actor);
        if (bIsLoot)   PerceivedLoot.Remove(Actor);
        // KnownItems and PerceivedHouses intentionally NOT cleared here — we remember them
    }
}

AActor* UStudentPerceptor::GetHighestThreatZombie()
{
    // Purge any zombies that have been destroyed (e.g. shot dead) since perception last updated
    PerceivedZombies.RemoveAll([](AActor* Z) { return !IsValid(Z); });

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