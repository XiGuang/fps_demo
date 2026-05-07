#include "VisualRewindComponent.h"

#include "Components/SkeletalMeshComponent.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/Controller.h"
#include "GameFramework/Pawn.h"
#include "Net/UnrealNetwork.h"

namespace
{
FRotator LerpVisualRewindRotatorShortestPath(const FRotator& From, const FRotator& To, float Alpha)
{
	const FQuat ResultQuat = FQuat::Slerp(From.Quaternion(), To.Quaternion(), Alpha);
	return ResultQuat.Rotator();
}
}

UVisualRewindComponent::UVisualRewindComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	SetIsReplicatedByDefault(true);
}

void UVisualRewindComponent::BeginPlay()
{
	Super::BeginPlay();

	RecordSnapshot();
}

void UVisualRewindComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(ServerSettlementTimerHandle);
	}

	Super::EndPlay(EndPlayReason);
}

void UVisualRewindComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	if (bIsLocalPlaybackActive)
	{
		TickLocalPlayback(DeltaTime);
		return;
	}

	if (!CanRecordSnapshots())
	{
		return;
	}

	const float CurrentTimeSeconds = GetCurrentTimeSeconds();
	if (LastRecordTimestampSeconds < 0.0f || CurrentTimeSeconds - LastRecordTimestampSeconds >= RecordIntervalSeconds)
	{
		RecordSnapshot();
	}
}

void UVisualRewindComponent::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	DOREPLIFETIME(UVisualRewindComponent, RewindRepState);
	DOREPLIFETIME(UVisualRewindComponent, bHiddenFromOtherPlayers);
}

bool UVisualRewindComponent::StartRewind()
{
	if (!CanStartRewind())
	{
		return false;
	}

	bool bStarted = false;
	const AActor* OwnerActor = GetOwner();

	// 本地控制者始终播放完整过程，包括 Standalone 和 Listen Server 主机。
	if (IsLocallyControlledOwner())
	{
		bStarted |= StartLocalPlayback();
	}

	if (OwnerActor && OwnerActor->HasAuthority())
	{
		bStarted |= StartServerSettlement();
	}

	return bStarted;
}

bool UVisualRewindComponent::CanStartRewind() const
{
	return GetOwner() && !bIsLocalPlaybackActive && !RewindRepState.bIsRewinding && RewindHistory.Num() > 0;
}

void UVisualRewindComponent::RejectPredictedRewindFromServer()
{
	if (GetOwner() && GetOwner()->HasAuthority())
	{
		ClientRejectRewind();
	}
}

void UVisualRewindComponent::OnRep_RewindRepState()
{
	ServerFinalSnapshot.Location = RewindRepState.FinalLocation;
	ServerFinalSnapshot.ActorRotation = RewindRepState.FinalActorRotation;
	ServerFinalSnapshot.ControlRotation = RewindRepState.FinalControlRotation;
	ServerFinalSnapshot.TimestampSeconds = RewindRepState.ServerStartTimeSeconds - RewindLookbackSeconds;
	bHasServerFinalSnapshot = RewindRepState.DurationSeconds > 0.0f;

	if (IsLocallyControlledOwner() && !RewindRepState.bIsRewinding && !bIsLocalPlaybackActive && bHasServerFinalSnapshot)
	{
		ApplySnapshot(ServerFinalSnapshot);
	}

	ApplyVisibilityToOwner();
}

void UVisualRewindComponent::OnRep_HiddenFromOtherPlayers()
{
	ApplyVisibilityToOwner();
}

void UVisualRewindComponent::ClientRejectRewind_Implementation()
{
	FinishLocalPlayback(true);
	OnVisualRewindRejected.Broadcast();
}

void UVisualRewindComponent::RecordSnapshot()
{
	AActor* OwnerActor = GetOwner();
	if (!OwnerActor)
	{
		return;
	}

	FVisualRewindStateSnapshot Snapshot;
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

bool UVisualRewindComponent::SampleSnapshotAtTime(float TargetTimestampSeconds, FVisualRewindStateSnapshot& OutSnapshot) const
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
		const FVisualRewindStateSnapshot& Previous = RewindHistory[Index - 1];
		const FVisualRewindStateSnapshot& Next = RewindHistory[Index];
		if (TargetTimestampSeconds <= Next.TimestampSeconds)
		{
			const float TimeSpan = FMath::Max(Next.TimestampSeconds - Previous.TimestampSeconds, KINDA_SMALL_NUMBER);
			const float Alpha = FMath::Clamp((TargetTimestampSeconds - Previous.TimestampSeconds) / TimeSpan, 0.0f, 1.0f);

			OutSnapshot.Location = FMath::Lerp(Previous.Location, Next.Location, Alpha);
			OutSnapshot.ActorRotation = LerpVisualRewindRotatorShortestPath(Previous.ActorRotation, Next.ActorRotation, Alpha);
			OutSnapshot.ControlRotation = LerpVisualRewindRotatorShortestPath(Previous.ControlRotation, Next.ControlRotation, Alpha);
			OutSnapshot.TimestampSeconds = TargetTimestampSeconds;
			return true;
		}
	}

	OutSnapshot = RewindHistory.Last();
	return true;
}

