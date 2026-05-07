#include "RewindComponent.h"

#include "Components/SkeletalMeshComponent.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/Controller.h"
#include "GameFramework/Pawn.h"
#include "Gameplay_Demo/RewindCharacterMovementComponent.h"
#include "Net/UnrealNetwork.h"
#include "TimerManager.h"

namespace
{
FRotator LerpRotatorShortestPath(const FRotator& From, const FRotator& To, float Alpha)
{
	const FQuat ResultQuat = FQuat::Slerp(From.Quaternion(), To.Quaternion(), Alpha);
	return ResultQuat.Rotator();
}
}

URewindComponent::URewindComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	SetIsReplicatedByDefault(true);
}

void URewindComponent::BeginPlay()
{
	Super::BeginPlay();

	RecordSnapshot();
}

void URewindComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	Super::EndPlay(EndPlayReason);
}

void URewindComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	if (bIsRewinding)
	{
		// 预测模式下，CharacterMovement 会从 PhysCustom 调用 AdvanceRewindPlayback。
		// 组件 Tick 只驱动历史兼容/兜底的非预测路径。
		if (!bPlaybackDrivenByMovementComponent)
		{
			UpdateRewindPlayback(DeltaTime);
		}
		return;
	}

	if (!CanRecordSnapshots())
	{
		return;
	}

	const float CurrentTimeSeconds = GetCurrentTimeSeconds();
	if (LastRecordTimestampSeconds < 0.0f || (CurrentTimeSeconds - LastRecordTimestampSeconds) >= RecordIntervalSeconds)
	{
		RecordSnapshot();
	}
}

void URewindComponent::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	DOREPLIFETIME(URewindComponent, bHiddenFromOtherPlayers);
}

bool URewindComponent::StartRewind()
{
	if (!CanStartRewind())
	{
		return false;
	}

	if (const ACharacter* CharacterOwner = Cast<ACharacter>(GetOwner()))
	{
		if (URewindCharacterMovementComponent* RewindMovement = Cast<URewindCharacterMovementComponent>(CharacterOwner->GetCharacterMovement()))
		{
			// 排队一个 saved move 输入 bit，而不是立刻启动。
			// 这样 CharacterMovement 可以在服务器重放同一个开始帧。
			return RewindMovement->RequestPredictedRewind();
		}
	}

	// 非 Character 或没有自定义移动组件的 Owner 仍使用本地兜底路径。
	return StartRewindPlayback(false);
}

bool URewindComponent::CanStartRewind() const
{
	return GetOwner() && !bIsRewinding && RewindHistory.Num() > 0;
}

bool URewindComponent::StartRewindPlayback(bool bDrivenByMovementComponent)
{
	AActor* OwnerActor = GetOwner();
	if (!CanStartRewind())
	{
		return false;
	}

	RecordSnapshot();

	const float CurrentTimeSeconds = GetCurrentTimeSeconds();
	const float TargetTimestampSeconds = CurrentTimeSeconds - RewindLookbackSeconds;
	FRewindStateSnapshot StartingSnapshot;
	FRewindStateSnapshot TargetSnapshot;

	if (!SampleSnapshotAtTime(CurrentTimeSeconds, StartingSnapshot))
	{
		StartingSnapshot = RewindHistory.Last();
	}

	if (!SampleSnapshotAtTime(TargetTimestampSeconds, TargetSnapshot))
	{
		TargetSnapshot = RewindHistory[0];
	}

	// 端点仍按时间采样，端点之间的播放则按“当前 -> 过去”路径上的距离采样。
	RewindPlaybackElapsedSeconds = 0.0f;
	BuildRewindPlaybackPath(StartingSnapshot, TargetSnapshot);
	bIsRewinding = true;
	bPlaybackDrivenByMovementComponent = bDrivenByMovementComponent;

	ApplyVisibilityState(true);

	if (ACharacter* CharacterOwner = Cast<ACharacter>(OwnerActor))
	{
		if (UCharacterMovementComponent* MovementComponent = CharacterOwner->GetCharacterMovement())
		{
			CachedMovementMode = MovementComponent->MovementMode;
			CachedCustomMovementMode = MovementComponent->CustomMovementMode;
			bHasCachedMovementMode = true;
			MovementComponent->StopMovementImmediately();
			if (!bPlaybackDrivenByMovementComponent)
			{
				MovementComponent->DisableMovement();
			}
		}
	}

	if (bPlaybackDrivenByMovementComponent)
	{
		// 预测模式下 Actor 移动由 CharacterMovement 负责。
		// 这里只应用控制旋转，避免在 saved move 重放链路之外改变位置。
		ApplyControlRotation(StartingSnapshot);
	}
	else
	{
		ApplySnapshot(StartingSnapshot);
	}

	return true;
}

