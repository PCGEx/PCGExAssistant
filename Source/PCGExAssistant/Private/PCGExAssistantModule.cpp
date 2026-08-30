// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#include "PCGExAssistantModule.h"

#include "PCGExToolset.h"
#include "ToolsetRegistry/UToolsetRegistry.h"

#define LOCTEXT_NAMESPACE "PCGExAssistant"

DEFINE_LOG_CATEGORY_STATIC(LogPCGExAssistant, Log, All);

void FPCGExAssistantModule::StartupModule()
{
	// ToolsetRegistry is experimental and NoRedist; the .uplugin dependency
	// guarantees the module exists, but the subsystem may still be unavailable
	// (commandlets, -game, very early loading). Registering with zero valid
	// tools is also silently refused by the registry, so keep an eye on
	// LogToolsetRegistry when adding tools.
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
