#pragma once

#include "CoreMinimal.h"
#include "llama.h"

class FLlamaRunner
{
public:
    FLlamaRunner();
    ~FLlamaRunner();

    bool Init(const FString& ModelPath, int32 ContextLength, int32 NumThreads);

    FString Generate(const FString& Prompt, int32 MaxTokens, float Temperature, TFunctionRef<void(const FString&)> OnToken);

private:
    llama_model* Model;
    llama_context* Context;
    int32 NPast;
};