void UVisualRewindComponent::BuildLocalPlaybackPath(const FVisualRewindStateSnapshot& StartingSnapshot, const FVisualRewindStateSnapshot& TargetSnapshot)
{
	LocalPlaybackPath.Reset();
	LocalPlaybackTotalDistance = 0.0f;

	LocalPlaybackPath.Add(StartingSnapshot);

	for (int32 Index = RewindHistory.Num() - 1; Index >= 0; --Index)
	{
		const FVisualRewindStateSnapshot& Snapshot = RewindHistory[Index];
		const bool bInsidePlaybackWindow =
			Snapshot.TimestampSeconds < StartingSnapshot.TimestampSeconds - KINDA_SMALL_NUMBER &&
			Snapshot.TimestampSeconds > TargetSnapshot.TimestampSeconds + KINDA_SMALL_NUMBER;

		if (bInsidePlaybackWindow)
		{
			LocalPlaybackPath.Add(Snapshot);
		}
	}

	LocalPlaybackPath.Add(TargetSnapshot);

	for (int32 Index = 0; Index < LocalPlaybackPath.Num(); ++Index)
	{
		if (Index > 0)
		{
			LocalPlaybackTotalDistance += FVector::Dist(
				LocalPlaybackPath[Index - 1].Location,
				LocalPlaybackPath[Index].Location);
		}

		LocalPlaybackPath[Index].DistanceAlongRewindPath = LocalPlaybackTotalDistance;
	}
}

bool UVisualRewindComponent::SampleLocalSnapshotAtDistance(float TargetDistance, FVisualRewindStateSnapshot& OutSnapshot) const
{
	if (LocalPlaybackPath.Num() == 0)
	{
		return false;
	}

	if (TargetDistance <= 0.0f || LocalPlaybackTotalDistance <= KINDA_SMALL_NUMBER)
	{
		OutSnapshot = LocalPlaybackPath[0];
		return true;
	}

	if (TargetDistance >= LocalPlaybackTotalDistance)
	{
		OutSnapshot = LocalPlaybackPath.Last();
		return true;
	}

	for (int32 Index = 1; Index < LocalPlaybackPath.Num(); ++Index)
	{
		const FVisualRewindStateSnapshot& Previous = LocalPlaybackPath[Index - 1];
		const FVisualRewindStateSnapshot& Next = LocalPlaybackPath[Index];
		if (TargetDistance <= Next.DistanceAlongRewindPath)
		{
			const float DistanceSpan = FMath::Max(Next.DistanceAlongRewindPath - Previous.DistanceAlongRewindPath, KINDA_SMALL_NUMBER);
			const float Alpha = FMath::Clamp((TargetDistance - Previous.DistanceAlongRewindPath) / DistanceSpan, 0.0f, 1.0f);

			OutSnapshot.Location = FMath::Lerp(Previous.Location, Next.Location, Alpha);
			OutSnapshot.ActorRotation = LerpVisualRewindRotatorShortestPath(Previous.ActorRotation, Next.ActorRotation, Alpha);
			OutSnapshot.ControlRotation = LerpVisualRewindRotatorShortestPath(Previous.ControlRotation, Next.ControlRotation, Alpha);
			OutSnapshot.TimestampSeconds = FMath::Lerp(Previous.TimestampSeconds, Next.TimestampSeconds, Alpha);
			OutSnapshot.DistanceAlongRewindPath = TargetDistance;
			return true;
		}
	}

	OutSnapshot = LocalPlaybackPath.Last();
	return true;
}

