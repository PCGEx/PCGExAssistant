// Copyright 2026 Timothé Lapetite and contributors


#include "PCGExAssistantModule.h"

#include "PCGExToolset.h"
#include "ToolsetRegistry/UToolsetRegistry.h"

#define LOCTEXT_NAMESPACE "PCGExAssistant"

DEFINE_LOG_CATEGORY_STATIC(LogPCGExAssistant, Log, All);

void FPCGExAssistantModule::StartupModule()
{
	// False only while the ToolsetRegistry module is not loaded; the .uplugin dependency loads it before PostEngineInit.
	if (!UToolsetRegistry::IsAvailable())
	{
		UE_LOG(LogPCGExAssistant, Warning, TEXT("Toolset Registry unavailable; PCGEx toolset not registered."));
		return;
	}

	UToolsetRegistry::RegisterToolsetClass(UPCGExToolset::StaticClass());
}

void FPCGExAssistantModule::ShutdownModule()
{
	if (UToolsetRegistry::IsAvailable())
	{
		UToolsetRegistry::UnregisterToolsetClass(UPCGExToolset::StaticClass());
	}
}

#undef LOCTEXT_NAMESPACE

IMPLEMENT_MODULE(FPCGExAssistantModule, PCGExAssistant)
