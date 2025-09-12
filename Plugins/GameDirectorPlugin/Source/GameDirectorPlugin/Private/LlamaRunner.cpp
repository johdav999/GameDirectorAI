#pragma once
#include "LlamaRunner.h"
#include "HAL/Platform.h"

#if PLATFORM_WINDOWS
#include "Windows/AllowWindowsPlatformTypes.h"
#include <windows.h>
#include "Windows/HideWindowsPlatformTypes.h"
#endif


static bool TryLoad(const TCHAR* DllName, bool bRequired = true)
{
    void* Handle = FPlatformProcess::GetDllHandle(DllName);
    if (!Handle)
    {

        return false;
    }
    FPlatformProcess::FreeDllHandle(Handle);
    return true;
}
static void LlamaLog(ggml_log_level level, const char* msg, void*) {
    UE_LOG(LogTemp, Warning, TEXT("[llama] %hs"), msg);
}
// If you know which backend you built llama.cpp with, only check those DLLs.
// If you don't, you can check all buckets; it's cheap.
static bool PreflightLlamaDependencies()
{
    bool ok = true;

    // DirectML / D3D12 stack (typical on Windows)
    ok &= TryLoad(TEXT("DirectML.dll"), /*required*/false);  // optional on some OS builds if linked differently
    ok &= TryLoad(TEXT("d3d12.dll"),     /*required*/false);
    ok &= TryLoad(TEXT("d3d12core.dll"), /*required*/false);
    ok &= TryLoad(TEXT("dxil.dll"),      /*required*/false);
    ok &= TryLoad(TEXT("d3dcompiler_47.dll"), /*required*/false);

    // Vulkan
    ok &= TryLoad(TEXT("vulkan-1.dll"),  /*required*/false);

    // CUDA
    ok &= TryLoad(TEXT("nvcuda.dll"),    /*required*/false);
    // If you know the exact cuBLAS you linked against, probe that too, e.g.:
    // ok &= TryLoad(TEXT("cublas64_12.dll"), /*required*/false);

    // If none of the above were found, assume CPU-only or misconfigured GPU build.
    // You can add logic to insist on at least one GPU runtime when you expect it.
    return true; // return ok; if you want to hard-fail when nothing is present
}

// Optional: add a directory so Windows can find your third-party DLLs
static void PushThirdPartyDllDir()
{
    const FString Dir = FPaths::Combine(FPaths::ProjectDir(), TEXT("Binaries/ThirdParty/llama"));
    if (FPaths::DirectoryExists(Dir))
    {
        FPlatformProcess::PushDllDirectory(*Dir);
        UE_LOG(LogTemp, Display, TEXT("Added DLL search dir: %s"), *Dir);
    }
}


LlamaRunner::LlamaRunner() {}

LlamaRunner::~LlamaRunner()
{
    Shutdown();
}

