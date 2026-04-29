#pragma once

#include "CoreMinimal.h"
#include "UObject/Interface.h"
#include "Abilities/Tasks/AbilityTask.h"

#include "AbilityTask_WeaponFireLoop.generated.h"

class UGameplayAbility;

/**
 * 武器开火模式。
 *
 * 该枚举用于驱动 Task 的开火时序：
 * - SemiAuto：半自动，激活后仅尝试触发一发
 * - FullAuto：全自动，首发立即触发，后续按 FireInterval 循环
 * - Burst：点射，首发立即触发，后续按 BurstShotInterval 连发，达到 BurstCount 后结束
 */
UENUM(BlueprintType)
enum class EWeaponFireMode : uint8
{
	SemiAuto UMETA(DisplayName = "Semi Auto"),
	FullAuto UMETA(DisplayName = "Full Auto"),
	Burst UMETA(DisplayName = "Burst")
};

/**
 * 项目侧“本发是否允许开火”检查结果。
 *
 * Task 本身不管理真实弹药和开火许可状态，而是在每次准备发射前，
 * 通过外部接口查询当前这一发是否可以继续执行。
 */
UENUM(BlueprintType)
enum class EWeaponFireLoopCanFireResult : uint8
{
	Allowed UMETA(DisplayName = "Allowed"),
	OutOfAmmo UMETA(DisplayName = "Out Of Ammo"),
	Blocked UMETA(DisplayName = "Blocked")
};

/**
 * Task 的结束原因。
 *
 * 该枚举主要用于：
 * - 日志打印
 * - Blueprint 调试观察
 * - OnFinished / OnInterrupted 的原因透传
 */
UENUM(BlueprintType)
enum class EWeaponFireLoopEndReason : uint8
{
	None UMETA(DisplayName = "None"),
	Completed UMETA(DisplayName = "Completed"),
	StopRequested UMETA(DisplayName = "Stop Requested"),
	OutOfAmmo UMETA(DisplayName = "Out Of Ammo"),
	InvalidParameters UMETA(DisplayName = "Invalid Parameters"),
	MissingValidationInterface UMETA(DisplayName = "Missing Validation Interface"),
	CanFireBlocked UMETA(DisplayName = "Can Fire Blocked"),
	OwnerInvalid UMETA(DisplayName = "Owner Invalid"),
	WorldInvalid UMETA(DisplayName = "World Invalid"),
	AbilityEnded UMETA(DisplayName = "Ability Ended")
};

/**
 * 每次尝试开火前，传给项目侧校验接口的查询数据。
 *
 * 说明：
 * - ShotIndex：当前 Task 生命周期内的全局第几发，采用 0 基索引
 * - BurstIndex：Burst 模式下当前是一轮点射中的第几发，采用 0 基索引；
 *   非 Burst 模式固定为 -1
 *
 * 项目侧通常可以借助该结构执行如下逻辑：
 * - 查询当前是否还有子弹
 * - 查询当前武器是否允许开火（例如换弹中、切枪中、冷却中等）
 * - 为日志、调试或统计系统提供更完整的上下文
 */
USTRUCT(BlueprintType)
struct FPS_DEMO_API FWeaponFireLoopQuery
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "Weapon Fire")
	int32 ShotIndex = 0;

	UPROPERTY(BlueprintReadOnly, Category = "Weapon Fire")
	int32 BurstIndex = -1;

	UPROPERTY(BlueprintReadOnly, Category = "Weapon Fire")
	EWeaponFireMode FireMode = EWeaponFireMode::SemiAuto;

	UPROPERTY(BlueprintReadOnly, Category = "Weapon Fire")
	float Damage = 0.0f;

	UPROPERTY(BlueprintReadOnly, Category = "Weapon Fire")
	FVector AimOrigin = FVector::ZeroVector;

	UPROPERTY(BlueprintReadOnly, Category = "Weapon Fire")
	FVector BaseAimDirection = FVector::ForwardVector;

	UPROPERTY(BlueprintReadOnly, Category = "Weapon Fire")
	float TraceRange = 0.0f;
};

