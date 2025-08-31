// Copyright Epic Games, Inc. All Rights Reserved.

#include "GameDirectorPluginSubsystem.h"
#include "Misc/Paths.h"
#include "LlamaRunner.h"

void UGameDirectorPluginSubsystem::InitiateLlamaRunner()
{
    // Ensure the Llama backend is initialised before attempting to launch any runner process
    ULlamaRunner::InitiateLlama();

    if (LlamaRunnerHandle.IsValid())
    {
        return;
    }

    const FString RunnerPath = FPaths::ConvertRelativePathToFull(FPaths::Combine(FPaths::ProjectDir(), TEXT("Binaries/Win64/llama-runner.exe")));
    LlamaRunnerHandle = FPlatformProcess::CreateProc(*RunnerPath, nullptr, true, false, false, nullptr, 0, nullptr, nullptr);

    if (!LlamaRunnerHandle.IsValid())
    {
        UE_LOG(LogTemp, Warning, TEXT("Failed to start Llama runner process at %s"), *RunnerPath);
    }
}

void UGameDirectorPluginSubsystem::Deinitialize()
{
    if (LlamaRunnerHandle.IsValid())
    {
        FPlatformProcess::TerminateProc(LlamaRunnerHandle, true);
        LlamaRunnerHandle.Reset();
    }
}

