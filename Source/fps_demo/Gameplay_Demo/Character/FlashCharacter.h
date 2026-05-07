// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Gameplay_Demo/CharacterBase.h"
#include "FlashCharacter.generated.h"

class URewindComponent;
class UVisualRewindComponent;

UCLASS()
class FPS_DEMO_API AFlashCharacter : public ACharacterBase
{
	GENERATED_BODY()

public:
	// Sets default values for this character's properties
	AFlashCharacter();

protected:
	// Called when the game starts or when spawned
	virtual void BeginPlay() override;

	void BuildAbilityInputPayload_Implementation(EAbilityInputSlot InputSlot, bool bPressed, FGameplayEventData& OutPayload) const override;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly)
	TObjectPtr<URewindComponent> RewindComponent;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly)
	TObjectPtr<UVisualRewindComponent> VisualRewindComponent;
public:
	// Called every frame
	virtual void Tick(float DeltaTime) override;

	// Called to bind functionality to input
	virtual void SetupPlayerInputComponent(class UInputComponent* PlayerInputComponent) override;
};
