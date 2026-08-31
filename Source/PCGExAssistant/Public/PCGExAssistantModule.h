// Copyright 2026 Timothé Lapetite and contributors


#pragma once

#include "CoreMinimal.h"
#include "Modules/ModuleInterface.h"

class FPCGExAssistantModule final : public IModuleInterface
{
public:
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;
};
