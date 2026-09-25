// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class MinecraftClone : ModuleRules
{
	public MinecraftClone(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(new string[] {
			"Core",
			"CoreUObject",
			"Engine",
			"InputCore",
			"EnhancedInput",
			"UMG",
			"Slate",
			"SlateCore",
			"AIModule",
			"GameplayTasks",
			"NavigationSystem",
			"Json",
			"JsonUtilities",
			"ProceduralMeshComponent"
		});

		// ImageCore: FImage za citanje piksela iz Texture->Source (ItemMeshExtruder)
		PrivateDependencyModuleNames.AddRange(new string[] { "ImageCore" });

		PublicIncludePaths.AddRange(new string[] {
			"MinecraftClone",
			"MinecraftClone/Voxel",
			"MinecraftClone/Voxel/AI"
		});

		// Uncomment if you are using Slate UI
		// PrivateDependencyModuleNames.AddRange(new string[] { "Slate", "SlateCore" });

		// Uncomment if you are using online features
		// PrivateDependencyModuleNames.Add("OnlineSubsystem");

		// To include OnlineSubsystemSteam, add it to the plugins section in your uproject file with the Enabled attribute set to true
	}
}