void URewindComponent::OnRep_HiddenFromOtherPlayers()
{
	ApplyVisibilityToOwner();
}

void URewindComponent::FinishRewindPlayback()
{
	if (!bIsRewinding)
	{
		return;
	}

	FRewindStateSnapshot FinalSnapshot;
	if (RewindPlaybackPath.Num() > 0)
	{
		FinalSnapshot = RewindPlaybackPath.Last();
		if (bPlaybackDrivenByMovementComponent)
		{
			// 最后一次 PhysRewind tick 后，移动组件应已到达最终位置；
			// 这里只应用视角旋转。
			ApplyControlRotation(FinalSnapshot);
		}
		else
		{
			ApplySnapshot(FinalSnapshot);
		}
	}

	bIsRewinding = false;
	ApplyVisibilityState(false);

	if (ACharacter* CharacterOwner = Cast<ACharacter>(GetOwner()))
	{
		if (UCharacterMovementComponent* MovementComponent = CharacterOwner->GetCharacterMovement())
		{
			if (bHasCachedMovementMode)
			{
				MovementComponent->SetMovementMode(CachedMovementMode, CachedCustomMovementMode);
			}
			else
			{
				MovementComponent->SetMovementMode(MOVE_Walking);
			}
		}
	}

	bHasCachedMovementMode = false;
	bPlaybackDrivenByMovementComponent = false;
	RewindPlaybackPath.Reset();
	RewindPlaybackTotalDistance = 0.0f;
	LastRecordTimestampSeconds = -1.0f;

	// 记录一份回溯后的新状态，让后续回溯从校正后的终点开始。
	RecordSnapshot();

	// 所有状态都已经恢复并记录完成后再通知外部，避免监听方看到半清理状态。
	OnRewindFinished.Broadcast();
}

void URewindComponent::RecordSnapshot()
{
	AActor* OwnerActor = GetOwner();
	if (!OwnerActor)
	{
		return;
	}

	FRewindStateSnapshot Snapshot;
	Snapshot.Location = OwnerActor->GetActorLocation();
	Snapshot.ActorRotation = OwnerActor->GetActorRotation();

	if (const APawn* PawnOwner = Cast<APawn>(OwnerActor))
	{
		if (const AController* Controller = PawnOwner->GetController())
		{
			Snapshot.ControlRotation = Controller->GetControlRotation();
		}
		else
		{
			Snapshot.ControlRotation = Snapshot.ActorRotation;
		}
	}
	else
	{
		Snapshot.ControlRotation = Snapshot.ActorRotation;
	}

	Snapshot.TimestampSeconds = GetCurrentTimeSeconds();

	RewindHistory.Add(Snapshot);
	LastRecordTimestampSeconds = Snapshot.TimestampSeconds;

	const float ExpireBeforeSeconds = Snapshot.TimestampSeconds - RewindLookbackSeconds;
	while (RewindHistory.Num() > 1 && RewindHistory[0].TimestampSeconds < ExpireBeforeSeconds)
	{
		RewindHistory.RemoveAt(0);
	}
}

bool URewindComponent::SampleSnapshotAtTime(float TargetTimestampSeconds, FRewindStateSnapshot& OutSnapshot) const
{
	if (RewindHistory.Num() == 0)
	{
		return false;
	}

	if (TargetTimestampSeconds <= RewindHistory[0].TimestampSeconds)
	{
		OutSnapshot = RewindHistory[0];
		return true;
	}

	if (TargetTimestampSeconds >= RewindHistory.Last().TimestampSeconds)
	{
		OutSnapshot = RewindHistory.Last();
		return true;
	}

	for (int32 Index = 1; Index < RewindHistory.Num(); ++Index)
	{
		const FRewindStateSnapshot& Previous = RewindHistory[Index - 1];
		const FRewindStateSnapshot& Next = RewindHistory[Index];
		if (TargetTimestampSeconds <= Next.TimestampSeconds)
		{
			const float TimeSpan = FMath::Max(Next.TimestampSeconds - Previous.TimestampSeconds, KINDA_SMALL_NUMBER);
			const float Alpha = FMath::Clamp((TargetTimestampSeconds - Previous.TimestampSeconds) / TimeSpan, 0.0f, 1.0f);

			OutSnapshot.Location = FMath::Lerp(Previous.Location, Next.Location, Alpha);
			OutSnapshot.ActorRotation = LerpRotatorShortestPath(Previous.ActorRotation, Next.ActorRotation, Alpha);
			OutSnapshot.ControlRotation = LerpRotatorShortestPath(Previous.ControlRotation, Next.ControlRotation, Alpha);
			OutSnapshot.TimestampSeconds = TargetTimestampSeconds;
			return true;
		}
	}

	OutSnapshot = RewindHistory.Last();
	return true;
}

