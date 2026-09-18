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
			"Slate",
			"SlateCore",
			"NavigationSystem",
			"DeveloperSettings"
		});

		PrivateDependencyModuleNames.AddRange(new string[] { "Niagara" });

		// 실제 WBP 컴파일을 검증하는 자동화 테스트에서만 에디터 모듈을 사용합니다.
		if (Target.bBuildEditor)
		{
			PrivateDependencyModuleNames.AddRange(new string[] { "UnrealEd", "UMGEditor", "RenderCore", "RHI" });
		}

		// 기능별 폴더를 포함한 모듈 루트 기준의 include 경로를 사용합니다.
		PublicIncludePaths.Add(ModuleDirectory);

		PublicIncludePaths.AddRange(new string[] {
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
