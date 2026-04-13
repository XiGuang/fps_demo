#include "CharacterBase.h"
#include "AbilitySystemComponent.h"
#include "MyAttributeSet.h"
#include "Abilities/GameplayAbility.h"
#include "EnhancedInputComponent.h"
#include "GameplayTagContainer.h"

ACharacterBase::ACharacterBase()
{
	PrimaryActorTick.bCanEverTick=true;

	AbilitySystemComponent = CreateDefaultSubobject<UAbilitySystemComponent>(FName("AbilitySystemComponent"));
	AbilitySystemComponent->SetIsReplicated(true);
	AbilitySystemComponent->SetReplicationMode(ReplicationMode);

	AttributeSet = CreateDefaultSubobject<UMyAttributeSet>(FName("AttributeSet"));
}

UAbilitySystemComponent* ACharacterBase::GetAbilitySystemComponent() const
{
	return AbilitySystemComponent;
}

void ACharacterBase::BeginPlay()
{
	Super::BeginPlay();
}

void ACharacterBase::EndPlay(EEndPlayReason::Type EndPlayReason)
{
	Super::EndPlay(EndPlayReason);
}

void ACharacterBase::SetupPlayerInputComponent(UInputComponent* PlayerInputComponent)
{
	Super::SetupPlayerInputComponent(PlayerInputComponent);

	if (UEnhancedInputComponent* EnhancedInputComponent = Cast<UEnhancedInputComponent>(PlayerInputComponent))
	{
		if (AbilityInput1Action)
		{
			EnhancedInputComponent->BindAction(AbilityInput1Action, ETriggerEvent::Started, this, &ACharacterBase::OnAbilityInput1Pressed);
			EnhancedInputComponent->BindAction(AbilityInput1Action, ETriggerEvent::Completed, this, &ACharacterBase::OnAbilityInput1Released);
			EnhancedInputComponent->BindAction(AbilityInput1Action, ETriggerEvent::Canceled, this, &ACharacterBase::OnAbilityInput1Released);
		}

		if (AbilityInput2Action)
		{
			EnhancedInputComponent->BindAction(AbilityInput2Action, ETriggerEvent::Started, this, &ACharacterBase::OnAbilityInput2Pressed);
			EnhancedInputComponent->BindAction(AbilityInput2Action, ETriggerEvent::Completed, this, &ACharacterBase::OnAbilityInput2Released);
			EnhancedInputComponent->BindAction(AbilityInput2Action, ETriggerEvent::Canceled, this, &ACharacterBase::OnAbilityInput2Released);
		}
	}
}

FGameplayAbilitySpecHandle ACharacterBase::GrantAbilityToInput(
	EAbilityInputSlot InputSlot,
	TSubclassOf<UGameplayAbility> AbilityClass,
	int32 AbilityLevel,
	bool bReplaceExisting)
{
	if (!AbilitySystemComponent || !AbilityClass)
	{
		return FGameplayAbilitySpecHandle();
	}

	if (!HasAuthority())
	{
		return FGameplayAbilitySpecHandle();
	}

	const int32 SlotIndex = static_cast<int32>(InputSlot);
	if (SlotIndex < 0 || SlotIndex >= UE_ARRAY_COUNT(InputAbilityHandles))
	{
		return FGameplayAbilitySpecHandle();
	}

	if (bReplaceExisting && InputAbilityHandles[SlotIndex].IsValid())
	{
		AbilitySystemComponent->ClearAbility(InputAbilityHandles[SlotIndex]);
		InputAbilityHandles[SlotIndex] = FGameplayAbilitySpecHandle();
	}

	FGameplayAbilitySpec Spec(AbilityClass, AbilityLevel, SlotIndex, this);
	FGameplayAbilitySpecHandle NewHandle = AbilitySystemComponent->GiveAbility(Spec);
	InputAbilityHandles[SlotIndex] = NewHandle;

	return NewHandle;
}

void ACharacterBase::ClearAbilityFromInput(EAbilityInputSlot InputSlot)
{
	if (!AbilitySystemComponent)
	{
		return;
	}

	if (!HasAuthority())
	{
		return;
	}

	const int32 SlotIndex = static_cast<int32>(InputSlot);
	if (SlotIndex < 0 || SlotIndex >= UE_ARRAY_COUNT(InputAbilityHandles))
	{
		return;
	}

	if (InputAbilityHandles[SlotIndex].IsValid())
	{
		AbilitySystemComponent->ClearAbility(InputAbilityHandles[SlotIndex]);
		InputAbilityHandles[SlotIndex] = FGameplayAbilitySpecHandle();
	}
}