/**
 * 每次真正触发一发时，对外广播的射击数据。
 *
 * 该结构是 Task 与项目侧武器逻辑/Effect 逻辑之间的标准事件载体。
 * Task 只负责生成并广播这些数据，不直接：
 * - 扣弹
 * - 做命中检测
 * - 应用伤害
 * - 播放具体的表现层逻辑
 *
 * 项目侧应在 OnFireShot 中消费该结构，并自行完成：
 * - 弹药消耗 GameplayEffect
 * - 射线检测 / 命中确认
 * - 伤害 GameplayEffectSpec 构建与应用
 * - GameplayCue、特效、动画、音效等表现逻辑
 */
USTRUCT(BlueprintType)
struct FPS_DEMO_API FWeaponFireShotData
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "Weapon Fire")
	int32 ShotIndex = 0;

	UPROPERTY(BlueprintReadOnly, Category = "Weapon Fire")
	int32 BurstIndex = -1;

	UPROPERTY(BlueprintReadOnly, Category = "Weapon Fire")
	FVector AimOrigin = FVector::ZeroVector;

	UPROPERTY(BlueprintReadOnly, Category = "Weapon Fire")
	FVector BaseAimDirection = FVector::ForwardVector;

	UPROPERTY(BlueprintReadOnly, Category = "Weapon Fire")
	FVector FinalShotDirection = FVector::ForwardVector;

	UPROPERTY(BlueprintReadOnly, Category = "Weapon Fire")
	float Damage = 0.0f;

	UPROPERTY(BlueprintReadOnly, Category = "Weapon Fire")
	EWeaponFireMode FireMode = EWeaponFireMode::SemiAuto;

	UPROPERTY(BlueprintReadOnly, Category = "Weapon Fire")
	float TraceRange = 0.0f;
};

/**
 * 通用项目侧校验接口。
 *
 * UAbilityTask_WeaponFireLoop 会在每一发真正触发前调用它，
 * 以决定“当前这一发是否允许发射”。
 *
 * 推荐实现位置：
 * - Owning GameplayAbility（优先）
 * - AvatarActor（兜底）
 *
 * Task 默认解析顺序：
 * 1. 先检查 OwningAbility 是否实现该接口
 * 2. 若未实现，再检查 AvatarActor 是否实现该接口
 *
 * 返回值语义：
 * - Allowed：允许本发开火，Task 将继续生成扩散并广播 OnFireShot
 * - OutOfAmmo：判定为无弹，Task 将广播 OnOutOfAmmo 并结束
 * - Blocked：其他原因不允许开火，Task 将广播 OnInterrupted 并结束
 */
UINTERFACE(BlueprintType)
class FPS_DEMO_API UWeaponFireLoopValidationInterface : public UInterface
{
	GENERATED_BODY()
};

class FPS_DEMO_API IWeaponFireLoopValidationInterface
{
	GENERATED_BODY()

public:
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "Weapon Fire")
	EWeaponFireLoopCanFireResult CanFireWeaponShot(const FWeaponFireLoopQuery& FireQuery);
};

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FWeaponFireLoopShotDelegate, FWeaponFireShotData, ShotData);
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FWeaponFireLoopSimpleDelegate);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FWeaponFireLoopEndDelegate, EWeaponFireLoopEndReason, EndReason);

/**
 * 用于驱动武器开火流程的自定义 AbilityTask。
 *
 * 核心职责：
 * - 校验启动参数和运行上下文是否合法
 * - 在每次发射前调用外部 Hook，判断当前这一发是否允许执行
 * - 基于基础瞄准方向计算锥形均匀随机扩散
 * - 每次真正发射时广播一份完整的射击数据
 * - 在收到停止信号、无弹、Ability 结束、Owner 失效、World 失效等场景下安全收尾
 *
 * 明确不负责的内容：
 * - 不直接扣除弹药
 * - 不直接做伤害结算
 * - 不直接调用非 GAS 的 ApplyDamage
 *
 * 项目侧推荐接法：
 * 1. 在 Ability 或 Avatar 上实现 UWeaponFireLoopValidationInterface
 * 2. 在 CanFireWeaponShot 中做弹药与开火许可校验
 * 3. 在 OnFireShot 中执行项目自己的扣弹 GE、命中检测、伤害 GE、表现逻辑
 *
 * 多人推荐执行侧：
 * - 更推荐服务器权威执行，由服务器决定真实开火、真实扣弹和真实伤害
 * - 若项目需要本地预测，也可以在 LocalPredicted Ability 中使用；
 *   但应保证客户端与服务器使用一致的输入和随机种子，以减少扩散偏差
 */
