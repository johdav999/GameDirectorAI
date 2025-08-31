#pragma once

#include "CoreMinimal.h"
#include "llama.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "GameDirectorPluginSubsystem.generated.h"

class FLlamaRunner;

UCLASS()
class GAMEDIRECTORPLUGIN_API UGameDirectorPluginSubsystem : public UGameInstanceSubsystem
{
    GENERATED_BODY()
public:
    virtual void Initialize(FSubsystemCollectionBase& Collection) override;

    UFUNCTION(BlueprintCallable, Category = "Game Director")
    FString GenerateFromPrompt(const FString& Prompt);

private:
    TUniquePtr<FLlamaRunner> Runner;
};

