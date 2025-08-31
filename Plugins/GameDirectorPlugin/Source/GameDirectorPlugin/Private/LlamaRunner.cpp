#include "LlamaRunner.h"
#include "llama.h"
#include "ggml-cuda.h"
#include <vector>
#include <string>


#ifndef LLAMA_API_VERSION
#define LLAMA_API_VERSION 0
#endif

// ---- COMPAT HELPERS (do NOT expose llama_vocab outside) ----

static inline int32_t CompatTokenize(
    llama_model* Model, const std::string& text,
    std::vector<llama_token>& out,
    bool add_special = true, bool parse_special = false)
{
    // Old, stable path: use vocab-based API internally
    const llama_vocab* v = llama_model_get_vocab(Model);
    return llama_tokenize(
        v,
        text.c_str(),
        (int32_t)text.size(),
        out.data(),
        (int32_t)out.size(),
        add_special,
        parse_special);
}

static inline int32_t CompatTokenToPiece(
    llama_model* Model, llama_token tok, char* out, int32_t cap,
    bool lstrip = false, bool special = false)
{
    const llama_vocab* v = llama_model_get_vocab(Model);
    return llama_token_to_piece(v, tok, out, cap, lstrip, special);
}

static inline bool CompatIsEOS(llama_model* Model, llama_token tok)
{
    const llama_vocab* v = llama_model_get_vocab(Model);
    return tok == llama_vocab_eos(v);
}

FLlamaRunner::FLlamaRunner()
    : Model(nullptr)
    , Ctx(nullptr)
    , Sampler(nullptr)
{
}

FLlamaRunner::~FLlamaRunner()
{
    Shutdown();
}

bool FLlamaRunner::Init(const FLlamaParams& Params)
{
    P = Params;

      llama_backend_init();

    std::string ModelPathStr = TCHAR_TO_UTF8(*P.ModelPath);

    llama_model_params ModelParams = llama_model_default_params();
#ifdef GGML_USE_CUDA
    if (P.bPreferGPU && ggml_backend_cuda_get_device_count() > 0)
    {
        ModelParams.n_gpu_layers = P.NGpuLayers;
        ModelParams.n_batch = P.GPUBatchSize;
    }
    else
    {
        ModelParams.n_gpu_layers = 0;
    }
#else
    ModelParams.n_gpu_layers = 0;
#endif

      Model = llama_load_model_from_file(ModelPathStr.c_str(), ModelParams);
    if (!Model)
    {
        return false;
    }

    llama_context_params CtxParams = llama_context_default_params();
    CtxParams.n_ctx = P.ContextLength;
    CtxParams.n_threads = P.NumThreads;

      Ctx = llama_new_context_with_model(Model, CtxParams);

    return Ctx != nullptr;
}

void FLlamaRunner::Shutdown()
{
    FScopeLock _(&GenLock);
    bShuttingDown.store(true);
    bCancel.store(true);

    if (Ctx)
    {
        llama_free(Ctx);
        Ctx = nullptr;
    }
    if (Model)
    {
        llama_free_model(Model);
        Model = nullptr;
    }
      if (Sampler)
      {
          llama_sampler_free(Sampler);
          Sampler = nullptr;
      }

      llama_backend_free();
}

void FLlamaRunner::Cancel()
{
    bCancel.store(true);
}

