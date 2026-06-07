#include "BTTask_ExecuteGoapAction.h"
#include "AIController.h"
#include "GoapAgentBrain.h"
#include "NavigationSystem.h"
#include "Kismet/GameplayStatics.h"
#include "Navigation/PathFollowingComponent.h"

UBTTask_ExecuteGoapAction::UBTTask_ExecuteGoapAction()
{
    NodeName = "Execute GOAP Action";
    bNotifyTick = true;
}

EBTNodeResult::Type UBTTask_ExecuteGoapAction::ExecuteTask(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory)
{
    AAIController* AIC = OwnerComp.GetAIOwner();
    if (!AIC) { UE_LOG(LogTemp, Error, TEXT("BTTask: No AIC")); return EBTNodeResult::Failed; }

    APawn* ControlledPawn = AIC->GetPawn();
    if (!ControlledPawn) { UE_LOG(LogTemp, Error, TEXT("BTTask: No Pawn")); return EBTNodeResult::Failed; }

    UGoapAgentBrain* Brain = AIC->FindComponentByClass<UGoapAgentBrain>();
    if (!Brain) { UE_LOG(LogTemp, Error, TEXT("BTTask: No Brain found on AIC")); return EBTNodeResult::Failed; }
    if (Brain->GetCurrentPlan().Num() == 0) { UE_LOG(LogTemp, Error, TEXT("BTTask: Brain has no plan")); return EBTNodeResult::Failed; }

    UStudentPerceptor* Perceptor = ControlledPawn->FindComponentByClass<UStudentPerceptor>();
    if (!Perceptor) { UE_LOG(LogTemp, Error, TEXT("BTTask: No Perceptor found on Pawn")); return EBTNodeResult::Failed; }

    FGoapAction CurrentAction = Brain->GetCurrentPlan()[0];
    UE_LOG(LogTemp, Warning, TEXT("BTTask: Trying to execute action '%s'"), *CurrentAction.ActionName);
    
    if (CurrentAction.ActionName == TEXT("Action_Kiting"))
    {
        AActor* Threat = Perceptor->GetHighestThreatZombie();
        if (!Threat) return EBTNodeResult::Failed;
        Perceptor->ActivateFleeFrom(Threat->GetActorLocation());
        return EBTNodeResult::InProgress;
    }
    if (CurrentAction.ActionName == TEXT("Action_SearchHouse"))
    {
        // Navigate toward a random nearby house tag
        // For now: just wander until you have a house system
        Perceptor->ActivateWanderMode();
        return EBTNodeResult::InProgress;
    }
    if (CurrentAction.ActionName == TEXT("Action_PickupLoot"))
    {
        AActor* Loot = Perceptor->GetNearestLoot();
        if (!Loot) return EBTNodeResult::Failed;
        Perceptor->ActivateNavigationTarget(Loot->GetActorLocation());
        return EBTNodeResult::InProgress;
    }
    if (CurrentAction.ActionName == TEXT("Action_UseMedkit"))
    {
        // No movement needed — instant use
        // Just complete it immediately
        Brain->CompleteCurrentAction();
        FinishLatentTask(OwnerComp, EBTNodeResult::Succeeded);
        return EBTNodeResult::Succeeded;
    }
   
    return EBTNodeResult::Failed;
}

void UBTTask_ExecuteGoapAction::TickTask(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory, float DeltaSeconds)
{
    AAIController* AIC = OwnerComp.GetAIOwner();
    APawn* ControlledPawn = AIC->GetPawn();
    UGoapAgentBrain* Brain = ControlledPawn->FindComponentByClass<UGoapAgentBrain>();
    UStudentPerceptor* Perceptor = ControlledPawn->FindComponentByClass<UStudentPerceptor>();
    
    if (Perceptor->IsCurrentActionFinished())
    {
        Brain->CompleteCurrentAction();
        FinishLatentTask(OwnerComp, EBTNodeResult::Succeeded);
    }
}