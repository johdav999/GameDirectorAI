// Copyright Epic Games, Inc. All Rights Reserved.

using System.IO;
using UnrealBuildTool;

public class GameDirectorPlugin : ModuleRules
{
	public GameDirectorPlugin(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = ModuleRules.PCHUsageMode.UseExplicitOrSharedPCHs;
		
		PublicIncludePaths.AddRange(
			new string[] {
				// ... add public include paths required here ...
			}
			);
				
		
		PrivateIncludePaths.AddRange(
			new string[] {
				// ... add other private include paths required here ...
			}
			);
			
		
		PublicDependencyModuleNames.AddRange(
			new string[]
			{
				"Core"
				// ... add other public dependencies that you statically link with here ...
			}
			);
			
		
		PrivateDependencyModuleNames.AddRange(
			new string[]
			{
				"CoreUObject",
				"Engine",
				"Slate",
				"SlateCore","llama","Json","JsonUtilities","UMG","InputCore","EnhancedInput"
				// ... add private dependencies that you statically link with here ...	
			}
			);

        //string TP = Path.GetFullPath(Path.Combine(ModuleDirectory, "..", "ThirdParty", "llama"));
        //PublicIncludePaths.Add(Path.Combine(TP, "include"));

        //if (Target.Platform == UnrealTargetPlatform.Win64)
        //{
        //    string lib = Path.Combine(TP, "Win64", "lib", "llama.lib");
        //    string bin = Path.Combine(TP, "Win64", "bin", "llama.dll");

        //    PublicAdditionalLibraries.Add(lib);
        //    RuntimeDependencies.Add(Path.Combine("$(TargetOutputDir)", "llama.dll"), bin);
        //}
        DynamicallyLoadedModuleNames.AddRange(
			new string[]
			{
				// ... add any modules that your module loads dynamically here ...
			}
			);

	}
}
