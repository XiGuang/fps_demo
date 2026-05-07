#pragma once

#include "CoreMinimal.h"
#include "Abilities/GameplayAbility.h"

#include "RewindAbility.generated.h"

class URewindComponent;

/**
 * Starts the rewind playback handled by URewindComponent.
 */
UCLASS()
class FPS_DEMO_API URewindAbility : public UGameplayAbility
{
	GENERATED_BODY()

public:
	URewindAbility();

	virtual void ActivateAbility(
		const FGameplayAbilitySpecHandle Handle,
		const FGameplayAbilityActorInfo* ActorInfo,
		const FGameplayAbilityActivationInfo ActivationInfo,
		const FGameplayEventData* TriggerEventData) override;

	virtual void EndAbility(
		const FGameplayAbilitySpecHandle Handle,
		const FGameplayAbilityActorInfo* ActorInfo,
		const FGameplayAbilityActivationInfo ActivationInfo,
		bool bReplicateEndAbility,
		bool bWasCancelled) override;

private:
	void HandleRewindFinished();
	void UnbindRewindFinishedDelegate();

	UPROPERTY()
	TObjectPtr<URewindComponent> ActiveRewindComponent;

	FDelegateHandle RewindFinishedDelegateHandle;
	FGameplayAbilitySpecHandle ActiveRewindAbilityHandle;
	FGameplayAbilityActivationInfo ActiveRewindActivationInfo;
	const FGameplayAbilityActorInfo* ActiveRewindActorInfo = nullptr;
};
