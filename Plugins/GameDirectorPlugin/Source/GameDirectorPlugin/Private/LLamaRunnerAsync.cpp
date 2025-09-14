#include "LLamaRunnerAsync.h"
#include "Misc/Paths.h"
#include "HAL/PlatformProcess.h"

#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

#if PLATFORM_WINDOWS
#include "Windows/AllowWindowsPlatformTypes.h"
#include <windows.h>
#include "Windows/HideWindowsPlatformTypes.h"
#endif

// If you prefer to include "llama.h" here, do it now:


DECLARE_LOG_CATEGORY_EXTERN(LogGameAI, Log, All);

DEFINE_LOG_CATEGORY(LogGameAI);


// Pseudocode – adapt to your init flow

static bool ExtractStrictJSONObject(const FString& In, FString& Out, FString* OutErr = nullptr)
{
    const TCHAR* S = *In;
    const int32 N = In.Len();

    bool bInStr = false;
    bool bEsc = false;
    int32 depth = 0;
    int32 start = -1;

    for (int32 i = 0; i < N; ++i)
    {
        const TCHAR c = S[i];

        if (bEsc) { bEsc = false; continue; }

        if (c == TEXT('\\'))
        {
            bEsc = true;
            continue;
        }

        if (c == TEXT('"'))
        {
            bInStr = !bInStr;
            continue;
        }

        if (bInStr) continue;

        if (c == TEXT('{'))
        {
            if (depth == 0) start = i;
            depth++;
        }
        else if (c == TEXT('}'))
        {
            if (depth > 0) depth--;
            if (depth == 0 && start >= 0)
            {
                // Found a complete object
                Out = In.Mid(start, i - start + 1);

                // Ensure there is no non-whitespace, non-control trailing text after the object
                for (int32 j = i + 1; j < N; ++j)
                {
                    if (!FChar::IsWhitespace(S[j]))
                    {
                        // Allow a trailing assistant stop token like "</s>" or special junk only if whitespace
                        if (OutErr) *OutErr = TEXT("Trailing non-whitespace after JSON object.");
                        // Still return the object; caller may choose to accept strictly the object.
                        return true;
                    }
                }
                return true;
            }
        }
    }

    if (OutErr) *OutErr = (depth > 0) ? TEXT("Unclosed JSON object.") : TEXT("No JSON object found.");
    return false;
}

// Small helpers
static bool RequireString(const TSharedPtr<FJsonObject>& Obj, const FString& Key, FString& OutErr)
{
    FString Tmp;
    if (!Obj.IsValid() || !Obj->TryGetStringField(Key, Tmp) || Tmp.TrimStartAndEnd().IsEmpty())
    {
        OutErr = FString::Printf(TEXT("Missing or empty string field '%s'."), *Key);
        return false;
    }
    return true;
}

static bool OptionalStringNonEmpty(const TSharedPtr<FJsonObject>& Obj, const FString& Key)
{
    FString Tmp;
    return Obj.IsValid() && Obj->TryGetStringField(Key, Tmp) && !Tmp.TrimStartAndEnd().IsEmpty();
}

static bool IsInSet(const FString& Value, const TSet<FString>& Allowed)
{
    return Allowed.Contains(Value);
}


