#include "BTTask_ExecuteGoapAction.h"
#include "AIController.h"
#include "GoapAgentBrain.h"
#include "NavigationSystem.h"
#include "Kismet/GameplayStatics.h"
#include "Navigation/PathFollowingComponent.h"
#include "Common/InventoryComponent.h"
#include "Items/BaseItem.h"

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
        // Check if we've already spotted a house we can head to
        AActor* House = Perceptor->GetNearestUnexploredHouse();
        if (House)
        {
            // We know where a house is — navigate straight to it
            Perceptor->ActivateNavigationTarget(House->GetActorLocation());
        }
        else
        {
            // No house spotted yet — wander until AIPerception picks one up
            Perceptor->ActivateWanderMode();
        }
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
    if (!AIC) return;
    APawn* ControlledPawn = AIC->GetPawn();
    if (!ControlledPawn) return;

    UGoapAgentBrain* Brain = AIC->FindComponentByClass<UGoapAgentBrain>();
    UStudentPerceptor* Perceptor = ControlledPawn->FindComponentByClass<UStudentPerceptor>();
    if (!Brain || !Perceptor || Brain->GetCurrentPlan().Num() == 0) return;

    const FGoapAction& CurrentAction = Brain->GetCurrentPlan()[0];

    // --- SearchHouse: wander until we spot a house, then navigate to it ---
    if (CurrentAction.ActionName == TEXT("Action_SearchHouse"))
    {
        if (!Perceptor->IsCurrentActionFinished())
        {
            // If we were wandering and just spotted a house, switch to navigation
            if (Perceptor->GetCurrentTargetHouse() == nullptr)
            {
                AActor* House = Perceptor->GetNearestUnexploredHouse();
                if (House)
                {
                    Perceptor->ActivateNavigationTarget(House->GetActorLocation());
                }
            }
            return; // Still traveling
        }

        // Arrived at the house — mark it explored and complete the action
        Perceptor->MarkCurrentHouseExplored();
        Brain->CompleteCurrentAction();
        FinishLatentTask(OwnerComp, EBTNodeResult::Succeeded);
        return;
    }

    // --- PickupLoot: navigate to item, then actually grab it ---
    if (CurrentAction.ActionName == TEXT("Action_PickupLoot"))
    {
        if (!Perceptor->IsCurrentActionFinished()) return; // Still traveling

        // Arrived near the loot — find the inventory and grab the nearest item
        UInventoryComponent* Inv = ControlledPawn->FindComponentByClass<UInventoryComponent>();
        AActor* LootActor = Perceptor->GetNearestLoot();

        if (Inv && LootActor)
        {
            ABaseItem* Item = Cast<ABaseItem>(LootActor);
            if (Item)
            {
                const TArray<ABaseItem*>& InvSlots = Inv->GetInventory();
                for (int32 i = 0; i < InvSlots.Num(); ++i)
                {
                    if (InvSlots[i] == nullptr)
                    {
                        Inv->GrabItem(i, Item);
                        UE_LOG(LogTemp, Log, TEXT("BTTask: Picked up item '%s' into slot %d"), *Item->GetName(), i);
                        break;
                    }
                }
            }
        }

        Brain->CompleteCurrentAction();
        FinishLatentTask(OwnerComp, EBTNodeResult::Succeeded);
        return;
    }

    // --- Default: all other actions finish when perceptor signals done ---
    if (Perceptor->IsCurrentActionFinished())
    {
        Brain->CompleteCurrentAction();
        FinishLatentTask(OwnerComp, EBTNodeResult::Succeeded);
    }
}