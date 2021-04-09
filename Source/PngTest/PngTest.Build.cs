// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class PngTest : ModuleRules
{
	public PngTest(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
	
		PublicDependencyModuleNames.AddRange(new string[] { "Core", "CoreUObject", "Engine", "InputCore" });

        AddEngineThirdPartyPrivateStaticDependencies(Target,
            "zlib",
            "UElibPNG"
        );
    }
}
