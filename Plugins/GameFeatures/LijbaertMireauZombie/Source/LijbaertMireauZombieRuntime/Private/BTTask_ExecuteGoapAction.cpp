#include "BTTask_ExecuteGoapAction.h"
#include "AIController.h"
#include "GoapAgentBrain.h"
#include "NavigationSystem.h"
#include "Kismet/GameplayStatics.h"
#include "Navigation/PathFollowingComponent.h"

UBTTask_ExecuteGoapAction::UBTTask_ExecuteGoapAction()
{
    NodeName = "Execute GOAP Action";
    bNotifyTick = true; // Crucial: Keeps TickTask firing every frame
}

EBTNodeResult::Type UBTTask_ExecuteGoapAction::ExecuteTask(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory)
{
    AAIController* AIC = OwnerComp.GetAIOwner();
    if (!AIC) return EBTNodeResult::Failed;

    APawn* ControlledPawn = AIC->GetPawn();
    if (!ControlledPawn) return EBTNodeResult::Failed;

    // 1. Grab your custom GOAP Brain sitting on the pawn
    UGoapAgentBrain* Brain = ControlledPawn->FindComponentByClass<UGoapAgentBrain>();
    if (!Brain || Brain->GetCurrentPlan().Num() == 0) return EBTNodeResult::Failed;

    // 2. See what text string action the planner wants to run right now
    FGoapAction CurrentAction = Brain->GetCurrentPlan()[0];

    // ─────────────────────────────────────────────────────────────
    // HARDWARE RUNTIME: WANDER FALLBACK MOVEMENT
    // ─────────────────────────────────────────────────────────────
    if (CurrentAction.ActionName == TEXT("Wander Around"))
    {
        UNavigationSystemV1* NavSys = FNavigationSystem::GetCurrent<UNavigationSystemV1>(GetWorld());
        if (NavSys)
        {
            FNavLocation RandomNavLocation;
            // Find a legal spot on the teacher's pre-existing green NavMesh
            if (NavSys->GetRandomReachablePointInRadius(ControlledPawn->GetActorLocation(), 1500.0f, RandomNavLocation))
            {
                // Command native engine pathfinding to steer the actor around obstacles
                AIC->MoveToLocation(RandomNavLocation.Location, 100.0f);
                
                // Update debug visuals on the brain
                Brain->VisualCurrentActionName = CurrentAction.ActionName;
                return EBTNodeResult::InProgress; // Stay inside this node while walking
            }
        }
    }

    // ─────────────────────────────────────────────────────────────
    // HARDWARE RUNTIME: TARGETED MOVEMENT (e.g., TargetObjectTag is set)
    // ─────────────────────────────────────────────────────────────
    if (!CurrentAction.TargetObjectTag.IsEmpty())
    {
        TArray<AActor*> FoundActors;
        UGameplayStatics::GetAllActorsWithTag(GetWorld(), FName(*CurrentAction.TargetObjectTag), FoundActors);
        AActor* TargetActor = (FoundActors.Num() > 0) ? FoundActors[0] : nullptr;

        if (TargetActor)
        {
            AIC->MoveToActor(TargetActor, 100.0f);
            Brain->VisualCurrentActionName = CurrentAction.ActionName;
            return EBTNodeResult::InProgress;
        }
    }

    // If an action doesn't require walking (like a fast static interaction), handle it instantly
    return EBTNodeResult::Failed;
}

void UBTTask_ExecuteGoapAction::TickTask(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory, float DeltaSeconds)
{
    AAIController* AIC = OwnerComp.GetAIOwner();
    if (!AIC) 
    {
        FinishLatentTask(OwnerComp, EBTNodeResult::Failed);
        return;
    }

    // 3. Monitor native pathfinding state directly from the controller
    if (AIC->GetMoveStatus() == EPathFollowingStatus::Type::Idle)
    {
        APawn* ControlledPawn = AIC->GetPawn();
        UGoapAgentBrain* Brain = ControlledPawn ? ControlledPawn->FindComponentByClass<UGoapAgentBrain>() : nullptr;
        
        if (Brain)
        {
            // Cleanly pop the finished action out of the plan array and apply effects to state
            Brain->CompleteCurrentAction(); 
        }

        // Complete this Behavior Tree task execution step cleanly
        FinishLatentTask(OwnerComp, EBTNodeResult::Succeeded);
    }
}