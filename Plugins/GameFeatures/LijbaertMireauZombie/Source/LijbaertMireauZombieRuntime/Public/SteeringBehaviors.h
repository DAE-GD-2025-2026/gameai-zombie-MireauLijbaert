#pragma once

#include "SteeringAgent.h"
#include "Kismet/KismetMathLibrary.h"

class ASteeringAgent;

// SteeringBehavior base, all steering behaviors should derive from this.
class ISteeringBehavior
{
public:
	ISteeringBehavior() = default;
	virtual ~ISteeringBehavior() = default;

	// Override to implement your own behavior
	virtual SteeringOutput CalculateSteering(float DeltaT, ASteeringAgent & Agent) = 0;

	void SetTarget(const FTargetData& NewTarget) { Target = NewTarget; }
	
	template<class T, std::enable_if_t<std::is_base_of_v<ISteeringBehavior, T>>* = nullptr>
	T* As()
	{ return static_cast<T*>(this); }

protected:
	FTargetData Target;
};

class Seek : public ISteeringBehavior
{
public:
	Seek() = default;
	virtual ~Seek() override = default;

	// Steering

	virtual SteeringOutput CalculateSteering(float DeltaT, ASteeringAgent& Agent) override;
};

class Flee : public Seek
{
public:
	Flee() = default;
	virtual ~Flee() override = default;

	// Steering
	virtual SteeringOutput CalculateSteering(float DeltaT, ASteeringAgent& Agent) override;
};

class Arrive : public Seek
{
public:
	Arrive() = default;
	virtual ~Arrive() override = default;
	
	// Steering
	virtual SteeringOutput CalculateSteering(float DeltaT, ASteeringAgent& Agent) override;
	void SetTargetRadius(float radius);

private:	
	float m_SlowRadius = 600.0f;
	float m_TargetRadius = 300.0f;
	float m_OriginalMaxSpeed = -1; // Set max speed to an impossible value so you only ever set it once
};

class Face : public ISteeringBehavior
{
public:
	Face() = default;
	virtual ~Face() override = default;
	
	// Steering
	virtual SteeringOutput CalculateSteering(float DeltaT, ASteeringAgent& Agent) override;
};

class Pursuit : public Seek
{
public:
	Pursuit() = default;
	virtual ~Pursuit() override = default;
	
	// Steering
	virtual SteeringOutput CalculateSteering(float DeltaT, ASteeringAgent& Agent) override;
	
};

class Evade : public Pursuit
{
public:
	Evade() = default;
	virtual ~Evade() override = default;
	
	// Steering
	virtual SteeringOutput CalculateSteering(float DeltaT, ASteeringAgent& Agent) override;
	
private:
	float m_EvadeRadius = 450.0f; 
};

class Wander : public Seek
{
public:
	Wander() = default;
	virtual ~Wander() override = default;
	
	// Steering
	virtual SteeringOutput CalculateSteering(float DeltaT, ASteeringAgent& Agent) override;
	
private:	
	float m_CircleDistance = 300.0f;
	float m_CircleRadius = 150.0f;
	float m_MaxAngleChange = 30.f;
	float m_Angle = 0.0f;
};

