#include "VisualRewindAbility.h"

#include "Gameplay_Demo/Ability/VisualRewindComponent.h"

UVisualRewindAbility::UVisualRewindAbility()
{
	InstancingPolicy = EGameplayAbilityInstancingPolicy::InstancedPerActor;
	NetExecutionPolicy = EGameplayAbilityNetExecutionPolicy::LocalPredicted;
}

void UVisualRewindAbility::ActivateAbility(
	const FGameplayAbilitySpecHandle Handle,
	const FGameplayAbilityActorInfo* ActorInfo,
	const FGameplayAbilityActivationInfo ActivationInfo,
	const FGameplayEventData* TriggerEventData)
{
	AActor* AvatarActor = ActorInfo ? ActorInfo->AvatarActor.Get() : nullptr;
	UVisualRewindComponent* VisualRewindComponent = AvatarActor ? AvatarActor->FindComponentByClass<UVisualRewindComponent>() : nullptr;
	if (!VisualRewindComponent || !VisualRewindComponent->CanStartRewind())
	{
		if (VisualRewindComponent)
		{
			VisualRewindComponent->RejectPredictedRewindFromServer();
		}
		EndAbility(Handle, ActorInfo, ActivationInfo, true, true);
		return;
	}

	if (!CommitAbility(Handle, ActorInfo, ActivationInfo))
	{
		VisualRewindComponent->RejectPredictedRewindFromServer();
		EndAbility(Handle, ActorInfo, ActivationInfo, true, true);
		return;
	}

	ActiveVisualRewindComponent = VisualRewindComponent;
	ActiveVisualRewindAbilityHandle = Handle;
	ActiveVisualRewindActivationInfo = ActivationInfo;
	ActiveVisualRewindActorInfo = ActorInfo;
	VisualRewindFinishedDelegateHandle = VisualRewindComponent->OnVisualRewindFinished.AddUObject(this, &UVisualRewindAbility::HandleVisualRewindFinished);
	VisualRewindRejectedDelegateHandle = VisualRewindComponent->OnVisualRewindRejected.AddUObject(this, &UVisualRewindAbility::HandleVisualRewindRejected);

	if (!VisualRewindComponent->StartRewind())
	{
		RejectPredictedRewindIfAuthority();
		UnbindVisualRewindDelegates();
		EndAbility(Handle, ActorInfo, ActivationInfo, true, true);
		return;
	}

	// 不立即 EndAbility。组件会在本地播放完成、服务器结算完成或被拒绝时通知。
}

void UVisualRewindAbility::EndAbility(
	const FGameplayAbilitySpecHandle Handle,
	const FGameplayAbilityActorInfo* ActorInfo,
	const FGameplayAbilityActivationInfo ActivationInfo,
	bool bReplicateEndAbility,
	bool bWasCancelled)
{
	UnbindVisualRewindDelegates();

	Super::EndAbility(Handle, ActorInfo, ActivationInfo, bReplicateEndAbility, bWasCancelled);
}

void UVisualRewindAbility::HandleVisualRewindFinished()
{
	const FGameplayAbilitySpecHandle AbilityHandle = ActiveVisualRewindAbilityHandle;
	const FGameplayAbilityActivationInfo ActivationInfo = ActiveVisualRewindActivationInfo;
	const FGameplayAbilityActorInfo* ActorInfo = ActiveVisualRewindActorInfo;

	UnbindVisualRewindDelegates();

	// 客户端和服务器各自等本地组件完成后结束 Ability。
	EndAbility(AbilityHandle, ActorInfo, ActivationInfo, false, false);
}

void UVisualRewindAbility::HandleVisualRewindRejected()
{
	const FGameplayAbilitySpecHandle AbilityHandle = ActiveVisualRewindAbilityHandle;
	const FGameplayAbilityActivationInfo ActivationInfo = ActiveVisualRewindActivationInfo;
	const FGameplayAbilityActorInfo* ActorInfo = ActiveVisualRewindActorInfo;

	UnbindVisualRewindDelegates();
	EndAbility(AbilityHandle, ActorInfo, ActivationInfo, true, true);
}

void UVisualRewindAbility::UnbindVisualRewindDelegates()
{
	if (ActiveVisualRewindComponent)
	{
		if (VisualRewindFinishedDelegateHandle.IsValid())
		{
			ActiveVisualRewindComponent->OnVisualRewindFinished.Remove(VisualRewindFinishedDelegateHandle);
		}

		if (VisualRewindRejectedDelegateHandle.IsValid())
		{
			ActiveVisualRewindComponent->OnVisualRewindRejected.Remove(VisualRewindRejectedDelegateHandle);
		}
	}

	VisualRewindFinishedDelegateHandle.Reset();
	VisualRewindRejectedDelegateHandle.Reset();
	ActiveVisualRewindComponent = nullptr;
	ActiveVisualRewindAbilityHandle = FGameplayAbilitySpecHandle();
	ActiveVisualRewindActivationInfo = FGameplayAbilityActivationInfo();
	ActiveVisualRewindActorInfo = nullptr;
}

void UVisualRewindAbility::RejectPredictedRewindIfAuthority() const
{
	if (ActiveVisualRewindComponent)
	{
		ActiveVisualRewindComponent->RejectPredictedRewindFromServer();
	}
}