bool UVisualRewindComponent::StartLocalPlayback()
{
	RecordSnapshot();

	const float CurrentTimeSeconds = GetCurrentTimeSeconds();
	const float TargetTimestampSeconds = CurrentTimeSeconds - RewindLookbackSeconds;
	FVisualRewindStateSnapshot StartingSnapshot;
	FVisualRewindStateSnapshot TargetSnapshot;

	if (!SampleSnapshotAtTime(CurrentTimeSeconds, StartingSnapshot))
	{
		StartingSnapshot = RewindHistory.Last();
	}

	if (!SampleSnapshotAtTime(TargetTimestampSeconds, TargetSnapshot))
	{
		TargetSnapshot = RewindHistory[0];
	}

	BuildLocalPlaybackPath(StartingSnapshot, TargetSnapshot);
	LocalPlaybackElapsedSeconds = 0.0f;
	bIsLocalPlaybackActive = true;
	bHasServerFinalSnapshot = false;

	CacheAndDisableMovement();
	ApplySnapshot(StartingSnapshot);
	return true;
}

bool UVisualRewindComponent::StartServerSettlement()
{
	RecordSnapshot();

	const float CurrentTimeSeconds = GetCurrentTimeSeconds();
	const float TargetTimestampSeconds = CurrentTimeSeconds - RewindLookbackSeconds;
	FVisualRewindStateSnapshot TargetSnapshot;
	if (!SampleSnapshotAtTime(TargetTimestampSeconds, TargetSnapshot))
	{
		if (RewindHistory.Num() == 0)
		{
			return false;
		}

		TargetSnapshot = RewindHistory[0];
	}

	ServerFinalSnapshot = TargetSnapshot;
	bHasServerFinalSnapshot = true;

	CacheAndDisableMovement();
	ApplyVisibilityState(true);

	RewindRepState.bIsRewinding = true;
	RewindRepState.ServerStartTimeSeconds = CurrentTimeSeconds;
	RewindRepState.DurationSeconds = RewindPlaybackDurationSeconds;
	RewindRepState.FinalLocation = TargetSnapshot.Location;
	RewindRepState.FinalActorRotation = TargetSnapshot.ActorRotation;
	RewindRepState.FinalControlRotation = TargetSnapshot.ControlRotation;
	RewindRepState.RewindSequence = ++NextRewindSequence;

	if (AActor* OwnerActor = GetOwner())
	{
		OwnerActor->ForceNetUpdate();
	}

	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().SetTimer(
			ServerSettlementTimerHandle,
			this,
			&UVisualRewindComponent::FinishServerSettlement,
			RewindPlaybackDurationSeconds,
			false);
	}

	return true;
}

void UVisualRewindComponent::TickLocalPlayback(float DeltaTime)
{
	const float PlaybackDuration = FMath::Max(RewindPlaybackDurationSeconds, KINDA_SMALL_NUMBER);
	LocalPlaybackElapsedSeconds = FMath::Min(LocalPlaybackElapsedSeconds + DeltaTime, PlaybackDuration);

	const float PlaybackAlpha = LocalPlaybackElapsedSeconds / PlaybackDuration;
	const float TargetDistance = LocalPlaybackTotalDistance * PlaybackAlpha;

	FVisualRewindStateSnapshot PlaybackSnapshot;
	bool bSampledSnapshot = false;
	if (LocalPlaybackTotalDistance <= KINDA_SMALL_NUMBER && LocalPlaybackPath.Num() >= 2)
	{
		const FVisualRewindStateSnapshot& StartSnapshot = LocalPlaybackPath[0];
		const FVisualRewindStateSnapshot& TargetSnapshot = LocalPlaybackPath.Last();
		PlaybackSnapshot.Location = FMath::Lerp(StartSnapshot.Location, TargetSnapshot.Location, PlaybackAlpha);
		PlaybackSnapshot.ActorRotation = LerpVisualRewindRotatorShortestPath(StartSnapshot.ActorRotation, TargetSnapshot.ActorRotation, PlaybackAlpha);
		PlaybackSnapshot.ControlRotation = LerpVisualRewindRotatorShortestPath(StartSnapshot.ControlRotation, TargetSnapshot.ControlRotation, PlaybackAlpha);
		PlaybackSnapshot.TimestampSeconds = FMath::Lerp(StartSnapshot.TimestampSeconds, TargetSnapshot.TimestampSeconds, PlaybackAlpha);
		bSampledSnapshot = true;
	}
	else
	{
		bSampledSnapshot = SampleLocalSnapshotAtDistance(TargetDistance, PlaybackSnapshot);
	}

	if (bSampledSnapshot)
	{
		ApplySnapshot(PlaybackSnapshot);
	}

	if (LocalPlaybackElapsedSeconds >= PlaybackDuration)
	{
		FinishLocalPlayback(false);
	}
}

