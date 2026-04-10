// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameModeBase.h"
#include "fps_demoGameMode.generated.h"

/**
 *  Simple GameMode for a first person game
 */
UCLASS(abstract)
class Afps_demoGameMode : public AGameModeBase
{
	GENERATED_BODY()

public:
	Afps_demoGameMode();
};



