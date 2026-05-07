// Fill out your copyright notice in the Description page of Project Settings.


#include "FlashCharacter.h"
#include "../Ability/RewindComponent.h"
#include "../Ability/VisualRewindComponent.h"


// Sets default values
AFlashCharacter::AFlashCharacter()
{
	// Set this character to call Tick() every frame.  You can turn this off to improve performance if you don't need it.
	PrimaryActorTick.bCanEverTick = true;

	RewindComponent=CreateDefaultSubobject<URewindComponent>(TEXT("RewindComponent"));
	VisualRewindComponent=CreateDefaultSubobject<UVisualRewindComponent>(TEXT("VisualRewindComponent"));
}

// Called when the game starts or when spawned
void AFlashCharacter::BeginPlay()
{
	Super::BeginPlay();
	
}

void AFlashCharacter::BuildAbilityInputPayload_Implementation(EAbilityInputSlot InputSlot, bool bPressed,
	FGameplayEventData& OutPayload) const
{
	Super::BuildAbilityInputPayload_Implementation(InputSlot, bPressed, OutPayload);
	if (InputSlot==EAbilityInputSlot::AbilityInput1)
	{
		// 1. 客户端计算闪现方向
		FVector BlinkDirection = GetLastMovementInputVector();
		if (BlinkDirection.IsNearlyZero())
		{
			BlinkDirection = GetActorForwardVector();
		}
		BlinkDirection.Z = 0.0f; // 保证纯水平
		BlinkDirection.Normalize();

		// 2. 将方向打包进 GAS 的 TargetData
		FGameplayAbilityTargetData_LocationInfo* LocationData = new FGameplayAbilityTargetData_LocationInfo();
		LocationData->TargetLocation.LocationType = EGameplayAbilityTargetingLocationType::LiteralTransform;
		// 巧妙利用 Location 字段来传递我们的方向向量
		LocationData->TargetLocation.LiteralTransform = FTransform(BlinkDirection);
		OutPayload.TargetData.Add(LocationData);
		FGameplayTag EventTag = FGameplayTag::RequestGameplayTag(FName("Ability.Move.Blink"));
		OutPayload.EventTag = EventTag;
	}else
	{
		FGameplayTag EventTag = FGameplayTag::RequestGameplayTag(FName("Ability.Move.Rewind"));
		OutPayload.EventTag = EventTag;
	}
}

// Called every frame
void AFlashCharacter::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);
}

// Called to bind functionality to input
void AFlashCharacter::SetupPlayerInputComponent(UInputComponent* PlayerInputComponent)
{
	Super::SetupPlayerInputComponent(PlayerInputComponent);
}

