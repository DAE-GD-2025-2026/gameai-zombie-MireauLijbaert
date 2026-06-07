#include "BTTask_ExecuteGoapAction.h"
#include "AIController.h"
#include "GoapAgentBrain.h"
#include "NavigationSystem.h"
#include "Kismet/GameplayStatics.h"
#include "Navigation/PathFollowingComponent.h"
#include "Common/InventoryComponent.h"
#include "Common/HealthComponent.h"
#include "Items/BaseItem.h"
#include "Items/ItemType.h"
#include "Items/Weapon.h"
#include "Items/Pistol.h"
#include "Items/Shotgun.h"
#include "Items/Medkit.h"
#include "Items/Food.h"
#include "Common/StaminaComponent.h"
#include "Village/House/House.h"
#include "Survivor/SurvivorPawn.h"

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
    if (Brain->GetCurrentPlan().Num() == 0)
    {
        // Brain is still replanning (e.g. right after completing the last action) stay alive and wait
        return EBTNodeResult::InProgress;
    }

    UStudentPerceptor* Perceptor = ControlledPawn->FindComponentByClass<UStudentPerceptor>();
    if (!Perceptor) { UE_LOG(LogTemp, Error, TEXT("BTTask: No Perceptor found on Pawn")); return EBTNodeResult::Failed; }

    // Always reset external rotation control at the start of each action
    Perceptor->bExternalRotationControl = false;
    KitingShootTimer = 0.f;
    KitingApproachTimer = 0.f;
    bKitingToHouse = false;

    FGoapAction CurrentAction = Brain->GetCurrentPlan()[0];
    UE_LOG(LogTemp, Warning, TEXT("BTTask: Trying to execute action '%s'"), *CurrentAction.ActionName);

    if (CurrentAction.ActionName == TEXT("Action_Kiting"))
    {
        // TickTask owns all kiting movement init (armed=fight, unarmed=flee/house-dash).
        // Supports both direct threat and threat-memory-only cases.
        return EBTNodeResult::InProgress;
    }
    if (CurrentAction.ActionName == TEXT("Action_SearchHouse"))
    {
        AActor* HouseActor = Perceptor->GetNearestUnexploredHouse();
        if (HouseActor)
        {
            AHouse* House = Cast<AHouse>(HouseActor);
            FVector Target = House ? House->GetBounds().Origin : HouseActor->GetActorLocation();
            Perceptor->ActivateNavigationTarget(Target);
        }
        else
        {
            // No house in sight yet wander until perception spots one
            Perceptor->ActivateWanderMode();
        }
        return EBTNodeResult::InProgress;
    }
    if (CurrentAction.ActionName == TEXT("Action_PickupLoot"))
    {
        // Reset state from previous action
        Perceptor->BeginLootSearch();
        return EBTNodeResult::InProgress;
    }
    if (CurrentAction.ActionName == TEXT("Action_UseMedkit"))
    {
        ASurvivorPawn* SurvivorPawn = Cast<ASurvivorPawn>(ControlledPawn);
        UInventoryComponent* Inv = ControlledPawn->FindComponentByClass<UInventoryComponent>();
        if (SurvivorPawn && Inv)
        {
            const TArray<ABaseItem*>& Slots = Inv->GetInventory();
            for (int32 i = 0; i < Slots.Num(); ++i)
            {
                if (AMedkit* Medkit = Cast<AMedkit>(Slots[i]))
                {
                    Medkit->UseItem(*SurvivorPawn);
                    Inv->RemoveItem(i);
                    UE_LOG(LogTemp, Log, TEXT("BTTask: Used and consumed medkit '%s' from slot %d"), *Medkit->GetName(), i);
                    break;
                }
            }
        }
        Brain->CompleteCurrentAction();
        FinishLatentTask(OwnerComp, EBTNodeResult::Succeeded);
        return EBTNodeResult::Succeeded;
    }
    if (CurrentAction.ActionName == TEXT("Action_UseFood"))
    {
        ASurvivorPawn* SurvivorPawn = Cast<ASurvivorPawn>(ControlledPawn);
        UInventoryComponent* Inv = ControlledPawn->FindComponentByClass<UInventoryComponent>();
        if (SurvivorPawn && Inv)
        {
            UStaminaComponent* StamComp = SurvivorPawn->FindComponentByClass<UStaminaComponent>();
            const float StaminaMissing = StamComp ? (StamComp->GetMaxStamina() - StamComp->GetCurrentStamina()) : 10.0f;
            const TArray<ABaseItem*>& Slots = Inv->GetInventory();
            for (int32 i = 0; i < Slots.Num(); ++i)
            {
                if (AFood* Food = Cast<AFood>(Slots[i]))
                {
                    if (Food->GetValue() > 0 && static_cast<float>(Food->GetValue()) <= StaminaMissing)
                    {
                        UE_LOG(LogTemp, Log, TEXT("BTTask: Eating food '%s' (value %d, stamina missing %.1f)"), *Food->GetName(), Food->GetValue(), StaminaMissing);
                        Food->UseItem(*SurvivorPawn);
                        Inv->RemoveItem(i);
                        break;
                    }
                }
            }
        }
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

    // ─── Shared inventory-limit helper ───────────────────────────────────────────
    // Returns true if the survivor should pick up this item given current counts.
    // Limits: 3 weapons combined (pistol+shotgun), 2 medkits, 2 food items.
    // Garbage is always skipped.
    auto ShouldPickUp = [](ABaseItem* Item, UInventoryComponent* Inv) -> bool
    {
        if (!Item || !Inv) return false;
        if (Item->GetItemType() == EItemType::Garbage) return false;

        int32 NW = 0, NM = 0, NF = 0;
        for (ABaseItem* Slot : Inv->GetInventory())
        {
            if (!Slot) continue;
            if (Cast<AWeapon>(Slot)) ++NW;
            else if (Cast<AMedkit>(Slot)) ++NM;
            else if (Cast<AFood>(Slot))   ++NF;
        }
        if (Cast<AWeapon>(Item))  return NW < 3;
        if (Cast<AMedkit>(Item)) return NM < 2;
        if (Cast<AFood>(Item))   return NF < 2;
        return true; // unknown item types — always accept
    };

    // Opportunistic pickup: grab any wanted item within touching range every tick,
    // regardless of current action. Respects per-type inventory limits.
    {
        UInventoryComponent* Inv = ControlledPawn->FindComponentByClass<UInventoryComponent>();
        if (Inv)
        {
            const float GrabRadiusSq = 150.f * 150.f;
            FVector MyLoc = ControlledPawn->GetActorLocation();

            for (int32 i = Perceptor->KnownItems.Num() - 1; i >= 0; --i)
            {
                AActor* LootActor = Perceptor->KnownItems[i];
                if (!IsValid(LootActor)) { Perceptor->KnownItems.RemoveAt(i); continue; }
                if (FVector::DistSquared(MyLoc, LootActor->GetActorLocation()) > GrabRadiusSq) continue;

                ABaseItem* Item = Cast<ABaseItem>(LootActor);
                if (!ShouldPickUp(Item, Inv)) continue; // skip garbage or over-limit types

                const TArray<ABaseItem*>& Slots = Inv->GetInventory();
                for (int32 Slot = 0; Slot < Slots.Num(); ++Slot)
                {
                    if (Slots[Slot] == nullptr)
                    {
                        Inv->GrabItem(Slot, Item);
                        Perceptor->PerceivedLoot.Remove(LootActor);
                        Perceptor->KnownItems.RemoveAt(i);
                        UE_LOG(LogTemp, Log, TEXT("BTTask: Opportunistic pickup '%s' into slot %d"), *Item->GetName(), Slot);
                        break;
                    }
                }
                break; // one item per tick
            }
        }
    }

    const FGoapAction& CurrentAction = Brain->GetCurrentPlan()[0];

    // SearchHouse: exit house → wander → spot house → navigate to center → rotate 360° to scan
    if (CurrentAction.ActionName == TEXT("Action_SearchHouse"))
    {
        // State: wandering in open world check each tick if a house comes into view
        if (Perceptor->IsWandering())
        {
            AActor* HouseActor = Perceptor->GetNearestUnexploredHouse();
            if (HouseActor)
            {
                AHouse* House = Cast<AHouse>(HouseActor);
                FVector Target = House ? House->GetBounds().Origin : HouseActor->GetActorLocation();
                Perceptor->ActivateNavigationTarget(Target);
            }
            return;
        }

        if (!Perceptor->IsCurrentActionFinished())
        {
            // State: rotation in progress just wait
            if (Perceptor->IsRotatingSearch())
                return;

            // State: navigating redirect if a house is spotted mid-path
            if (Perceptor->IsNavigating())
            {
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
                return;
            }

            // State: idle (None) plan just became ready after ExecuteTask returned early.
            // Mirror what ExecuteTask would have done.
            AActor* HouseActor = Perceptor->GetNearestUnexploredHouse();
            if (HouseActor)
            {
                AHouse* House = Cast<AHouse>(HouseActor);
                FVector Target = House ? House->GetBounds().Origin : HouseActor->GetActorLocation();
                Perceptor->ActivateNavigationTarget(Target);
            }
            else
            {
                Perceptor->ActivateWanderMode();
            }
            return;
        }

        // Something finished figure out which phase just ended
        if (Perceptor->IsHouseRotationDone())
        {
            // Full 360° scan done mark explored and move to looting
            Perceptor->MarkCurrentHouseExplored();
            Brain->CompleteCurrentAction();
            FinishLatentTask(OwnerComp, EBTNodeResult::Succeeded);
        }
        else if (Perceptor->GetCurrentTargetHouse() != nullptr)
        {
            // Arrived at the house center start rotating to scan with the FOV cone
            Perceptor->ActivateRotationSearch();
        }
        else
        {
            // Arrived at exit navigation point now safely outside, start wander
            AActor* HouseActor = Perceptor->GetNearestUnexploredHouse();
            if (HouseActor)
            {
                UE_LOG(LogTemp, Warning, TEXT("BTTask: Arrived at exit point, house spotted — navigating to it"));
                AHouse* House = Cast<AHouse>(HouseActor);
                FVector Target = House ? House->GetBounds().Origin : HouseActor->GetActorLocation();
                Perceptor->ActivateNavigationTarget(Target);
            }
            else
            {
                UE_LOG(LogTemp, Warning, TEXT("BTTask: Arrived at exit point, no house — activating wander"));
                Perceptor->ActivateWanderMode();
            }
        }
        return;
    }

    // PickupLoot: priority-aware three-phase loop — find best item → navigate → grab → repeat
    if (CurrentAction.ActionName == TEXT("Action_PickupLoot"))
    {
        UInventoryComponent* LootInv = ControlledPawn->FindComponentByClass<UInventoryComponent>();

        // Snapshot inventory counts and health/stamina for priority decisions
        int32 NWeap = 0, NMed = 0, NFood = 0;
        bool bHasAnyGun = false;
        float LootHP = 10.f, LootStamMissing = 0.f;
        bool bHasFreeSlot = false;
        if (LootInv)
        {
            for (ABaseItem* S : LootInv->GetInventory())
            {
                if (!S) { bHasFreeSlot = true; continue; }
                if (AWeapon* W = Cast<AWeapon>(S)) { if (W->GetValue() > 0) { bHasAnyGun = true; ++NWeap; } }
                else if (Cast<AMedkit>(S)) ++NMed;
                else if (Cast<AFood>(S))   ++NFood;
            }
        }
        if (APawn* P = Cast<APawn>(ControlledPawn))
        {
            if (UHealthComponent*  HC = P->FindComponentByClass<UHealthComponent>())  LootHP          = static_cast<float>(HC->GetHealth());
            if (UStaminaComponent* SC = P->FindComponentByClass<UStaminaComponent>()) LootStamMissing = SC->GetMaxStamina() - SC->GetCurrentStamina();
        }

        // Shared finish helper
        auto FinishLooting = [&](const TCHAR* Reason)
        {
            UE_LOG(LogTemp, Log, TEXT("BTTask: Looting done (%s)"), Reason);
            Brain->CompleteCurrentAction();
            Brain->GetCurrentState().Add(TEXT("HouseExplored"), 0);
            Brain->GetCurrentState().Add(TEXT("HasResources"), 0);
            FinishLatentTask(OwnerComp, EBTNodeResult::Succeeded);
        };

        // Phase 2: still traveling — wait
        if (Perceptor->IsNavigating()) return;

        // Phase 3: arrived — grab the nearest wanted item
        if (Perceptor->IsCurrentActionFinished())
        {
            if (LootInv)
            {
                // Find nearest item within grab range that we actually want
                AActor* LootActor = nullptr;
                float BestDistSq = MAX_FLT;
                FVector MyLoc3 = ControlledPawn->GetActorLocation();
                for (AActor* KA : Perceptor->KnownItems)
                {
                    if (!IsValid(KA)) continue;
                    ABaseItem* Candidate = Cast<ABaseItem>(KA);
                    if (!ShouldPickUp(Candidate, LootInv)) continue;
                    float DistSq = FVector::DistSquared(MyLoc3, KA->GetActorLocation());
                    if (DistSq < BestDistSq) { BestDistSq = DistSq; LootActor = KA; }
                }

                if (LootActor)
                {
                    ABaseItem* Item = Cast<ABaseItem>(LootActor);
                    const TArray<ABaseItem*>& Slots = LootInv->GetInventory();
                    bool bGrabbed = false;
                    for (int32 i = 0; i < Slots.Num(); ++i)
                    {
                        if (Slots[i] == nullptr)
                        {
                            LootInv->GrabItem(i, Item);
                            Perceptor->PerceivedLoot.Remove(LootActor);
                            Perceptor->KnownItems.Remove(LootActor);
                            UE_LOG(LogTemp, Log, TEXT("BTTask: Grabbed '%s' into slot %d"), *Item->GetName(), i);
                            bGrabbed = true;
                            break;
                        }
                    }
                    if (!bGrabbed) { FinishLooting(TEXT("inventory full")); return; }
                }
            }

            // Grabbed (or nothing nearby) — reset to Phase 1 to look for the next item
            Perceptor->BeginLootSearch();
            return;
        }

        // Phase 1: choose the most-needed item and navigate to it
        // Priority: weapon (if none) > medkit (if injured) > food (if low stamina) > anything wanted
        AActor* Loot = nullptr;

        if (!bHasAnyGun && NWeap < 3)
            Loot = Perceptor->GetNearestKnownWeapon();

        if (!Loot && LootHP < 3.f && NMed < 2)
            Loot = Perceptor->GetNearestKnownMedkit();

        if (!Loot && LootStamMissing > 5.f && NFood < 2)
            Loot = Perceptor->GetNearestKnownFood();

        if (!Loot)
        {
            // Fallback: nearest KnownItem we still want (respects type limits)
            float BestDistSq = MAX_FLT;
            FVector MyLoc1 = ControlledPawn->GetActorLocation();
            for (AActor* KA : Perceptor->KnownItems)
            {
                if (!IsValid(KA)) continue;
                ABaseItem* Item = Cast<ABaseItem>(KA);
                if (!ShouldPickUp(Item, LootInv)) continue;
                float DistSq = FVector::DistSquared(MyLoc1, KA->GetActorLocation());
                if (DistSq < BestDistSq) { BestDistSq = DistSq; Loot = KA; }
            }
        }

        if (Loot)
        {
            Perceptor->ActivateNavigationTarget(Loot->GetActorLocation());
            UE_LOG(LogTemp, Log, TEXT("BTTask: Navigating to loot '%s'"), *Loot->GetName());
        }
        else
        {
            FinishLooting(TEXT("nothing wanted in memory"));
        }
        return;
    }

    // UseFood: instant use handles the post-interrupt case where ExecuteTask never re-ran
    if (CurrentAction.ActionName == TEXT("Action_UseFood"))
    {
        ASurvivorPawn* SurvivorPawn = Cast<ASurvivorPawn>(ControlledPawn);
        UInventoryComponent* Inv = ControlledPawn->FindComponentByClass<UInventoryComponent>();
        if (SurvivorPawn && Inv)
        {
            UStaminaComponent* StamComp = SurvivorPawn->FindComponentByClass<UStaminaComponent>();
            const float StaminaMissing = StamComp ? (StamComp->GetMaxStamina() - StamComp->GetCurrentStamina()) : 10.0f;
            const TArray<ABaseItem*>& Slots = Inv->GetInventory();
            for (int32 i = 0; i < Slots.Num(); ++i)
            {
                if (AFood* Food = Cast<AFood>(Slots[i]))
                {
                    if (Food->GetValue() > 0 && static_cast<float>(Food->GetValue()) <= StaminaMissing)
                    {
                        UE_LOG(LogTemp, Log, TEXT("BTTask: Eating food '%s' (TickTask path)"), *Food->GetName());
                        Food->UseItem(*SurvivorPawn);
                        Inv->RemoveItem(i);
                        break;
                    }
                }
            }
        }
        Brain->CompleteCurrentAction();
        FinishLatentTask(OwnerComp, EBTNodeResult::Succeeded);
        return;
    }

    // UseMedkit: instant use handles the post-interrupt case where ExecuteTask never re-ran
    if (CurrentAction.ActionName == TEXT("Action_UseMedkit"))
    {
        ASurvivorPawn* SurvivorPawn = Cast<ASurvivorPawn>(ControlledPawn);
        UInventoryComponent* Inv = ControlledPawn->FindComponentByClass<UInventoryComponent>();
        if (SurvivorPawn && Inv)
        {
            const TArray<ABaseItem*>& Slots = Inv->GetInventory();
            for (int32 i = 0; i < Slots.Num(); ++i)
            {
                if (AMedkit* Medkit = Cast<AMedkit>(Slots[i]))
                {
                    Medkit->UseItem(*SurvivorPawn);
                    Inv->RemoveItem(i);
                    UE_LOG(LogTemp, Log, TEXT("BTTask: Used and consumed medkit '%s' from slot %d (TickTask path)"), *Medkit->GetName(), i);
                    break;
                }
            }
        }
        Brain->CompleteCurrentAction();
        FinishLatentTask(OwnerComp, EBTNodeResult::Succeeded);
        return;
    }

    // Kiting: fight when armed (approach + shoot), fleewhen unarmed
    if (CurrentAction.ActionName == TEXT("Action_Kiting"))
    {
        AActor* Threat = Perceptor->GetHighestThreatZombie();
        const FVector MyLoc = ControlledPawn->GetActorLocation();
        const float ThreatDist = Threat
            ? FVector::Dist(MyLoc, Threat->GetActorLocation())
            : TNumericLimits<float>::Max();

        // Kiting only completes when the threat is fully gone
        // This prevents the re-interrupt loop where CalculateDesirability re-raises ZombiesNearby
        // the moment the flee behavior's distance gate fires bActionFinished.
        const bool bThreatGone = !Threat && !Perceptor->HasThreatMemory();

        // --- Weapon check ---
        bool bHasUsableWeapon = false;
        AWeapon* BestWeapon = nullptr;
        {
            UInventoryComponent* Inv = ControlledPawn->FindComponentByClass<UInventoryComponent>();
            if (Inv)
            {
                for (ABaseItem* Item : Inv->GetInventory())
                {
                    AWeapon* W = Cast<AWeapon>(Item);
                    if (W && W->GetValue() > 0) { bHasUsableWeapon = true; BestWeapon = W; break; }
                }
            }
        }

        // === ARMED: FIGHT MODE ===
        // Stand ground and shoot rather than fleeing, zombie walks into range, we kill it.
        if (bHasUsableWeapon && BestWeapon)
        {
            bKitingToHouse = false;
            Perceptor->bExternalRotationControl = true; // face the zombie

            // Stop sprinting — no need for speed when standing to fight
            if (ASurvivorPawn* SP = Cast<ASurvivorPawn>(ControlledPawn))
                SP->StopRunning();

            if (Threat)
            {
                const float MaxRange = Cast<AShotgun>(BestWeapon) ? 400.f : 1000.f;

                // Always face zombie in fight mode
                FVector ToThreat = (Threat->GetActorLocation() - MyLoc).GetSafeNormal();
                ControlledPawn->SetActorRotation(FRotator(0.f, ToThreat.ToOrientationRotator().Yaw, 0.f));

                if (ThreatDist <= MaxRange)
                {
                    // In range, stand still and shoot.
                    // Stop any active flee so we don't keep running backwards.
                    if (Perceptor->IsFleeing())
                        Perceptor->ActivateNavigationTarget(MyLoc); // navigate to self = stop immediately

                    KitingShootTimer -= DeltaSeconds;
                    if (KitingShootTimer <= 0.f)
                    {
                        ASurvivorPawn* SurvivorPawn = Cast<ASurvivorPawn>(ControlledPawn);
                        if (SurvivorPawn)
                        {
                            BestWeapon->UseItem(*SurvivorPawn);
                            UE_LOG(LogTemp, Log, TEXT("BTTask: Kiting — fired %s (dist %.0f / max %.0f)"),
                                *BestWeapon->GetName(), ThreatDist, MaxRange);
                        }
                        KitingShootTimer = 0.5f;
                    }
                }
                else
                {
                    // Out of range, approach zombie (throttled to avoid pathfinding spam)
                    KitingApproachTimer -= DeltaSeconds;
                    if (KitingApproachTimer <= 0.f)
                    {
                        Perceptor->ActivateNavigationTarget(Threat->GetActorLocation());
                        KitingApproachTimer = 1.5f;
                        UE_LOG(LogTemp, Verbose, TEXT("BTTask: Kiting — approaching zombie (dist %.0f / max %.0f)"),
                            ThreatDist, MaxRange);
                    }
                }
            }

            if (bThreatGone)
            {
                Perceptor->bExternalRotationControl = false;
                if (ASurvivorPawn* SP = Cast<ASurvivorPawn>(ControlledPawn))
                    SP->StopRunning();
                Brain->CompleteCurrentAction();
                FinishLatentTask(OwnerComp, EBTNodeResult::Succeeded);
            }
            return;
        }

        // === UNARMED: HOUSE-DASH or FLEE ===
        // Sprint whenever possible, unarmed survivors need every advantage
        {
            ASurvivorPawn* SP = Cast<ASurvivorPawn>(ControlledPawn);
            if (SP)
            {
                UStaminaComponent* SC = SP->FindComponentByClass<UStaminaComponent>();
                if (SC && SC->GetCurrentStamina() > 0.f)
                    SP->StartRunning();
                else
                    SP->StopRunning();
            }
        }

        {
            const bool bZombieTooClose = (ThreatDist < 500.f);

            if (bKitingToHouse)
            {
                if (bZombieTooClose)
                {
                    // Zombie too close, abort house dash and resume fleeing
                    UE_LOG(LogTemp, Warning, TEXT("BTTask: Kiting — zombie too close (%.0f), aborting house dash"), ThreatDist);
                    bKitingToHouse = false;
                    if (Threat)
                        Perceptor->ActivateFleeFrom(Threat->GetActorLocation());
                    else if (Perceptor->HasThreatMemory())
                        Perceptor->ActivateFleeFrom(Perceptor->GetLastKnownZombieLocation());
                }
                else if (Perceptor->IsCurrentActionFinished() || !Perceptor->IsNavigating())
                {
                    // Arrived at house, opportunistic pickup will grab items next tick
                    UE_LOG(LogTemp, Log, TEXT("BTTask: Kiting — arrived at house"));
                    bKitingToHouse = false;
                    // Fall through to flee reinit
                }
                else
                {
                    // Still navigating to the house, face movement direction (perceive surroundings)
                    Perceptor->bExternalRotationControl = false;
                    return;
                }
            }

            if (!bKitingToHouse && !bZombieTooClose)
            {
                // Try to dash into a nearby house to find a weapon
                AActor* HouseActor = Perceptor->GetNearestUnexploredHouse();
                if (!HouseActor)
                    HouseActor = Perceptor->GetCurrentTargetHouse(); // explored is still shelter
                if (HouseActor)
                {
                    AHouse* House = Cast<AHouse>(HouseActor);
                    FVector HouseCenter = House ? House->GetBounds().Origin : HouseActor->GetActorLocation();
                    UE_LOG(LogTemp, Warning, TEXT("BTTask: Kiting — unarmed, dashing to house (%.0f,%.0f)"),
                        HouseCenter.X, HouseCenter.Y);
                    Perceptor->ActivateNavigationTarget(HouseCenter);
                    bKitingToHouse = true;
                    Perceptor->bExternalRotationControl = false;
                    return;
                }
            }
        }

        // Normal flee re-init (no house found, or zombie too close)
        if (!Perceptor->IsFleeing() && !bKitingToHouse)
        {
            if (Threat)
            {
                Perceptor->ActivateFleeFrom(Threat->GetActorLocation());
                UE_LOG(LogTemp, Log, TEXT("BTTask: Kiting — flee initialized (unarmed)"));
            }
            else if (Perceptor->HasThreatMemory())
            {
                Perceptor->ActivateFleeFrom(Perceptor->GetLastKnownZombieLocation());
                UE_LOG(LogTemp, Log, TEXT("BTTask: Kiting — fleeing from last known zombie position"));
            }
        }

        Perceptor->bExternalRotationControl = false;

        // Complete only when threat truly gone
        if (bThreatGone)
        {
            bKitingToHouse = false;
            if (ASurvivorPawn* SP = Cast<ASurvivorPawn>(ControlledPawn))
                SP->StopRunning();
            Brain->CompleteCurrentAction();
            FinishLatentTask(OwnerComp, EBTNodeResult::Succeeded);
        }
        return;
    }

    // Default: all other actions finish when perceptor signals done
    if (Perceptor->IsCurrentActionFinished())
    {
        Brain->CompleteCurrentAction();
        FinishLatentTask(OwnerComp, EBTNodeResult::Succeeded);
    }
}