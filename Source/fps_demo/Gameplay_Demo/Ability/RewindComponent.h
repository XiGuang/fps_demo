#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "TimerManager.h"

#include "RewindComponent.generated.h"

USTRUCT(BlueprintType)
struct FRewindStateSnapshot
{
	GENERATED_BODY()

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Rewind")
	FVector Location = FVector::ZeroVector;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Rewind")
	FRotator ActorRotation = FRotator::ZeroRotator;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Rewind")
	FRotator ControlRotation = FRotator::ZeroRotator;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Rewind")
	float TimestampSeconds = 0.0f;
};

UCLASS(ClassGroup=(Custom), BlueprintType, Blueprintable, meta=(BlueprintSpawnableComponent))
class FPS_DEMO_API URewindComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	URewindComponent();

	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	UFUNCTION(BlueprintCallable, Category = "Rewind")
	bool StartRewind();

protected:
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Rewind")
	float RewindLookbackSeconds = 3.0f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Rewind")
	float RecordIntervalSeconds = 0.02f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Rewind")
	float RewindPlaybackDurationSeconds = 0.75f;

	UFUNCTION()
	void OnRep_HiddenFromOtherPlayers();

	UFUNCTION()
	void FinishRewindPlayback();

	void RecordSnapshot();
	bool SampleSnapshotAtTime(float TargetTimestampSeconds, FRewindStateSnapshot& OutSnapshot) const;
	void ApplyVisibilityState(bool bHideFromOtherPlayers);
	void ApplyVisibilityToOwner() const;
	float GetCurrentTimeSeconds() const;
	void UpdateRewindPlayback(float DeltaTime);
	void ApplySnapshot(const FRewindStateSnapshot& Snapshot) const;
	bool CanRecordSnapshots() const;

private:
	UPROPERTY(ReplicatedUsing = OnRep_HiddenFromOtherPlayers)
	bool bHiddenFromOtherPlayers = false;

	UPROPERTY()
	TArray<FRewindStateSnapshot> RewindHistory;

	float LastRecordTimestampSeconds = -1.0f;
	float RewindPlaybackElapsedSeconds = 0.0f;
	float RewindPlaybackStartTimestampSeconds = 0.0f;
	float RewindPlaybackTargetTimestampSeconds = 0.0f;
	bool bIsRewinding = false;
	TEnumAsByte<EMovementMode> CachedMovementMode = MOVE_Walking;
	uint8 CachedCustomMovementMode = 0;
	bool bHasCachedMovementMode = false;
};