void URewindComponent::BuildRewindPlaybackPath(const FRewindStateSnapshot& StartingSnapshot, const FRewindStateSnapshot& TargetSnapshot)
{
	RewindPlaybackPath.Reset();
	RewindPlaybackTotalDistance = 0.0f;

	// 播放路径按“现在 -> 过去”排序。RewindHistory 本身是“最旧 -> 最新”，
	// 因此中间点需要倒序追加。
	RewindPlaybackPath.Add(StartingSnapshot);

	for (int32 Index = RewindHistory.Num() - 1; Index >= 0; --Index)
	{
		const FRewindStateSnapshot& Snapshot = RewindHistory[Index];
		const bool bInsidePlaybackWindow =
			Snapshot.TimestampSeconds < StartingSnapshot.TimestampSeconds - KINDA_SMALL_NUMBER &&
			Snapshot.TimestampSeconds > TargetSnapshot.TimestampSeconds + KINDA_SMALL_NUMBER;

		if (bInsidePlaybackWindow)
		{
			RewindPlaybackPath.Add(Snapshot);
		}
	}

	RewindPlaybackPath.Add(TargetSnapshot);

	for (int32 Index = 0; Index < RewindPlaybackPath.Num(); ++Index)
	{
		if (Index > 0)
		{
			// 使用累计距离让播放进度映射到空间距离，而不是历史时间戳。
			RewindPlaybackTotalDistance += FVector::Dist(
				RewindPlaybackPath[Index - 1].Location,
				RewindPlaybackPath[Index].Location);
		}

		RewindPlaybackPath[Index].DistanceAlongRewindPath = RewindPlaybackTotalDistance;
	}
}

bool URewindComponent::SampleSnapshotAtDistance(float TargetDistance, FRewindStateSnapshot& OutSnapshot) const
{
	if (RewindPlaybackPath.Num() == 0)
	{
		return false;
	}

	if (TargetDistance <= 0.0f || RewindPlaybackTotalDistance <= KINDA_SMALL_NUMBER)
	{
		OutSnapshot = RewindPlaybackPath[0];
		return true;
	}

	if (TargetDistance >= RewindPlaybackTotalDistance)
	{
		OutSnapshot = RewindPlaybackPath.Last();
		return true;
	}

	for (int32 Index = 1; Index < RewindPlaybackPath.Num(); ++Index)
	{
		const FRewindStateSnapshot& Previous = RewindPlaybackPath[Index - 1];
		const FRewindStateSnapshot& Next = RewindPlaybackPath[Index];

		if (TargetDistance <= Next.DistanceAlongRewindPath)
		{
			// 在包含 TargetDistance 的路径段内插值。
			const float DistanceSpan = FMath::Max(
				Next.DistanceAlongRewindPath - Previous.DistanceAlongRewindPath,
				KINDA_SMALL_NUMBER);
			const float Alpha = FMath::Clamp(
				(TargetDistance - Previous.DistanceAlongRewindPath) / DistanceSpan,
				0.0f,
				1.0f);

			OutSnapshot.Location = FMath::Lerp(Previous.Location, Next.Location, Alpha);
			OutSnapshot.ActorRotation = LerpRotatorShortestPath(Previous.ActorRotation, Next.ActorRotation, Alpha);
			OutSnapshot.ControlRotation = LerpRotatorShortestPath(Previous.ControlRotation, Next.ControlRotation, Alpha);
			OutSnapshot.TimestampSeconds = FMath::Lerp(Previous.TimestampSeconds, Next.TimestampSeconds, Alpha);
			OutSnapshot.DistanceAlongRewindPath = TargetDistance;
			return true;
		}
	}

	OutSnapshot = RewindPlaybackPath.Last();
	return true;
}

void URewindComponent::ApplyVisibilityState(bool bHideFromOtherPlayers)
{
	if (GetOwner() && GetOwner()->HasAuthority())
	{
		bHiddenFromOtherPlayers = bHideFromOtherPlayers;
	}

	ApplyVisibilityToOwner();
}

