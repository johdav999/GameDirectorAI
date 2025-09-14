// Fill out your copyright notice in the Description page of Project Settings.


#include "GameDirectorSubsystem.h"
#include "Misc/Paths.h"

#if PLATFORM_WINDOWS
#include "Windows/AllowWindowsPlatformTypes.h"
#include <delayimp.h>
#include "Windows/HideWindowsPlatformTypes.h"

// Link the delay-load helper (ok to keep here for VS; alternatively add in Build.cs)
#pragma comment(lib, "delayimp")

// Extract the first balanced top-level {...} JSON object from arbitrary text.
// Respects quoted strings and escapes.
static bool ExtractTopLevelJsonObject(const FString& In, FString& OutJson) {
    OutJson.Empty();
    const TCHAR* S = *In;
    const int32 N = In.Len();

    // find first '{'
    int32 i = 0;
    while (i < N && S[i] != TCHAR('{')) ++i;
    if (i == N) return false;

    bool inStr = false, esc = false;
    int32 depth = 0;
    int32 start = i;

    for (; i < N; ++i) {
        const TCHAR c = S[i];

        if (esc) { esc = false; continue; }
        if (c == TCHAR('\\')) { if (inStr) esc = true; continue; }
        if (c == TCHAR('"')) { inStr = !inStr; continue; }
        if (inStr) continue;

        if (c == TCHAR('{')) { if (depth == 0) start = i; ++depth; }
        else if (c == TCHAR('}')) {
            --depth;
            if (depth == 0) { OutJson = In.Mid(start, i - start + 1); return true; }
        }
    }
    return false; // never closed
}

// Remove code fences, unwrap quoted/escaped JSON, then extract a top-level object
static FString SanitizeModelOutputToJsonObject(const FString& Raw) {
    FString S = Raw;

    // strip code fences if present
    S.ReplaceInline(TEXT("```json"), TEXT(""));
    S.ReplaceInline(TEXT("```"), TEXT(""));
    S = S.TrimStartAndEnd();

    // exact object?
    if (S.StartsWith(TEXT("{")) && S.EndsWith(TEXT("}"))) return S;

    // try extract from mixed text
    FString Extracted;
    if (ExtractTopLevelJsonObject(S, Extracted)) return Extracted;

    // quoted/escaped JSON like: "{ \"intent\": ... }"
    if (S.StartsWith(TEXT("\"{")) && S.EndsWith(TEXT("}\""))) {
        S = S.Mid(1, S.Len() - 2);                  // drop outer quotes
        S.ReplaceInline(TEXT("\\\""), TEXT("\""));
        S.ReplaceInline(TEXT("\\\\"), TEXT("\\"));
        S.ReplaceInline(TEXT("\\n"), TEXT("\n"));
        S.ReplaceInline(TEXT("\\r"), TEXT("\r"));
        S.ReplaceInline(TEXT("\\t"), TEXT("\t"));
        if (ExtractTopLevelJsonObject(S, Extracted)) return Extracted;
    }

    // give back as-is (parse will fail and you can log Clean)
    return Raw;
}
static FARPROC WINAPI DelayLoadFailureHook(unsigned dliNotify, PDelayLoadInfo pdli)
{
    if (dliNotify == dliFailLoadLib)
    {
        UE_LOG(LogTemp, Error,
            TEXT("[DelayLoad] Failed to load library: %s (GetLastError=%lu)"),
            ANSI_TO_TCHAR(pdli->szDll), pdli->dwLastError);
    }
    else if (dliNotify == dliFailGetProc)
    {
        UE_LOG(LogTemp, Error,
            TEXT("[DelayLoad] Failed to get proc '%s' from %s (GetLastError=%lu)"),
            ANSI_TO_TCHAR(pdli->dlp.szProcName), ANSI_TO_TCHAR(pdli->szDll), pdli->dwLastError);
    }
    return nullptr; // don't “fix” it, just log
}

// Define the global hook pointer (this is a variable, not a function)
extern "C" __declspec(selectany) const PfnDliHook __pfnDliFailureHook2 = DelayLoadFailureHook;
#endif


