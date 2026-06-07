// Fill out your copyright notice in the Description page of Project Settings.

#pragma once


#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Perception/AIPerceptionComponent.h"
#include "Perception/AISenseConfig_Sight.h"
#include "Perception/AISenseConfig_Damage.h"
#include "Perception/AISense_Damage.h"
#include "SteeringAgent.h"
#include "SteeringBehaviors.h"
#include "PathFollowSteeringBehavior.h"
#include "CombinedSteeringBehaviors.h"
#include "StudentPerceptorLijbaertMireau.generated.h"

UENUM(BlueprintType)
enum class EMovementState : uint8
{
	None,
	Wander,
	PathFollowing,
	Fleeing,
	RotatingSearch  // Spinning in place inside a house to scan for items
};

UCLASS(ClassGroup=(Custom), meta=(BlueprintSpawnableComponent))
class LIJBAERTMIREAUZOMBIERUNTIME_API UStudentPerceptor : public UActorComponent
{
	GENERATED_BODY()

public:
	UStudentPerceptor();
    
	virtual void BeginPlay() override;
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

	UFUNCTION()
	virtual void OnPerceptionUpdated(AActor* Actor, FAIStimulus Stimulus);

	// Public triggers that your Behavior Tree tasks will call
	UFUNCTION(BlueprintCallable, Category = "AI Movement")
	void ActivateWanderMode();

	UFUNCTION(BlueprintCallable, Category = "AI Movement")
	void ActivateNavigationTarget(const FVector& TargetWorldPos);
	
	UFUNCTION(BlueprintCallable, Category = "AI Movement")
	void ActivateFleeFrom(const FVector& ThreatWorldPos);

	// Called by BT task to ask "are you done yet?"
	bool IsCurrentActionFinished() const { return bActionFinished; }

	// True while actively path-following toward a target
	bool IsNavigating() const { return CurrentState == EMovementState::PathFollowing; }

	// True while spinning to scan the house interior
	bool IsRotatingSearch() const { return CurrentState == EMovementState::RotatingSearch; }

	// True after a full 360° rotation has been completed
	bool IsHouseRotationDone() const { return bHouseRotationDone; }

	// True while in open-world wander (steering behavior)
	bool IsWandering() const { return CurrentState == EMovementState::Wander; }

	// True while actively fleeing a threat
	bool IsFleeing() const { return CurrentState == EMovementState::Fleeing; }

	// Start a 360° spin in place to trigger perception on items inside the house
	void ActivateRotationSearch();

	// Reset state at the start of a new loot-search phase
	// (bActionFinished may still be true from the previous action — this clears it)
	void BeginLootSearch() { bActionFinished = false; CurrentState = EMovementState::None; }

	// When true, TickComponent will not override actor rotation based on movement direction.
	// Set by the BT task during kiting so it can control facing toward the zombie.
	bool bExternalRotationControl = false;
	
	UPROPERTY()
	TArray<AActor*> PerceivedZombies;

	// Currently visible loot (cleared when items leave the FOV)
	UPROPERTY()
	TArray<AActor*> PerceivedLoot;

	UPROPERTY()
	TArray<AActor*> PerceivedHouses;

	// Persistent memory — every item ever spotted, kept until actually picked up.
	// This survives the FOV leaving after a house rotation scan.
	UPROPERTY()
	TArray<AActor*> KnownItems;
	
	float GetCurrentHP() const;
	float GetCurrentStamina() const;
	
	AActor* GetHighestThreatZombie();
	AActor* GetNearestLoot();
	AActor* GetNearestUsefulLoot(); // Nearest non-garbage item
	AActor* GetNearestUnexploredHouse();
	void MarkCurrentHouseExplored();
	AActor* GetCurrentTargetHouse() const { return CurrentTargetHouse; }

	// Degrees per second for the house scan rotation (full 360° / this = scan duration)
	UPROPERTY(EditAnywhere, Category = "AI|Search")
	float RotationSearchSpeed = 90.0f;

private:
	EMovementState CurrentState = EMovementState::None;
	bool bActionFinished = false;
	bool bHouseRotationDone = false;
	float TotalRotationDone = 0.0f;

	Wander*         MyWanderBehavior    = nullptr;
	Seek*           MyDriftSeek         = nullptr;
	BlendedSteering* MyBlendedWander   = nullptr;
	PathFollow*     MyPathFollowBehavior = nullptr;
	Flee*           MyFleeBehavior      = nullptr;

	// Current long-range drift target for blended wander
	FVector2D CurrentDriftTarget = FVector2D::ZeroVector;
	float WanderStuckTimer = 0.f;
	void PickNewDriftTarget();

	TArray<AActor*> ExploredHouses;
	AActor* CurrentTargetHouse = nullptr;
};
