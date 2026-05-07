#include "RewindAbility.h"

#include "Gameplay_Demo/Ability/RewindComponent.h"

URewindAbility::URewindAbility()
{
	InstancingPolicy = EGameplayAbilityInstancingPolicy::InstancedPerActor;
	NetExecutionPolicy = EGameplayAbilityNetExecutionPolicy::LocalPredicted;
}

void URewindAbility::ActivateAbility(
	const FGameplayAbilitySpecHandle Handle,
	const FGameplayAbilityActorInfo* ActorInfo,
	const FGameplayAbilityActivationInfo ActivationInfo,
	const FGameplayEventData* TriggerEventData)
{
	AActor* AvatarActor = ActorInfo ? ActorInfo->AvatarActor.Get() : nullptr;
	URewindComponent* RewindComponent = AvatarActor ? AvatarActor->FindComponentByClass<URewindComponent>() : nullptr;
	if (!RewindComponent || !RewindComponent->CanStartRewind())
	{
		EndAbility(Handle, ActorInfo, ActivationInfo, true, true);
		return;
	}

	if (!CommitAbility(Handle, ActorInfo, ActivationInfo))
	{
		EndAbility(Handle, ActorInfo, ActivationInfo, true, true);
		return;
	}

	ActiveRewindComponent = RewindComponent;
	ActiveRewindAbilityHandle = Handle;
	ActiveRewindActivationInfo = ActivationInfo;
	ActiveRewindActorInfo = ActorInfo;
	RewindFinishedDelegateHandle = RewindComponent->OnRewindFinished.AddUObject(this, &URewindAbility::HandleRewindFinished);

	if (!RewindComponent->StartRewind())
	{
		UnbindRewindFinishedDelegate();
		EndAbility(Handle, ActorInfo, ActivationInfo, true, true);
		return;
	}

	// 回溯移动由 RewindComponent/CharacterMovement 后续 tick 推进。
	// Ability 保持 active，直到收到 OnRewindFinished 后再结束。
}

void URewindAbility::EndAbility(
	const FGameplayAbilitySpecHandle Handle,
	const FGameplayAbilityActorInfo* ActorInfo,
	const FGameplayAbilityActivationInfo ActivationInfo,
	bool bReplicateEndAbility,
	bool bWasCancelled)
{
	UnbindRewindFinishedDelegate();

	Super::EndAbility(Handle, ActorInfo, ActivationInfo, bReplicateEndAbility, bWasCancelled);
}

void URewindAbility::HandleRewindFinished()
{
	const FGameplayAbilitySpecHandle AbilityHandle = ActiveRewindAbilityHandle;
	const FGameplayAbilityActivationInfo ActivationInfo = ActiveRewindActivationInfo;
	const FGameplayAbilityActorInfo* ActorInfo = ActiveRewindActorInfo;

	UnbindRewindFinishedDelegate();

	// 客户端和服务器各自等本地回溯播放完成后结束 Ability。
	// 不把结束请求复制给另一端，避免客户端预测播放时间提前结束服务器上的权威 Ability。
	EndAbility(AbilityHandle, ActorInfo, ActivationInfo, false, false);
}

void URewindAbility::UnbindRewindFinishedDelegate()
{
	if (ActiveRewindComponent && RewindFinishedDelegateHandle.IsValid())
	{
		ActiveRewindComponent->OnRewindFinished.Remove(RewindFinishedDelegateHandle);
	}

	RewindFinishedDelegateHandle.Reset();
	ActiveRewindComponent = nullptr;
	ActiveRewindAbilityHandle = FGameplayAbilitySpecHandle();
	ActiveRewindActivationInfo = FGameplayAbilityActivationInfo();
	ActiveRewindActorInfo = nullptr;
}