bool FLlamaRunner::BuildSamplerChainIfNeeded(const FString& GrammarFilePath)
{
    if (Sampler)
    {
        return true;
    }

    llama_sampler_chain_params params = llama_sampler_chain_default_params();
    Sampler = llama_sampler_chain_init(params);
    if (!Sampler)
    {
        return false;
    }

    llama_sampler_chain_add(Sampler, llama_sampler_init_top_k(40));
    llama_sampler_chain_add(Sampler, llama_sampler_init_top_p(0.9f, 1));
    llama_sampler_chain_add(Sampler, llama_sampler_init_temp(P.Temperature));

    // Grammar support can be added here using GrammarFilePath if needed
    return true;
}
void FLlamaRunner::Generate(
    const FString& Prompt,
    TFunction<void(const FString&)> OnToken,
    TFunction<void(const FString&)> OnComplete,
    TFunction<void(const FString&)> OnError)
{
    // quick guard
    {
        FScopeLock _(&GenLock);
        if (bShuttingDown.load(std::memory_order_relaxed)) return;
        bCancel.store(false);
    }

    const FString PromptCopy = Prompt;

    Async(EAsyncExecution::ThreadPool, [this, PromptCopy, OnToken, OnComplete, OnError]()
        {
            auto SendTokenGT = [OnToken](const FString& Tok)
                {
                    if (!OnToken) return;
                    AsyncTask(ENamedThreads::GameThread, [Tok, OnToken] { OnToken(Tok); });
                };
            auto SendCompleteGT = [OnComplete](const FString& Out)
                {
                    if (!OnComplete) return;
                    AsyncTask(ENamedThreads::GameThread, [Out, OnComplete] { OnComplete(Out); });
                };
            auto SendErrorGT = [OnError](const FString& Err)
                {
                    if (!OnError) return;
                    AsyncTask(ENamedThreads::GameThread, [Err, OnError] { OnError(Err); });
                };

            // single-flight
            FScopeLock _(&GenLock);

            if (!Model || !Ctx) { SendErrorGT(TEXT("Model not initialized.")); return; }
            if (!BuildSamplerChainIfNeeded(P.GrammarFilePath)) { SendErrorGT(TEXT("Failed to init sampler chain.")); return; }
            ensureAlwaysMsgf(Sampler != nullptr, TEXT("Sampler null"));

            llama_model* M = Model;
            llama_context* C = Ctx;

            // --- Tokenize (no direct vocab) ---
            std::string PStr = TCHAR_TO_UTF8(*PromptCopy);
            std::vector<llama_token> tokens(std::max<size_t>(PStr.size() + 8, 32));

            int32_t n_tokens = CompatTokenize(M, PStr, tokens, /*add_special=*/true, /*parse_special=*/false);
            if (n_tokens < 0) {
                tokens.resize((size_t)(-n_tokens));
                n_tokens = CompatTokenize(M, PStr, tokens, /*add_special=*/true, /*parse_special=*/false);
            }
            if (n_tokens <= 0) { SendErrorGT(TEXT("Tokenization failed.")); return; }
            tokens.resize(n_tokens);

            // --- Prompt eval (no llama_batch_add) ---
            auto make_batch = [](int32_t cap) { return llama_batch_init(cap, /*embd=*/0, /*n_seq_max=*/1); };
            llama_batch promptBatch = make_batch((int32)tokens.size());

            for (int32 i = 0; i < (int32)tokens.size(); ++i) {
                promptBatch.token[i] = tokens[i];
                promptBatch.pos[i] = i;
                promptBatch.n_seq_id[i] = 1;
                promptBatch.seq_id[i][0] = 0;
                promptBatch.logits[i] = (i == (int32)tokens.size() - 1);
            }
            promptBatch.n_tokens = (int32)tokens.size();

            if (llama_decode(C, promptBatch) != 0) {
                llama_batch_free(promptBatch);
                SendErrorGT(TEXT("Decode failed (prompt)."));
                return;
            }
            llama_batch_free(promptBatch);

            // --- Generation ---
            llama_sampler_reset(Sampler);

            TArray<llama_token> OutTokens;
            OutTokens.Reserve(P.MaxTokens);

            FString Out;
            int32 n_past = (int32)tokens.size();

            llama_batch step = llama_batch_init(1, 0, 1); // reuse
            TArray<llama_token> StreamBuf;
            StreamBuf.Reserve(32);

            for (int i = 0; i < P.MaxTokens && !bCancel.load(); ++i)
            {
                llama_token next_id = llama_sampler_sample(Sampler, C, /*idx_seq=*/0);
                llama_sampler_accept(Sampler, next_id);

                if (CompatIsEOS(M, next_id)) break;

                OutTokens.Add(next_id);
                StreamBuf.Add(next_id);

                // feed back
                step.token[0] = next_id;
                step.pos[0] = n_past;
                step.n_seq_id[0] = 1;
                step.seq_id[0][0] = 0;
                step.logits[0] = 1;
                step.n_tokens = 1;

                if (llama_decode(C, step) != 0) {
                    SendErrorGT(TEXT("Decode failed (loop)."));
                    break;
                }
                ++n_past;

                // stream every ~8 tokens
                if (StreamBuf.Num() >= 8) {
                    std::string chunk;
                    chunk.reserve(StreamBuf.Num() * 8);
                    for (llama_token t : StreamBuf) {
                        char buf[2048];
                        int32_t n = CompatTokenToPiece(M, t, buf, (int32)sizeof(buf),
                            /*lstrip=*/false, /*special=*/false);
                        if (n < 0) n = 0;
                        if (n >= (int32)sizeof(buf)) n = (int32)sizeof(buf) - 1;
                        buf[n] = '\0';
                        chunk.append(buf, (size_t)n);
                    }
                    const FString FChunk = UTF8_TO_TCHAR(chunk.c_str());
                    Out += FChunk;
                    SendTokenGT(FChunk);
                    StreamBuf.Reset();
                }
            }

            // flush remainder
            if (StreamBuf.Num() > 0) {
                std::string chunk;
                chunk.reserve(StreamBuf.Num() * 8);
                for (llama_token t : StreamBuf) {
                    char buf[2048];
                    int32_t n = CompatTokenToPiece(M, t, buf, (int32)sizeof(buf),
                        /*lstrip=*/false, /*special=*/false);
                    if (n < 0) n = 0;
                    if (n >= (int32)sizeof(buf)) n = (int32)sizeof(buf) - 1;
                    buf[n] = '\0';
                    chunk.append(buf, (size_t)n);
                }
                const FString FChunk = UTF8_TO_TCHAR(chunk.c_str());
                Out += FChunk;
                SendTokenGT(FChunk);
            }

            llama_batch_free(step);
            llama_perf_context_print(C);

            if (bCancel.load()) SendErrorGT(TEXT("Cancelled"));
            else                SendCompleteGT(Out.TrimStartAndEnd());
        });
}

