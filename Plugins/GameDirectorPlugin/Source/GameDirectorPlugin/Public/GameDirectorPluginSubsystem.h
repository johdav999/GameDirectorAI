// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "HAL/PlatformProcess.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "GameDirectorPluginSubsystem.generated.h"

UCLASS()
class GAMEDIRECTORPLUGIN_API UGameDirectorPluginSubsystem : public UGameInstanceSubsystem
{
    GENERATED_BODY()

public:
    UFUNCTION(BlueprintCallable, Category="GameDirector")
    void InitiateLlamaRunner();

    virtual void Deinitialize() override;

private:
    FProcHandle LlamaRunnerHandle;
};

