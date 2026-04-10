#include "RewindFlashCharacter.h"

#include "Gameplay_Demo/Ability/RewindAbility.h"
#include "Gameplay_Demo/Ability/RewindComponent.h"

ARewindFlashCharacter::ARewindFlashCharacter()
{
	RewindComponent = CreateDefaultSubobject<URewindComponent>(TEXT("RewindComponent"));
	DefaultAbilityInput2 = URewindAbility::StaticClass();
}
