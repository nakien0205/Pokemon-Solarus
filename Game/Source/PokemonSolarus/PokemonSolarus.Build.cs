// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class PokemonSolarus : ModuleRules
{
	public PokemonSolarus(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(new string[]
		{
			"Core",
			"CoreUObject",
			"Engine",
			"UMG"
		});

		PrivateDependencyModuleNames.Add("EnhancedInput");
	}
}