UCLASS()
class FPS_DEMO_API UAbilityTask_WeaponFireLoop : public UAbilityTask
{
	GENERATED_BODY()

public:
	UPROPERTY(BlueprintAssignable, Category = "Ability|Tasks|Weapon Fire", meta = (DisplayName = "On Fire Shot"))
	FWeaponFireLoopShotDelegate OnFireShot;

	UPROPERTY(BlueprintAssignable, Category = "Ability|Tasks|Weapon Fire", meta = (DisplayName = "On Out Of Ammo"))
	FWeaponFireLoopSimpleDelegate OnOutOfAmmo;

	UPROPERTY(BlueprintAssignable, Category = "Ability|Tasks|Weapon Fire", meta = (DisplayName = "On Finished"))
	FWeaponFireLoopEndDelegate OnFinished;

	UPROPERTY(BlueprintAssignable, Category = "Ability|Tasks|Weapon Fire", meta = (DisplayName = "On Interrupted"))
	FWeaponFireLoopEndDelegate OnInterrupted;

	/** 记录 Task 最近一次进入终态时的结束原因，便于 Blueprint/C++ 调试。 */
	UPROPERTY(BlueprintReadOnly, Category = "Ability|Tasks|Weapon Fire")
	EWeaponFireLoopEndReason LastEndReason = EWeaponFireLoopEndReason::None;

	/** 记录最近一次发射前校验得到的结果，便于排查为什么当前这一发没有打出去。 */
	UPROPERTY(BlueprintReadOnly, Category = "Ability|Tasks|Weapon Fire")
	EWeaponFireLoopCanFireResult LastCanFireResult = EWeaponFireLoopCanFireResult::Blocked;

	/**
	 * 创建一个自包含的武器开火 Task。
	 *
	 * 参数说明：
	 * @param OwningAbility 拥有该 Task 的 GameplayAbility。
	 * @param InFireMode 本次开火流程使用的模式：半自动、全自动或点射。
	 * @param InDamage 本次射击的基础伤害值。Task 不直接应用它，只在 OnFireShot 中原样抛出。
	 * @param InFireInterval 连续两发之间的基础时间间隔（秒）。
	 * 对 FullAuto 生效；对 SemiAuto 主要用于合法性校验。
	 * @param InBurstCount 点射模式下一轮要连续发射多少发。仅 Burst 模式使用。
	 * @param InBurstShotInterval 点射模式内部相邻两发之间的时间间隔（秒）。
	 * 第一发依然会立即触发，不会先等待一个间隔。
	 * @param InMaxSpreadAngleDegrees 扩散锥体的最大半角，单位为角度。
	 * @param InAimOrigin 射线起点。Task 不负责 trace，但会把该值透传给外部。
	 * @param InBaseAimDirection 扩散前的基础瞄准方向。要求是非零向量，内部会安全归一化。
	 * @param InTraceRange 射程参数。Task 不直接做命中检测，但会把该值透传给外部逻辑。
	 * @param bInUseDeterministicSpread 是否启用确定性随机扩散。
	 * 对多人预测或回放一致性有帮助，默认 false。
	 * @param InRandomStreamSeed 当启用确定性扩散时使用的随机种子，默认 0。
	 */
	UFUNCTION(BlueprintCallable, Category = "Ability|Tasks|Weapon Fire", meta = (HidePin = "OwningAbility", DefaultToSelf = "OwningAbility", BlueprintInternalUseOnly = "TRUE", DisplayName = "Create Weapon Fire Loop"))
	static UAbilityTask_WeaponFireLoop* CreateWeaponFireLoop(
		UGameplayAbility* OwningAbility,
		EWeaponFireMode InFireMode,
		float InDamage,
		float InFireInterval,
		int32 InBurstCount,
		float InBurstShotInterval,
		float InMaxSpreadAngleDegrees,
		FVector InAimOrigin,
		FVector InBaseAimDirection,
		float InTraceRange = 10000.0f,
		bool bInUseDeterministicSpread = false,
		int32 InRandomStreamSeed = 0);