void UVisualRewindComponent::FinishLocalPlayback(bool bWasRejected)
{
	if (!bIsLocalPlaybackActive && !bWasRejected)
	{
		return;
	}

	bIsLocalPlaybackActive = false;
	LocalPlaybackPath.Reset();
	LocalPlaybackTotalDistance = 0.0f;
	LocalPlaybackElapsedSeconds = 0.0f;

	if (!bWasRejected && bHasServerFinalSnapshot)
	{
		ApplySnapshot(ServerFinalSnapshot);
	}

	RestoreCachedMovement();
	LastRecordTimestampSeconds = -1.0f;
	RecordSnapshot();

	if (!bWasRejected)
	{
		OnVisualRewindFinished.Broadcast();
	}
}

void UVisualRewindComponent::FinishServerSettlement()
{
	if (!GetOwner() || !GetOwner()->HasAuthority() || !RewindRepState.bIsRewinding)
	{
		return;
	}

	ApplySnapshot(ServerFinalSnapshot);
	RestoreCachedMovement();
	ApplyVisibilityState(false);

	RewindRepState.bIsRewinding = false;
	RewindRepState.ServerStartTimeSeconds = GetCurrentTimeSeconds();
	RewindRepState.DurationSeconds = RewindPlaybackDurationSeconds;
	RewindRepState.FinalLocation = ServerFinalSnapshot.Location;
	RewindRepState.FinalActorRotation = ServerFinalSnapshot.ActorRotation;
	RewindRepState.FinalControlRotation = ServerFinalSnapshot.ControlRotation;

	if (AActor* OwnerActor = GetOwner())
	{
		OwnerActor->ForceNetUpdate();
	}

	LastRecordTimestampSeconds = -1.0f;
	RecordSnapshot();
	OnVisualRewindFinished.Broadcast();
}

void UVisualRewindComponent::CacheAndDisableMovement()
{
	ACharacter* CharacterOwner = Cast<ACharacter>(GetOwner());
	if (!CharacterOwner)
	{
		return;
	}

	UCharacterMovementComponent* MovementComponent = CharacterOwner->GetCharacterMovement();
	if (!MovementComponent)
	{
		return;
	}

	if (!bHasCachedMovementMode)
	{
		CachedMovementMode = MovementComponent->MovementMode;
		CachedCustomMovementMode = MovementComponent->CustomMovementMode;
		bHasCachedMovementMode = true;
	}

	MovementComponent->StopMovementImmediately();
	MovementComponent->DisableMovement();
}

void UVisualRewindComponent::RestoreCachedMovement()
{
	ACharacter* CharacterOwner = Cast<ACharacter>(GetOwner());
	if (!CharacterOwner)
	{
		bHasCachedMovementMode = false;
		return;
	}

	UCharacterMovementComponent* MovementComponent = CharacterOwner->GetCharacterMovement();
	if (!MovementComponent)
	{
		bHasCachedMovementMode = false;
		return;
	}

	if (bHasCachedMovementMode)
	{
		MovementComponent->SetMovementMode(CachedMovementMode, CachedCustomMovementMode);
	}
	else
	{
		MovementComponent->SetMovementMode(MOVE_Walking);
	}

	bHasCachedMovementMode = false;
}

void UVisualRewindComponent::ApplySnapshot(const FVisualRewindStateSnapshot& Snapshot) const
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

	ApplyControlRotation(Snapshot);
}

void UVisualRewindComponent::ApplyControlRotation(const FVisualRewindStateSnapshot& Snapshot) const
{
	if (const APawn* PawnOwner = Cast<APawn>(GetOwner()))
	{
		if (AController* Controller = PawnOwner->GetController())
		{
			Controller->SetControlRotation(Snapshot.ControlRotation);
		}
	}
}

void UVisualRewindComponent::ApplyVisibilityState(bool bHideFromOtherPlayers)
{
	if (GetOwner() && GetOwner()->HasAuthority())
	{
		bHiddenFromOtherPlayers = bHideFromOtherPlayers;
	}

	ApplyVisibilityToOwner();
}

void UVisualRewindComponent::ApplyVisibilityToOwner() const
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

bool UVisualRewindComponent::IsLocallyControlledOwner() const
{
	const APawn* PawnOwner = Cast<APawn>(GetOwner());
	return PawnOwner && PawnOwner->IsLocallyControlled();
}

bool UVisualRewindComponent::CanRecordSnapshots() const
{
	return GetOwner() && !bIsLocalPlaybackActive && !RewindRepState.bIsRewinding;
}

float UVisualRewindComponent::GetCurrentTimeSeconds() const
{
	const UWorld* World = GetWorld();
	return World ? World->GetTimeSeconds() : 0.0f;
}
