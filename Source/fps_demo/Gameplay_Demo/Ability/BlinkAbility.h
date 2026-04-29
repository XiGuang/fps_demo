#pragma once

#include "CoreMinimal.h"
#include "Abilities/GameplayAbility.h"

#include "BlinkAbility.generated.h"

class ACharacter;
struct FCollisionQueryParams;

/**
 * Blink ability that chooses a safe destination using collision, ground, and NavMesh checks.
 */
UCLASS()
class FPS_DEMO_API UBlinkAbility : public UGameplayAbility
{
	GENERATED_BODY()

public:
	UBlinkAbility();

	virtual void ActivateAbility(
		const FGameplayAbilitySpecHandle Handle,
		const FGameplayAbilityActorInfo* ActorInfo,
		const FGameplayAbilityActivationInfo ActivationInfo,
		const FGameplayEventData* TriggerEventData) override;

protected:
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Blink")
	float BlinkDistance = 800.0f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Blink")
	float GroundFlatIgnoreDistance = 30.0f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Blink|Trace")
	TEnumAsByte<ECollisionChannel> MovementTraceChannel = ECC_Visibility;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Blink|Trace")
	TEnumAsByte<ECollisionChannel> GroundTraceChannel = ECC_Visibility;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Blink|Repair", meta = (ClampMin = "0.0"))
	float GroundProbeDistance = 1000.0f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Blink|Path", meta = (ClampMin = "10.0"))
	float PathStepDistance = 50.0f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Blink|Path", meta = (ClampMin = "0.0"))
	float MaxGroundFollowDrop = 80.0f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Blink|Path", meta = (ClampMin = "0.0"))
	float MaxGroundFollowRise = 45.0f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Blink|Repair", meta = (ClampMin = "0.0", ClampMax = "89.0"))
	float BlockingSurfaceAngle = 70.0f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Blink|Repair", meta = (ClampMin = "0.0"))
	float ObstacleBackoffDistance = 50.0f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Blink|Repair", meta = (ClampMin = "2", ClampMax = "32"))
	int32 RepairSampleCount = 10;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Blink|Repair", meta = (ClampMin = "0.0"))
	float CapsuleOverlapShrink = 2.0f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Blink|Repair")
	bool bProjectToNavigation = false;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Blink|Debug")
	bool bDrawDebug = false;

	FVector GetBlinkDirection(const ACharacter* Character, const FGameplayEventData* TriggerEventData) const;
	bool FindBlinkDestination(ACharacter* Character, const FVector& BlinkDirection, FVector& OutTargetLocation) const;
	FVector RepairBlinkLocation(ACharacter* Character, const FVector& SourceLocation, const FVector& DesiredLocation, const FVector& BlinkDirection, const FCollisionQueryParams& QueryParams) const;
	FVector TraceBlinkPath(ACharacter* Character, const FVector& SourceLocation, const FVector& BlinkDirection, float MaxDistance, const FCollisionQueryParams& QueryParams) const;
	bool TryFindWalkableGround(ACharacter* Character, const FVector& Location, float UpDistance, float DownDistance, const FCollisionQueryParams& QueryParams, FVector& OutGroundedLocation, FHitResult& OutGroundHit) const;
	bool IsWalkableHit(ACharacter* Character, const FHitResult& Hit) const;
	bool TryProjectToGround(ACharacter* Character, const FVector& Location, const FCollisionQueryParams& QueryParams, FVector& OutGroundedLocation) const;
	bool TrySampleBackToValidGround(ACharacter* Character, const FVector& InvalidLocation, const FVector& SourceLocation, const FCollisionQueryParams& QueryParams, FVector& OutLocation) const;
	bool IsCapsuleBlockedAt(ACharacter* Character, const FVector& Location, const FCollisionQueryParams& QueryParams) const;
	bool TryProjectToNavigation(ACharacter* Character, const FVector& Location, const FCollisionQueryParams& QueryParams, FVector& OutLocation) const;
};
