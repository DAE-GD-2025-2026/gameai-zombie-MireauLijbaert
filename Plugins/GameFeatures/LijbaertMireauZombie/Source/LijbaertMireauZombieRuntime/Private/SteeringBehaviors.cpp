#include "SteeringBehaviors.h"

#include "MeshPaintVisualize.h"

//SEEK
SteeringOutput Seek::CalculateSteering(float DeltaT, ASteeringAgent& Agent)
{
	Agent.SetIsAutoOrienting(true);
	SteeringOutput Steering{};

	Steering.LinearVelocity = Target.Position - Agent.GetPosition();

	// Add debug rendering
	// Draw Target
	if (Agent.GetDebugRenderingEnabled())
	{
		FVector VelocityTowardsTarget = Agent.GetVelocity().ProjectOnTo(FVector(Steering.LinearVelocity, 0));
		DrawDebugPoint(Agent.GetWorld(),FVector(Target.Position.X, Target.Position.Y, 0), 10, FColor::Yellow );
		DrawDebugLine(Agent.GetWorld(),Agent.GetActorLocation(),
			Agent.GetActorLocation() + Agent.GetActorForwardVector()*(Agent.GetLinearVelocity().Length()/2), FColor::Cyan);
		DrawDebugLine(Agent.GetWorld(),Agent.GetActorLocation(), Agent.GetActorLocation() + VelocityTowardsTarget/2, FColor::Magenta );
	}
	
	return Steering;
}

//FLEE
SteeringOutput Flee::CalculateSteering(float DeltaT, ASteeringAgent& Agent)
{
	Agent.SetIsAutoOrienting(true);
	SteeringOutput Steering{Seek::CalculateSteering(DeltaT, Agent)};
	Steering.LinearVelocity *= -1.f; // Flee is just the opposite of Seek, so we can reuse its logic and just invert the velocity
	// Add debug rendering
	
	return Steering;
}

//ARRIVE
SteeringOutput Arrive::CalculateSteering(float DeltaT, ASteeringAgent& Agent)
{
	Agent.SetIsAutoOrienting(true);
	// Get original max speed, max speed can never be less than 0 so this should only happen once after construction
	if (m_OriginalMaxSpeed < 0.f)
	{
		m_OriginalMaxSpeed = Agent.GetMaxLinearSpeed();
	}
	
	SteeringOutput Steering{Seek::CalculateSteering(DeltaT, Agent)};
	float DistanceAgentToTarget = (Target.Position - Agent.GetPosition()).Length();
	
	 if (DistanceAgentToTarget < m_SlowRadius)
	 {
	 	// Calculate how far along the target is between the SlowRadius and the target radius
	 	float MaxSpeedScalePercentage = (DistanceAgentToTarget - m_TargetRadius) / (m_SlowRadius - m_TargetRadius);
	 	Agent.SetMaxLinearSpeed(MaxSpeedScalePercentage*m_OriginalMaxSpeed);
	 }
	 	
	 else if (DistanceAgentToTarget < m_TargetRadius)
	 {
	 	Agent.SetMaxLinearSpeed(0.f);
	 }
	
	else Agent.SetMaxLinearSpeed(m_OriginalMaxSpeed);
	
	// Debug
	if (Agent.GetDebugRenderingEnabled())
	{
		DrawDebugCircle(Agent.GetWorld(), FVector(Agent.GetActorLocation()), m_SlowRadius, 10, FColor::Blue,
			false, -1.f, 0.f, 4.f, FVector::RightVector, FVector::ForwardVector);
		DrawDebugCircle(Agent.GetWorld(), FVector(Agent.GetActorLocation()), m_TargetRadius, 10, FColor::Red,
			false, -1.f, 0.f, 4.f, FVector::RightVector, FVector::ForwardVector);
	}
	
	return Steering;
}

void Arrive::SetTargetRadius(float Radius)
{
	m_TargetRadius = Radius;
	m_SlowRadius = Radius * 2; // Slow at double the distance as arriving
}