bool UGameDirectorSubsystem::InitializeRunner()
{
    if (!RunnerAsync)
    {
        RunnerAsync = MakeUnique<LLamaRunnerAsync>();
        FString ModelPath = FPaths::ConvertRelativePathToFull(
            FPaths::ProjectDir() / TEXT("gptoss20b.f16pure.gguf")
        );

        return RunnerAsync->Initiate(*ModelPath, 4096);
       // return RunnerAsync->Initiate(TEXT("C:\\models\\rpg_director\\gptoss20b.f16pure.gguf"), 4096);
    }

    //if (!Runner)
    //{
    //    Runner = MakeUnique<LlamaRunner>();
    //
    //    return Runner->Initiate(TEXT("C:\\models\\rpg_director\\gptoss20b.f16pure.gguf"), 4096);
    //}
    return false;
}
bool UGameDirectorSubsystem::Generate2(FString Prompt, FString Intent)
{
    RunnerAsync->GenerateJSONAsync(Prompt,
        [this](FString Output)
        {
            // This lambda runs on the Game Thread
            FString Intent;
            TArray<FToolCall> Tools;
            TArray<FObjective> Objectives;
            FDialogue Dialogue;
			FString Json;
            if (ParseDirectorJSON(Output, Intent, Tools, Objectives, Dialogue,Json))
            {
                FDirectorDecision D;
                D.Intent = MoveTemp(Intent);
                D.ToolCalls = MoveTemp(Tools);
                D.Objectives = MoveTemp(Objectives);
                D.Dialogue = MoveTemp(Dialogue);
                D.Response = Json;
                OnDirectorDecision.Broadcast(D);
            }
        },Intent);
    return true;
}
bool UGameDirectorSubsystem::Generate(FString Prompt)
{


    const int   kMaxNew = 128;
    const int   top_k = 20;
    const float top_p = 0.8f;
    const float temp = 0.20f;

    LlamaRunner* RunnerPtr = Runner.Get();
    if (!RunnerPtr)
    {
        UE_LOG(LogTemp, Error, TEXT("No LlamaRunner available"));
        return false;
    }

    // Optional: reentrancy guard
    if (bIsGenerating.Load())
    {
        UE_LOG(LogTemp, Warning, TEXT("Generate already in progress"));
        return false;
    }
    bIsGenerating.Store(true);

    // Run the heavy work directly (on this thread)
    FString Output = RunnerPtr->GenerateJSON(Prompt, kMaxNew, top_k, top_p, temp);

    // Parse
    FString Intent;
    TArray<FToolCall> Tools;
    TArray<FObjective> Objectives;
    FDialogue Dialogue;
	FString Json;
    const bool bOk = ParseDirectorJSON(Output, Intent, Tools, Objectives, Dialogue,Json);

    if (bOk)
    {
        FDirectorDecision Decision;
        Decision.Intent = MoveTemp(Intent);
        Decision.Reason = TEXT("");
        Decision.ToolCalls = MoveTemp(Tools);
        Decision.Objectives = MoveTemp(Objectives);
        Decision.Dialogue = MoveTemp(Dialogue);

        OnDirectorDecision.Broadcast(Decision);
    }
    else
    {
        UE_LOG(LogTemp, Error, TEXT("Failed to parse director JSON output"));
        // Optional: OnGenerationFailed.Broadcast();
    }

    bIsGenerating.Store(false);
    return bOk;
}

