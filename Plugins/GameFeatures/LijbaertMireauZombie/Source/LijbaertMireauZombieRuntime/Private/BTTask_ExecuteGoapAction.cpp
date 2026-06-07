#include "BTTask_ExecuteGoapAction.h"
#include "AIController.h"
#include "GoapAgentBrain.h"
#include "NavigationSystem.h"
#include "Kismet/GameplayStatics.h"
#include "Navigation/PathFollowingComponent.h"
#include "Common/InventoryComponent.h"
#include "Items/BaseItem.h"
#include "Items/ItemType.h"
#include "Village/House/House.h"

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
        AActor* HouseActor = Perceptor->GetNearestUnexploredHouse();
        if (HouseActor)
        {
            // Navigate to the geometric center of the house so we end up inside
            AHouse* House = Cast<AHouse>(HouseActor);
            FVector Target = House ? House->GetBounds().Origin : HouseActor->GetActorLocation();
            Perceptor->ActivateNavigationTarget(Target);
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
        // Navigate to the first useful item we can see (garbage is ignored)
        AActor* Loot = Perceptor->GetNearestUsefulLoot();
        if (!Loot)
        {
            // House appears empty (or only garbage) — nothing to do here
            Brain->CompleteCurrentAction();
            FinishLatentTask(OwnerComp, EBTNodeResult::Succeeded);
            return EBTNodeResult::Succeeded;
        }
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
                AActor* HouseActor = Perceptor->GetNearestUnexploredHouse();
                if (HouseActor)
                {
                    AHouse* House = Cast<AHouse>(HouseActor);
                    FVector Target = House ? House->GetBounds().Origin : HouseActor->GetActorLocation();
                    Perceptor->ActivateNavigationTarget(Target);
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

    // --- PickupLoot: navigate to each useful item in the house until none remain or inventory is full ---
    if (CurrentAction.ActionName == TEXT("Action_PickupLoot"))
    {
        if (!Perceptor->IsCurrentActionFinished()) return; // Still traveling to current item

        UInventoryComponent* Inv = ControlledPawn->FindComponentByClass<UInventoryComponent>();
        AActor* LootActor = Perceptor->GetNearestUsefulLoot();

        if (Inv && LootActor)
        {
            ABaseItem* Item = Cast<ABaseItem>(LootActor);
            if (Item)
            {
                // Find the first empty inventory slot and grab the item
                const TArray<ABaseItem*>& Slots = Inv->GetInventory();
                bool bInventoryFull = true;
                for (int32 i = 0; i < Slots.Num(); ++i)
                {
                    if (Slots[i] == nullptr)
                    {
                        Inv->GrabItem(i, Item);
                        Perceptor->PerceivedLoot.Remove(LootActor); // We have it now, stop targeting it
                        UE_LOG(LogTemp, Log, TEXT("BTTask: Grabbed '%s' into slot %d"), *Item->GetName(), i);
                        bInventoryFull = false;
                        break;
                    }
                }

                if (bInventoryFull)
                {
                    UE_LOG(LogTemp, Log, TEXT("BTTask: Inventory full, leaving remaining loot"));
                    Brain->CompleteCurrentAction();
                    FinishLatentTask(OwnerComp, EBTNodeResult::Succeeded);
                    return;
                }
            }
        }

        // Check if there's still more useful loot in the house
        AActor* NextLoot = Perceptor->GetNearestUsefulLoot();
        if (NextLoot)
        {
            // Navigate to the next item — TickTask will fire again when we arrive
            Perceptor->ActivateNavigationTarget(NextLoot->GetActorLocation());
        }
        else
        {
            // House fully looted (or only garbage left)
            UE_LOG(LogTemp, Log, TEXT("BTTask: House fully looted, moving on"));
            Brain->CompleteCurrentAction();
            FinishLatentTask(OwnerComp, EBTNodeResult::Succeeded);
        }
        return;
    }

    // --- Default: all other actions finish when perceptor signals done ---
    if (Perceptor->IsCurrentActionFinished())
    {
        Brain->CompleteCurrentAction();
        FinishLatentTask(OwnerComp, EBTNodeResult::Succeeded);
    }
}