// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "GameDirectorPluginSubsystem.generated.h"

// Forward declarations for Llama runner support
struct FLlamaParams;
class FLlamaRunner;

UCLASS()
class GAMEDIRECTORPLUGIN_API UGameDirectorPluginSubsystem : public UGameInstanceSubsystem
{
    GENERATED_BODY()

public:
    UFUNCTION(BlueprintCallable, Category="GameDirector")
    bool InitiateLlamaRunner();

    virtual void Deinitialize() override;

private:
    // In-process Llama runner instance
    TUniquePtr<FLlamaRunner> Runner;
};