bool UGameDirectorSubsystem::GenerateAsync(FString Prompt)
{
    const int   kMaxNew = 1024;
    const int   top_k = 40;
    const float top_p = 0.95f;
    const float temp = 0.80f;

    // Capture a *raw* pointer – do NOT copy the TUniquePtr
    LlamaRunner* RunnerPtr = Runner.Get();

    // Optionally gate reentrancy
    if (bIsGenerating.Load()) { return false; }
    bIsGenerating.Store(true);

    Async(EAsyncExecution::ThreadPool,
        [this, Prompt, RunnerPtr, kMaxNew, top_k, top_p, temp]()
        {
            FString Output;

            if (RunnerPtr)
            {
                // Heavy work off the game thread
                Output = RunnerPtr->GenerateJSON(Prompt, kMaxNew, top_k, top_p, temp);
            }

            // Parse on the worker as well (no UObject touching)
            FString Intent;
            TArray<FToolCall> Tools;
            TArray<FObjective> Objectives;
            FDialogue Dialogue;
			FString Json;
            const bool bOk = ParseDirectorJSON(Output, Intent, Tools, Objectives, Dialogue,Json);

            // Hop back to the game thread for BP/Delegates/UObjects
            AsyncTask(ENamedThreads::GameThread,
                [this, bOk, Intent = MoveTemp(Intent), Tools = MoveTemp(Tools),
                Objectives = MoveTemp(Objectives), Dialogue = MoveTemp(Dialogue)]() mutable
                {
                    bIsGenerating.Store(false);

                    if (!IsValid(this)) { return; } // Subsystem could be tearing down in PIE exit

                    if (bOk)
                    {
                        FDirectorDecision Decision;
                        Decision.Intent = MoveTemp(Intent);
                        Decision.Reason = TEXT("");
                        Decision.ToolCalls = MoveTemp(Tools);
                        Decision.Objectives = MoveTemp(Objectives);
                        Decision.Dialogue = MoveTemp(Dialogue);

                        OnDirectorDecision.Broadcast(Decision);
                    }
                    else
                    {
                        // Optional: OnGenerationFailed.Broadcast();
                    }
                });
        });
	return true;
}
// Compact-stringify a JSON object
static bool JsonToString(const TSharedPtr<FJsonObject>& Obj, FString& Out)
{
    Out.Reset();
    if (!Obj.IsValid()) return false;
    TSharedRef<TJsonWriter<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>> Writer =
        TJsonWriterFactory<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>::Create(&Out);
    return FJsonSerializer::Serialize(Obj.ToSharedRef(), Writer);
}
static TArray<FString> JsonToStringArray(const TArray<TSharedPtr<FJsonValue>>& InVals)
{
    TArray<FString> Out;
    Out.Reserve(InVals.Num());
    for (const TSharedPtr<FJsonValue>& V : InVals)
    {
        FString S;
        // prefer strings; otherwise compact-stringify value
        if (V->Type == EJson::String)
        {
            S = V->AsString();
        }
        else
        {
            TSharedRef<TJsonWriter<>> W = TJsonWriterFactory<>::Create(&S);
            FJsonSerializer::Serialize(V, {}, W);
            W->Close();
        }
        Out.Add(S);
    }
    return Out;
}

// --- helpers you already have (keep yours or these versions) ---
static void ReplaceSmartQuotes(FString& S) {
    S.ReplaceInline(TEXT("“"), TEXT("\""));
    S.ReplaceInline(TEXT("”"), TEXT("\""));
    S.ReplaceInline(TEXT("„"), TEXT("\""));
    S.ReplaceInline(TEXT("‟"), TEXT("\""));
    S.ReplaceInline(TEXT("’"), TEXT("'"));
    S.ReplaceInline(TEXT("‘"), TEXT("'"));
}
static void StripBackticksAndFences(FString& S) {
    S.ReplaceInline(TEXT("```json"), TEXT(""));
    S.ReplaceInline(TEXT("```"), TEXT(""));
    S.ReplaceInline(TEXT("`"), TEXT(""));
}
static void TrimBomAndWhitespace(FString& S) {
    if (S.Len() && S[0] == 0xFEFF) { S.RemoveAt(0); }
    S = S.TrimStartAndEnd();
}
// optional: strip stray control chars (except \t\r\n)
static void StripControlChars(FString& S) {
    FString Out; Out.Reserve(S.Len());
    for (TCHAR c : S) {
        if (c >= 32 || c == TEXT('\n') || c == TEXT('\r') || c == TEXT('\t')) {
            Out.AppendChar(c);
        }
    }
    S = MoveTemp(Out);
}
// remove common chat markers that sometimes sneak in
static void StripChatMarkers(FString& S) {
    S.ReplaceInline(TEXT("<|end|>"), TEXT(""));
    S.ReplaceInline(TEXT("<|start|>"), TEXT(""));
    S.ReplaceInline(TEXT("<|assistant|>"), TEXT(""));
    S.ReplaceInline(TEXT("<|user|>"), TEXT(""));
}