bool LlamaRunner::Initiate(const FString& ModelPath, int32 ContextSize)
{

    Shutdown(); // in case re-init
    llama_log_set(LlamaLog, nullptr);
    PushThirdPartyDllDir();

    if (!PreflightLlamaDependencies())
    {
        UE_LOG(LogTemp, Error, TEXT("Llama GPU runtime dependencies not found. "
            "Install the required runtime or rebuild llama.cpp CPU-only."));
        return false;
    }

    if (HMODULE Mod = ::GetModuleHandleW(L"llama.dll")) {
        wchar_t buf[MAX_PATH]; GetModuleFileNameW(Mod, buf, MAX_PATH);
        UE_LOG(LogTemp, Display, TEXT("Loaded llama.dll from: %s"), buf);
    }
    llama_log_set([](ggml_log_level level, const char* msg, void*) {
        UE_LOG(LogTemp, Warning, TEXT("[llama] %hs"), msg);
        }, nullptr);
    llama_backend_init();

    UE_LOG(LogTemp, Display, TEXT("llama.cpp version: %hs"), llama_print_system_info());
   
    llama_model_params mparams = llama_model_default_params();
    mparams.n_gpu_layers = 0;   // 0 = CPU-only, -1 = as many as fit, or a specific N
    mparams.main_gpu = 0;
    // (you can tweak mparams here if needed)

    // Convert path to UTF-8 for llama.cpp
    FTCHARToUTF8 PathUtf8(*ModelPath);
    Model = llama_load_model_from_file(PathUtf8.Get(), mparams);
    if (!Model)
    {
        UE_LOG(LogTemp, Error, TEXT("LlamaRunner: Failed to load model: %s"), *ModelPath);
        llama_backend_free();
        return false;
    }

    llama_context_params cparams = llama_context_default_params();
    cparams.n_ctx = FMath::Max(256, ContextSize);
   
    cparams.n_threads = FPlatformMisc::NumberOfCores(); // optional: CPU threads

    Ctx = llama_new_context_with_model(Model, cparams);
    if (!Ctx)
    {
        UE_LOG(LogTemp, Error, TEXT("LlamaRunner: Failed to create context"));
        llama_free_model(Model); Model = nullptr;
        llama_backend_free();
        return false;
    }

    Vocab = llama_model_get_vocab(Model);




    if (!Vocab)
    {
        UE_LOG(LogTemp, Error, TEXT("LlamaRunner: Failed to get vocab"));
        llama_free(Ctx);   Ctx = nullptr;
        llama_free_model(Model); Model = nullptr;
        llama_backend_free();
        return false;
    }
    const int n_vocab = llama_vocab_n_tokens(Vocab);
    UE_LOG(LogTemp, Display, TEXT("Vocab size: %d"), n_vocab);
    if (n_vocab <= 0) {
        UE_LOG(LogTemp, Error, TEXT("Tokenizer/vocab not loaded (mismatch or bad model)"));
        return false;
    }


    bInitialized = true;
    return true;
}
// Simple candidate
struct Cand { llama_token id; float logit; float p; };

// Softmax on a small set (numerically stable)
static void softmax(std::vector<Cand>& cands) {
    float m = -INFINITY;
    for (auto& c : cands) m = std::max(m, c.logit);
    float sum = 0.f;
    for (auto& c : cands) { c.p = std::exp(c.logit - m); sum += c.p; }
    if (sum > 0) for (auto& c : cands) c.p /= sum;
}

// Sample from categorical
static llama_token sample_from(std::vector<Cand>& cands, std::mt19937& rng) {
    std::uniform_real_distribution<float> U(0.f, 1.f);
    float r = U(rng);
    float acc = 0.f;
    for (auto& c : cands) { acc += c.p; if (r <= acc) return c.id; }
    return cands.back().id;
}
// Assumes you have: Model (llama_model*), Ctx (llama_context*), Vocab (const llama_vocab*)
static bool IsBalancedJson(const std::string& s) {
    int brace = 0;
    bool in_str = false;
    bool esc = false;
    for (char c : s) {
        if (esc) { esc = false; continue; }
        if (c == '\\') { if (in_str) esc = true; continue; }
        if (c == '"') { in_str = !in_str; continue; }
        if (in_str) continue;
        if (c == '{') ++brace;
        else if (c == '}') --brace;
    }
    return brace <= 0 && !in_str;
}

static void apply_top_k_top_p(std::vector<Cand>& cands, int top_k, float top_p) {
    if (top_k > 0 && (int)cands.size() > top_k) {
        std::partial_sort(cands.begin(), cands.begin() + top_k, cands.end(),
            [](const Cand& a, const Cand& b) { return a.logit > b.logit; });
        cands.resize(top_k);
    }
    else {
        std::sort(cands.begin(), cands.end(),
            [](const Cand& a, const Cand& b) { return a.logit > b.logit; });
    }
    softmax(cands);
    if (top_p > 0.f && top_p < 1.f) {
        float cum = 0.f;
        size_t keep = 0;
        for (; keep < cands.size(); ++keep) {
            cum += cands[keep].p;
            if (cum >= top_p) { ++keep; break; }
        }
        if (keep > 0 && keep < cands.size()) cands.resize(keep);
        // renorm after truncation
        float s = 0.f; for (auto& c : cands) s += c.p;
        if (s > 0) for (auto& c : cands) c.p /= s;
    }
}

