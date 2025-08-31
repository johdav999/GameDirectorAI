using UnrealBuildTool;
using System.IO;
using System;

public class llama : ModuleRules
{
    public llama(ReadOnlyTargetRules Target) : base(Target)
    {
        Type = ModuleType.External;

        string ThirdPartyRoot = Path.GetFullPath(Path.Combine(ModuleDirectory, "..")); // .../ThirdParty
        string LlamaRoot = Path.Combine(ThirdPartyRoot, "llama");
        string IncPath = Path.Combine(LlamaRoot, "include");
        PublicIncludePaths.Add(IncPath);

        if (Target.Platform == UnrealTargetPlatform.Win64)
        {
            string LibPath = Path.Combine(LlamaRoot, "Win64", "lib");
            string BinPath = Path.Combine(LlamaRoot, "Win64", "bin");

            PublicAdditionalLibraries.Add(Path.Combine(LibPath, "llama.lib"));

            // OPTION A (simpler, avoids delayhlp issues): do NOT delay-load; just stage the DLL.
            // (Recommended to start with this)
            RuntimeDependencies.Add(Path.Combine("$(TargetOutputDir)", "llama.dll"),
                                    Path.Combine(BinPath, "llama.dll"));

            // If you built with CUDA, stage those runtime DLLs too:
            string[] CudaDlls = new string[] { "cudart64_12.dll", "cublas64_12.dll", "cublasLt64_12.dll" };
            foreach (var dll in CudaDlls)
            {
                string candidate = Path.Combine(BinPath, dll);
                if (File.Exists(candidate))
                {
                    RuntimeDependencies.Add(Path.Combine("$(TargetOutputDir)", dll), candidate);
                }
            }

            // OPTION B (advanced): Delay-load. If you enable this, also link DelayImp.lib to avoid delayhlp errors.
            // PublicDelayLoadDLLs.Add("llama.dll");
            // PublicAdditionalLibraries.Add("Delayimp.lib"); // resolves __delayLoadHelper2 / delayhlp.cpp link issues
        }
    }
}