// ----- collect ALL balanced {...} substrings (quotes & escapes respected) -----
static void CollectBalancedObjects(const FString& In, TArray<FString>& OutObjs) {
    OutObjs.Reset();
    const TCHAR* P = *In;
    const int32 N = In.Len();

    bool inStr = false, esc = false;
    int32 depth = 0;
    int32 start = -1;

    for (int32 i = 0; i < N; ++i) {
        const TCHAR c = P[i];

        if (esc) { esc = false; continue; }
        if (inStr) {
            if (c == TEXT('\\')) { esc = true; continue; }
            if (c == TEXT('"')) { inStr = false; }
            continue;
        }

        if (c == TEXT('"')) { inStr = true; continue; }
        if (c == TEXT('{')) {
            if (depth == 0) start = i;
            ++depth;
        }
        else if (c == TEXT('}')) {
            if (depth > 0) {
                --depth;
                if (depth == 0 && start >= 0) {
                    OutObjs.Add(In.Mid(start, i - start + 1));
                    start = -1;
                }
            }
        }
    }
}

// --- your compact serializer helper (unchanged) ---
static FString ObjectToCompactString(const TSharedPtr<FJsonObject>& Obj) {
    FString Out;
    TSharedRef<TJsonWriter<>> W = TJsonWriterFactory<>::Create(&Out);
    FJsonSerializer::Serialize(Obj.ToSharedRef(), W);
    W->Close();
    return Out;
}

