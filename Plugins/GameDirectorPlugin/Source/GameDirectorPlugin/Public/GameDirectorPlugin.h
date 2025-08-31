// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Modules/ModuleManager.h"
#include "HAL/PlatformProcess.h"

class FGameDirectorPluginModule : public IModuleInterface
{
public:

	/** IModuleInterface implementation */
	virtual void StartupModule() override;
        virtual void ShutdownModule() override;

        /** Launches the external Llama runner process */
        void StartLlamaRunner();

private:
        /** Handle to the spawned Llama runner process */
        FProcHandle LlamaRunnerHandle;
};