std::vector<llama_token> LlamaRunner::TokenizePrompt(const FString& Prompt) const {
    std::vector<llama_token> tokens;

    if (!Model || !Ctx || !Vocab) {
        UE_LOG(LogTemp, Error, TEXT("TokenizePrompt: llama not initialized"));
        return tokens;
    }

    // Convert Unreal FString → UTF-8 C string
    FTCHARToUTF8 PromptUtf8(*Prompt);
    const char* user_c = PromptUtf8.Get();

    // Wrap as a chat message
    llama_chat_message msgs[1];
    msgs[0].role = "user";
    msgs[0].content = user_c;

    // --- Step 1: Ask template how many bytes needed
    int32_t templ_bytes_needed = llama_chat_apply_template(
        nullptr, // use model’s default template
        msgs,
        1,
        true,    // add_assistant tag
        nullptr,
        0
    );
    if (templ_bytes_needed <= 0) templ_bytes_needed = -templ_bytes_needed;
    if (templ_bytes_needed <= 0) {
        UE_LOG(LogTemp, Error, TEXT("TokenizePrompt: apply_template size query failed"));
        return tokens;
    }

    // --- Step 2: Render templated prompt into buffer
    std::string templ_buf((size_t)templ_bytes_needed, '\0');
    int32_t templ_written = llama_chat_apply_template(
        nullptr,
        msgs,
        1,
        true,
        templ_buf.data(),
        templ_bytes_needed
    );
    if (templ_written <= 0) {
        UE_LOG(LogTemp, Error, TEXT("TokenizePrompt: apply_template failed (%d)"), templ_written);
        return tokens;
    }

    // --- Step 3: Tokenize the buffer
    int32_t tok_needed = llama_tokenize(
        Vocab,
        templ_buf.data(),
        templ_written,
        nullptr,
        0,
        false,   // don’t add extra BOS/EOS
        true     // parse <|special|> tokens
    );
    if (tok_needed <= 0) tok_needed = -tok_needed;
    if (tok_needed <= 0) {
        UE_LOG(LogTemp, Error, TEXT("TokenizePrompt: tokenize size query failed"));
        return tokens;
    }

    tokens.resize(tok_needed);
    int32_t tok_written = llama_tokenize(
        Vocab,
        templ_buf.data(),
        templ_written,
        tokens.data(),
        tok_needed,
        false,
        true
    );
    if (tok_written <= 0) {
        UE_LOG(LogTemp, Error, TEXT("TokenizePrompt: tokenize failed (%d)"), tok_written);
        tokens.clear();
    }
    else {
        tokens.resize(tok_written);
    }

    return tokens;
}
// Minimal, targeted system directive: "JSON only"
//static const char* kJsonSystem =
//"You are a game director.\n"
//"OUTPUT RULES:\n"
//"- Answer in STRICT JSON only (no extra text, no code fences).\n"
//"- Use keys exactly: intent, reason, tool_calls, dialogue, quest_patch.\n"
//"-tool_calls name can be either QuestPatch,WeatherControl or SpawnEncounter\n"
//"- Keep text short and actionable.\n";