// Validates the specific schema you expect from the director.
// Returns true only if:
//  - A strict top-level JSON object was found and parsed
//  - Mandatory fields have correct types and allowed values
//  - Optional sections (dialogue, quest_patch, tool_calls) have correct shapes if present
// On success: OutCleanedJSON = the extracted object (no outer noise). On failure: OutError explains why.
bool LLamaRunnerAsync::IsValidDirectorJSON(const FString& RawText, /*out*/ FString& OutCleanedJSON, /*out*/ FString& OutError) const
{
    OutCleanedJSON.Empty();
    OutError.Empty();

    FString Err;
    if (!ExtractStrictJSONObject(RawText, OutCleanedJSON, &Err))
    {
        OutError = Err.IsEmpty() ? TEXT("Failed to extract JSON object.") : Err;
        return false;
    }

    // Quick sanity: reject trivially tiny objects like {"x":1}
    if (OutCleanedJSON.Len() < 20)
    {
        OutError = TEXT("JSON object too short/minimal to be valid.");
        return false;
    }

    // Parse JSON
    TSharedPtr<FJsonObject> Root;
    {
        TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(OutCleanedJSON);
        if (!FJsonSerializer::Deserialize(Reader, Root) || !Root.IsValid())
        {
            OutError = TEXT("JSON parse failed (malformed).");
            return false;
        }
    }

    // ----- Schema checks -----
    // Required: intent, reason
    FString Intent;
    if (!Root->TryGetStringField(TEXT("intent"), Intent))
    {
        OutError = TEXT("Missing 'intent' (string).");
        return false;
    }

    Intent = Intent.TrimStartAndEnd();
    static const TSet<FString> kAllowedIntents = {
        TEXT("offer_quest"), TEXT("warn"), TEXT("give_clue"),
        TEXT("continue"), TEXT("escalate"), TEXT("deescalate"),
        TEXT("spawn_event") // keep if you actually use this in your system JSON above
    };
    if (!IsInSet(Intent, kAllowedIntents))
    {
        OutError = FString::Printf(TEXT("Invalid 'intent': %s"), *Intent);
        return false;
    }

    if (!RequireString(Root, TEXT("reason"), OutError)) return false;

    // tool_calls: optional but must be an array of {name, args:{}}
    if (Root->HasTypedField<EJson::Array>(TEXT("tool_calls")))
    {
        const TArray<TSharedPtr<FJsonValue>>* CallsPtr = nullptr;
        if (!Root->TryGetArrayField(TEXT("tool_calls"), CallsPtr) || !CallsPtr)
        {
            OutError = TEXT("'tool_calls' present but not an array.");
            return false;
        }

        static const TSet<FString> kAllowedTools = {
            TEXT("QuestPatch"), TEXT("SpawnEncounter"), TEXT("SetFlag"),
            TEXT("GiveItem"), TEXT("WeatherControl"), TEXT("ForeshadowEvent"),
            TEXT("TensionMeterAdjust")
        };

        for (int32 i = 0; i < CallsPtr->Num(); ++i)
        {
            const TSharedPtr<FJsonObject> Call = (*CallsPtr)[i]->AsObject();
            if (!Call.IsValid())
            {
                OutError = FString::Printf(TEXT("tool_calls[%d] is not an object."), i);
                return false;
            }

            FString Name;
            if (!Call->TryGetStringField(TEXT("name"), Name))
            {
                OutError = FString::Printf(TEXT("tool_calls[%d].name missing."), i);
                return false;
            }
            if (!IsInSet(Name, kAllowedTools))
            {
                OutError = FString::Printf(TEXT("tool_calls[%d].name '%s' not allowed."), i, *Name);
                return false;
            }

            const TSharedPtr<FJsonObject>* ArgsObj = nullptr;
            if (!Call->TryGetObjectField(TEXT("args"), ArgsObj) || !ArgsObj || !ArgsObj->IsValid())
            {
                OutError = FString::Printf(TEXT("tool_calls[%d].args must be an object."), i);
                return false;
            }
        }
    }
    else if (!Root->HasField(TEXT("tool_calls")))
    {
        // If your policy demands at least one tool call for most cases, enforce it here:
        // OutError = TEXT("Missing 'tool_calls' array.");
        // return false;
    }

    // dialogue: optional; if present, check shape
    if (Root->HasTypedField<EJson::Object>(TEXT("dialogue")))
    {
        const TSharedPtr<FJsonObject>* Dlg = nullptr;
        if (!Root->TryGetObjectField(TEXT("dialogue"), Dlg) || !Dlg || !Dlg->IsValid())
        {
            OutError = TEXT("'dialogue' must be an object.");
            return false;
        }

        FString Speaker;
        if (!(*Dlg)->TryGetStringField(TEXT("speaker"), Speaker) || Speaker.TrimStartAndEnd().IsEmpty())
        {
            OutError = TEXT("'dialogue.speaker' is required and must be non-empty.");
            return false;
        }

        // emote optional but if present must be non-empty
        if ((*Dlg)->HasField(TEXT("emote")) && !OptionalStringNonEmpty(*Dlg, TEXT("emote")))
        {
            OutError = TEXT("'dialogue.emote' must be a non-empty string if present.");
            return false;
        }

        // lines array of strings (at least one short line)
        const TArray<TSharedPtr<FJsonValue>>* Lines = nullptr;
        if (!(*Dlg)->TryGetArrayField(TEXT("lines"), Lines) || !Lines || Lines->Num() == 0)
        {
            OutError = TEXT("'dialogue.lines' must be a non-empty array of strings.");
            return false;
        }
        for (int32 i = 0; i < Lines->Num(); ++i)
        {
            FString L;
            if (!(*Lines)[i]->TryGetString(L) || L.TrimStartAndEnd().IsEmpty())
            {
                OutError = FString::Printf(TEXT("'dialogue.lines[%d]' must be a non-empty string."), i);
                return false;
            }
        }
    }

    // quest_patch: optional; if present check shape
    if (Root->HasTypedField<EJson::Object>(TEXT("quest_patch")))
    {
        const TSharedPtr<FJsonObject>* QP = nullptr;
        if (!Root->TryGetObjectField(TEXT("quest_patch"), QP) || !QP || !QP->IsValid())
        {
            OutError = TEXT("'quest_patch' must be an object.");
            return false;
        }

        // If present, allow empty {}, OR validate fields if provided
        if ((*QP)->HasField(TEXT("questId")) || (*QP)->HasField(TEXT("addObjectives")))
        {
            if (!RequireString(*QP, TEXT("questId"), OutError)) return false;

            if ((*QP)->HasTypedField<EJson::Array>(TEXT("addObjectives")))
            {
                const TArray<TSharedPtr<FJsonValue>>* Objs = nullptr;
                if (!(*QP)->TryGetArrayField(TEXT("addObjectives"), Objs) || !Objs)
                {
                    OutError = TEXT("'quest_patch.addObjectives' must be an array.");
                    return false;
                }
                for (int32 i = 0; i < Objs->Num(); ++i)
                {
                    const TSharedPtr<FJsonObject> O = (*Objs)[i]->AsObject();
                    if (!O.IsValid())
                    {
                        OutError = FString::Printf(TEXT("'quest_patch.addObjectives[%d]' must be an object."), i);
                        return false;
                    }
                    if (!RequireString(O, TEXT("id"), OutError)) { OutError += TEXT(" (in addObjectives)"); return false; }
                    if (!RequireString(O, TEXT("desc"), OutError)) { OutError += TEXT(" (in addObjectives)"); return false; }
                }
            }
        }
    }

    // Guard against common "reasoning leak" stubs
    // e.g. {"toolscall":"spawn_event"} or {"name":"WeatherControl"} alone
    {
        const TArray<FString> RequiredTopKeys = { TEXT("intent"), TEXT("reason"), TEXT("tool_calls"), TEXT("dialogue"), TEXT("quest_patch") };
        int PresentKeyCount = 0;
        for (const FString& K : RequiredTopKeys)
        {
            if (Root->HasField(K)) ++PresentKeyCount;
        }
        if (PresentKeyCount < 3) // tune threshold; 3+ makes tiny stubs very unlikely
        {
            OutError = TEXT("JSON too skeletal: missing several required sections.");
            return false;
        }
    }

    return true;
}





