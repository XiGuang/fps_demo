#include "BlinkAbility.h"

#include "Abilities/GameplayAbilityTargetTypes.h"
#include "Components/CapsuleComponent.h"
#include "DrawDebugHelpers.h"
#include "Engine/CollisionProfile.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
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

	const FVector BlinkDirection = GetBlinkDirection(Character, TriggerEventData);

	FVector TargetLocation = Character->GetActorLocation();
	if (!FindBlinkDestination(Character, BlinkDirection, TargetLocation))
	{
		EndAbility(Handle, ActorInfo, ActivationInfo, true, true);
		return;
	}

	if (!CommitAbility(Handle, ActorInfo, ActivationInfo))
	{
		UE_LOG(LogTemp, Warning, TEXT("BlinkAbility: CommitAbility failed."));
		EndAbility(Handle, ActorInfo, ActivationInfo, true, true);
		return;
	}

	// The destination has already been validated with the character capsule profile.
	// Force the teleport so TeleportTo does not reject a valid floor-touching capsule.
	const bool bTeleported = Character->TeleportTo(TargetLocation, Character->GetActorRotation(), false, true);
	if (!bTeleported)
	{
		UE_LOG(LogTemp, Warning, TEXT("BlinkAbility: TeleportTo failed. TargetLocation=%s"), *TargetLocation.ToString());
		EndAbility(Handle, ActorInfo, ActivationInfo, true, true);
		return;
	}

	EndAbility(Handle, ActorInfo, ActivationInfo, false, false);
}

FVector UBlinkAbility::GetBlinkDirection(const ACharacter* Character, const FGameplayEventData* TriggerEventData) const
{
	FVector BlinkDirection = Character ? Character->GetActorForwardVector() : FVector::ForwardVector;

	if (TriggerEventData && TriggerEventData->TargetData.Num() > 0)
	{
		if (const FGameplayAbilityTargetData* TargetData = TriggerEventData->TargetData.Get(0))
		{
			BlinkDirection = TargetData->GetEndPoint();
		}
	}

	BlinkDirection.Z = 0.0f;
	if (!BlinkDirection.Normalize())
	{
		BlinkDirection = Character ? Character->GetActorForwardVector() : FVector::ForwardVector;
		BlinkDirection.Z = 0.0f;
		BlinkDirection.Normalize();
	}

	return BlinkDirection;
}

bool UBlinkAbility::FindBlinkDestination(ACharacter* Character, const FVector& BlinkDirection, FVector& OutTargetLocation) const
{
	if (!Character || BlinkDirection.IsNearlyZero())
	{
		return false;
	}

	UWorld* World = Character->GetWorld();
	UCapsuleComponent* Capsule = Character->GetCapsuleComponent();
	if (!World || !Capsule)
	{
		return false;
	}

	FCollisionQueryParams QueryParams(SCENE_QUERY_STAT(BlinkAbility), false, Character);
	QueryParams.AddIgnoredActor(Character);

	const FVector SourceLocation = Character->GetActorLocation();
	const FVector DesiredLocation = SourceLocation + BlinkDirection * BlinkDistance;

	OutTargetLocation = RepairBlinkLocation(Character, SourceLocation, DesiredLocation, BlinkDirection, QueryParams);

	FVector NavLocation = OutTargetLocation;
	if (bProjectToNavigation && TryProjectToNavigation(Character, OutTargetLocation, QueryParams, NavLocation))
	{
		OutTargetLocation = NavLocation;
	}

	if (bDrawDebug)
	{
		DrawDebugSphere(World, DesiredLocation, 24.0f, 12, FColor::Yellow, false, 2.0f);
		DrawDebugSphere(World, OutTargetLocation, 32.0f, 12, FColor::Cyan, false, 2.0f);
		DrawDebugLine(World, SourceLocation, OutTargetLocation, FColor::Cyan, false, 2.0f, 0, 2.0f);
	}

	return true;
}

