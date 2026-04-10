#include "BlinkAbility.h"

#include "Components/CapsuleComponent.h"
#include "GameFramework/Character.h"
#include "GameFramework/Controller.h"
#include "NavigationSystem.h"

UBlinkAbility::UBlinkAbility()
{
	InstancingPolicy = EGameplayAbilityInstancingPolicy::InstancedPerActor;
	NetExecutionPolicy = EGameplayAbilityNetExecutionPolicy::LocalPredicted;
}

void UBlinkAbility::ActivateAbility(
	const FGameplayAbilitySpecHandle Handle,
	const FGameplayAbilityActorInfo* ActorInfo,
	const FGameplayAbilityActivationInfo ActivationInfo,
	const FGameplayEventData* TriggerEventData)
{
	ACharacter* Character = ActorInfo ? Cast<ACharacter>(ActorInfo->AvatarActor.Get()) : nullptr;
	if (!Character)
	{
		EndAbility(Handle, ActorInfo, ActivationInfo, true, true);
		return;
	}

	UWorld* World = Character->GetWorld();
	UCapsuleComponent* Capsule = Character->GetCapsuleComponent();
	if (!World || !Capsule)
	{
		EndAbility(Handle, ActorInfo, ActivationInfo, true, true);
		return;
	}

	FVector BlinkDirection = Character->GetActorForwardVector(); 
	BlinkDirection.Z = 0.0f;

	// 如果是通过 Event 触发，且带有 TargetData，则读取客户端传来的方向
	if (TriggerEventData && TriggerEventData->TargetData.Num() > 0)
	{
		UE_LOG(LogTemp,Warning,TEXT("TriggerEventData"));
		// 提取我们在 Character 里塞进去的向量
		BlinkDirection = TriggerEventData->TargetData.Get(0)->GetEndPoint(); 
	}
	BlinkDirection.Normalize();

	const FVector StartLocation = Character->GetActorLocation();
	const FVector FixedStartLocation = FVector(StartLocation.X, StartLocation.Y, StartLocation.Z+GroundFlatIgnoreDistance);
	const FVector FixedDesiredLocation = StartLocation + (BlinkDirection * BlinkDistance);
	
	const float CapsuleRadius = Capsule->GetScaledCapsuleRadius();
	const float CapsuleHalfHeight = Capsule->GetScaledCapsuleHalfHeight();
	const FCollisionShape CapsuleShape = FCollisionShape::MakeCapsule(CapsuleRadius, CapsuleHalfHeight);
	
	FCollisionQueryParams QueryParams(SCENE_QUERY_STAT(BlinkSweep), false, Character);
	QueryParams.AddIgnoredActor(Character);
	FHitResult HitResult;
	
	bool bHit = World->SweepSingleByChannel(HitResult,FixedStartLocation,FixedDesiredLocation,
		FQuat::Identity, ECC_Visibility, CapsuleShape, QueryParams);

	FVector TargetLocation = FixedDesiredLocation;
	TargetLocation.Z -= GroundFlatIgnoreDistance;
	
	if (bHit && HitResult.IsValidBlockingHit())
	{
		UE_LOG(LogTemp,Warning,TEXT("hit result: %f"),HitResult.Distance);
		DrawDebugSphere(World, HitResult.ImpactPoint, 50.0f, 12, FColor::Red, false, 5.0f);
		DrawDebugLine(World, HitResult.ImpactPoint, HitResult.ImpactPoint + (HitResult.ImpactNormal * 100.0f), FColor::Green, false, 5.0f, 0, 2.0f);
		if (HitResult.ImpactNormal.Z > 0.7f)
		{
			// 斜坡，则继续沿着斜坡方向进行碰撞检测
			const FVector SlopeDirection = (HitResult.ImpactNormal+BlinkDirection).GetSafeNormal();
			// FVector SlopeDirection = FVector::VectorPlaneProject(BlinkDirection, HitResult.ImpactNormal).GetSafeNormal();
			const FVector SlopeStartLocation = HitResult.Location + CapsuleHalfHeight+GroundFlatIgnoreDistance;
			const FVector SlopeDesiredLocation = SlopeStartLocation + (SlopeDirection * (BlinkDistance - HitResult.Distance));

			bool bHitSlope = World->SweepSingleByChannel(HitResult,SlopeStartLocation,SlopeDesiredLocation,
	FQuat::Identity, ECC_Visibility, CapsuleShape, QueryParams);

			if (bHitSlope && HitResult.IsValidBlockingHit())
			{
				TargetLocation = HitResult.Location;
			}
			else
			{
				TargetLocation = SlopeDesiredLocation;
			}
			// 确保目标位置在地面上
			FHitResult GroundHit;
			bool bGroundHit = World->LineTraceSingleByChannel(GroundHit, TargetLocation, TargetLocation - FVector(0.0f, 0.0f, 1000.0f), ECC_Visibility, QueryParams);
			if (bGroundHit && GroundHit.IsValidBlockingHit())
			{
				TargetLocation.Z = GroundHit.ImpactPoint.Z + CapsuleHalfHeight;
			}
			
		}else
		{
			TargetLocation = HitResult.Location - GroundFlatIgnoreDistance;
		}
	}

	const bool bTeleported = Character->TeleportTo(TargetLocation, Character->GetActorRotation(), false, false);
	if (!bTeleported)
	{
		EndAbility(Handle, ActorInfo, ActivationInfo, true, true);
		return;
	}

	if (!CommitAbility(Handle, ActorInfo, ActivationInfo))
	{
		EndAbility(Handle, ActorInfo, ActivationInfo, true, true);
		return;
	}

	EndAbility(Handle, ActorInfo, ActivationInfo, false, false);
}
