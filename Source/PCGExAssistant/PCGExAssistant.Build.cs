// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

using UnrealBuildTool;

public class PCGExAssistant : ModuleRules
{
	public PCGExAssistant(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
		IWYUSupport = IWYUSupport.Full;

		PublicDependencyModuleNames.AddRange(new string[]
		{
			"Core",
			"CoreUObject",
			"Engine",
			"ToolsetRegistry", // UToolsetDefinition, UToolsetRegistry, AgentSkill
		});

		PrivateDependencyModuleNames.AddRange(new string[]
		{
			"UnrealEd",
			"Json",
			"JsonUtilities",
			"Projects", // IPluginManager -> plugin base dir for the Knowledge/ bundle
			"PCG",
		});
	}
}
