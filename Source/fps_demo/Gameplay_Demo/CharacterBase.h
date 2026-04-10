// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "AbilitySystemComponent.h"
#include "fps_demoCharacter.h"
#include "AbilitySystemInterface.h"
#include "GameplayAbilitySpec.h"
#include "GameplayTagContainer.h"
#include "Abilities/GameplayAbilityTypes.h"


#include "CharacterBase.generated.h"

enum class EGameplayEffectReplicationMode : uint8;
class UInputAction;
class UInputComponent;
class UGameplayAbility;
class UMyAttributeSet;

UENUM(BlueprintType)
enum class EAbilityInputSlot : uint8
{
	AbilityInput1 = 0 UMETA(DisplayName = "Ability Input 1"),
	AbilityInput2 = 1 UMETA(DisplayName = "Ability Input 2")
};

/**
 *  A player controllable first person shooter character
 *  Manages a weapon inventory through the IShooterWeaponHolder interface
 *  Manages health and death
 */
UCLASS(abstract)
class FPS_DEMO_API ACharacterBase : public Afps_demoCharacter, public IAbilitySystemInterface
{
	GENERATED_BODY()

public:
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="AbilitySystem")
	TObjectPtr<UAbilitySystemComponent> AbilitySystemComponent;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="AbilitySystem")
	TObjectPtr<UMyAttributeSet> AttributeSet;

	/** Constructor */
	ACharacterBase();
	
	virtual UAbilitySystemComponent* GetAbilitySystemComponent() const override;
protected:
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "AbilitySystem|Input")
	TObjectPtr<UInputAction> AbilityInput1Action;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "AbilitySystem|Input")
	TObjectPtr<UInputAction> AbilityInput2Action;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="AbilitySystem")
	EGameplayEffectReplicationMode ReplicationMode{EGameplayEffectReplicationMode::Mixed};

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "AbilitySystem|Abilities")
	TSubclassOf<UGameplayAbility> DefaultAbilityInput1;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "AbilitySystem|Abilities")
	TSubclassOf<UGameplayAbility> DefaultAbilityInput2;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "AbilitySystem|Input|Event")
	FGameplayTag AbilityInput1PressedEventTag;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "AbilitySystem|Input|Event")
	FGameplayTag AbilityInput1ReleasedEventTag;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "AbilitySystem|Input|Event")
	FGameplayTag AbilityInput2PressedEventTag;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "AbilitySystem|Input|Event")
	FGameplayTag AbilityInput2ReleasedEventTag;

	/** Gameplay initialization */
	virtual void BeginPlay() override;

	/** Gameplay cleanup */
	virtual void EndPlay(EEndPlayReason::Type EndPlayReason) override;

	/** Set up input action bindings */
	virtual void SetupPlayerInputComponent(UInputComponent* PlayerInputComponent) override;

	UFUNCTION(BlueprintCallable, Category = "AbilitySystem|Input")
	FGameplayAbilitySpecHandle GrantAbilityToInput(
		EAbilityInputSlot InputSlot,
		TSubclassOf<UGameplayAbility> AbilityClass,
		int32 AbilityLevel = 1,
		bool bReplaceExisting = true);

	UFUNCTION(BlueprintCallable, Category = "AbilitySystem|Input")
	void ClearAbilityFromInput(EAbilityInputSlot InputSlot);
	
	virtual void PossessedBy(AController* NewController) override;

	virtual void OnRep_PlayerState() override;

	UFUNCTION(BlueprintNativeEvent, Category = "AbilitySystem")
	void InitializeCharacterAbilities();

	virtual void InitializeCharacterAbilities_Implementation();

	UFUNCTION(BlueprintNativeEvent, Category = "AbilitySystem|Input")
	void BuildAbilityInputPayload(EAbilityInputSlot InputSlot, bool bPressed, FGameplayEventData& OutPayload) const;

	virtual void BuildAbilityInputPayload_Implementation(EAbilityInputSlot InputSlot, bool bPressed, FGameplayEventData& OutPayload) const;

	void HandleAbilityInputPressed(EAbilityInputSlot InputSlot);
	void HandleAbilityInputReleased(EAbilityInputSlot InputSlot);
	void SendAbilityInputEvent(EAbilityInputSlot InputSlot, bool bPressed);
	FGameplayTag GetAbilityInputEventTag(EAbilityInputSlot InputSlot, bool bPressed) const;

	UFUNCTION()
	void OnAbilityInput1Pressed();

	UFUNCTION()
	void OnAbilityInput1Released();

	UFUNCTION()
	void OnAbilityInput2Pressed();

	UFUNCTION()
	void OnAbilityInput2Released();

private:
	FGameplayAbilitySpecHandle InputAbilityHandles[2];
};
