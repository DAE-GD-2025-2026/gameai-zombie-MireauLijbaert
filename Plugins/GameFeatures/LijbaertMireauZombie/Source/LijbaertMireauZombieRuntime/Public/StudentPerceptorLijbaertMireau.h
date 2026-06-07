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
#include "StudentPerceptorLijbaertMireau.generated.h"

UENUM(BlueprintType)
enum class EMovementState : uint8
{
	None,
	Wander,
	PathFollowing,
	Fleeing
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

	// NEW: called by BT task to ask "are you done yet?"
	bool IsCurrentActionFinished() const { return bActionFinished; }
	
	UPROPERTY()
	TArray<AActor*> PerceivedZombies;

	UPROPERTY()
	TArray<AActor*> PerceivedLoot;

	UPROPERTY()
	TArray<AActor*> PerceivedHouses;
	
	float GetCurrentHP() const;
	float GetCurrentStamina() const;
	
	AActor* GetHighestThreatZombie();
	AActor* GetNearestLoot();
	AActor* GetNearestUsefulLoot(); // Nearest non-garbage item
	AActor* GetNearestUnexploredHouse();
	void MarkCurrentHouseExplored();
	AActor* GetCurrentTargetHouse() const { return CurrentTargetHouse; }

private:
	EMovementState CurrentState = EMovementState::None;
	bool bActionFinished = false; 
	
	Wander* MyWanderBehavior = nullptr;
	PathFollow* MyPathFollowBehavior = nullptr;
	Flee*       MyFleeBehavior       = nullptr;
	
	TArray<AActor*> ExploredHouses;
	AActor* CurrentTargetHouse = nullptr;
};
