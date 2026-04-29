#include "AbilityTask_WeaponFireLoop.h"

#include "Abilities/GameplayAbility.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "TimerManager.h"

DEFINE_LOG_CATEGORY_STATIC(LogWeaponFireLoopAbilityTask, Log, All);

UAbilityTask_WeaponFireLoop* UAbilityTask_WeaponFireLoop::CreateWeaponFireLoop(
	UGameplayAbility* OwningAbility,
	EWeaponFireMode InFireMode,
	float InDamage,
	float InFireInterval,
	int32 InBurstCount,
	float InBurstShotInterval,
	float InMaxSpreadAngleDegrees,
	FVector InAimOrigin,
	FVector InBaseAimDirection,
	float InTraceRange,
	bool bInUseDeterministicSpread,
	int32 InRandomStreamSeed)
{
	// AbilityTask 必须通过 NewAbilityTask 创建，确保其生命周期被 AbilitySystem 正确管理。
	UAbilityTask_WeaponFireLoop* Task = NewAbilityTask<UAbilityTask_WeaponFireLoop>(OwningAbility);

	// 缓存所有初始化参数；Activate 时只消费这些已冻结的输入，不再依赖外部瞬时状态。
	Task->FireMode = InFireMode;
	Task->Damage = InDamage;
	Task->FireInterval = InFireInterval;
	Task->BurstCount = InBurstCount;
	Task->BurstShotInterval = InBurstShotInterval;
	Task->MaxSpreadAngleDegrees = InMaxSpreadAngleDegrees;
	Task->AimOrigin = InAimOrigin;
	Task->BaseAimDirection = InBaseAimDirection.GetSafeNormal();
	Task->TraceRange = InTraceRange;
	Task->CurrentShotIndex = 0;
	Task->CurrentBurstShotIndex = (InFireMode == EWeaponFireMode::Burst) ? 0 : -1;
	Task->bStopRequested = false;
	Task->bHasEnded = false;
	Task->bUseDeterministicSpread = bInUseDeterministicSpread;
	Task->RandomStreamSeed = InRandomStreamSeed;
	Task->SpreadRandomStream.Initialize(InRandomStreamSeed);
	Task->LastEndReason = EWeaponFireLoopEndReason::None;
	Task->LastCanFireResult = EWeaponFireLoopCanFireResult::Blocked;
	return Task;
}

void UAbilityTask_WeaponFireLoop::Activate()
{
	Super::Activate();

	if (bHasEnded)
	{
		return;
	}

	// 先做参数层面的快速失败，避免非法配置进入真正的时序逻辑。
	FString FailureMessage;
	EWeaponFireLoopEndReason FailureReason = EWeaponFireLoopEndReason::InvalidParameters;
	if (!ValidateTaskParameters(FailureMessage, FailureReason))
	{
		UE_LOG(LogWeaponFireLoopAbilityTask, Error, TEXT("激活失败：%s"), *FailureMessage);
		FinishTaskAsInterrupted(FailureReason, TEXT("Activate"));
		return;
	}

	// 若启用确定性随机，则在激活时重置随机流，保证同样输入可复现同样扩散序列。
	if (bUseDeterministicSpread)
	{
		SpreadRandomStream.Initialize(RandomStreamSeed);
	}

	// 真正进入开火状态机。
	StartFireLoop();
}

void UAbilityTask_WeaponFireLoop::OnDestroy(bool bInOwnerFinished)
{
	// 无论什么销毁路径，先清理定时器，避免悬挂回调打到已销毁对象。
	ClearScheduledShot();

	if (!bHasEnded)
	{
		// 如果 Task 在未显式正常结束的情况下被销毁，则按中断路径处理。
		const EWeaponFireLoopEndReason EndReason = bInOwnerFinished
			? EWeaponFireLoopEndReason::AbilityEnded
			: EWeaponFireLoopEndReason::OwnerInvalid;

		LastEndReason = EndReason;
		bHasEnded = true;

		UE_LOG(
			LogWeaponFireLoopAbilityTask,
			Warning,
			TEXT("武器开火 Task 在正常结束前被销毁。原因=%s"),
			*StaticEnum<EWeaponFireLoopEndReason>()->GetValueAsString(EndReason));

		if (ShouldBroadcastAbilityTaskDelegates())
		{
			OnInterrupted.Broadcast(EndReason);
		}
	}

	Super::OnDestroy(bInOwnerFinished);
}

