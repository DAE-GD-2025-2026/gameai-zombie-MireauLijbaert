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
	PathFollowing
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
	
	UPROPERTY()
	TArray<AActor*> PerceivedZombies;

	UPROPERTY()
	TArray<AActor*> PerceivedLoot;
	
	float GetCurrentHP() const;
	float GetCurrentStamina() const;
	
	AActor* GetHighestThreatZombie();

private:
	EMovementState CurrentState = EMovementState::None;

	// Allocate your ported Reynolds math engines
	Wander* MyWanderBehavior = nullptr;
	PathFollow* MyPathFollowBehavior = nullptr;
};