FVector UBlinkAbility::RepairBlinkLocation(
	ACharacter* Character,
	const FVector& SourceLocation,
	const FVector& DesiredLocation,
	const FVector& BlinkDirection,
	const FCollisionQueryParams& QueryParams) const
{
	const float MaxDistance = FVector::Dist2D(SourceLocation, DesiredLocation);
	return TraceBlinkPath(Character, SourceLocation, BlinkDirection, MaxDistance, QueryParams);
}

FVector UBlinkAbility::TraceBlinkPath(
	ACharacter* Character,
	const FVector& SourceLocation,
	const FVector& BlinkDirection,
	float MaxDistance,
	const FCollisionQueryParams& QueryParams) const
{
	UWorld* World = Character->GetWorld();
	UCapsuleComponent* Capsule = Character->GetCapsuleComponent();
	UCharacterMovementComponent* CharacterMovement = Character->GetCharacterMovement();
	if (!World || !Capsule || BlinkDirection.IsNearlyZero() || MaxDistance <= 0.0f)
	{
		return SourceLocation;
	}

	const float CapsuleRadius = Capsule->GetScaledCapsuleRadius();
	const float CapsuleHalfHeight = Capsule->GetScaledCapsuleHalfHeight();
	const FCollisionShape CapsuleShape = FCollisionShape::MakeCapsule(CapsuleRadius, CapsuleHalfHeight);
	const FVector HorizontalDirection = FVector(BlinkDirection.X, BlinkDirection.Y, 0.0f).GetSafeNormal();
	const float StepDistance = FMath::Max(PathStepDistance, 10.0f);
	const int32 StepCount = FMath::Max(FMath::CeilToInt(MaxDistance / StepDistance), 1);

	FVector CurrentLocation = SourceLocation;
	FVector StartGroundedLocation = SourceLocation;
	FHitResult StartGroundHit;
	bool bGrounded = CharacterMovement ? CharacterMovement->IsMovingOnGround() : false;
	if (!bGrounded)
	{
		bGrounded = TryFindWalkableGround(
			Character,
			SourceLocation,
			GroundFlatIgnoreDistance + CapsuleHalfHeight,
			GroundFlatIgnoreDistance + CapsuleHalfHeight,
			QueryParams,
			StartGroundedLocation,
			StartGroundHit);
	}

	if (bGrounded)
	{
		CurrentLocation.Z = StartGroundedLocation.Z;
	}

	float RemainingDistance = MaxDistance;
	for (int32 StepIndex = 0; StepIndex < StepCount && RemainingDistance > KINDA_SMALL_NUMBER; ++StepIndex)
	{
		const float ThisStepDistance = FMath::Min(StepDistance, RemainingDistance);
		RemainingDistance -= ThisStepDistance;

		FVector CandidateLocation = CurrentLocation + HorizontalDirection * ThisStepDistance;
		bool bCandidateGrounded = false;

		if (bGrounded)
		{
			FVector GroundedLocation = CandidateLocation;
			FHitResult GroundHit;
			const bool bFoundGround = TryFindWalkableGround(
				Character,
				CandidateLocation,
				CapsuleHalfHeight + MaxGroundFollowRise + GroundFlatIgnoreDistance,
				CapsuleHalfHeight + MaxGroundFollowDrop + GroundFlatIgnoreDistance,
				QueryParams,
				GroundedLocation,
				GroundHit);

			if (bFoundGround)
			{
				const float DeltaZ = GroundedLocation.Z - CurrentLocation.Z;
				if (DeltaZ <= MaxGroundFollowRise && -DeltaZ <= MaxGroundFollowDrop)
				{
					CandidateLocation = GroundedLocation;
					bCandidateGrounded = true;
				}
				else if (DeltaZ < -MaxGroundFollowDrop)
				{
					CandidateLocation.Z = CurrentLocation.Z;
					bGrounded = false;
				}
				else
				{
					break;
				}
			}
			else
			{
				CandidateLocation.Z = CurrentLocation.Z;
				bGrounded = false;
			}
		}
		else
		{
			CandidateLocation.Z = CurrentLocation.Z;
		}

		if (IsCapsuleBlockedAt(Character, CandidateLocation, QueryParams))
		{
			break;
		}

		FHitResult MoveHit;
		const FVector SweepStart = CurrentLocation + FVector::UpVector * GroundFlatIgnoreDistance;
		const FVector SweepEnd = CandidateLocation + FVector::UpVector * GroundFlatIgnoreDistance;
		const bool bMoveBlocked = World->SweepSingleByChannel(
			MoveHit,
			SweepStart,
			SweepEnd,
			FQuat::Identity,
			MovementTraceChannel.GetValue(),
			CapsuleShape,
			QueryParams);

		if (bMoveBlocked && MoveHit.IsValidBlockingHit() && !(bCandidateGrounded && IsWalkableHit(Character, MoveHit)))
		{
			const FVector MoveDirection = (CandidateLocation - CurrentLocation).GetSafeNormal();
			FVector SafeLocation = MoveHit.Location - FVector::UpVector * GroundFlatIgnoreDistance - MoveDirection * FMath::Max(CapsuleOverlapShrink, 1.0f);
			if (!IsCapsuleBlockedAt(Character, SafeLocation, QueryParams))
			{
				CurrentLocation = SafeLocation;
			}
			break;
		}

		CurrentLocation = CandidateLocation;
		bGrounded = bCandidateGrounded;

		if (bDrawDebug)
		{
			DrawDebugSphere(World, CurrentLocation, 12.0f, 8, bGrounded ? FColor::Blue : FColor::Purple, false, 2.0f);
		}
	}

	return CurrentLocation;
}

