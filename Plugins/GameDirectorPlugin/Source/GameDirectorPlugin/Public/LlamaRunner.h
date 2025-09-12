#pragma once

#include "CoreMinimal.h"
#include <string>
#include <vector>
#include "llama.h"
#include "Misc/Paths.h"
#include <vector>
#include <string>
#include <random>     // mt19937, uniform_real_distribution, random_device
#include <algorithm>  // sort, partial_sort, max
#include <cmath>      // std::exp, INFINITY
#include <algorithm>
#include <random>
#include <numeric>
#include <cstring>
#include <cfloat>
#include <cmath>
#include "HAL/PlatformProcess.h"
// If Unreal hasn't generated the module API macro yet, make it a no-op so this header still parses.
#ifndef GAMEDIRECTORPLUGIN_API
#define GAMEDIRECTORPLUGIN_API
#endif

// Forward declarations from llama.cpp (avoid pulling llama.h into public headers)
struct llama_model;
struct llama_context;
struct llama_vocab;

/**
 * Lightweight wrapper around llama.cpp with explicit init/shutdown and a simple greedy Generate().
 * Call Initiate() once, then Generate() as needed, then Shutdown() (or rely on destructor).
 */
class GAMEDIRECTORPLUGIN_API LlamaRunner
{
public:
    LlamaRunner();
    ~LlamaRunner();

    /** Load model + create context. Returns true on success. */
    bool Initiate(const FString& ModelPath, int32 ContextSize = 4096);

  

  

    ///** Greedy-generate up to MaxTokens based on Prompt. Returns generated text. */
    //FString Generate(const FString& Prompt, int32 MaxTokens = 64);

  
    std::vector<llama_token> TokenizePrompt(const FString& Prompt) const;

    FString GenerateJSON(const FString& Prompt, int max_new, int top_k, float top_p, float temp);

   // std::string GenerateJSON(const std::vector<llama_token>& prompt_tokens, int max_new_tokens, int top_k, float top_p, float temp);

    /** Explicit cleanup (also called by destructor). Safe to call multiple times. */
    void Shutdown();

    /** True after successful Initiate() until Shutdown(). */
    bool IsInitialized() const { return bInitialized; }

private:
    bool                 bInitialized = false;
    llama_model* Model = nullptr;
    llama_context* Ctx = nullptr;
    const llama_vocab* Vocab = nullptr;

    // Non-copyable
    LlamaRunner(const LlamaRunner&) = delete;
    LlamaRunner& operator=(const LlamaRunner&) = delete;
    mutable FCriticalSection DecodeMutex;
    // Movable (optional; uncomment if you need it)
    // LlamaRunner(LlamaRunner&&) = default;
    // LlamaRunner& operator=(LlamaRunner&&) = default;
};