// ---------- Local helpers (keep static in this TU) ----------
static bool TryLoad(const TCHAR* DllName, bool bRequired = true)
{
    void* Handle = FPlatformProcess::GetDllHandle(DllName);
    if (!Handle) return !bRequired;
    FPlatformProcess::FreeDllHandle(Handle);
    return true;
}

static void LlamaLog(ggml_log_level /*level*/, const char* msg, void*)
{
    UE_LOG(LogTemp, Warning, TEXT("[llama] %hs"), msg);
}

static bool PreflightLlamaDependencies()
{
    bool ok = true;
    ok &= TryLoad(TEXT("DirectML.dll"), false);
    ok &= TryLoad(TEXT("d3d12.dll"), false);
    ok &= TryLoad(TEXT("d3d12core.dll"), false);
    ok &= TryLoad(TEXT("dxil.dll"), false);
    ok &= TryLoad(TEXT("d3dcompiler_47.dll"), false);
    ok &= TryLoad(TEXT("vulkan-1.dll"), false);
    ok &= TryLoad(TEXT("nvcuda.dll"), false);
    return true; // or 'ok' if you want to hard-fail
}

static void PushThirdPartyDllDir()
{
    const FString Dir = FPaths::Combine(FPaths::ProjectDir(), TEXT("Binaries/ThirdParty/llama"));
    if (FPaths::DirectoryExists(Dir))
    {
        FPlatformProcess::PushDllDirectory(*Dir);
        UE_LOG(LogTemp, Display, TEXT("Added DLL search dir: %s"), *Dir);
    }
}

