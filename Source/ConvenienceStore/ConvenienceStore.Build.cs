// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class ConvenienceStore : ModuleRules
{
	public ConvenienceStore(ReadOnlyTargetRules Target) : base(Target)
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
			"ConvenienceStore",
			"ConvenienceStore/Variant_Horror",
			"ConvenienceStore/Variant_Horror/UI",
			"ConvenienceStore/Variant_Shooter",
			"ConvenienceStore/Variant_Shooter/AI",
			"ConvenienceStore/Variant_Shooter/UI",
			"ConvenienceStore/Variant_Shooter/Weapons"
		});

		// Uncomment if you are using Slate UI
		// PrivateDependencyModuleNames.AddRange(new string[] { "Slate", "SlateCore" });

		// Uncomment if you are using online features
		// PrivateDependencyModuleNames.Add("OnlineSubsystem");

		// To include OnlineSubsystemSteam, add it to the plugins section in your uproject file with the Enabled attribute set to true
	}
}