void UAbilityTask_WeaponFireLoop::StopFiring()
{
	if (bHasEnded)
	{
		return;
	}

	UE_LOG(LogWeaponFireLoopAbilityTask, Log, TEXT("收到 StopFiring 请求。"));

	// 先打停止标记，再清理 Timer，最后走统一正常结束路径。
	bStopRequested = true;
	ClearScheduledShot();
	FinishTaskNormally(EWeaponFireLoopEndReason::StopRequested);
}

EWeaponFireLoopCanFireResult UAbilityTask_WeaponFireLoop::CanFireThisShot(
	const FWeaponFireLoopQuery& FireQuery,
	EWeaponFireLoopEndReason& OutBlockedReason) const
{
	if (UObject* ValidationTarget = ResolveCanFireValidationTarget())
	{
		OutBlockedReason = EWeaponFireLoopEndReason::CanFireBlocked;
		return IWeaponFireLoopValidationInterface::Execute_CanFireWeaponShot(ValidationTarget, FireQuery);
	}

	// TODO(Project)：
	// 请在 Ability 或 Avatar 上实现 UWeaponFireLoopValidationInterface，
	// 让 Task 能在每一发触发前查询真实弹药状态和其他开火门禁条件。
	UE_LOG(
		LogWeaponFireLoopAbilityTask,
		Warning,
		TEXT("CanFireThisShot 未找到校验目标。请在 OwningAbility 或 AvatarActor 上实现 UWeaponFireLoopValidationInterface。"));

	OutBlockedReason = EWeaponFireLoopEndReason::MissingValidationInterface;
	return EWeaponFireLoopCanFireResult::Blocked;
}

UObject* UAbilityTask_WeaponFireLoop::ResolveCanFireValidationTarget() const
{
	// 优先使用 Ability 作为开火门禁决策源，便于把逻辑收敛在 GAS Ability 自身。
	if (Ability && Ability->GetClass()->ImplementsInterface(UWeaponFireLoopValidationInterface::StaticClass()))
	{
		return Ability;
	}

	// 若 Ability 未实现接口，则退回到 AvatarActor。
	const FGameplayAbilityActorInfo* ActorInfo = Ability ? Ability->GetCurrentActorInfo() : nullptr;
	AActor* AvatarActor = ActorInfo ? ActorInfo->AvatarActor.Get() : nullptr;
	if (IsValid(AvatarActor) && AvatarActor->GetClass()->ImplementsInterface(UWeaponFireLoopValidationInterface::StaticClass()))
	{
		return AvatarActor;
	}

	return nullptr;
}

FVector UAbilityTask_WeaponFireLoop::ComputeSpreadDirection(const FVector& InBaseDirection, float InMaxSpreadAngleDegrees)
{
	// 基础方向无效时返回一个安全默认值，避免生成 NaN 向量继续污染后续逻辑。
	const FVector SafeBaseDirection = InBaseDirection.GetSafeNormal();
	if (SafeBaseDirection.IsNearlyZero())
	{
		return FVector::ForwardVector;
	}

	// 没有扩散时直接返回基础方向。
	if (InMaxSpreadAngleDegrees <= 0.0f)
	{
		return SafeBaseDirection;
	}

	// 这里按“立体角均匀”采样圆锥：
	// 1. phi 在 [0, 2pi) 均匀分布
	// 2. cos(theta) 在 [cos(maxAngle), 1] 均匀分布
	// 这样得到的点在锥体表面积意义下是均匀的，而不是简单的 Pitch/Yaw 扰动。
	const float ClampedSpreadAngleDegrees = FMath::Clamp(InMaxSpreadAngleDegrees, 0.0f, 179.9f);
	const float MaxSpreadRadians = FMath::DegreesToRadians(ClampedSpreadAngleDegrees);
	const float CosMaxAngle = FMath::Cos(MaxSpreadRadians);
	const float SampleA = bUseDeterministicSpread ? SpreadRandomStream.GetFraction() : FMath::FRand();
	const float SampleB = bUseDeterministicSpread ? SpreadRandomStream.GetFraction() : FMath::FRand();
	const float CosTheta = FMath::Lerp(1.0f, CosMaxAngle, SampleA);
	const float SinTheta = FMath::Sqrt(FMath::Max(0.0f, 1.0f - FMath::Square(CosTheta)));
	const float Phi = 2.0f * UE_PI * SampleB;

	FVector TangentX;
	FVector TangentY;
	// 自动构造与基础方向正交的稳定局部基，避免接近世界 Up 时出现退化。
	SafeBaseDirection.FindBestAxisVectors(TangentX, TangentY);

	const FVector RadialDirection = (TangentX * FMath::Cos(Phi)) + (TangentY * FMath::Sin(Phi));
	return (SafeBaseDirection * CosTheta + RadialDirection * SinTheta).GetSafeNormal();
}

