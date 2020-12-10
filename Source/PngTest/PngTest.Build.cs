// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class PngTest : ModuleRules
{
	public PngTest(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
	
		PublicDependencyModuleNames.AddRange(new string[] { "Core", "CoreUObject", "Engine", "InputCore" });

		PrivateDependencyModuleNames.AddRange(new string[] { "ImageWrapper" });

        PublicIncludePaths.AddRange(new string[] {  System.IO.Path.Combine(EngineDirectory, "Source/Runtime/ImageWrapper/Private") });

        AddEngineThirdPartyPrivateStaticDependencies(Target,
            "zlib",
            "UElibPNG"
        );
    }
}
