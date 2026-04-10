#pragma once

#include "CoreMinimal.h"
#include "Abilities/GameplayAbility.h"

#include "DashAbility.generated.h"

/**
 * A simple dash ability that launches the avatar in the facing direction.
 */
UCLASS()
class FPS_DEMO_API UDashAbility : public UGameplayAbility
{
	GENERATED_BODY()

public:
	UDashAbility();

	virtual void ActivateAbility(
		const FGameplayAbilitySpecHandle Handle,
		const FGameplayAbilityActorInfo* ActorInfo,
		const FGameplayAbilityActivationInfo ActivationInfo,
		const FGameplayEventData* TriggerEventData) override;

protected:
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Dash")
	float DashStrength = 1200.0f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Dash")
	float UpwardStrength = 100.0f;
};
