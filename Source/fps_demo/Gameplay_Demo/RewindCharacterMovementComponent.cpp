#include "RewindCharacterMovementComponent.h"

#include "GameFramework/Character.h"
#include "Gameplay_Demo/Ability/RewindComponent.h"

namespace
{
// 开始回溯是单帧输入请求，因此可以放进一个自定义 saved move bit。
constexpr uint8 RewindSavedMoveFlag = FSavedMove_Character::FLAG_Custom_0;

/**
 * 回溯预测使用的已保存移动数据。
 *
 * CharacterMovement 会把这些 move 从 autonomous proxy 序列化到服务器，
 * 并在校正时在本地重放。回溯请求必须保存在这里，这样服务器才能看到
 * 客户端预测过的同一帧“现在开始回溯”输入。
 */
class FSavedMove_RewindCharacter : public FSavedMove_Character
{
public:
	using Super = FSavedMove_Character;

	virtual void Clear() override
	{
		Super::Clear();
		bSavedWantsToStartRewind = false;
	}

	virtual uint8 GetCompressedFlags() const override
	{
		uint8 Result = Super::GetCompressedFlags();

		// 把单帧回溯请求编码进 move 的压缩 flag 字节。
		if (bSavedWantsToStartRewind)
		{
			Result |= RewindSavedMoveFlag;
		}

		return Result;
	}

	virtual bool CanCombineWith(const FSavedMovePtr& NewMove, ACharacter* InCharacter, float MaxDelta) const override
	{
		const FSavedMove_RewindCharacter* NewRewindMove = static_cast<const FSavedMove_RewindCharacter*>(NewMove.Get());

		// 开始回溯的帧必须保持独立，否则请求可能被合并到相邻 move，
		// 并在错误的模拟帧重放。
		if (NewRewindMove && bSavedWantsToStartRewind != NewRewindMove->bSavedWantsToStartRewind)
		{
			return false;
		}

		return Super::CanCombineWith(NewMove, InCharacter, MaxDelta);
	}

	virtual void SetMoveFor(ACharacter* Character, float InDeltaTime, const FVector& NewAccel, FNetworkPredictionData_Client_Character& ClientData) override
	{
		Super::SetMoveFor(Character, InDeltaTime, NewAccel, ClientData);

		// 把移动组件里的临时请求快照保存到这个 saved move。
		if (const URewindCharacterMovementComponent* RewindMovement = Cast<URewindCharacterMovementComponent>(Character ? Character->GetCharacterMovement() : nullptr))
		{
			bSavedWantsToStartRewind = RewindMovement->WantsToStartRewind();
		}
	}

	virtual void PrepMoveFor(ACharacter* Character) override
	{
		Super::PrepMoveFor(Character);

		// 重放前恢复保存的 bit，让预测校正能重新模拟同一个开始回溯输入。
		if (URewindCharacterMovementComponent* RewindMovement = Cast<URewindCharacterMovementComponent>(Character ? Character->GetCharacterMovement() : nullptr))
		{
			RewindMovement->SetWantsToStartRewindForReplay(bSavedWantsToStartRewind);
		}
	}

private:
	bool bSavedWantsToStartRewind = false;
};

// 为 URewindCharacterMovementComponent 分配自定义 saved move。
class FNetworkPredictionData_Client_RewindCharacter : public FNetworkPredictionData_Client_Character
{
public:
	explicit FNetworkPredictionData_Client_RewindCharacter(const UCharacterMovementComponent& ClientMovement)
		: FNetworkPredictionData_Client_Character(ClientMovement)
	{
	}

	virtual FSavedMovePtr AllocateNewMove() override
	{
		return FSavedMovePtr(new FSavedMove_RewindCharacter());
	}
};
}

bool URewindCharacterMovementComponent::RequestPredictedRewind()
{
	// 同一帧内多次 Ability 调用仍然只代表一次移动输入。
	if (bWantsToStartRewind)
	{
		return true;
	}

	// 设置 saved move bit 之前，先拒绝明显非法的请求。
	if (const URewindComponent* RewindComponent = CharacterOwner ? CharacterOwner->FindComponentByClass<URewindComponent>() : nullptr)
	{
		if (!RewindComponent->CanStartRewind())
		{
			return false;
		}
	}

	// 服务器上由 GAS 在 CommitAbility 后调用时，这个标记允许后续移动模拟
	// 接受压缩的回溯请求。没有这个门闩，客户端可能只靠 movement 数据
	// 就启动权威回溯。
	if (CharacterOwner && CharacterOwner->HasAuthority())
	{
		bServerAuthorizedRewind = true;
	}

	bWantsToStartRewind = true;
	return true;
}