// ---------- LLamaRunnerAsync ----------
LLamaRunnerAsync::LLamaRunnerAsync() {}
LLamaRunnerAsync::~LLamaRunnerAsync()
{
    Shutdown();
}

// ---------- Worker implementation (note full qualification) ----------
LLamaRunnerAsync::FWorker::FWorker(LLamaRunnerAsync* InOwner)
    : Owner(InOwner)
{
    WakeEvent = FPlatformProcess::GetSynchEventFromPool(false);
}

LLamaRunnerAsync::FWorker::~FWorker()
{
    Shutdown();
    if (WakeEvent)
    {
        FPlatformProcess::ReturnSynchEventToPool(WakeEvent);
        WakeEvent = nullptr;
    }
}

bool LLamaRunnerAsync::FWorker::Init()
{
    bStop = false;
    return true;
}

uint32 LLamaRunnerAsync::FWorker::Run()
{
    while (!bStop)
    {
        if (WakeEvent) WakeEvent->Wait();

        FJob Job;
        while (!bStop && Queue.Dequeue(Job))
        {
            FString Output;
            if (Owner && Owner->IsInitialized())
            {
                const int   top_k = 20;
                const float top_p = 0.8f;
                const float temp = 0.20f;

                // Call synchronous generation on the worker thread
                Output = Owner->GenerateJSON(Job.Prompt, /*max_new*/800, /*top_k*/20, /*top_p*/0.8f, /*temp*/0.20f,Job.Intent);
            }

            if (Job.OnDone)
            {
                AsyncTask(ENamedThreads::GameThread,
                    [OnDone = MoveTemp(Job.OnDone), Output = MoveTemp(Output)]() mutable
                    {
                        OnDone(Output);
                    });
            }
        }
    }
    return 0;
}

void LLamaRunnerAsync::FWorker::Stop()
{
    bStop = true;
    if (WakeEvent) WakeEvent->Trigger();
}

void LLamaRunnerAsync::FWorker::Enqueue(FJob&& Job)
{
    Queue.Enqueue(MoveTemp(Job));
    if (WakeEvent) WakeEvent->Trigger();
}

void LLamaRunnerAsync::FWorker::Shutdown()
{
    if (!bStop) Stop();
}

// ---------- Runner thread mgmt ----------
void LLamaRunnerAsync::StartWorkerIfNeeded()
{
    if (!Worker)
        Worker = MakeUnique<FWorker>(this);
    if (!WorkerThread)
        WorkerThread.Reset(FRunnableThread::Create(Worker.Get(), TEXT("LlamaRunnerWorker"), 0, TPri_BelowNormal));
}