bool UAbilityTask_WeaponFireLoop::ValidateTaskParameters(
	FString& OutFailureMessage,
	EWeaponFireLoopEndReason& OutFailureReason) const
{
	if (!Ability)
	{
		OutFailureReason = EWeaponFireLoopEndReason::AbilityEnded;
		OutFailureMessage = TEXT("OwningAbility 为空。");
		return false;
	}

	if (!FMath::IsFinite(Damage))
	{
		OutFailureReason = EWeaponFireLoopEndReason::InvalidParameters;
		OutFailureMessage = TEXT("Damage 必须是有限数值。");
		return false;
	}

	if (!FMath::IsFinite(AimOrigin.X) || !FMath::IsFinite(AimOrigin.Y) || !FMath::IsFinite(AimOrigin.Z))
	{
		OutFailureReason = EWeaponFireLoopEndReason::InvalidParameters;
		OutFailureMessage = TEXT("AimOrigin 包含非法的非有限数值。");
		return false;
	}

	if (!FMath::IsFinite(BaseAimDirection.X) || !FMath::IsFinite(BaseAimDirection.Y) || !FMath::IsFinite(BaseAimDirection.Z) || BaseAimDirection.IsNearlyZero())
	{
		OutFailureReason = EWeaponFireLoopEndReason::InvalidParameters;
		OutFailureMessage = TEXT("BaseAimDirection 必须是合法的非零向量。");
		return false;
	}

	if (!FMath::IsFinite(MaxSpreadAngleDegrees))
	{
		OutFailureReason = EWeaponFireLoopEndReason::InvalidParameters;
		OutFailureMessage = TEXT("MaxSpreadAngleDegrees 必须是有限数值。");
		return false;
	}

	if (!FMath::IsFinite(TraceRange) || TraceRange <= 0.0f)
	{
		OutFailureReason = EWeaponFireLoopEndReason::InvalidParameters;
		OutFailureMessage = TEXT("TraceRange 必须大于 0。");
		return false;
	}

	if ((FireMode == EWeaponFireMode::SemiAuto || FireMode == EWeaponFireMode::FullAuto) && FireInterval <= KINDA_SMALL_NUMBER)
	{
		OutFailureReason = EWeaponFireLoopEndReason::InvalidParameters;
		OutFailureMessage = TEXT("SemiAuto 和 FullAuto 模式下，FireInterval 必须大于 0。");
		return false;
	}

	if (FireMode == EWeaponFireMode::Burst)
	{
		if (BurstCount <= 0)
		{
			OutFailureReason = EWeaponFireLoopEndReason::InvalidParameters;
			OutFailureMessage = TEXT("BurstCount 必须大于 0。");
			return false;
		}

		if (BurstCount > 1 && BurstShotInterval <= KINDA_SMALL_NUMBER)
		{
			OutFailureReason = EWeaponFireLoopEndReason::InvalidParameters;
			OutFailureMessage = TEXT("当 BurstCount 大于 1 时，BurstShotInterval 必须大于 0。");
			return false;
		}
	}

	return true;
}

bool UAbilityTask_WeaponFireLoop::IsExecutionContextValid(EWeaponFireLoopEndReason& OutFailureReason) const
{
	if (!Ability)
	{
		OutFailureReason = EWeaponFireLoopEndReason::AbilityEnded;
		return false;
	}

	UWorld* World = GetWorld();
	if (!IsValid(World))
	{
		OutFailureReason = EWeaponFireLoopEndReason::WorldInvalid;
		return false;
	}

	const FGameplayAbilityActorInfo* ActorInfo = Ability->GetCurrentActorInfo();
	if (!ActorInfo)
	{
		OutFailureReason = EWeaponFireLoopEndReason::OwnerInvalid;
		return false;
	}

	AActor* AvatarActor = ActorInfo->AvatarActor.Get();
	if (!IsValid(AvatarActor))
	{
		OutFailureReason = EWeaponFireLoopEndReason::OwnerInvalid;
		return false;
	}

	return true;
}