void URewindCharacterMovementComponent::UpdateFromCompressedFlags(uint8 Flags)
{
	Super::UpdateFromCompressedFlags(Flags);

	// 服务器侧恢复客户端发送的单帧回溯请求。
	bWantsToStartRewind = (Flags & RewindSavedMoveFlag) != 0;
}

FNetworkPredictionData_Client* URewindCharacterMovementComponent::GetPredictionData_Client() const
{
	if (ClientPredictionData == nullptr)
	{
		// ClientPredictionData 由 CharacterMovement 持有并负责释放。
		URewindCharacterMovementComponent* MutableThis = const_cast<URewindCharacterMovementComponent*>(this);
		MutableThis->ClientPredictionData = new FNetworkPredictionData_Client_RewindCharacter(*this);
	}

	return ClientPredictionData;
}

void URewindCharacterMovementComponent::PerformMovement(float DeltaTime)
{
	if (bWantsToStartRewind)
	{
		// 在基础移动前消费 saved move 输入，让客户端和服务器都能在
		// 这个模拟帧立即进入自定义回溯移动。
		TryStartPredictedRewind();
		bWantsToStartRewind = false;
	}

	Super::PerformMovement(DeltaTime);
}

void URewindCharacterMovementComponent::PhysCustom(float DeltaTime, int32 Iterations)
{
	if (CustomMovementMode == static_cast<uint8>(ERewindCustomMovementMode::Rewind))
	{
		PhysRewind(DeltaTime, Iterations);
		return;
	}

	Super::PhysCustom(DeltaTime, Iterations);
}

bool URewindCharacterMovementComponent::TryStartPredictedRewind()
{
	if (!CharacterOwner)
	{
		return false;
	}

	// 服务器可能在 Ability RPC 处理前后收到压缩 movement flags。
	// 只有经过 GAS 授权的路径才能启动权威回溯。
	if (CharacterOwner->HasAuthority() && !CharacterOwner->IsLocallyControlled() && !bServerAuthorizedRewind)
	{
		return false;
	}

	URewindComponent* RewindComponent = CharacterOwner->FindComponentByClass<URewindComponent>();
	if (!RewindComponent || !RewindComponent->StartRewindPlayback(true))
	{
		// 即使回溯组件拒绝启动，也要清掉授权门闩。
		bServerAuthorizedRewind = false;
		return false;
	}

	bServerAuthorizedRewind = false;
	StopMovementImmediately();

	// 从这里到 FinishRewindPlayback，位置更新由 PhysCustom 负责。
	SetMovementMode(MOVE_Custom, static_cast<uint8>(ERewindCustomMovementMode::Rewind));
	return true;
}

void URewindCharacterMovementComponent::PhysRewind(float DeltaTime, int32 Iterations)
{
	if (DeltaTime <= KINDA_SMALL_NUMBER || !CharacterOwner || !UpdatedComponent)
	{
		return;
	}

	URewindComponent* RewindComponent = CharacterOwner->FindComponentByClass<URewindComponent>();
	if (!RewindComponent || !RewindComponent->IsRewinding())
	{
		// 回溯组件是播放生命周期的事实来源。
		SetMovementMode(MOVE_Walking);
		return;
	}

	FRewindStateSnapshot PlaybackSnapshot;
	bool bFinished = false;
	if (!RewindComponent->AdvanceRewindPlayback(DeltaTime, PlaybackSnapshot, bFinished))
	{
		// 采样失败时干净结束，避免移动组件停留在 MOVE_Custom。
		RewindComponent->FinishRewindPlayback();
		return;
	}

	const FVector PreviousLocation = UpdatedComponent->GetComponentLocation();
	const FVector MoveDelta = PlaybackSnapshot.Location - PreviousLocation;

	// 保持 CharacterMovement 的 Velocity 一致，便于预测/校正诊断，
	// 也方便其他系统在回溯期间读取速度。
	Velocity = MoveDelta / DeltaTime;

	FHitResult MoveHit;
	MoveUpdatedComponent(
		MoveDelta,
		PlaybackSnapshot.ActorRotation.Quaternion(),
		false,
		&MoveHit,
		ETeleportType::TeleportPhysics);

	RewindComponent->ApplyControlRotation(PlaybackSnapshot);

	if (bFinished)
	{
		// Finish 会恢复之前的移动模式，并记录回溯后的状态。
		RewindComponent->FinishRewindPlayback();
	}
}