// ---------- Init / Shutdown ----------
bool LLamaRunnerAsync::Initiate(const FString& ModelPath, int32 ContextSize)
{
    Shutdown(); // in case re-init

    llama_log_set(LlamaLog, nullptr);
    PushThirdPartyDllDir();

    if (!PreflightLlamaDependencies())
    {
        UE_LOG(LogTemp, Error, TEXT("GPU runtime deps not found"));
        return false;
    }

#if PLATFORM_WINDOWS
    if (HMODULE Mod = ::GetModuleHandleW(L"llama.dll"))
    {
        wchar_t buf[MAX_PATH];
        GetModuleFileNameW(Mod, buf, MAX_PATH);
        UE_LOG(LogTemp, Display, TEXT("Loaded llama.dll from: %s"), buf);
    }
#endif

    llama_log_set([](ggml_log_level, const char* msg, void*)
        {
            UE_LOG(LogTemp, Warning, TEXT("[llama] %hs"), msg);
        }, nullptr);

    llama_backend_init();
    UE_LOG(LogTemp, Display, TEXT("llama.cpp: %hs"), llama_print_system_info());

    // --- Model params (adjust as needed) ---
    llama_model_params mparams = llama_model_default_params();
    mparams.n_gpu_layers = -1;; // CPU-only for now
    mparams.main_gpu = 0;

    // --- Load model ---
    FTCHARToUTF8 PathUtf8(*ModelPath);
    Model = llama_model_load_from_file(PathUtf8.Get(), mparams);
    if (!Model)
    {
        UE_LOG(LogTemp, Error, TEXT("Failed to load model: %s"), *ModelPath);
        llama_backend_free();
        return false;
    }

    // --- Context params ---
   cparams = llama_context_default_params();
    cparams.n_ctx = FMath::Max(256, ContextSize);
    cparams.n_threads = FPlatformMisc::NumberOfCores();

    // --- Create context from model ---
    Ctx = llama_init_from_model(Model, cparams);
    if (!Ctx)
    {
        UE_LOG(LogTemp, Error, TEXT("Failed to create context"));
        llama_model_free(Model); Model = nullptr;
        llama_backend_free();
        return false;
    }

    // --- Grab vocab and sanity-check ---
    Vocab = llama_model_get_vocab(Model);
    if (!Vocab)
    {
        UE_LOG(LogTemp, Error, TEXT("Failed to get vocab"));
        llama_free(Ctx);           Ctx = nullptr;
        llama_model_free(Model);   Model = nullptr;
        llama_backend_free();
        return false;
    }

    const int n_vocab = llama_vocab_n_tokens(Vocab);
    if (n_vocab <= 0)
    {
        UE_LOG(LogTemp, Error, TEXT("Bad tokenizer/vocab"));
        llama_free(Ctx);           Ctx = nullptr;
        llama_model_free(Model);   Model = nullptr;
        llama_backend_free();
        return false;
    }

    bInitialized = true;
    StartWorkerIfNeeded();
    return true;
}
void LLamaRunnerAsync::Shutdown()
{
    // stop worker first
    if (Worker) Worker->Shutdown();
    if (WorkerThread)
    {
        WorkerThread->Kill(true);
        WorkerThread.Reset();
    }
    Worker.Reset();

    if (Ctx) { llama_free(Ctx);   Ctx = nullptr; }
    if (Model) { llama_free_model(Model); Model = nullptr; }
    Vocab = nullptr;

    if (bInitialized)
    {
        llama_backend_free();
        bInitialized = false;
    }
}

// ---------- Async enqueue ----------
void LLamaRunnerAsync::GenerateJSONAsync(const FString& Prompt, TFunction<void(FString)> OnDone,FString Intent)
{
    if (!IsInitialized() || !Worker)
    {
        AsyncTask(ENamedThreads::GameThread, [OnDone = MoveTemp(OnDone)]() mutable {
            if (OnDone) OnDone(TEXT("{}"));
            });
        return;
    }

    FJob Job;
    Job.Prompt = Prompt;
    Job.OnDone = MoveTemp(OnDone);
    Job.Intent = Intent;
    Worker->Enqueue(MoveTemp(Job));
}
void  LLamaRunnerAsync::ResetContext() {
    FScopeLock _(&DecodeMutex);
    if (Ctx) { llama_free(Ctx); Ctx = nullptr; }
    // Recreate with the same params/model you used in Initiate()
    Ctx = llama_new_context_with_model(Model, cparams);
}