void URewindComponent::ApplyVisibilityToOwner() const
{
	const ACharacter* CharacterOwner = Cast<ACharacter>(GetOwner());
	if (!CharacterOwner)
	{
		return;
	}

	USkeletalMeshComponent* Mesh = CharacterOwner->GetMesh();
	if (!Mesh)
	{
		return;
	}

	const bool bHideMesh = bHiddenFromOtherPlayers && !CharacterOwner->IsLocallyControlled();
	Mesh->SetVisibility(!bHideMesh, true);
	Mesh->SetCastShadow(!bHideMesh);
}

float URewindComponent::GetCurrentTimeSeconds() const
{
	const UWorld* World = GetWorld();
	return World ? World->GetTimeSeconds() : 0.0f;
}

void URewindComponent::UpdateRewindPlayback(float DeltaTime)
{
	FRewindStateSnapshot PlaybackSnapshot;
	bool bFinished = false;
	if (AdvanceRewindPlayback(DeltaTime, PlaybackSnapshot, bFinished))
	{
		ApplySnapshot(PlaybackSnapshot);
	}

	if (bFinished)
	{
		FinishRewindPlayback();
	}
}

bool URewindComponent::AdvanceRewindPlayback(float DeltaTime, FRewindStateSnapshot& OutSnapshot, bool& bOutFinished)
{
	bOutFinished = false;
	if (!bIsRewinding || RewindPlaybackPath.Num() == 0)
	{
		return false;
	}

	const float PlaybackDuration = FMath::Max(RewindPlaybackDurationSeconds, KINDA_SMALL_NUMBER);
	RewindPlaybackElapsedSeconds = FMath::Min(RewindPlaybackElapsedSeconds + DeltaTime, PlaybackDuration);

	const float PlaybackAlpha = RewindPlaybackElapsedSeconds / PlaybackDuration;
	const float TargetDistance = RewindPlaybackTotalDistance * PlaybackAlpha;

	bool bSampledSnapshot = false;
	if (RewindPlaybackTotalDistance <= KINDA_SMALL_NUMBER && RewindPlaybackPath.Num() >= 2)
	{
		// 如果 Actor 几乎没有移动，就没有可采样的距离轴。
		// 仍然插值 Actor/控制器旋转，确保回溯能到达过去的瞄准状态。
		const FRewindStateSnapshot& StartSnapshot = RewindPlaybackPath[0];
		const FRewindStateSnapshot& TargetSnapshot = RewindPlaybackPath.Last();
		OutSnapshot.Location = FMath::Lerp(StartSnapshot.Location, TargetSnapshot.Location, PlaybackAlpha);
		OutSnapshot.ActorRotation = LerpRotatorShortestPath(StartSnapshot.ActorRotation, TargetSnapshot.ActorRotation, PlaybackAlpha);
		OutSnapshot.ControlRotation = LerpRotatorShortestPath(StartSnapshot.ControlRotation, TargetSnapshot.ControlRotation, PlaybackAlpha);
		OutSnapshot.TimestampSeconds = FMath::Lerp(StartSnapshot.TimestampSeconds, TargetSnapshot.TimestampSeconds, PlaybackAlpha);
		OutSnapshot.DistanceAlongRewindPath = 0.0f;
		bSampledSnapshot = true;
	}
	else
	{
		bSampledSnapshot = SampleSnapshotAtDistance(TargetDistance, OutSnapshot);
	}

	bOutFinished = RewindPlaybackElapsedSeconds >= PlaybackDuration;
	return bSampledSnapshot;
}

void URewindComponent::ApplySnapshot(const FRewindStateSnapshot& Snapshot) const
{
	AActor* OwnerActor = GetOwner();
	if (!OwnerActor)
	{
		return;
	}

	OwnerActor->SetActorLocationAndRotation(
		Snapshot.Location,
		Snapshot.ActorRotation,
		false,
		nullptr,
		ETeleportType::TeleportPhysics);

	if (Cast<APawn>(OwnerActor))
	{
		ApplyControlRotation(Snapshot);
	}
}

void URewindComponent::ApplyControlRotation(const FRewindStateSnapshot& Snapshot) const
{
	if (const APawn* PawnOwner = Cast<APawn>(GetOwner()))
	{
		if (AController* Controller = PawnOwner->GetController())
		{
			Controller->SetControlRotation(Snapshot.ControlRotation);
		}
	}
}

bool URewindComponent::CanRecordSnapshots() const
{
	const AActor* OwnerActor = GetOwner();
	return OwnerActor && !bIsRewinding;
}