FString LlamaRunner::GenerateJSON(const FString& Prompt, int maxNew, int top_k, float top_p, float temp) {
    if (!Ctx || !Vocab || !Model) {
        UE_LOG(LogTemp, Error, TEXT("LlamaRunner not initialized"));
        return "{}";
    }

    // 0) Nudge model toward JSON-only
    UE_LOG(LogTemp, Error, TEXT("0) Nudge model toward JSON-only"));
  //  static const char* kSystemJSON = "You are a game director planner. OUTPUT RULES: - Reply in STRICT JSON only (no prose, no markdown). - Use exactly these keys and shapes: { \"intent\": \"<one of: offer_quest, warn, give_clue, continue, escalate, deescalate>\", \"reason\": \"<short rationale>\", \"tool_calls\": [ { \"name\": \"<QuestPatch|SpawnEncounter|SetFlag|GiveItem>\", \"args\": { /* pure-JSON arguments for the tool */ } } ], \"dialogue\": { \"speaker\": \"<NPC name like GuardCaptain>\", \"emote\": \"<brief cue like urgent, wary, calm>\", \"lines\": [\"<very short line>\", \"...\"] }, \"quest_patch\": { \"questId\": \"<string id or omit if none>\", \"addObjectives\": [ { \"id\": \"<string>\", \"desc\": \"<short>\" } ] } } - Do NOT include any keys other than the five above. If a section is not needed, use an empty array [] or an empty object {} as appropriate. - Keep text concise and actionable. EXAMPLE (style and shape only): {\"intent\":\"offer_quest\",\"reason\":\"Player arrived; militia needs coverage at west ruins.\",\"tool_calls\":[{\"name\":\"QuestPatch\",\"args\":{\"questId\":\"defense_west\",\"addObjectives\":[{\"id\":\"guard_ruins\",\"desc\":\"Move to the west ruins and hold the line\"}]}}],\"dialogue\":{\"speaker\":\"GuardCaptain\",\"emote\":\"urgent\",\"lines\":[\"We’re stretched thin—cover the west ruins, now!\"]},\"quest_patch\":{\"questId\":\"defense_west\",\"addObjectives\":[{\"id\":\"guard_ruins\",\"desc\":\"Move to the west ruins and hold the line\"}]} } If the player is in CitySquare with GuardCaptain present and world.risk=medium, prefer an offer_quest or warn intent by default, and prefer tool_calls that update quests (QuestPatch) with minimal, necessary changes only.";
    static const char* kSystemJSON = "You are a game director planner. OUTPUT RULES: - Reply in STRICT JSON only (no prose, no markdown).";
    // 1) Chat messages (system + user)
    llama_chat_message msgs[2] = {
        { "system", kSystemJSON },
        { "user",   TCHAR_TO_UTF8(*Prompt) }
    };

    // 2) Apply chat template (size)
    UE_LOG(LogTemp, Error, TEXT("2) Apply chat template (size)"));
    int32_t templ_bytes_needed = llama_chat_apply_template(nullptr, msgs, 2, /*add_assistant*/ true, nullptr, 0);
    if (templ_bytes_needed <= 0) {
        UE_LOG(LogTemp, Error, TEXT("apply_template(size) failed (%d)"), templ_bytes_needed);
        return "{}";
    }

    // 3) Render template
    UE_LOG(LogTemp, Error, TEXT("3) Render template"));
    std::string templ((size_t)templ_bytes_needed, '\0');
    int32_t templ_written = llama_chat_apply_template(nullptr, msgs, 2, /*add_assistant*/ true, templ.data(), templ_bytes_needed);
    if (templ_written <= 0 || templ_written > templ_bytes_needed) {
        UE_LOG(LogTemp, Error, TEXT("apply_template(write) failed (%d)"), templ_written);
        return "{}";
    }

    // 4) Tokenize (size + write)
    UE_LOG(LogTemp, Error, TEXT("4) Tokenize"));
    int32_t tok_needed = llama_tokenize(Vocab, templ.data(), templ_written, nullptr, 0, /*add_special*/ true, /*parse_special*/ true);
    if (tok_needed < 0) tok_needed = -tok_needed;
    if (tok_needed <= 0) {
        UE_LOG(LogTemp, Error, TEXT("tokenize(size) failed (%d)"), tok_needed);
        return "{}";
    }

    std::vector<llama_token> tokens((size_t)tok_needed);
    int32_t tok_count = llama_tokenize(Vocab, templ.data(), templ_written, tokens.data(), (int32_t)tokens.size(), /*add_special*/ true, /*parse_special*/ true);
    if (tok_count < 0) {
        UE_LOG(LogTemp, Error, TEXT("tokenize(write) failed (%d)"), tok_count);
        return "{}";
    }

    // 5) Decode prompt (logits only on last token)
    UE_LOG(LogTemp, Error, TEXT("5) Decode prompt"));
    llama_batch prompt_batch = llama_batch_init(tok_count, /*embd*/ 0, /*n_seq_max*/ 1);
    prompt_batch.n_tokens = tok_count;
    for (int i = 0; i < tok_count; ++i) {
        prompt_batch.token[i] = tokens[i];
        prompt_batch.pos[i] = i;
        prompt_batch.n_seq_id[i] = 1;
        prompt_batch.seq_id[i][0] = 0;
        prompt_batch.logits[i] = (i == tok_count - 1) ? 1 : 0;
    }


    // LlamaRunner.cpp
    int dec;
    {
        FScopeLock Lock(&DecodeMutex);
        dec = llama_decode(Ctx, prompt_batch);
    }

    if (dec < 0) {
        UE_LOG(LogTemp, Error, TEXT("llama_decode(prompt) failed (%d)"), dec);
        llama_batch_free(prompt_batch);
        return "{}";
    }

    // 6) Manual sampling setup
    UE_LOG(LogTemp, Error, TEXT("6) Manual sampling setup"));
    const int n_vocab = llama_vocab_n_tokens(Vocab);
    std::vector<float> work_logits((size_t)n_vocab);
    std::vector<int>   idx((size_t)n_vocab);
    std::iota(idx.begin(), idx.end(), 0);

    std::mt19937 rng((uint32_t)(llama_time_us() & 0xFFFFFFFFu));
    std::uniform_real_distribution<float> uni(0.0f, 1.0f);

    auto greedy_pick = [&](const float* l)->int {
        int best = 0;
        float m = l[0];
        for (int i = 1; i < n_vocab; ++i) if (l[i] > m) { m = l[i]; best = i; }
        return best;
        };

    auto sample_topk_topp_temp = [&](const float* logits)->int {
        // Copy logits
        std::memcpy(work_logits.data(), logits, sizeof(float) * (size_t)n_vocab);

        // Temperature
        if (temp > 0.0f) {
            const float invT = 1.0f / temp;
            for (int i = 0; i < n_vocab; ++i) work_logits[i] *= invT;
        }

        // Top-k
        int K = (top_k > 0) ? std::min(top_k, n_vocab) : n_vocab;
        std::nth_element(idx.begin(), idx.begin() + K, idx.end(),
            [&](int a, int b) { return work_logits[a] > work_logits[b]; });
        idx.resize((size_t)K);

        // Softmax over K (stable)
        float maxl = -FLT_MAX;
        for (int id : idx) maxl = std::max(maxl, work_logits[id]);
        float sum = 0.0f;
        for (int id : idx) { work_logits[id] = std::exp(work_logits[id] - maxl); sum += work_logits[id]; }
        if (sum <= 0.0f) return idx[0];
        for (int id : idx) work_logits[id] /= sum;

        // Sort by prob desc
        std::sort(idx.begin(), idx.end(), [&](int a, int b) { return work_logits[a] > work_logits[b]; });

        // Top-p
        if (top_p > 0.0f && top_p < 1.0f) {
            float cum = 0.0f;
            size_t cut = idx.size();
            for (size_t j = 0; j < idx.size(); ++j) {
                cum += work_logits[idx[j]];
                if (cum >= top_p) { cut = j + 1; break; }
            }
            if (cut < idx.size()) idx.resize(cut);
        }

        // Sample
        float r = uni(rng);
        float acc = 0.0f;
        int choice = idx.back();
        for (int id : idx) { acc += work_logits[id]; if (r <= acc) { choice = id; break; } }
        // restore idx size for next step
        idx.resize((size_t)n_vocab);
        std::iota(idx.begin(), idx.end(), 0);
        return choice;
        };

    // 7) Generate loop
    UE_LOG(LogTemp, Error, TEXT("7) Generate loop"));
    std::vector<llama_token> out_tokens;
    out_tokens.reserve(maxNew);

    llama_batch step = llama_batch_init(/*capacity*/ 1, /*embd*/ 0, /*n_seq_max*/ 1);
    int cur_pos = tok_count;

    // Stream buffer + “is JSON closed?” detector
    std::string stream;
    stream.reserve(1024);
    auto json_done = [&](const std::string& s) -> bool {
        int depth = 0;
        bool in_q = false, escp = false, seen_open = false;
        for (unsigned char ch : s) {
            if (escp) { escp = false; continue; }
            if (ch == '\\') { escp = true; continue; }
            if (ch == '"') { in_q = !in_q; continue; }
            if (in_q) continue;
            if (ch == '{') {
                ++depth;
                seen_open = true;
            }
            else if (ch == '}') {
                if (depth > 0) --depth;
                if (seen_open && depth == 0) {
                    // log before returning
                    UE_LOG(LogTemp, Display, TEXT("Exit Auto: %s"), *FString(s.c_str()));
                    return true;
                }
            }
        }
        return false;
        };

    for (int i = 0; i < maxNew; ++i) {
        // Use last logits
        const float* logits = llama_get_logits_ith(Ctx, -1);
        if (!logits) {
            UE_LOG(LogTemp, Error, TEXT("null logits pointer from llama_get_logits_ith"));
            break;
        }

        // Pick token
        int id = (temp <= 0.0f && top_k <= 1) ? greedy_pick(logits)
            : sample_topk_topp_temp(logits);
        if (id < 0 || id >= n_vocab) {
            UE_LOG(LogTemp, Warning, TEXT("sampled invalid token id=%d, stopping"), id);
            break;
        }
        if (llama_vocab_is_eog(Vocab, (llama_token)id)) break;

        // Append piece to stream (for JSON stop check)
        {
            char piece[256];
            int pn = llama_token_to_piece(Vocab, (llama_token)id, piece, sizeof(piece), 0, /*special*/ false);
            if (pn > 0) stream.append(piece, piece + pn);
            FString LastPiece(piece);
        }



        out_tokens.push_back((llama_token)id);
        if (json_done(stream)) break;

        // Feed back
        step.n_tokens = 1;
        step.token[0] = (llama_token)id;
        step.pos[0] = cur_pos++;
        step.n_seq_id[0] = 1;
        step.seq_id[0][0] = 0;
        step.logits[0] = 1;

        if (llama_decode(Ctx, step) < 0) break;
    }

    // 8) Prefer stream (already text)
    UE_LOG(LogTemp, Error, TEXT("8) Prefer stream "));
    std::string out_str = stream;
    if (out_str.empty() && !out_tokens.empty()) {
        out_str.assign(out_tokens.size() * 8, '\0');
        int32_t w = llama_detokenize(Vocab, out_tokens.data(), (int32_t)out_tokens.size(),
            out_str.data(), (int32_t)out_str.size(),
            /*remove_special*/ true, /*unparse_special*/ false);
        if (w > 0) out_str.resize((size_t)w); else out_str.clear();
    }

    // 9) Cleanup
    llama_batch_free(step);
    llama_batch_free(prompt_batch);

    if (out_str.empty()) return "{}";
    return FString(out_str.c_str());
}
void LlamaRunner::Shutdown()
{
    if (Ctx) { llama_free(Ctx);   Ctx = nullptr; }
    if (Model) { llama_free_model(Model); Model = nullptr; }
    Vocab = nullptr;

    if (bInitialized)
    {
        llama_backend_free();
        bInitialized = false;
    }
}