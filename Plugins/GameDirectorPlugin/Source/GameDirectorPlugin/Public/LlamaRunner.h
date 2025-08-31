#pragma once
#include "CoreMinimal.h"
#include <random>
#include <atomic>

struct FLlamaParams {
    FString ModelPath;
    int32   ContextLength = 4096;
    int32   NumThreads = 0;
    int32   MaxTokens = 256;
    float   Temperature = 0.8f;
    bool    bPreferGPU = true;
    int32   NGpuLayers = 100;
    int32   GPUBatchSize = 512;
};

class FLlamaRunner
{
public:
    bool Init(const FLlamaParams& Params);

    // NEW: overload to match old call-site (3 args)
    bool Init(const FString& ModelPath, int32 ContextLen, int32 NumThreads)
    {
        FLlamaParams P;
        P.ModelPath = ModelPath;
        P.ContextLength = ContextLen;
        P.NumThreads = NumThreads;
        return Init(P);
    }

    void Shutdown();
    void Cancel();

    // Canonical Generate:
    void Generate(
        const FString& Prompt,
        TFunction<void(const FString&)> OnToken,
        TFunction<void(const FString&)> OnComplete,
        TFunction<void(const FString&)> OnError);

    // NEW: overload to match old call-site (int MaxTokens as 2nd arg)
    void Generate(
        const FString& Prompt,
        int32 MaxTokens,
        TFunction<void(const FString&)> OnComplete,
        TFunction<void(const FString&)> OnError)
    {
        // bridge: stream nothing, only final
        Generate(
            Prompt,
            /*OnToken=*/nullptr,
            /*OnComplete=*/[OnComplete](const FString& S) { if (OnComplete) OnComplete(S); },
            /*OnError=*/OnError
        );
    }

private:
    FLlamaParams P;
    struct llama_model* Model = nullptr;
    struct llama_context* Ctx = nullptr;

    FCriticalSection GenLock;
    std::atomic_bool bShuttingDown{ false };
    std::atomic_bool bCancel{ false };

    static void LlamaLogCallback(ggml_log_level level, const char* msg, void*);
};