void UAbilityTask_WeaponFireLoop::StartFireLoop()
{
	if (bStopRequested)
	{
		FinishTaskNormally(EWeaponFireLoopEndReason::StopRequested);
		return;
	}

	// 启动前再次确认 Ability / World / Avatar 都仍然有效。
	EWeaponFireLoopEndReason FailureReason = EWeaponFireLoopEndReason::None;
	if (!IsExecutionContextValid(FailureReason))
	{
		UE_LOG(LogWeaponFireLoopAbilityTask, Error, TEXT("StartFireLoop 失败：执行上下文无效。"));
		FinishTaskAsInterrupted(FailureReason, TEXT("StartFireLoop"));
		return;
	}

	// 首发立即尝试，不先等待任何间隔。
	TryFireNextShot();
}

void UAbilityTask_WeaponFireLoop::HandleScheduledShot()
{
	if (bHasEnded)
	{
		return;
	}

	TryFireNextShot();
}

void UAbilityTask_WeaponFireLoop::TryFireNextShot()
{
	if (bHasEnded)
	{
		return;
	}

	// 当前这次回调开始时，先移除旧的计划，防止重复计时。
	ClearScheduledShot();

	if (bStopRequested)
	{
		FinishTaskNormally(EWeaponFireLoopEndReason::StopRequested);
		return;
	}

	EWeaponFireLoopEndReason ContextFailureReason = EWeaponFireLoopEndReason::None;
	if (!IsExecutionContextValid(ContextFailureReason))
	{
		UE_LOG(LogWeaponFireLoopAbilityTask, Warning, TEXT("TryFireNextShot 中止：执行上下文已失效。"));
		FinishTaskAsInterrupted(ContextFailureReason, TEXT("TryFireNextShot"));
		return;
	}

	// 每一发发射前都重新问项目侧：这一发现在是否允许触发。
	const FWeaponFireLoopQuery FireQuery = BuildCurrentFireQuery();
	EWeaponFireLoopEndReason CanFireBlockedReason = EWeaponFireLoopEndReason::CanFireBlocked;
	LastCanFireResult = CanFireThisShot(FireQuery, CanFireBlockedReason);

	if (LastCanFireResult == EWeaponFireLoopCanFireResult::OutOfAmmo)
	{
		UE_LOG(LogWeaponFireLoopAbilityTask, Log, TEXT("武器开火 Task 因 CanFireThisShot 返回 OutOfAmmo 而结束。"));
		FinishTaskOutOfAmmo();
		return;
	}

	if (LastCanFireResult != EWeaponFireLoopCanFireResult::Allowed)
	{
		UE_LOG(
			LogWeaponFireLoopAbilityTask,
			Warning,
			TEXT("武器开火 Task 被中断：CanFireThisShot 拒绝了第 %d 发。"),
			CurrentShotIndex);
		FinishTaskAsInterrupted(CanFireBlockedReason, TEXT("CanFireThisShot"));
		return;
	}

	// 只有真正允许开火时，才生成最终扩散方向并广播本发数据。
	const FVector FinalShotDirection = ComputeSpreadDirection(BaseAimDirection, MaxSpreadAngleDegrees);
	const FWeaponFireShotData ShotData = BuildShotData(FinalShotDirection);

	if (ShouldBroadcastAbilityTaskDelegates())
	{
		OnFireShot.Broadcast(ShotData);
	}

	if (bHasEnded)
	{
		return;
	}

	// 广播完成后再推进内部索引，保证事件拿到的是“本发”的序号。
	++CurrentShotIndex;
	if (FireMode == EWeaponFireMode::Burst)
	{
		++CurrentBurstShotIndex;
	}

	if (bStopRequested)
	{
		FinishTaskNormally(EWeaponFireLoopEndReason::StopRequested);
		return;
	}

	switch (FireMode)
	{
	case EWeaponFireMode::SemiAuto:
		// 半自动只打一发，发完即正常结束。
		FinishTaskNormally(EWeaponFireLoopEndReason::Completed);
		return;

	case EWeaponFireMode::FullAuto:
		// 全自动在成功触发本发后，按 FireInterval 安排下一发。
		ScheduleNextShot(FireInterval);
		return;

	case EWeaponFireMode::Burst:
		// 点射达到指定发数后正常结束，否则继续安排下一发点射子弹。
		if (CurrentBurstShotIndex >= BurstCount)
		{
			FinishTaskNormally(EWeaponFireLoopEndReason::Completed);
			return;
		}

		ScheduleNextShot(BurstShotInterval);
		return;

	default:
		FinishTaskAsInterrupted(EWeaponFireLoopEndReason::InvalidParameters, TEXT("TryFireNextShot::UnknownFireMode"));
		return;
	}
}

