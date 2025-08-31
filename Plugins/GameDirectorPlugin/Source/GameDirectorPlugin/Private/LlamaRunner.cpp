#include "LlamaRunner.h"
#include "ggml-cuda.h"
#include <vector>
#include <string>

FLlamaRunner::FLlamaRunner()
    : Model(nullptr)
    , Context(nullptr)
    , NPast(0)
{
}

FLlamaRunner::~FLlamaRunner()
{
    if (Context)
    {
        llama_free(Context);
    }
    if (Model)
    {
        llama_free_model(Model);
    }
    llama_backend_free();
}

bool FLlamaRunner::Init(const FString& ModelPath, int32 ContextLength, int32 NumThreads)
{
    llama_backend_init();

    std::string ModelPathStr = TCHAR_TO_UTF8(*ModelPath);

    llama_model_params ModelParams = llama_model_default_params();
#ifdef GGML_USE_CUDA
    ModelParams.n_gpu_layers = ggml_backend_cuda_get_device_count() > 0 ? 99 : 0;
#else
    ModelParams.n_gpu_layers = 0;
#endif
    Model = llama_load_model_from_file(ModelPathStr.c_str(), ModelParams);
    if (!Model)
    {
        return false;
    }

    llama_context_params CtxParams = llama_context_default_params();
    CtxParams.n_ctx = ContextLength;
    CtxParams.n_threads = NumThreads;

    Context = llama_new_context_with_model(Model, CtxParams);
    NPast = 0;

    return Context != nullptr;
}

FString FLlamaRunner::Generate(const FString& Prompt, int32 MaxTokens, float Temperature, TFunctionRef<void(const FString&)> OnToken)
{
    if (!Context)
    {
        return FString();
    }

    std::string PromptStr = TCHAR_TO_UTF8(*Prompt);
    std::vector<llama_token> PromptTokens = llama_tokenize(Model, PromptStr, true);

    llama_batch Batch = llama_batch_init(PromptTokens.size(), 0, 1);
    for (size_t i = 0; i < PromptTokens.size(); ++i)
    {
        llama_batch_add(&Batch, PromptTokens[i], NPast + i, {0}, false);
    }
    llama_decode(Context, Batch);
    NPast += PromptTokens.size();

    llama_sampler_chain_params SamplerParams = llama_sampler_chain_default_params();
    llama_sampler* Sampler = llama_sampler_chain_init(SamplerParams);
    llama_sampler_chain_add(Sampler, llama_sampler_init_top_k(40));
    llama_sampler_chain_add(Sampler, llama_sampler_init_top_p(0.9f, 1));
    llama_sampler_chain_add(Sampler, llama_sampler_init_temp(Temperature));
    llama_sampler_chain_add(Sampler, llama_sampler_init_greedy());

    FString Result;
    for (int32 i = 0; i < MaxTokens; ++i)
    {
        const llama_token Token = llama_sampler_sample(Sampler, Context, -1);
        llama_sampler_accept(Sampler, Token);

        llama_batch TokenBatch = llama_batch_init(1, 0, 1);
        llama_batch_add(&TokenBatch, Token, NPast, {0}, true);
        llama_decode(Context, TokenBatch);
        NPast += 1;

        const char* Piece = llama_token_to_piece(Model, Token);
        FString TokenStr = UTF8_TO_TCHAR(Piece);
        OnToken(TokenStr);
        Result += TokenStr;
        if (Token == llama_token_eos(Model))
        {
            break;
        }
    }

    llama_sampler_free(Sampler);
    return Result;
}

