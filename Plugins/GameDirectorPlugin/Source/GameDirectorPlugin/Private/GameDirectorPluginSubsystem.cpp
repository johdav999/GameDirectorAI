// Copyright Epic Games, Inc. All Rights Reserved.

#include "GameDirectorPluginSubsystem.h"
#include "Misc/Paths.h"

#include "llama.h"

#include "LlamaRunner.h"


bool UGameDirectorPluginSubsystem::InitiateLlamaRunner()
{

    if (Runner.IsValid())
    // Ensure the Llama backend is initialised before attempting to launch any runner process
    ULlamaRunner::InitiateLlama();

    if (LlamaRunnerHandle.IsValid())

    {
        return true;
    }

    Runner = MakeUnique<FLlamaRunner>();

    FLlamaParams P;

    // Model + runtime settings
    P.ModelPath = FPaths::ConvertRelativePathToFull(FPaths::Combine(FPaths::ProjectDir(), TEXT("Models"), TEXT("model.gguf")));
    P.ContextLength = 2048;      // best for RTX 2060
    P.MaxTokens = 512;           // cap per call, bump if needed
    P.Temperature = 0.8f;
    P.NumThreads = 0;            // auto-detect threads
    P.bPreferGPU = true;
    P.NGpuLayers = 12;           // sweet spot from CLI tests
    P.GPUBatchSize = 2048;       // prompt ingestion speed

    if (!Runner->Init(P))
    {
        Runner.Reset();
        UE_LOG(LogTemp, Error, TEXT("Failed to load model"));
        return false;
    }

    return true;
}

void UGameDirectorPluginSubsystem::Deinitialize()
{
    if (Runner.IsValid())
    {
        Runner.Reset();
    }
}

