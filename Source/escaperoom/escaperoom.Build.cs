// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class escaperoom : ModuleRules
{
	public escaperoom(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(new string[] { "Core", "CoreUObject", "Engine", "InputCore", "EnhancedInput", "GeometryCollectionEngine", "Niagara", "ActorSequence", "MovieScene", "UMG", "Slate", "SlateCore" });
	}
}
