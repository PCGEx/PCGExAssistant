// Copyright 2026 Timothé Lapetite and contributors


#pragma once

#include "CoreMinimal.h"

class FJsonValue;

namespace PCGExKnowledgeSearch
{
	/**
	 * Ranks the rows of a parsed search.json array against a free-text query and returns the
	 * best rows, best first. The index is rebuilt only when Root changes identity, so callers
	 * pass the cached bundle root. Game thread only.
	 */
	TArray<TSharedPtr<FJsonValue>> Search(const TSharedPtr<FJsonValue>& Root, const FString& Query, int32 MaxResults);
}