// Try to extract the FIRST substring that parses as a JSON object.
// Returns true and writes the *valid* JSON (compact) into OutJson.
static bool ExtractStrictJSONObject(const FString& Raw, FString& OutJson) {
    FString S = Raw;
    TrimBomAndWhitespace(S);
    StripBackticksAndFences(S);
    ReplaceSmartQuotes(S);
    StripChatMarkers(S);
    StripControlChars(S);
    TrimBomAndWhitespace(S);

    // Fast path: whole string looks like an object -> try parse directly
    if (S.StartsWith(TEXT("{")) && S.EndsWith(TEXT("}"))) {
        TSharedPtr<FJsonObject> Obj;
        const TSharedRef<TJsonReader<>> R = TJsonReaderFactory<>::Create(S);
        if (FJsonSerializer::Deserialize(R, Obj) && Obj.IsValid()) {
            OutJson = ObjectToCompactString(Obj);
            return true;
        }
    }

    // Otherwise, collect all balanced objects and try each until one parses.
    TArray<FString> Candidates;
    CollectBalancedObjects(S, Candidates);

    for (FString& Cand : Candidates) {
        // Optional: remove trailing commas inside the candidate
        // (simple pass – safe enough for LLM output)
        {
            FString Clean; Clean.Reserve(Cand.Len());
            for (int32 i = 0; i < Cand.Len(); ++i) {
                const TCHAR c = Cand[i];
                if (c == TCHAR(',')) {
                    int32 j = i + 1;
                    while (j < Cand.Len() && FChar::IsWhitespace(Cand[j])) ++j;
                    if (j < Cand.Len() && (Cand[j] == TCHAR('}') || Cand[j] == TCHAR(']'))) {
                        continue; // drop trailing comma
                    }
                }
                Clean.AppendChar(c);
            }
            Cand = MoveTemp(Clean);
        }

        TSharedPtr<FJsonObject> Obj;
        const TSharedRef<TJsonReader<>> R = TJsonReaderFactory<>::Create(Cand);
        if (FJsonSerializer::Deserialize(R, Obj) && Obj.IsValid()) {
            OutJson = ObjectToCompactString(Obj);
            return true;
        }
    }

    // Nothing parsed
    return false;
}
// --- parser ---
// ---- New implementation with Dialogue ----
bool UGameDirectorSubsystem::ParseDirectorJSON(
    const FString& JsonText,
    FString& OutIntent,
    TArray<FToolCall>& OutTools,
    TArray<FObjective>& OutObjectives,
    FDialogue& OutDialogue, FString& Json)
{
    OutIntent.Empty();
    OutTools.Reset();
    OutObjectives.Reset();
    OutDialogue = FDialogue(); // clear

    // Clean/extract a single top-level {...} object
    FString Clean;

    if (!ExtractStrictJSONObject(JsonText, Clean)) {
        UE_LOG(LogTemp, Warning, TEXT("No valid JSON object found in model output (len=%d)."), JsonText.Len());
        // You can log a short prefix to debug:
        UE_LOG(LogTemp, Verbose, TEXT("Head: %s"), *JsonText.Left(200));
    }
    else {
        // Now parse CleanJson into your structs (Intent, Tools, etc.)
    }



    if (!Clean.StartsWith(TEXT("{")) || !Clean.EndsWith(TEXT("}"))) {
        UE_LOG(LogTemp, Error, TEXT("Director JSON: not a pure object after cleaning.\nCleaned:\n%s"), *Clean);
        return false;
    }

    // Parse
    TSharedPtr<FJsonObject> Root;
    const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Clean);
    if (!FJsonSerializer::Deserialize(Reader, Root) || !Root.IsValid()) {
        UE_LOG(LogTemp, Error, TEXT("Director JSON parse failed.\nRaw:\n%s\nCleaned:\n%s"), *JsonText, *Clean);
        return false;
    }
    Json = Clean;
    // intent
    Root->TryGetStringField(TEXT("intent"), OutIntent);

    // tool_calls
    const TArray<TSharedPtr<FJsonValue>>* ToolCallsArray = nullptr;
    if (Root->TryGetArrayField(TEXT("tool_calls"), ToolCallsArray) && ToolCallsArray) {
        for (const auto& V : *ToolCallsArray) {
            if (!V.IsValid()) continue;
            const TSharedPtr<FJsonObject> ToolObj = V->AsObject();
            if (!ToolObj.IsValid()) continue;

            FToolCall T;
            ToolObj->TryGetStringField(TEXT("name"), T.Name);

            // args: compact-stringify the object
            const TSharedPtr<FJsonObject>* ArgsPtr = nullptr;
            if (ToolObj->TryGetObjectField(TEXT("args"), ArgsPtr) && ArgsPtr && ArgsPtr->IsValid()) {
                FString ArgsStr;
                if (JsonToString(*ArgsPtr, ArgsStr)) {
                    T.ArgsJson = MoveTemp(ArgsStr);
                }
            }
            OutTools.Add(T);
        }
    }

    // dialogue
    const TSharedPtr<FJsonObject>* DialoguePtr = nullptr;
    if (Root->TryGetObjectField(TEXT("dialogue"), DialoguePtr) && DialoguePtr && DialoguePtr->IsValid()) {
        (*DialoguePtr)->TryGetStringField(TEXT("speaker"), OutDialogue.Speaker);
        (*DialoguePtr)->TryGetStringField(TEXT("emote"), OutDialogue.Emote);

        const TArray<TSharedPtr<FJsonValue>>* LinesArray = nullptr;
        if ((*DialoguePtr)->TryGetArrayField(TEXT("lines"), LinesArray) && LinesArray) {
            for (const auto& LineVal : *LinesArray) {
                FString Line;
                if (LineVal.IsValid() && LineVal->TryGetString(Line)) {
                    OutDialogue.Lines.Add(Line);
                }
            }
        }
    }

    // quest_patch → addObjectives
    const TSharedPtr<FJsonObject>* QP = nullptr;
    if (Root->TryGetObjectField(TEXT("quest_patch"), QP) && QP && QP->IsValid()) {
        const TArray<TSharedPtr<FJsonValue>>* AddObjs = nullptr;
        if ((*QP)->TryGetArrayField(TEXT("addObjectives"), AddObjs) && AddObjs) {
            for (const auto& V : *AddObjs) {
                const TSharedPtr<FJsonObject> O = V->AsObject();
                if (!O.IsValid()) continue;

                FObjective Obj;
                O->TryGetStringField(TEXT("id"), Obj.Id);
                O->TryGetStringField(TEXT("desc"), Obj.Desc);
                OutObjectives.Add(Obj);
            }
        }
    }

    return true;
}