bool UBlinkAbility::TryFindWalkableGround(
	ACharacter* Character,
	const FVector& Location,
	float UpDistance,
	float DownDistance,
	const FCollisionQueryParams& QueryParams,
	FVector& OutGroundedLocation,
	FHitResult& OutGroundHit) const
{
	UWorld* World = Character ? Character->GetWorld() : nullptr;
	UCapsuleComponent* Capsule = Character ? Character->GetCapsuleComponent() : nullptr;
	if (!World || !Capsule)
	{
		return false;
	}

	const FVector TraceStart = Location + FVector::UpVector * FMath::Max(UpDistance, 0.0f);
	const FVector TraceEnd = Location - FVector::UpVector * FMath::Max(DownDistance, 0.0f);
	const bool bHitGround = World->LineTraceSingleByChannel(
		OutGroundHit,
		TraceStart,
		TraceEnd,
		GroundTraceChannel.GetValue(),
		QueryParams);

	if (!bHitGround || !OutGroundHit.IsValidBlockingHit() || !IsWalkableHit(Character, OutGroundHit))
	{
		return false;
	}

	OutGroundedLocation = OutGroundHit.ImpactPoint + FVector::UpVector * Capsule->GetScaledCapsuleHalfHeight();

	if (bDrawDebug)
	{
		DrawDebugLine(World, TraceStart, TraceEnd, FColor::Blue, false, 2.0f, 0, 1.0f);
		DrawDebugSphere(World, OutGroundHit.ImpactPoint, 12.0f, 8, FColor::Blue, false, 2.0f);
	}

	return true;
}

bool UBlinkAbility::IsWalkableHit(ACharacter* Character, const FHitResult& Hit) const
{
	if (!Hit.IsValidBlockingHit())
	{
		return false;
	}

	if (const UCharacterMovementComponent* CharacterMovement = Character ? Character->GetCharacterMovement() : nullptr)
	{
		return CharacterMovement->IsWalkable(Hit);
	}

	const float SurfaceDot = FMath::Clamp(FVector::DotProduct(Hit.ImpactNormal.GetSafeNormal(), FVector::UpVector), -1.0f, 1.0f);
	const float SurfaceAngle = FMath::RadiansToDegrees(FMath::Acos(SurfaceDot));
	return SurfaceAngle <= BlockingSurfaceAngle;
}

