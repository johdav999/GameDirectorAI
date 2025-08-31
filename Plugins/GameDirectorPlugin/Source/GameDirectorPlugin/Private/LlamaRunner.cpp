#include "LlamaRunner.h"

#include "llama.h"

void ULlamaRunner::InitiateLlama()
{
    // Initialize the llama backend; required before any other llama.cpp calls.
    llama_backend_init();

    UE_LOG(LogTemp, Log, TEXT("Llama backend initialized for build 58fc6cefe213c8af02153b994b738b3cb6ef2ba7"));
}

