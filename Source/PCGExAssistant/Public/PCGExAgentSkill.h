// Copyright 2026 Timothé Lapetite and contributors


#pragma once

#include "CoreMinimal.h"
#include "ToolsetRegistry/AgentSkill.h"

#include "PCGExAgentSkill.generated.h"

/**
 * The PCGEx foundation skill. Its static text covers grounding, ids, placeability and graph editing; the authoring
 * section composes at every fetch from the mediator registries (ListFormats and DescribeGuides over the reflection
 * bridge), so the hosts, actions and rules it names are the installed toolkit's, never this plugin's.
 */
UCLASS()
class PCGEXASSISTANT_API UPCGExAgentSkill : public UAgentSkill
{
	GENERATED_BODY()

public:
	UPCGExAgentSkill();

protected:
	virtual FString GeneratePrompt_Implementation(const FString& InitialInstructions) const override;
};
