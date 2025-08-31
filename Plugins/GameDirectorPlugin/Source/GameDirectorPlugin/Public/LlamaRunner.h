#pragma once
#include "CoreMinimal.h"
#include <random>
#include <atomic>

enum ggml_log_level : int;

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

};