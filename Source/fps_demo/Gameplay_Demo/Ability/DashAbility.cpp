#include "DashAbility.h"

#include "GameFramework/Character.h"
#include "GameFramework/Controller.h"

UDashAbility::UDashAbility()
{
	InstancingPolicy = EGameplayAbilityInstancingPolicy::InstancedPerActor;
	NetExecutionPolicy = EGameplayAbilityNetExecutionPolicy::LocalPredicted;
}

void UDashAbility::ActivateAbility(
	const FGameplayAbilitySpecHandle Handle,
	const FGameplayAbilityActorInfo* ActorInfo,
	const FGameplayAbilityActivationInfo ActivationInfo,
	const FGameplayEventData* TriggerEventData)
{
	if (!CommitAbility(Handle, ActorInfo, ActivationInfo))
	{
		EndAbility(Handle, ActorInfo, ActivationInfo, true, true);
		return;
	}

	ACharacter* Character = ActorInfo ? Cast<ACharacter>(ActorInfo->AvatarActor.Get()) : nullptr;
	if (!Character)
	{
		EndAbility(Handle, ActorInfo, ActivationInfo, true, true);
		return;
	}

	FVector DashDirection = Character->GetActorForwardVector();
	if (AController* Controller = Character->GetController())
	{
		const FRotator ControlRotation = Controller->GetControlRotation();
		DashDirection = FRotationMatrix(FRotator(0.0f, ControlRotation.Yaw, 0.0f)).GetUnitAxis(EAxis::X);
	}

	DashDirection.Normalize();
	const FVector LaunchVelocity = (DashDirection * DashStrength) + (FVector::UpVector * UpwardStrength);

    

	Character->LaunchCharacter(LaunchVelocity, true, false);

	EndAbility(Handle, ActorInfo, ActivationInfo, false, false);
}
