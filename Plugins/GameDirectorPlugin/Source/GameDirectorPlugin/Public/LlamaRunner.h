#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "LlamaRunner.generated.h"

/**
 * Utility library for working with the bundled Llama build.
 * Provides helpers for initializing the backend prior to use.
 */
UCLASS()
class GAMEDIRECTORPLUGIN_API ULlamaRunner : public UBlueprintFunctionLibrary
{
    GENERATED_BODY()

public:
    /**
     * Initialises the Llama backend used by this plugin.
     * The implementation targets build 58fc6cefe213c8af02153b994b738b3cb6ef2ba7.
     */
    UFUNCTION(BlueprintCallable, Category="Llama")
    static void InitiateLlama();
};

