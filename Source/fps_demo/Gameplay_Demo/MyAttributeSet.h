// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "AttributeSet.h"
#include "AbilitySystemComponent.h"

#include "MyAttributeSet.generated.h"

/**
 * 
 */
UCLASS()
class FPS_DEMO_API UMyAttributeSet : public UAttributeSet
{
	GENERATED_BODY()

public:
	UMyAttributeSet();

public:
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Attributes", ReplicatedUsing=OnRep_MaxHealth)
	FGameplayAttributeData MaxHealth;
	ATTRIBUTE_ACCESSORS_BASIC(UMyAttributeSet, MaxHealth)
	
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Attributes", ReplicatedUsing=OnRep_CurrentHealth)
	FGameplayAttributeData CurrentHealth;
	ATTRIBUTE_ACCESSORS_BASIC(UMyAttributeSet, CurrentHealth)

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Attributes", ReplicatedUsing=OnRep_CurrentAmmunition)
	FGameplayAttributeData CurrentAmmunition;
	ATTRIBUTE_ACCESSORS_BASIC(UMyAttributeSet, CurrentAmmunition)

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Attributes", ReplicatedUsing=OnRep_MaxAmmunition)
	FGameplayAttributeData MaxAmmunition;
	ATTRIBUTE_ACCESSORS_BASIC(UMyAttributeSet, MaxAmmunition)

	UFUNCTION()
	void OnRep_MaxHealth(const FGameplayAttributeData& OldValue) const
	{
		GAMEPLAYATTRIBUTE_REPNOTIFY(UMyAttributeSet, MaxHealth, OldValue);
	}

	UFUNCTION()
	void OnRep_CurrentHealth(const FGameplayAttributeData& OldValue) const
	{
		GAMEPLAYATTRIBUTE_REPNOTIFY(UMyAttributeSet, CurrentHealth, OldValue);
	}

	UFUNCTION()
	void OnRep_CurrentAmmunition(const FGameplayAttributeData& OldValue) const
	{
		GAMEPLAYATTRIBUTE_REPNOTIFY(UMyAttributeSet, CurrentAmmunition, OldValue);
	}

	UFUNCTION()
	void OnRep_MaxAmmunition(const FGameplayAttributeData& OldValue) const
	{
		GAMEPLAYATTRIBUTE_REPNOTIFY(UMyAttributeSet, MaxAmmunition, OldValue);
	}

	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
};