	virtual void Activate() override;
	virtual void OnDestroy(bool bInOwnerFinished) override;

	/**
	 * 外部停止信号。
	 *
	 * 设计目标：
	 * - Blueprint 可直接调用
	 * - 可重入，多次调用安全无害
	 * - 对 FullAuto 和 Burst 生效
	 * - 调用后会清理挂起的 Timer，阻止后续新的一发继续触发
	 * - 最终只会进入一次结束流程，不会重复广播结束事件
	 */
	UFUNCTION(BlueprintCallable, Category = "Ability|Tasks|Weapon Fire")
	void StopFiring();

protected:
	/**
	 * 每发开火前调用的项目侧 Hook。
	 *
	 * 默认实现会优先在 OwningAbility 上调用接口；
	 * 若 Ability 未实现，再退回到 AvatarActor。
	 *
	 * 如项目有更复杂的接入方式，也可以在 C++ 中覆写该函数。
	 *
	 * TODO(Project)：
	 * 请在项目侧实现真实的弹药检查与开火许可检查，并返回：
	 * - Allowed：允许发射，Task 将继续本发逻辑
	 * - OutOfAmmo：无弹，Task 将广播 OnOutOfAmmo 并结束
	 * - Blocked：其他原因阻止开火，Task 将广播 OnInterrupted 并结束
	 */
	virtual EWeaponFireLoopCanFireResult CanFireThisShot(const FWeaponFireLoopQuery& FireQuery, EWeaponFireLoopEndReason& OutBlockedReason) const;

	/** 解析由哪个对象来响应 CanFireThisShot()；默认先找 Ability，再找 Avatar。 */
	virtual UObject* ResolveCanFireValidationTarget() const;

	/**
	 * 计算一个围绕基础方向的“锥体内均匀随机方向”。
	 *
	 * 数学原理：
	 * - 在方位角 phi 上做 [0, 2pi) 的均匀采样
	 * - 在 cos(theta) 上做 [cos(MaxAngle), 1] 的均匀采样
	 *
	 * 这样得到的是“按立体角均匀分布”的圆锥采样结果，
	 * 能避免以下常见问题：
	 * - 边缘聚集
	 * - 十字型分布
	 * - 简单 Pitch/Yaw 扰动带来的非均匀性
	 *
	 * 返回值始终会做安全归一化。
	 */
	virtual FVector ComputeSpreadDirection(const FVector& InBaseDirection, float InMaxSpreadAngleDegrees);

private:
	bool ValidateTaskParameters(FString& OutFailureMessage, EWeaponFireLoopEndReason& OutFailureReason) const;
	bool IsExecutionContextValid(EWeaponFireLoopEndReason& OutFailureReason) const;
	void StartFireLoop();
	void HandleScheduledShot();
	void TryFireNextShot();
	FWeaponFireLoopQuery BuildCurrentFireQuery() const;
	FWeaponFireShotData BuildShotData(const FVector& FinalShotDirection) const;
	void ScheduleNextShot(float DelaySeconds);
	void ClearScheduledShot();
	void FinishTaskNormally(EWeaponFireLoopEndReason EndReason);
	void FinishTaskOutOfAmmo();
	void FinishTaskAsInterrupted(EWeaponFireLoopEndReason EndReason, const TCHAR* LogContext);

private:
	EWeaponFireMode FireMode = EWeaponFireMode::SemiAuto;
	float Damage = 0.0f;
	float FireInterval = 0.0f;
	int32 BurstCount = 0;
	float BurstShotInterval = 0.0f;
	float MaxSpreadAngleDegrees = 0.0f;
	FVector AimOrigin = FVector::ZeroVector;
	FVector BaseAimDirection = FVector::ForwardVector;
	float TraceRange = 0.0f;
	int32 CurrentShotIndex = 0;
	int32 CurrentBurstShotIndex = -1;
	bool bStopRequested = false;
	bool bHasEnded = false;
	bool bUseDeterministicSpread = false;
	int32 RandomStreamSeed = 0;
	FTimerHandle TimerHandle;
	FRandomStream SpreadRandomStream;
};
