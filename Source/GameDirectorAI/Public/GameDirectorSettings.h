#pragma once

#include "CoreMinimal.h"
#include "Engine/DeveloperSettings.h"
#include "GameDirectorSettings.generated.h"

UCLASS(config = Game, defaultconfig, meta = (DisplayName = "Game Director AI"))
class GAMEDIRECTORAI_API UGameDirectorSettings : public UDeveloperSettings
{
    GENERATED_BODY()
public:
    UPROPERTY(config, EditAnywhere, Category = "Model")
    FFilePath GGUFModelPath;

    UPROPERTY(config, EditAnywhere, Category = "Generation", meta = (ClampMin = "1", UIMin = "1"))
    int32 ContextLength = 4096;

    UPROPERTY(config, EditAnywhere, Category = "Generation", meta = (ClampMin = "1", UIMin = "1"))
    int32 MaxTokens = 256;

    UPROPERTY(config, EditAnywhere, Category = "Generation", meta = (ClampMin = "0.0", UIMin = "0.0"))
    float Temperature = 0.7f;

    UPROPERTY(config, EditAnywhere, Category = "Performance", meta = (ClampMin = "0", UIMin = "0"))
    int32 NumThreads = 0;
};

