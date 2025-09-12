// Copyright Epic Games, Inc. All Rights Reserved.

#include "GameDirectorPlugin.h"
#include "Interfaces/IPluginManager.h"    // <-- add this
#include "HAL/PlatformProcess.h"


#define LOCTEXT_NAMESPACE "FGameDirectorPluginModule"

void FGameDirectorPluginModule::StartupModule()
{
#if PLATFORM_WINDOWS
    if (const TSharedPtr<IPlugin> Plugin = IPluginManager::Get().FindPlugin(TEXT("GameDirectorPlugin")))
    {
        const FString BinDir = Plugin->GetBaseDir() / TEXT("Binaries/Win64");
        FPlatformProcess::PushDllDirectory(*BinDir);

        // Preload + verbose error if it fails
        const FString DllPath = BinDir / TEXT("llama.dll");
        if (!FPaths::FileExists(DllPath))
        {
            UE_LOG(LogTemp, Error, TEXT("[llama] Not found at %s"), *DllPath);
        }
        else
        {
            void* Handle = FPlatformProcess::GetDllHandle(*DllPath);
            if (!Handle)
            {
 /*               uint32 Err = FPlatformMisc::GetLastError();
                FString Msg = FWindowsPlatformMisc::GetSystemErrorMessage(Err);
                UE_LOG(LogTemp, Error, TEXT("[llama] Load failed (%u): %s  (%s)"), Err, *Msg, *DllPath);*/
            }
            else
            {
                UE_LOG(LogTemp, Log, TEXT("[llama] Loaded OK from %s"), *DllPath);
                // Optional: keep it loaded (don’t FreeDllHandle) if you rely on delay-load symbols
            }
        }
    }
#endif
	// This code will execute after your module is loaded into memory; the exact timing is specified in the .uplugin file per-module
}

void FGameDirectorPluginModule::ShutdownModule()
{
	// This function may be called during shutdown to clean up your module.  For modules that support dynamic reloading,
	// we call this function before unloading the module.
}

#undef LOCTEXT_NAMESPACE
	
IMPLEMENT_MODULE(FGameDirectorPluginModule, GameDirectorPlugin)