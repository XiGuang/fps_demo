#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "TimerManager.h"

#include "VisualRewindComponent.generated.h"

DECLARE_MULTICAST_DELEGATE(FOnVisualRewindFinished);
DECLARE_MULTICAST_DELEGATE(FOnVisualRewindRejected);

USTRUCT(BlueprintType)
struct FVisualRewindStateSnapshot
{
	GENERATED_BODY()

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Visual Rewind")
	FVector Location = FVector::ZeroVector;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Visual Rewind")
	FRotator ActorRotation = FRotator::ZeroRotator;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Visual Rewind")
	FRotator ControlRotation = FRotator::ZeroRotator;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Visual Rewind")
	float TimestampSeconds = 0.0f;

	// 只在当前本地播放路径中填充，表示从回溯起点开始的累计距离。
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Visual Rewind")
	float DistanceAlongRewindPath = 0.0f;
};

USTRUCT(BlueprintType)
struct FVisualRewindRepState
{
	GENERATED_BODY()

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Visual Rewind")
	bool bIsRewinding = false;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Visual Rewind")
	float ServerStartTimeSeconds = 0.0f;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Visual Rewind")
	float DurationSeconds = 0.0f;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Visual Rewind")
	FVector FinalLocation = FVector::ZeroVector;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Visual Rewind")
	FRotator FinalActorRotation = FRotator::ZeroRotator;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Visual Rewind")
	FRotator FinalControlRotation = FRotator::ZeroRotator;

	// 用于让 OnRep 区分连续两次回溯，即使最终位置相同也能触发状态刷新。
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Visual Rewind")
	uint8 RewindSequence = 0;
};

/**
 * 本地表现型回溯组件。
 *
 * 本地拥有者会完整播放回溯路径；服务器只计算权威终点，并在持续时间结束后
 * 瞬移到该终点。其他客户端只收到隐藏/显示状态，不播放中间路径。
 */
UCLASS(ClassGroup=(Custom), BlueprintType, Blueprintable, meta=(BlueprintSpawnableComponent))
class FPS_DEMO_API UVisualRewindComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UVisualRewindComponent();

	FOnVisualRewindFinished OnVisualRewindFinished;
	FOnVisualRewindRejected OnVisualRewindRejected;

	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	UFUNCTION(BlueprintCallable, Category = "Visual Rewind")
	bool StartRewind();

	bool CanStartRewind() const;
	void RejectPredictedRewindFromServer();

protected:
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Visual Rewind")
	float RewindLookbackSeconds = 3.0f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Visual Rewind")
	float RecordIntervalSeconds = 0.02f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Visual Rewind")
	float RewindPlaybackDurationSeconds = 1.5f;

	UFUNCTION()
	void OnRep_RewindRepState();

	UFUNCTION()
	void OnRep_HiddenFromOtherPlayers();

	UFUNCTION(Client, Reliable)
	void ClientRejectRewind();

private:
	void RecordSnapshot();
	bool SampleSnapshotAtTime(float TargetTimestampSeconds, FVisualRewindStateSnapshot& OutSnapshot) const;
	void BuildLocalPlaybackPath(const FVisualRewindStateSnapshot& StartingSnapshot, const FVisualRewindStateSnapshot& TargetSnapshot);
	bool SampleLocalSnapshotAtDistance(float TargetDistance, FVisualRewindStateSnapshot& OutSnapshot) const;

	bool StartLocalPlayback();
	bool StartServerSettlement();
	void TickLocalPlayback(float DeltaTime);
	void FinishLocalPlayback(bool bWasRejected);
	void FinishServerSettlement();

	void CacheAndDisableMovement();
	void RestoreCachedMovement();
	void ApplySnapshot(const FVisualRewindStateSnapshot& Snapshot) const;
	void ApplyControlRotation(const FVisualRewindStateSnapshot& Snapshot) const;
	void ApplyVisibilityState(bool bHideFromOtherPlayers);
	void ApplyVisibilityToOwner() const;

	bool IsLocallyControlledOwner() const;
	bool CanRecordSnapshots() const;
	float GetCurrentTimeSeconds() const;

	UPROPERTY(ReplicatedUsing = OnRep_RewindRepState)
	FVisualRewindRepState RewindRepState;

	UPROPERTY(ReplicatedUsing = OnRep_HiddenFromOtherPlayers)
	bool bHiddenFromOtherPlayers = false;

	UPROPERTY()
	TArray<FVisualRewindStateSnapshot> RewindHistory;

	UPROPERTY()
	TArray<FVisualRewindStateSnapshot> LocalPlaybackPath;

	FVisualRewindStateSnapshot ServerFinalSnapshot;
	bool bHasServerFinalSnapshot = false;

	float LastRecordTimestampSeconds = -1.0f;
	float LocalPlaybackElapsedSeconds = 0.0f;
	float LocalPlaybackTotalDistance = 0.0f;
	bool bIsLocalPlaybackActive = false;

	FTimerHandle ServerSettlementTimerHandle;
	uint8 NextRewindSequence = 0;

	TEnumAsByte<EMovementMode> CachedMovementMode = MOVE_Walking;
	uint8 CachedCustomMovementMode = 0;
	bool bHasCachedMovementMode = false;
};

