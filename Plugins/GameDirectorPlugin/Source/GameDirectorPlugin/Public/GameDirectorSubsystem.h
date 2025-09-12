// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "LlamaRunner.h"
#include "LlamaRunnerAsync.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "GameDirectorSubsystem.generated.h"

// Fires once a JSON result has been parsed successfully



USTRUCT(BlueprintType)
struct FToolCall
{
    GENERATED_BODY()

    UPROPERTY(BlueprintReadOnly)
    FString Name;

    // Keep raw JSON so BPs or code can decode it as needed
    UPROPERTY(BlueprintReadOnly)
    FString ArgsJson;
};

USTRUCT(BlueprintType)
struct FObjective
{
    GENERATED_BODY()

    UPROPERTY(BlueprintReadOnly)
    FString Id;

    UPROPERTY(BlueprintReadOnly)
    FString Desc;
};

USTRUCT(BlueprintType)
struct FDialogue
{
    GENERATED_BODY()

    UPROPERTY(BlueprintReadOnly)
    FString Speaker;

    UPROPERTY(BlueprintReadOnly)
    FString Emote;

    UPROPERTY(BlueprintReadOnly)
    TArray<FString> Lines;
};

USTRUCT(BlueprintType)
struct FDirectorDecision
{
    GENERATED_BODY()

    UPROPERTY(BlueprintReadOnly)
    FString Intent;

    UPROPERTY(BlueprintReadOnly)
    FString Reason;

    UPROPERTY(BlueprintReadOnly)
    TArray<FToolCall> ToolCalls;

    UPROPERTY(BlueprintReadOnly)
    TArray<FObjective> Objectives;

    UPROPERTY(BlueprintReadOnly)
    FDialogue Dialogue;
    UPROPERTY(BlueprintReadOnly)
    FString Response;
};
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnDirectorDecision, const FDirectorDecision&, Decision);

/**
 * 
 */
UCLASS()
class GAMEDIRECTORPLUGIN_API UGameDirectorSubsystem : public UGameInstanceSubsystem
{
	GENERATED_BODY()
public:
    UPROPERTY(BlueprintAssignable, Category = "GameDirector")
    FOnDirectorDecision OnDirectorDecision;




	UFUNCTION(BlueprintCallable, Category = "GameDirector")	
	bool InitializeRunner();

    UFUNCTION(BlueprintCallable, Category = "GameDirector")
    bool Generate2(FString Prompt);


    UFUNCTION(BlueprintCallable, Category = "GameDirector")
    bool GenerateAsync(FString Prompt);
	UFUNCTION(BlueprintCallable, Category = "GameDirector")
	bool Generate(FString Prompt);

    bool ParseDirectorJSON(
        const FString& JsonText,
        FString& OutIntent,
        TArray<FToolCall>& OutTools,
        TArray<FObjective>& OutObjectives,
        FDialogue& OutDialogue, FString& Json);



private:
	// Owns the llama runtime wrapper
	TUniquePtr<LlamaRunner> Runner;
    TUniquePtr<LLamaRunnerAsync> RunnerAsync;
    TAtomic<bool> bIsGenerating{ false };
};