// ---------- Synchronous GenerateJSON (PUT YOUR EXISTING BODY HERE) ----------
FString LLamaRunnerAsync::GenerateJSON(const FString& Prompt, int max_new, int top_k, float top_p, float temp,FString Intent)
{

    // Add near the top of GenerateJSON, after you include Json headers and have IsValidDirectorJSON available.

    enum class EJsonProbe { None, ClosedValid, ClosedInvalid };

    // Returns:
    //  - ClosedValid    -> first balanced object is valid per IsValidDirectorJSON
    //  - ClosedInvalid  -> a balanced object exists but fails schema; keep generating
    //  - None           -> no balanced top-level object yet
    auto CheckJsonClosure = [&](const std::string& s) -> EJsonProbe
        {
            int depth = 0;
            bool in_q = false, escp = false, seen_open = false;
            int start = -1;

            for (size_t i = 0; i < s.size(); ++i)
            {
                unsigned char ch = (unsigned char)s[i];

                if (escp) { escp = false; continue; }
                if (ch == '\\') { escp = true; continue; }
                if (ch == '"') { in_q = !in_q; continue; }
                if (in_q) continue;

                if (ch == '{')
                {
                    if (depth == 0) { start = (int)i; seen_open = true; }
                    ++depth;
                }
                else if (ch == '}')
                {
                    if (depth > 0) --depth;

                    if (seen_open && depth == 0 && start >= 0)
                    {
                        // Candidate complete object [start..i]
                        FString Candidate(UTF8_TO_TCHAR(std::string(s.begin() + start, s.begin() + i + 1).c_str()));
                        FString Clean, Err;

                        if (IsValidDirectorJSON(Candidate, /*out*/Clean, /*out*/Err))
                        {
                            UE_LOG(LogGameAI, Display, TEXT("Exit (valid JSON): %s"), *Clean);
                            return EJsonProbe::ClosedValid;
                        }
                        else
                        {
                            UE_LOG(LogGameAI, Display, TEXT("Balanced but invalid JSON, continuing. Error: %s\nCandidate:\n%s"),
                                *Err, *Candidate);
                            return EJsonProbe::ClosedInvalid; // keep generating
                        }
                    }
                }
            }
            return EJsonProbe::None;
        };

    {
        FScopeLock Lock(&DecodeMutex);      // same mutex you use for decode
        ResetContext();
        // wipe all past tokens
        // (optional) llama_reset_timings(Ctx);
    }

    if (!Ctx || !Vocab || !Model) {
        UE_LOG(LogGameAI, Display, TEXT("LlamaRunner not initialized"));
        return "{}";
    }

    // 0) Nudge model toward JSON-only
    UE_LOG(LogGameAI, Display, TEXT("0) Nudge model toward JSON-only"));
    static const char* kSystemJSONTrigger2 = R"(You are a game director planner. OUTPUT RULES: - STRICT JSON only; output must start with '{' and end with '}'. - Use exactly these keys: {"intent":"<intent_value>","reason":"<short>","tool_calls":[{"name":"<QuestPatch|SpawnEncounter|SetFlag|GiveItem|WeatherControl|ForeshadowEvent|TensionMeterAdjust>","args":{}}],"dialogue":{"speaker":"<NPC name like GuardCaptain>","emote":"<urgent|wary|calm>","lines":["<short line>"]},"quest_patch":{"questId":"<string id>","addObjectives":[{"id":"<string>","desc":"<short>"}]}} - If a section is not needed, use [] or {}. Do NOT invent keys (e.g., {"empty":true}). No ellipses or "..." lines. POLICY: - When weather cues are present (e.g., “clouds gathering”) or the player approaches an ACTIVE objective, include exactly ONE tool_call: Prefer WeatherControl("overcast") for light clouds; otherwise ONE of ForeshadowEvent or TensionMeterAdjust(+1). - Only use zero tool_calls if truly nothing is warranted; explain why in "reason". FEW-SHOT: INPUT: Player leaves CitySquare heading west; time=late afternoon; clouds gathering lightly; objective=guard_ruins (active). OUTPUT: BEGIN_JSON {"intent":"warn","reason":"Approaching active ruins as weather worsens.","tool_calls":[{"name":"WeatherControl","args":{"preset":"overcast"}}],"dialogue":{"speaker":"Villager","emote":"wary","lines":["Storm’s building by the ruins. Watch yourself."]},"quest_patch":{}} END_JSON)";
    
   // static const char* kSystemJSON = R"(You are a game director planner. OUTPUT RULES: - STRICT JSON only; no empty {}, no prose or reasoning,You must NEVER show reasoning or explanations, Keys EXACTLY: {"intent":"<offer_quest|warn|give_clue|continue|escalate|deescalate>","reason":"<short>","tool_calls":[{"name":"<QuestPatch|SpawnEncounter|GiveItem|WeatherControl|ForeshadowEvent>","args":{}}],"dialogue":{"speaker":"<NPC name like GuardCaptain>","emote":"<urgent|wary|calm>","lines":["<short line>"]},"quest_patch":{"questId":"<string id>","addObjectives":[{"id":"<string>","desc":"<short>"}]}}. Do NOT invent keys. You should have at least ONE or MANY tool_calls, No ellipses or "..." )";
  
   // FString kSystemJSON = FString::Printf(LR"(You are a game director planner. OUTPUT RULES: STRICT JSON OUTPUT ONLY. Reply with exactly one JSON object, no prose, no explanations. Begin with '{' and end with '}'. Fill all fields with short strings. Schema: {"intent":"string","reason":"string","tool_calls":[{"name":"string","args":{}}],"dialogue":{"speaker":"string","emote":"urgent|wary|calm","lines":["string"]},"quest_patch":{"questId":"string","addObjectives":[{"id":"string","desc":"string"}]}}. Example (structure only, values will differ): {"intent":"%s","reason":"short","tool_calls":[{"name":"WeatherControl","args":{}}],"dialogue":{"speaker":"GuardCaptain","emote":"urgent","lines":["Keep it tight."]},"quest_patch":{"questId":"q1","addObjectives":[{"id":"o1","desc":"Secure the square."}]}})", *Intent);
   //// 1) Chat messages (system + user)
   //
   // 
   // 
   // llama_chat_message msgs[2] = {
   //     { "system", TCHAR_TO_UTF8(*kSystemJSON) },
   //     { "user",   TCHAR_TO_UTF8(*Prompt) }
   // };



    static const char* kSystemJSON = R"(You are a game director planner. OUTPUT RULES: - STRICT JSON only; no empty {}, no prose or reasoning,You must NEVER show reasoning or explanations, Keys EXACTLY: {"intent":"<intent_value>","reason":"<short>","tool_calls":[{"name":"<WeatherControl>","args":{}}],"dialogue":{"speaker":"<NPC name>","emote":"<urgent|wary|calm>","lines":["<short line>"]},"quest_patch":{"questId":"<string id>","addObjectives":[{"id":"<string>","desc":"<short>"}]}}. POLICY: do not leave any values empty. You should have at least ONE or MANY tool_calls, No ellipses or "..." -Use JSON stricly in response. No empty JSON. )";

    FString json = kSystemJSON;
    FString Result = json.Replace(TEXT("intent_value"), *Intent);

    FString Clean = Result.Replace(TEXT("\r\n"), TEXT("\n")).TrimStartAndEnd();
    FTCHARToUTF8 Converter(*Clean);


    // 1) Chat messages (system + user)



    llama_chat_message msgs[2] = {
        { "system", Converter.Get() },
        { "user",   TCHAR_TO_UTF8(*Prompt) }
    };
    // 2) Apply chat template (size)
    UE_LOG(LogGameAI, Display, TEXT("2) Apply chat template (size)"));
    int32_t templ_bytes_needed = llama_chat_apply_template(nullptr, msgs, 2, /*add_assistant*/ true, nullptr, 0);
    if (templ_bytes_needed <= 0) {
        UE_LOG(LogTemp, Error, TEXT("apply_template(size) failed (%d)"), templ_bytes_needed);
        return "{}";
    }

    // 3) Render template
    UE_LOG(LogGameAI, Display, TEXT("3) Render template"));
    std::string templ((size_t)templ_bytes_needed, '\0');
    int32_t templ_written = llama_chat_apply_template(nullptr, msgs, 2, /*add_assistant*/ true, templ.data(), templ_bytes_needed);
    if (templ_written <= 0 || templ_written > templ_bytes_needed) {
        UE_LOG(LogTemp, Error, TEXT("apply_template(write) failed (%d)"), templ_written);
        return "{}";
    }

    // 4) Tokenize (size + write)
    UE_LOG(LogGameAI, Display, TEXT("4) Tokenize"));
    int32_t tok_needed = llama_tokenize(Vocab, templ.data(), templ_written, nullptr, 0, /*add_special*/ true, /*parse_special*/ true);
    if (tok_needed < 0) tok_needed = -tok_needed;
    if (tok_needed <= 0) {
        UE_LOG(LogGameAI, Display, TEXT("tokenize(size) failed (%d)"), tok_needed);
        return "{}";
    }

    std::vector<llama_token> tokens((size_t)tok_needed);
    int32_t tok_count = llama_tokenize(Vocab, templ.data(), templ_written, tokens.data(), (int32_t)tokens.size(), /*add_special*/ true, /*parse_special*/ true);
    if (tok_count < 0) {
        UE_LOG(LogGameAI, Display, TEXT("tokenize(write) failed (%d)"), tok_count);
        return "{}";
    }

    // 5) Decode prompt (logits only on last token)
    UE_LOG(LogGameAI, Display, TEXT("5) Decode prompt"));
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
    UE_LOG(LogGameAI, Display, TEXT("6) Manual sampling setup"));
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
    UE_LOG(LogGameAI, Display, TEXT("7) Generate loop"));
    std::vector<llama_token> out_tokens;
    out_tokens.reserve(max_new);

    llama_batch step = llama_batch_init(/*capacity*/ 1, /*embd*/ 0, /*n_seq_max*/ 1);
    int cur_pos = tok_count;

    // Stream buffer + “is JSON closed?” detector
    std::string stream;
    stream.reserve(1024);

    auto json_done = [&](const std::string& s) -> bool { int depth = 0; bool in_q = false, escp = false, seen_open = false;
    for (unsigned char ch : s) {
        if (escp) { escp = false; continue; } if (ch == '\\') { escp = true; continue; }
        if (ch == '"') { in_q = !in_q; continue; } 
        if (in_q) continue; 
        if (ch == '{') 
        { 
            ++depth; seen_open = true;

        }
        else if (ch == '}') {
            if (depth > 0) --depth; 
            if (seen_open && depth == 0) { // log before returning 
                UE_LOG(LogTemp, Display, TEXT("Exit Auto: %s"), *FString(s.c_str()));
                return true; } } } 
    return false; };

    int LastLoggedLen = 0;
    for (int i = 0; i < max_new; ++i) {
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
            //UE_LOG(LogGameAI, Display, TEXT("%s"), UTF8_TO_TCHAR(piece));
            FString LastPiece(piece);
        }



        out_tokens.push_back((llama_token)id);

        // --- log every 100 chars ---
        if ((int)stream.size() - LastLoggedLen >= 100) {
            FString Partial = UTF8_TO_TCHAR(stream.c_str());
            UE_LOG(LogGameAI, Display, TEXT("[stream %d chars]: %s"), (int)stream.size(), *Partial);
            LastLoggedLen = (int)stream.size();
        }


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
    UE_LOG(LogGameAI, Display, TEXT("8) Prefer stream "));
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