#pragma once

#include "CoreMinimal.h"
#include "Abilities/GameplayAbility.h"

#include "BlinkAbility.generated.h"

/**
 * Blink ability that chooses a safe destination using NavMesh and collision checks.
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
};