SteeringOutput Face::CalculateSteering(float DeltaT, ASteeringAgent& Agent)
{
	Agent.SetIsAutoOrienting(false);
	SteeringOutput Steering{};
	FVector2D DistanceAgentToTarget = (Target.Position - Agent.GetPosition()  );
	float DesiredAngle = FMath::RadiansToDegrees(atan2(DistanceAgentToTarget.Y, DistanceAgentToTarget.X));
	// Agent Forward is flipped (without it face uses the opposite direction)
	float CurrentAngle = Agent.GetRotation();
	float DeltaAngle = FMath::FindDeltaAngleDegrees( CurrentAngle, DesiredAngle);
	if (FMath::Abs(DeltaAngle) < 0.1f)
	{
		Steering.AngularVelocity = 0.f;
	}
	else
	{
		float MaxAngularSpeed = Agent.GetMaxAngularSpeed();
		if (DeltaAngle < 0) Steering.AngularVelocity = -MaxAngularSpeed;
		else Steering.AngularVelocity = MaxAngularSpeed;
		// Slow down when low values are reached so the agent doesn't oscillate around the target angle
		if (FMath::Abs(DeltaAngle) < 2.f) Steering.AngularVelocity *= 0.05f;
		else if (FMath::Abs(DeltaAngle) < 20.f) Steering.AngularVelocity *= 0.5f;
	}
	
	// Debug
	// Draw Target
	if (Agent.GetDebugRenderingEnabled())
	{
		DrawDebugPoint(Agent.GetWorld(),FVector(Target.Position.X, Target.Position.Y, 0), 10, FColor::Yellow );
		// if (GEngine) // make sure the engine exists
		// {
		// 	FString Msg = FString::Printf(TEXT("DeltaAngle: %f"), DeltaAngle);
		// 	GEngine->AddOnScreenDebugMessage(-1, 0.f, FColor::Green, Msg);
		// }
	}

	return Steering;
}

SteeringOutput Pursuit::CalculateSteering(float DeltaT, ASteeringAgent& Agent)
{
	Agent.SetIsAutoOrienting(true);
	SteeringOutput Steering{};
	float DistancePlayerToTarget = (Target.Position - Agent.GetPosition()).Length();
	float TimeToArrive = (DistancePlayerToTarget/ Agent.GetMaxLinearSpeed());
	FVector2D PredictedTarget = Target.Position + TimeToArrive*Target.LinearVelocity;
	
	// save original (otherwise flocking has problems when multiple agents in the flock use the same behavior
	FVector2D OldTarget = Target.Position; 
	Target.Position = PredictedTarget;
	
	Steering = Seek::CalculateSteering(DeltaT, Agent);
	
	Target.Position = OldTarget;
	
	// Debug
	return Steering;
}

SteeringOutput Evade::CalculateSteering(float DeltaT, ASteeringAgent& Agent)
{
	Agent.SetIsAutoOrienting(true);
	SteeringOutput Steering{Pursuit::CalculateSteering(DeltaT, Agent)};
	Steering.LinearVelocity *= -1.f;
	float DistanceAgentToTarget = (Target.Position - Agent.GetPosition()).Length();
	if (DistanceAgentToTarget <= m_EvadeRadius)
	{
		Steering.IsValid = true;
	}
	else Steering.IsValid = false;
	
	return Steering;
}

SteeringOutput Wander::CalculateSteering(float DeltaT, ASteeringAgent& Agent)
{
	Agent.SetIsAutoOrienting(true);
	SteeringOutput Steering{};
	FVector2D CircleCenter = Agent.GetPosition() + FVector2D(Agent.GetActorForwardVector() * m_CircleDistance);
	m_Angle = FMath::FRandRange(m_Angle - m_MaxAngleChange, m_Angle + m_MaxAngleChange);
	m_Angle = FMath::UnwindDegrees(m_Angle);
	
	FVector2D Offset;
	Offset.X = FMath::Cos(FMath::DegreesToRadians(m_Angle)) * m_CircleRadius;
	Offset.Y = FMath::Sin(FMath::DegreesToRadians(m_Angle)) * m_CircleRadius;
	
	
	Target.Position = CircleCenter + Offset;
	Steering = Seek::CalculateSteering(DeltaT, Agent);
	
	// Draw Target
	if (Agent.GetDebugRenderingEnabled())
	{
		DrawDebugCircle(Agent.GetWorld(), FVector(CircleCenter, 0), m_CircleRadius, 10, FColor::Blue,
		false, -1.f, 0.f, 4.f, FVector::RightVector, FVector::ForwardVector);
		// if (GEngine) // make sure the engine exists
		// {
		// 	FString Msg = FString::Printf(TEXT("m_Angle: %f"), m_Angle);
		// 	GEngine->AddOnScreenDebugMessage(-1, 0.f, FColor::Green, Msg);
		// }
	}
	return Steering;
}

