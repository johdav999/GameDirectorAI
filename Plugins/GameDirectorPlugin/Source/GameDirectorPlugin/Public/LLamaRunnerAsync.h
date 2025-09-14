#pragma once

#include "CoreMinimal.h"
#include "HAL/Runnable.h"
#include "HAL/RunnableThread.h"
#include "HAL/ThreadSafeBool.h"
#include "HAL/Event.h"
#include "HAL/CriticalSection.h"
#include "Containers/Queue.h"
#include "Async/Async.h"
#include <string>
#include <vector>
#include <random>
#include <algorithm>
#include <numeric>
#include <cstring>
#include <cfloat>
#include <cmath>
#include "llama.h"  
// Forward-declare llama types (avoid including llama.h in public headers if you want)
struct llama_model;
struct llama_context;
struct llama_vocab;

class LLamaRunnerAsync
{
public:

    LLamaRunnerAsync();
    ~LLamaRunnerAsync();

    bool Initiate(const FString& ModelPath, int32 ContextSize = 4096);
    void Shutdown();

    // Synchronous generation (IMPLEMENTATION LIVES IN .CPP)
    FString GenerateJSON(const FString& Prompt, int max_new, int top_k, float top_p, float temp,FString Intent);

    // Asynchronous enqueue (callback runs on Game Thread)
    void GenerateJSONAsync(const FString& Prompt, TFunction<void(FString)> OnDone,FString Intent);

    void ResetContext();

    bool IsInitialized() const { return bInitialized; }
    bool IsValidDirectorJSON(const FString& RawText, FString& OutCleanedJSON, FString& OutError) const;

    llama_context_params cparams;
private:
    // ---- llama state ----
    bool                 bInitialized = false;
    llama_model* Model = nullptr;
    llama_context* Ctx = nullptr;
    const llama_vocab* Vocab = nullptr;

    // serialize llama_decode just in case; worker is single-threaded anyway
    mutable FCriticalSection DecodeMutex;

    // ---- worker ----
    struct FJob
    {
        FString Prompt;
        TFunction<void(FString)> OnDone; // called on Game Thread
        FString Intent;
    };

    class FWorker : public FRunnable
    {
    public:
        explicit FWorker(LLamaRunnerAsync* InOwner);
        virtual ~FWorker();

        virtual bool   Init() override;
        virtual uint32 Run() override;
        virtual void   Stop() override;

        void Enqueue(FJob&& Job);
        void Shutdown();

    private:
        LLamaRunnerAsync* Owner = nullptr;
        TQueue<FJob, EQueueMode::Mpsc> Queue;
        FEvent* WakeEvent = nullptr;
        FThreadSafeBool  bStop = false;
    };

    TUniquePtr<FWorker>         Worker;
    TUniquePtr<FRunnableThread> WorkerThread;

    void StartWorkerIfNeeded();
};