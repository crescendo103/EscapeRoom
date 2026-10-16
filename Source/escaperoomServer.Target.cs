// Dedicated server build target (AWS). Building this requires a source-built Unreal Engine.

using UnrealBuildTool;
using System.Collections.Generic;

public class escaperoomServerTarget : TargetRules
{
	public escaperoomServerTarget(TargetInfo Target) : base(Target)
	{
		Type = TargetType.Server;
		DefaultBuildSettings = BuildSettingsVersion.V5;
		IncludeOrderVersion = EngineIncludeOrderVersion.Unreal5_4;
		ExtraModuleNames.Add("escaperoom");
	}
}
