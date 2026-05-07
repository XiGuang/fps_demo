#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "TimerManager.h"

#include "RewindComponent.generated.h"

DECLARE_MULTICAST_DELEGATE(FOnRewindFinished);

USTRUCT(BlueprintType)
struct FRewindStateSnapshot
{
	GENERATED_BODY()

	// 在 TimestampSeconds 时刻捕获的 Actor 变换和控制器旋转。
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Rewind")
	FVector Location = FVector::ZeroVector;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Rewind")
	FRotator ActorRotation = FRotator::ZeroRotator;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Rewind")
	FRotator ControlRotation = FRotator::ZeroRotator;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Rewind")
	float TimestampSeconds = 0.0f;

	// 只在当前播放路径中填充。它表示从回溯起点开始的累计距离，
	// 不是长期保存的历史数据。
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Rewind")
	float DistanceAlongRewindPath = 0.0f;
};

/**
 * 记录回溯历史，并对回溯播放路径进行采样。
 *
 * 这个组件刻意不负责网络移动预测。Owner 拥有 URewindCharacterMovementComponent
 * 时，StartRewind 会排队一个预测移动请求，并由移动组件驱动播放。
 * 如果没有这个自定义移动组件，则退回到组件自身 Tick 并直接传送 Actor。
 */
UCLASS(ClassGroup=(Custom), BlueprintType, Blueprintable, meta=(BlueprintSpawnableComponent))
class FPS_DEMO_API URewindComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	URewindComponent();

	// 回溯播放真正结束时广播。Ability 通过它把自身生命周期延长到完整回溯过程。
	FOnRewindFinished OnRewindFinished;

	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	// Ability 入口。可用时使用 CharacterMovement 预测。
	UFUNCTION(BlueprintCallable, Category = "Rewind")
	bool StartRewind();

	// 轻量校验，用于 GAS CommitAbility 前以及 saved move 请求前。
	bool CanStartRewind() const;
	bool IsRewinding() const { return bIsRewinding; }

	// 构建播放状态并进入回溯。移动组件驱动的播放由
	// URewindCharacterMovementComponent 推进；兜底播放由 Tick 推进。
	bool StartRewindPlayback(bool bDrivenByMovementComponent);

	// 推进播放时间，并采样当前帧的目标回溯状态。
	// bDrivenByMovementComponent 为 true 时，调用方负责应用移动。
	bool AdvanceRewindPlayback(float DeltaTime, FRewindStateSnapshot& OutSnapshot, bool& bOutFinished);

	// 只应用视角/控制旋转，不移动 Actor；供移动组件驱动的播放使用。
	void ApplyControlRotation(const FRewindStateSnapshot& Snapshot) const;

	// 结束回溯、恢复移动模式、清理播放路径，并记录最终状态。
	UFUNCTION()
	void FinishRewindPlayback();

protected:
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Rewind")
	float RewindLookbackSeconds = 3.0f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Rewind")
	float RecordIntervalSeconds = 0.02f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Rewind")
	float RewindPlaybackDurationSeconds = 0.75f;

	UFUNCTION()
	void OnRep_HiddenFromOtherPlayers();

	// 把当前 Owner 变换捕获到 RewindHistory。
	void RecordSnapshot();

	// 基于时间的历史采样仍用于寻找精确播放端点。
	bool SampleSnapshotAtTime(float TargetTimestampSeconds, FRewindStateSnapshot& OutSnapshot) const;

	// 创建“当前 -> 过去”的播放路径，并缓存累计移动距离。
	void BuildRewindPlaybackPath(const FRewindStateSnapshot& StartingSnapshot, const FRewindStateSnapshot& TargetSnapshot);

	// 沿当前播放路径按累计距离采样，而不是按时间戳采样。
	bool SampleSnapshotAtDistance(float TargetDistance, FRewindStateSnapshot& OutSnapshot) const;

	void ApplyVisibilityState(bool bHideFromOtherPlayers);
	void ApplyVisibilityToOwner() const;
	float GetCurrentTimeSeconds() const;

	// 非预测 Owner 使用的兜底播放路径。
	void UpdateRewindPlayback(float DeltaTime);

	// 直接传送 Actor 并应用控制旋转；移动组件驱动的 tick 不使用它。
	void ApplySnapshot(const FRewindStateSnapshot& Snapshot) const;

	bool CanRecordSnapshots() const;

private:
	UPROPERTY(ReplicatedUsing = OnRep_HiddenFromOtherPlayers)
	bool bHiddenFromOtherPlayers = false;

	UPROPERTY()
	TArray<FRewindStateSnapshot> RewindHistory;

	// 上一次记录历史快照的时间，用于限制捕获频率。
	float LastRecordTimestampSeconds = -1.0f;

	// 当前播放进度，单位为秒。
	float RewindPlaybackElapsedSeconds = 0.0f;

	// RewindPlaybackPath 的总移动距离，用于基于距离的播放。
	float RewindPlaybackTotalDistance = 0.0f;

	// 当前回溯路径，顺序为“现在 -> 过去目标点”。
	UPROPERTY()
	TArray<FRewindStateSnapshot> RewindPlaybackPath;

	bool bIsRewinding = false;

	// 当前播放的位置更新由 CharacterMovement 负责时为 true。
	bool bPlaybackDrivenByMovementComponent = false;

	// 兜底/移动组件驱动回溯开始前保存的移动模式。
	TEnumAsByte<EMovementMode> CachedMovementMode = MOVE_Walking;
	uint8 CachedCustomMovementMode = 0;
	bool bHasCachedMovementMode = false;
};
