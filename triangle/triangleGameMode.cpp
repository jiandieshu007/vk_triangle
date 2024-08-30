// Copyright Epic Games, Inc. All Rights Reserved.

#include "triangleGameMode.h"
#include "triangleCharacter.h"
#include "UObject/ConstructorHelpers.h"

AtriangleGameMode::AtriangleGameMode()
{
	// set default pawn class to our Blueprinted character
	static ConstructorHelpers::FClassFinder<APawn> PlayerPawnBPClass(TEXT("/Game/ThirdPerson/Blueprints/BP_ThirdPersonCharacter"));
	if (PlayerPawnBPClass.Class != NULL)
	{
		DefaultPawnClass = PlayerPawnBPClass.Class;
	}
}
