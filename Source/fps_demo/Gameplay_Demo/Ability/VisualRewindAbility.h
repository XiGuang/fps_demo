#pragma once

#include "CoreMinimal.h"
#include "Abilities/GameplayAbility.h"

#include "VisualRewindAbility.generated.h"

class UVisualRewindComponent;

/**
 * 本地表现型回溯 Ability。
 *
 * Ability 负责 GAS 提交和生命周期；实际本地播放/服务器结算由
 * UVisualRewindComponent 负责。启动成功后 Ability 保持 active，直到组件通知
 * 完成或服务器拒绝。
 */
UCLASS()
class FPS_DEMO_API UVisualRewindAbility : public UGameplayAbility
{
	GENERATED_BODY()

public:
	UVisualRewindAbility();

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
	void HandleVisualRewindFinished();
	void HandleVisualRewindRejected();
	void UnbindVisualRewindDelegates();
	void RejectPredictedRewindIfAuthority() const;

	UPROPERTY()
	TObjectPtr<UVisualRewindComponent> ActiveVisualRewindComponent;

	FDelegateHandle VisualRewindFinishedDelegateHandle;
	FDelegateHandle VisualRewindRejectedDelegateHandle;
	FGameplayAbilitySpecHandle ActiveVisualRewindAbilityHandle;
	FGameplayAbilityActivationInfo ActiveVisualRewindActivationInfo;
	const FGameplayAbilityActorInfo* ActiveVisualRewindActorInfo = nullptr;
};

