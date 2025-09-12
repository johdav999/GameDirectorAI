// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class GameDirectorAI : ModuleRules
{
	public GameDirectorAI(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(new string[] {
			"Core",
			"CoreUObject",
			"Engine",
			"InputCore",
			"EnhancedInput",
			"AIModule",
			"StateTreeModule",
			"GameplayStateTreeModule",
			"UMG",
			"Slate"
		});

		PrivateDependencyModuleNames.AddRange(new string[] { });

		PublicIncludePaths.AddRange(new string[] {
			"GameDirectorAI",
			"GameDirectorAI/Variant_Platforming",
			"GameDirectorAI/Variant_Platforming/Animation",
			"GameDirectorAI/Variant_Combat",
			"GameDirectorAI/Variant_Combat/AI",
			"GameDirectorAI/Variant_Combat/Animation",
			"GameDirectorAI/Variant_Combat/Gameplay",
			"GameDirectorAI/Variant_Combat/Interfaces",
			"GameDirectorAI/Variant_Combat/UI",
			"GameDirectorAI/Variant_SideScrolling",
			"GameDirectorAI/Variant_SideScrolling/AI",
			"GameDirectorAI/Variant_SideScrolling/Gameplay",
			"GameDirectorAI/Variant_SideScrolling/Interfaces",
			"GameDirectorAI/Variant_SideScrolling/UI"
		});

		// Uncomment if you are using Slate UI
		// PrivateDependencyModuleNames.AddRange(new string[] { "Slate", "SlateCore" });

		// Uncomment if you are using online features
		// PrivateDependencyModuleNames.Add("OnlineSubsystem");

		// To include OnlineSubsystemSteam, add it to the plugins section in your uproject file with the Enabled attribute set to true
	}
}