void ACharacterBase::PossessedBy(AController* NewController)
{
	Super::PossessedBy(NewController);

	if (AbilitySystemComponent)
	{
		AbilitySystemComponent->InitAbilityActorInfo(this,this);
	}

	if (HasAuthority())
	{
		InitializeCharacterAbilities();
	}
}

void ACharacterBase::OnRep_PlayerState()
{
	Super::OnRep_PlayerState();

	if (AbilitySystemComponent)
	{
		AbilitySystemComponent->InitAbilityActorInfo(this,this);
	}
}

void ACharacterBase::HandleAbilityInputPressed(EAbilityInputSlot InputSlot)
{
	if (!AbilitySystemComponent)
	{
		return;
	}

	SendAbilityInputEvent(InputSlot, true);

	// AbilitySystemComponent->AbilityLocalInputPressed(static_cast<int32>(InputSlot));
}

void ACharacterBase::HandleAbilityInputReleased(EAbilityInputSlot InputSlot)
{
	if (!AbilitySystemComponent)
	{
		return;
	}

	// SendAbilityInputEvent(InputSlot, false);

	// AbilitySystemComponent->AbilityLocalInputReleased(static_cast<int32>(InputSlot));
}

void ACharacterBase::InitializeCharacterAbilities_Implementation()
{
	GrantAbilityToInput(EAbilityInputSlot::AbilityInput1, DefaultAbilityInput1, 1, true);
	GrantAbilityToInput(EAbilityInputSlot::AbilityInput2, DefaultAbilityInput2, 1, true);
}

void ACharacterBase::BuildAbilityInputPayload_Implementation(
	EAbilityInputSlot InputSlot,
	bool bPressed,
	FGameplayEventData& OutPayload) const
{
	OutPayload = FGameplayEventData();
	OutPayload.EventTag = GetAbilityInputEventTag(InputSlot, bPressed);
	OutPayload.Instigator = const_cast<ACharacterBase*>(this);
	OutPayload.Target = const_cast<ACharacterBase*>(this);
}

void ACharacterBase::SendAbilityInputEvent(EAbilityInputSlot InputSlot, bool bPressed)
{
	if (!AbilitySystemComponent)
	{
		return;
	}

	FGameplayEventData EventPayload;
	BuildAbilityInputPayload(InputSlot, bPressed, EventPayload);

	UE_LOG(LogTemp, Warning, TEXT("ACharacterBase::SendAbilityInputEvent: EventTag=%s, Instigator=%s, Target=%s"),
		*EventPayload.EventTag.ToString(),
		*GetNameSafe(EventPayload.Instigator),
		*GetNameSafe(EventPayload.Target));

	if (!EventPayload.EventTag.IsValid())
	{
		EventPayload.EventTag = GetAbilityInputEventTag(InputSlot, bPressed);
	}
	
	if (!EventPayload.EventTag.IsValid())
	{
		return;
	}
	
	if (!IsValid(EventPayload.Instigator))
	{
		EventPayload.Instigator = this;
	}
	
	if (!IsValid(EventPayload.Target))
	{
		EventPayload.Target = this;
	}

	AbilitySystemComponent->HandleGameplayEvent(EventPayload.EventTag, &EventPayload);
}

FGameplayTag ACharacterBase::GetAbilityInputEventTag(EAbilityInputSlot InputSlot, bool bPressed) const
{
	switch (InputSlot)
	{
	case EAbilityInputSlot::AbilityInput1:
		return bPressed ? AbilityInput1PressedEventTag : AbilityInput1ReleasedEventTag;
	case EAbilityInputSlot::AbilityInput2:
		return bPressed ? AbilityInput2PressedEventTag : AbilityInput2ReleasedEventTag;
	default:
		return FGameplayTag();
	}
}

void ACharacterBase::OnAbilityInput1Pressed()
{
	HandleAbilityInputPressed(EAbilityInputSlot::AbilityInput1);
}

void ACharacterBase::OnAbilityInput1Released()
{
	HandleAbilityInputReleased(EAbilityInputSlot::AbilityInput1);
}

void ACharacterBase::OnAbilityInput2Pressed()
{
	HandleAbilityInputPressed(EAbilityInputSlot::AbilityInput2);
}

void ACharacterBase::OnAbilityInput2Released()
{
	HandleAbilityInputReleased(EAbilityInputSlot::AbilityInput2);
}
