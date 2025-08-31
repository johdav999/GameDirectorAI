#include "GameDirectorPluginSubsystem.h"
#include "GameDirectorSettings.h"
#include "LlamaRunner.h"
#include "Logging/LogMacros.h"

void UGameDirectorPluginSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
    Super::Initialize(Collection);

    const UGameDirectorSettings* Settings = GetDefault<UGameDirectorSettings>();
    Runner = MakeUnique<FLlamaRunner>();

    FString ModelPath = Settings->GGUFModelPath.FilePath.IsEmpty()
        ? TEXT("C:/models/rpg_director/gptoss20b.f16pure.gguf")
        : Settings->GGUFModelPath.FilePath;

    Runner->Init(ModelPath, Settings->ContextLength, Settings->NumThreads);
}

FString UGameDirectorPluginSubsystem::GenerateFromPrompt(const FString& Prompt)
{
    const UGameDirectorSettings* Settings = GetDefault<UGameDirectorSettings>();
    if (!Runner)
    {
        return FString();
    }

    return Runner->Generate(Prompt, Settings->MaxTokens, Settings->Temperature,
        [](const FString& Token)
        {
            UE_LOG(LogTemp, Display, TEXT("%s"), *Token);
        });
}