bool UBlinkAbility::TryProjectToGround(ACharacter* Character, const FVector& Location, const FCollisionQueryParams& QueryParams, FVector& OutGroundedLocation) const
{
	UCapsuleComponent* Capsule = Character ? Character->GetCapsuleComponent() : nullptr;
	if (!Capsule)
	{
		return false;
	}

	FHitResult GroundHit;
	return TryFindWalkableGround(
		Character,
		Location,
		GroundProbeDistance + Capsule->GetScaledCapsuleHalfHeight(),
		GroundProbeDistance,
		QueryParams,
		OutGroundedLocation,
		GroundHit);
}

bool UBlinkAbility::TrySampleBackToValidGround(
	ACharacter* Character,
	const FVector& InvalidLocation,
	const FVector& SourceLocation,
	const FCollisionQueryParams& QueryParams,
	FVector& OutLocation) const
{
	const int32 SampleCount = FMath::Max(RepairSampleCount, 2);
	for (int32 SampleIndex = 1; SampleIndex < SampleCount; ++SampleIndex)
	{
		const float Alpha = static_cast<float>(SampleIndex) / static_cast<float>(SampleCount);
		const FVector SampleLocation = FMath::Lerp(InvalidLocation, SourceLocation, Alpha);

		FVector GroundedLocation = SampleLocation;
		if (TryProjectToGround(Character, SampleLocation, QueryParams, GroundedLocation) &&
			!IsCapsuleBlockedAt(Character, GroundedLocation, QueryParams))
		{
			OutLocation = GroundedLocation;
			return true;
		}
	}

	return false;
}

bool UBlinkAbility::IsCapsuleBlockedAt(ACharacter* Character, const FVector& Location, const FCollisionQueryParams& QueryParams) const
{
	UWorld* World = Character ? Character->GetWorld() : nullptr;
	UCapsuleComponent* Capsule = Character ? Character->GetCapsuleComponent() : nullptr;
	if (!World || !Capsule)
	{
		return true;
	}

	const float ShrinkAmount = FMath::Max(CapsuleOverlapShrink, 0.0f);
	const float CapsuleRadius = FMath::Max(Capsule->GetScaledCapsuleRadius() - ShrinkAmount, 1.0f);
	const float CapsuleHalfHeight = FMath::Max(Capsule->GetScaledCapsuleHalfHeight() - ShrinkAmount, CapsuleRadius);
	const FCollisionShape CapsuleShape = FCollisionShape::MakeCapsule(CapsuleRadius, CapsuleHalfHeight);
	const FVector TestLocation = Location + FVector::UpVector * ShrinkAmount;

	return World->OverlapBlockingTestByProfile(
		TestLocation,
		FQuat::Identity,
		Capsule->GetCollisionProfileName(),
		CapsuleShape,
		QueryParams);
}

bool UBlinkAbility::TryProjectToNavigation(ACharacter* Character, const FVector& Location, const FCollisionQueryParams& QueryParams, FVector& OutLocation) const
{
	UWorld* World = Character ? Character->GetWorld() : nullptr;
	UCapsuleComponent* Capsule = Character ? Character->GetCapsuleComponent() : nullptr;
	UNavigationSystemV1* NavigationSystem = World ? FNavigationSystem::GetCurrent<UNavigationSystemV1>(World) : nullptr;
	if (!NavigationSystem || !Capsule)
	{
		return false;
	}

	FNavLocation NavLocation;
	const FVector QueryExtent(Capsule->GetScaledCapsuleRadius() * 2.0f, Capsule->GetScaledCapsuleRadius() * 2.0f, GroundProbeDistance);
	if (!NavigationSystem->ProjectPointToNavigation(Location, NavLocation, QueryExtent))
	{
		return false;
	}

	FVector ProjectedLocation = Location;
	ProjectedLocation.X = NavLocation.Location.X;
	ProjectedLocation.Y = NavLocation.Location.Y;

	FVector GroundedLocation = ProjectedLocation;
	if (TryProjectToGround(Character, ProjectedLocation, QueryParams, GroundedLocation) &&
		!IsCapsuleBlockedAt(Character, GroundedLocation, QueryParams))
	{
		OutLocation = GroundedLocation;
		return true;
	}

	return false;
}
