// Copyright Epic Games, Inc. All Rights Reserved.
using System;
using System.IO;
using UnrealBuildTool;


// GameDirectorPlugin.Build.cs

public class GameDirectorPlugin : ModuleRules
{

    public GameDirectorPlugin(ReadOnlyTargetRules Target) : base(Target)
    {
        Type = ModuleType.CPlusPlus;

        PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
        CppStandard = CppStandardVersion.Cpp20;
        IncludeOrderVersion = EngineIncludeOrderVersion.Latest;

        bUseUnity = false;
        OptimizeCode = CodeOptimization.Never;

        PublicDependencyModuleNames.AddRange(new string[] {
            "Core", "CoreUObject", "Engine","Projects","Json"
        });

        // --- THIRD-PARTY: llama (SINGLE SOURCE OF TRUTH) ---
        // Layout (example):
        // Plugins/GameDirectorPlugin/Source/ThirdParty/llama/
        //   include/llama.h
        //   include/ggml/...
        //   Win64/lib/llama.lib
        //   Win64/bin/llama.dll (+ any ggml-cuda/DirectML/cublas/etc deps you need)
        string ThirdPartyRoot = Path.Combine(ModuleDirectory, "..", "ThirdParty", "llama");
        string IncDir = Path.Combine(ThirdPartyRoot, "include");
        string IncGgmlDir = Path.Combine(IncDir, "ggml");
        string WinLibDir = Path.Combine(ThirdPartyRoot, "Win64", "lib");
        string WinBinDir = Path.Combine(ThirdPartyRoot, "Win64", "bin");

        // 1) Include ONLY these headers (remove all other llama include paths!)
        PublicIncludePaths.Add(IncDir);
        PublicIncludePaths.Add(IncGgmlDir);

        if (Target.Platform == UnrealTargetPlatform.Win64)
        {
            string LlamaLib = Path.Combine(WinLibDir, "llama.lib");
            if (!File.Exists(LlamaLib))
            {
                throw new BuildException($"[GameDirectorPlugin] llama.lib not found at {LlamaLib}");
            }
            PublicAdditionalLibraries.Add(LlamaLib);

            // Delay-load so Windows resolves llama.dll from our staged location at runtime
            PublicDelayLoadDLLs.Add("llama.dll");

            // Stage llama.dll + any dependent DLLs you ship with the plugin
            string[] DllsToStage = new string[]
            {
                "llama.dll",
                // Add the exact ones you actually ship (match your build!):
                // "ggml.dll", "ggml-cuda.dll", "ggml-dml.dll",
                // "cudart64_12.dll", "cublas64_12.dll", "cublasLt64_12.dll", "nvJitLink_120_0.dll",
                // "d3d12.dll", "d3d12core.dll", "dxil.dll", "DirectML.dll",
            };

            foreach (var dllName in DllsToStage)
            {
                string src = Path.Combine(WinBinDir, dllName);
                if (File.Exists(src))
                {
                    // Stage into the plugin’s Binaries/Win64 at build time
                    RuntimeDependencies.Add(
                        $"$(PluginDir)/Binaries/Win64/{dllName}",
                        src,
                        StagedFileType.NonUFS);
                }
                else
                {
                    // Optional: warn for missing optional DLLs; keep llama.dll mandatory
                    if (dllName.Equals("llama.dll", StringComparison.OrdinalIgnoreCase))
                    {
                        throw new BuildException($"[GameDirectorPlugin] Required DLL missing: {src}");
                    }
                    else
                    {
                        System.Console.WriteLine($"[GameDirectorPlugin] (optional) missing: {src}");
                    }
                }
            }
        }

        // IMPORTANT: Do NOT add any external llama.cpp include paths here.
        // Remove e.g. C:\Users\Johan\source\repos\llama.cpp\llama.cpp\include, etc.
        // Keep a single header source to guarantee ABI match with your DLL.
    }
}

