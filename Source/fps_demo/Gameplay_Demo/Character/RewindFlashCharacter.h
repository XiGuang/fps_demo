#pragma once

#include "CoreMinimal.h"
#include "Gameplay_Demo/CharacterBase.h"

#include "RewindFlashCharacter.generated.h"

class URewindComponent;

UCLASS()
class FPS_DEMO_API ARewindFlashCharacter : public ACharacterBase
{
	GENERATED_BODY()

public:
	ARewindFlashCharacter();

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Rewind")
	TObjectPtr<URewindComponent> RewindComponent;
};
