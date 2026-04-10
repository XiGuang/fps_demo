// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class fps_demo : ModuleRules
{
	public fps_demo(ReadOnlyTargetRules Target) : base(Target)
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

		PrivateDependencyModuleNames.AddRange(new string[]
		{
			"GameplayAbilities",
			"GameplayTags",
			"GameplayTasks",
			"NavigationSystem",
		});

		PublicIncludePaths.AddRange(new string[] {
			"fps_demo",
			"fps_demo/Variant_Horror",
			"fps_demo/Variant_Horror/UI",
			"fps_demo/Variant_Shooter",
			"fps_demo/Variant_Shooter/AI",
			"fps_demo/Variant_Shooter/UI",
			"fps_demo/Variant_Shooter/Weapons"
		});

		// Uncomment if you are using Slate UI
		// PrivateDependencyModuleNames.AddRange(new string[] { "Slate", "SlateCore" });

		// Uncomment if you are using online features
		// PrivateDependencyModuleNames.Add("OnlineSubsystem");

		// To include OnlineSubsystemSteam, add it to the plugins section in your uproject file with the Enabled attribute set to true
	}
}