FWeaponFireLoopQuery UAbilityTask_WeaponFireLoop::BuildCurrentFireQuery() const
{
	FWeaponFireLoopQuery Query;
	Query.ShotIndex = CurrentShotIndex;
	Query.BurstIndex = (FireMode == EWeaponFireMode::Burst) ? CurrentBurstShotIndex : -1;
	Query.FireMode = FireMode;
	Query.Damage = Damage;
	Query.AimOrigin = AimOrigin;
	Query.BaseAimDirection = BaseAimDirection;
	Query.TraceRange = TraceRange;
	return Query;
}

FWeaponFireShotData UAbilityTask_WeaponFireLoop::BuildShotData(const FVector& FinalShotDirection) const
{
	FWeaponFireShotData ShotData;
	ShotData.ShotIndex = CurrentShotIndex;
	ShotData.BurstIndex = (FireMode == EWeaponFireMode::Burst) ? CurrentBurstShotIndex : -1;
	ShotData.AimOrigin = AimOrigin;
	ShotData.BaseAimDirection = BaseAimDirection;
	ShotData.FinalShotDirection = FinalShotDirection.GetSafeNormal();
	ShotData.Damage = Damage;
	ShotData.FireMode = FireMode;
	ShotData.TraceRange = TraceRange;
	return ShotData;
}

void UAbilityTask_WeaponFireLoop::ScheduleNextShot(float DelaySeconds)
{
	if (bHasEnded || bStopRequested)
	{
		return;
	}

	UWorld* World = GetWorld();
	if (!IsValid(World))
	{
		UE_LOG(LogWeaponFireLoopAbilityTask, Error, TEXT("ScheduleNextShot 失败：World 无效。"));
		FinishTaskAsInterrupted(EWeaponFireLoopEndReason::WorldInvalid, TEXT("ScheduleNextShot"));
		return;
	}

	// 使用一次性 Timer 调度下一发，避免 Tick 轮询。
	World->GetTimerManager().SetTimer(
		TimerHandle,
		this,
		&UAbilityTask_WeaponFireLoop::HandleScheduledShot,
		DelaySeconds,
		false);
}

void UAbilityTask_WeaponFireLoop::ClearScheduledShot()
{
	UWorld* World = GetWorld();
	if (IsValid(World) && TimerHandle.IsValid())
	{
		World->GetTimerManager().ClearTimer(TimerHandle);
	}

	// 清理后显式失效句柄，避免旧句柄被误判为仍可用。
	TimerHandle.Invalidate();
}

void UAbilityTask_WeaponFireLoop::FinishTaskNormally(EWeaponFireLoopEndReason EndReason)
{
	if (bHasEnded)
	{
		return;
	}

	bHasEnded = true;
	LastEndReason = EndReason;
	ClearScheduledShot();

	UE_LOG(
		LogWeaponFireLoopAbilityTask,
		Log,
		TEXT("武器开火 Task 正常结束。原因=%s"),
		*StaticEnum<EWeaponFireLoopEndReason>()->GetValueAsString(EndReason));

	if (ShouldBroadcastAbilityTaskDelegates())
	{
		OnFinished.Broadcast(EndReason);
	}

	EndTask();
}

void UAbilityTask_WeaponFireLoop::FinishTaskOutOfAmmo()
{
	if (bHasEnded)
	{
		return;
	}

	bHasEnded = true;
	LastEndReason = EWeaponFireLoopEndReason::OutOfAmmo;
	ClearScheduledShot();

	UE_LOG(LogWeaponFireLoopAbilityTask, Warning, TEXT("武器开火 Task 因无弹而结束。"));

	if (ShouldBroadcastAbilityTaskDelegates())
	{
		OnOutOfAmmo.Broadcast();
	}

	EndTask();
}

void UAbilityTask_WeaponFireLoop::FinishTaskAsInterrupted(EWeaponFireLoopEndReason EndReason, const TCHAR* LogContext)
{
	if (bHasEnded)
	{
		return;
	}

	bHasEnded = true;
	LastEndReason = EndReason;
	ClearScheduledShot();

	UE_LOG(
		LogWeaponFireLoopAbilityTask,
		Warning,
		TEXT("%s 中断了武器开火 Task。原因=%s"),
		LogContext ? LogContext : TEXT("Unknown"),
		*StaticEnum<EWeaponFireLoopEndReason>()->GetValueAsString(EndReason));

	if (ShouldBroadcastAbilityTaskDelegates())
	{
		OnInterrupted.Broadcast(EndReason);
	}

	EndTask();
}
