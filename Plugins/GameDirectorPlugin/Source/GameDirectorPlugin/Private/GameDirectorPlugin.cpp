// Copyright Epic Games, Inc. All Rights Reserved.

#include "GameDirectorPlugin.h"
#include "Misc/Paths.h"
#include "HAL/PlatformProcess.h"

#define LOCTEXT_NAMESPACE "FGameDirectorPluginModule"

void FGameDirectorPluginModule::StartupModule()
{
        // This code will execute after your module is loaded into memory; the exact timing is specified in the .uplugin file per-module
        StartLlamaRunner();
}

void FGameDirectorPluginModule::ShutdownModule()
{
        // This function may be called during shutdown to clean up your module.  For modules that support dynamic reloading,
        // we call this function before unloading the module.
        if (LlamaRunnerHandle.IsValid())
        {
                FPlatformProcess::TerminateProc(LlamaRunnerHandle, true);
                LlamaRunnerHandle.Reset();
        }
}

void FGameDirectorPluginModule::StartLlamaRunner()
{
        const FString RunnerPath = FPaths::ConvertRelativePathToFull(FPaths::Combine(FPaths::ProjectDir(), TEXT("Binaries/Win64/llama-runner.exe")));
        LlamaRunnerHandle = FPlatformProcess::CreateProc(*RunnerPath, nullptr, true, false, false, nullptr, 0, nullptr, nullptr);

        if (!LlamaRunnerHandle.IsValid())
        {
                UE_LOG(LogTemp, Warning, TEXT("Failed to start Llama runner process at %s"), *RunnerPath);
        }
}

#undef LOCTEXT_NAMESPACE

IMPLEMENT_MODULE(FGameDirectorPluginModule, GameDirectorPlugin)
