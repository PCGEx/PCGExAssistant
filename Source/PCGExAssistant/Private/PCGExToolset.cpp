// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#include "PCGExToolset.h"

#include "Dom/JsonObject.h"
#include "Interfaces/IPluginManager.h"
#include "Kismet/KismetSystemLibrary.h"
#include "Serialization/JsonSerializer.h"

namespace PCGExAssistant
{
	// The offline knowledge bundle (rendered from the documentation card store)
	// ships inside the plugin, so the data always matches the installed version.
	FString GetKnowledgeDir()
	{
		const TSharedPtr<IPlugin> Plugin = IPluginManager::Get().FindPlugin(TEXT("PCGExAssistant"));
		return Plugin ? FPaths::Combine(Plugin->GetBaseDir(), TEXT("Knowledge")) : FString();
	}

	FString ToJsonString(const TArray<TSharedPtr<FJsonValue>>& Values)
	{
		FString Out;
		const TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&Out);
		FJsonSerializer::Serialize(Values, Writer);
		return Out;
	}
}

FString UPCGExToolset::SearchNodes(const FString& Query, const int32 MaxResults)
{
	// Phase 1 scaffold: served from the Knowledge/ bundle once render-bundle lands.
	UKismetSystemLibrary::RaiseScriptError(TEXT("SearchNodes is not implemented yet: the PCGEx knowledge bundle has not shipped with this build."));
	return FString();
}

FString UPCGExToolset::GetNode(const FString& NodeId)
{
	// Phase 1 scaffold: served from the Knowledge/ bundle once render-bundle lands.
	UKismetSystemLibrary::RaiseScriptError(TEXT("GetNode is not implemented yet: the PCGEx knowledge bundle has not shipped with this build."));
	return FString();
}

FString UPCGExToolset::GetInstalledPCGExPlugins()
{
	TArray<TSharedPtr<FJsonValue>> Results;

	for (const TSharedRef<IPlugin>& Plugin : IPluginManager::Get().GetDiscoveredPlugins())
	{
		const FString Name = Plugin->GetName();
		const bool bIsFamily =
			Name.StartsWith(TEXT("PCGEx")) ||
			Name == TEXT("KweaveBridge") ||
			Name == TEXT("WatabouBridge");

		if (!bIsFamily) { continue; }

		const FPluginDescriptor& Descriptor = Plugin->GetDescriptor();
		const TSharedPtr<FJsonObject> Entry = MakeShared<FJsonObject>();
		Entry->SetStringField(TEXT("name"), Name);
		Entry->SetStringField(TEXT("friendlyName"), Descriptor.FriendlyName);
		Entry->SetStringField(TEXT("version"), Descriptor.VersionName);
		Entry->SetBoolField(TEXT("enabled"), Plugin->IsEnabled());
		Results.Add(MakeShared<FJsonValueObject>(Entry));
	}

	return PCGExAssistant::ToJsonString(Results);
}

FString UPCGExToolset::ListRegisteredNodeTypes(const FString& ModuleFilter)
{
	// Phase 2 scaffold: walk UPCGSettings-derived classes whose package belongs to a
	// PCGEx-family module, honoring display names over class names.
	UKismetSystemLibrary::RaiseScriptError(TEXT("ListRegisteredNodeTypes is not implemented yet."));
	return FString();
}

FString UPCGExToolset::AddSubnodeChain(const FString& GraphPath, const FString& TargetNodeName, const FString& ChainJson)
{
	// Phase 3 scaffold: intentionally unimplemented. Wrap in an editor transaction
	// (GEditor->BeginTransaction / EndTransaction) when this becomes real; the
	// assistant's own undo buffer is off by default.
	UKismetSystemLibrary::RaiseScriptError(TEXT("AddSubnodeChain is not implemented yet."));
	return FString();
}
