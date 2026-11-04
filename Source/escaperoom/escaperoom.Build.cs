// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class escaperoom : ModuleRules
{
	public escaperoom(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(new string[] { "Core", "CoreUObject", "Engine", "InputCore", "EnhancedInput", "GeometryCollectionEngine", "Niagara", "ActorSequence", "MovieScene", "UMG", "Slate", "SlateCore",
			// [EOS] EIK(EOS Integration Kit) 온라인 서브시스템으로 Device ID 로그인 (IOnlineIdentity)
			"OnlineSubsystem", "OnlineSubsystemUtils" });
	}
}
