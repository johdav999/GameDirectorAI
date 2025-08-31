#pragma once
#include "CoreMinimal.h"
#include <random>
#include <atomic>

#include "llama.h"

struct FLlamaParams {
    FString ModelPath;
    int32   ContextLength = 4096;
    int32   NumThreads = 0;
    int32   MaxTokens = 256;
    float   Temperature = 0.8f;
    bool    bPreferGPU = true;
    int32   NGpuLayers = 100;
    int32   GPUBatchSize = 512;
    FString GrammarFilePath;
};

class FLlamaRunner
{
public:
    FLlamaRunner();
    ~FLlamaRunner();

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

    void Generate(
        const FString& Prompt,
        TFunction<void(const FString&)> OnToken,
        TFunction<void(const FString&)> OnComplete,
        TFunction<void(const FString&)> OnError);

private:
    bool BuildSamplerChainIfNeeded(const FString& GrammarFilePath);

    FLlamaParams P;
    llama_model* Model = nullptr;
    llama_context* Ctx = nullptr;
    llama_sampler* Sampler = nullptr;

    FCriticalSection GenLock;
    std::atomic<bool> bShuttingDown{false};
    std::atomic<bool> bCancel{false};
};