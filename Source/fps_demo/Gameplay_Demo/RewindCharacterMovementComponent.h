#pragma once

#include "CoreMinimal.h"
#include "GameFramework/CharacterMovementComponent.h"

#include "RewindCharacterMovementComponent.generated.h"

UENUM(BlueprintType)
enum class ERewindCustomMovementMode : uint8
{
	None = 0 UMETA(Hidden),

	// 回溯时移动由 URewindComponent 的播放采样驱动，而不是由玩家输入驱动。
	Rewind = 1 UMETA(DisplayName = "Rewind")
};

/**
 * 回溯能力的 CharacterMovement 集成层。
 *
 * URewindComponent 负责历史记录和路径采样；这个移动组件负责网络预测：
 * 它把“开始回溯”的输入保存进 FSavedMove，在服务器重放同一帧输入，
 * 并通过 MOVE_Custom 推进回溯移动。
 */
UCLASS()
class FPS_DEMO_API URewindCharacterMovementComponent : public UCharacterMovementComponent
{
	GENERATED_BODY()

public:
	/**
	 * 把开始回溯请求排到下一次 movement tick。
	 *
	 * 在 autonomous proxy 上，这个请求会被 FSavedMove 捕获，并通过正常的
	 * CharacterMovement 预测链路发送到服务器。在服务器上，如果它由 Ability
	 * 调用，也会标记本次请求已经通过 GAS 授权。
	 */
	bool RequestPredictedRewind();

	// FSavedMove_RewindCharacter::SetMoveFor 在序列化 move 前读取这个值。
	bool WantsToStartRewind() const { return bWantsToStartRewind; }

	// 客户端重放/校正时由 FSavedMove_RewindCharacter::PrepMoveFor 恢复这个值。
	void SetWantsToStartRewindForReplay(bool bNewWantsToStartRewind) { bWantsToStartRewind = bNewWantsToStartRewind; }

	// 服务器从压缩 saved move 中恢复回溯请求 bit。
	virtual void UpdateFromCompressedFlags(uint8 Flags) override;

	// 提供自定义 saved move 分配器，让回溯输入可以被序列化。
	virtual class FNetworkPredictionData_Client* GetPredictionData_Client() const override;

	// 在基础移动模拟运行前消费排队的回溯请求。
	virtual void PerformMovement(float DeltaTime) override;

	// 把 MOVE_Custom 的回溯 tick 分发给 PhysRewind。
	virtual void PhysCustom(float DeltaTime, int32 Iterations) override;

private:
	// 请求合法时启动回溯播放，并切换到 MOVE_Custom。
	bool TryStartPredictedRewind();

	// 使用 URewindComponent 的采样推进一帧预测回溯移动。
	void PhysRewind(float DeltaTime, int32 Iterations);

	// 临时输入 bit，会被复制进 FSavedMove，随后被消费。
	bool bWantsToStartRewind = false;

	// 服务器保护门闩：不能只靠移动标记绕过 GAS 提交/消耗检查。
	bool bServerAuthorizedRewind = false;
};
