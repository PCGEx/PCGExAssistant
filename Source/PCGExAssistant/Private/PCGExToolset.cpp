// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#include "PCGExToolset.h"

#include "Dom/JsonObject.h"
#include "HAL/FileManager.h"
#include "Interfaces/IPluginManager.h"
#include "Kismet/KismetSystemLibrary.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "PCGSettings.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "UObject/UObjectIterator.h"

namespace PCGExAssistant
{
	// The offline knowledge bundle (rendered from the documentation card store) is the
	// PCGExKnowledge submodule pinned under Knowledge/, so data matches the installed
	// versions. Layout: manifest.json (bundle + plugin versions), search.json (flat
	// search surface, the only file SearchKnowledge loads), concepts.json, and one
	// per-plugin file of full node entries loaded lazily by GetNode, resolved through
	// a single nodes-then-aliases lookup (inherits/see/alias ids all resolve there,
	// cross-file included). The per-plugin file set comes from manifest.plugins[].file,
	// never a hardcoded list: it grows and shrinks per render (a plugin whose only
	// cards are parked produces no file).
	FString GetKnowledgeDir()
	{
		const TSharedPtr<IPlugin> Plugin = IPluginManager::Get().FindPlugin(TEXT("PCGExAssistant"));
		return Plugin ? FPaths::Combine(Plugin->GetBaseDir(), TEXT("Knowledge")) : FString();
	}

	// Bundle files are read-only between renders; cache parsed roots, invalidated by mtime.
	TSharedPtr<FJsonValue> LoadJsonFile(const FString& FileName)
	{
		struct FCacheEntry
		{
			FDateTime Stamp;
			TSharedPtr<FJsonValue> Root;
		};
		static TMap<FString, FCacheEntry> Cache;

		const FString Dir = GetKnowledgeDir();
		if (Dir.IsEmpty()) { return nullptr; }

		const FString Path = FPaths::Combine(Dir, FileName);
		const FDateTime Stamp = IFileManager::Get().GetTimeStamp(*Path);
		if (Stamp == FDateTime::MinValue()) { return nullptr; }

		if (const FCacheEntry* Found = Cache.Find(Path); Found && Found->Stamp == Stamp)
		{
			return Found->Root;
		}

		FString Raw;
		if (!FFileHelper::LoadFileToString(Raw, *Path)) { return nullptr; }

		TSharedPtr<FJsonValue> Root;
		if (!FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Raw), Root) || !Root.IsValid())
		{
			return nullptr;
		}

		Cache.Add(Path, FCacheEntry{Stamp, Root});
		return Root;
	}

	FString ToJsonString(const TArray<TSharedPtr<FJsonValue>>& Values)
	{
		FString Out;
		const TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&Out);
		FJsonSerializer::Serialize(Values, Writer);
		return Out;
	}

	FString ToJsonString(const TSharedRef<FJsonObject>& Object)
	{
		FString Out;
		const TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&Out);
		FJsonSerializer::Serialize(Object, Writer);
		return Out;
	}

	// Per-plugin bundle files, in manifest order. Missing/unreadable files are skipped:
	// a partially synced bundle should degrade to partial answers, not to none.
	TArray<TSharedPtr<FJsonObject>> LoadPluginFiles()
	{
		TArray<TSharedPtr<FJsonObject>> Out;

		const TSharedPtr<FJsonValue> Manifest = LoadJsonFile(TEXT("manifest.json"));
		const TSharedPtr<FJsonObject>* ManifestObj = nullptr;
		if (!Manifest || !Manifest->TryGetObject(ManifestObj)) { return Out; }

		const TArray<TSharedPtr<FJsonValue>>* Plugins = nullptr;
		if (!(*ManifestObj)->TryGetArrayField(TEXT("plugins"), Plugins)) { return Out; }

		for (const TSharedPtr<FJsonValue>& Entry : *Plugins)
		{
			const TSharedPtr<FJsonObject>* EntryObj = nullptr;
			FString File;
			if (!Entry || !Entry->TryGetObject(EntryObj) || !(*EntryObj)->TryGetStringField(TEXT("file"), File)) { continue; }

			const TSharedPtr<FJsonValue> Root = LoadJsonFile(File);
			const TSharedPtr<FJsonObject>* RootObj = nullptr;
			if (Root && Root->TryGetObject(RootObj)) { Out.Add(*RootObj); }
		}

		return Out;
	}

	int32 ScoreRow(const TSharedPtr<FJsonObject>& Row, const FString& Query, const TArray<FString>& Tokens)
	{
		auto Field = [&Row](const TCHAR* Name)
		{
			FString Value;
			Row->TryGetStringField(Name, Value);
			return Value;
		};

		const FString Name = Field(TEXT("name"));
		const FString Id = Field(TEXT("id"));
		const FString Category = Field(TEXT("category"));
		const FString Purpose = Field(TEXT("purpose"));
		const FString Situation = Field(TEXT("situation"));

		TArray<FString> Extras;
		for (const TCHAR* ArrayField : {TEXT("aliases"), TEXT("presets")})
		{
			const TArray<TSharedPtr<FJsonValue>>* Values = nullptr;
			if (!Row->TryGetArrayField(ArrayField, Values)) { continue; }
			for (const TSharedPtr<FJsonValue>& Value : *Values)
			{
				FString AsString;
				if (Value && Value->TryGetString(AsString)) { Extras.Add(AsString); }
			}
		}

		int32 Score = 0;

		if (Name.Equals(Query, ESearchCase::IgnoreCase)) { Score += 100; }
		else if (Name.Contains(Query)) { Score += 40; }
		if (Purpose.Contains(Query) || Situation.Contains(Query)) { Score += 15; }

		for (const FString& Token : Tokens)
		{
			if (Name.Contains(Token)) { Score += 30; }
			if (Category.Equals(Token, ESearchCase::IgnoreCase)) { Score += 25; }
			if (Id.Contains(Token)) { Score += 15; }
			if (Purpose.Contains(Token)) { Score += 8; }
			if (Situation.Contains(Token)) { Score += 8; }
			for (const FString& Extra : Extras)
			{
				if (Extra.Contains(Token))
				{
					Score += 10;
					break;
				}
			}
		}

		return Score;
	}
}

FString UPCGExToolset::SearchKnowledge(const FString& Query, const int32 MaxResults)
{
	using namespace PCGExAssistant;

	const FString Trimmed = Query.TrimStartAndEnd();
	if (Trimmed.IsEmpty())
	{
		UKismetSystemLibrary::RaiseScriptError(TEXT("Query must not be empty."));
		return FString();
	}

	const TSharedPtr<FJsonValue> Root = LoadJsonFile(TEXT("search.json"));
	const TArray<TSharedPtr<FJsonValue>>* Rows = nullptr;
	if (!Root || !Root->TryGetArray(Rows))
	{
		UKismetSystemLibrary::RaiseScriptError(TEXT("Knowledge bundle unavailable: Knowledge/search.json is missing or unreadable."));
		return FString();
	}

	TArray<FString> Tokens;
	Trimmed.ParseIntoArray(Tokens, TEXT(" "), true);
	for (FString& Token : Tokens) { Token.TrimStartAndEndInline(); }
	Tokens.RemoveAll([](const FString& Token) { return Token.Len() < 2; });

	struct FScored
	{
		int32 Score;
		TSharedPtr<FJsonValue> Row;
	};
	TArray<FScored> Scored;

	for (const TSharedPtr<FJsonValue>& RowValue : *Rows)
	{
		const TSharedPtr<FJsonObject>* RowObj = nullptr;
		if (!RowValue || !RowValue->TryGetObject(RowObj)) { continue; }
		if (const int32 Score = ScoreRow(*RowObj, Trimmed, Tokens); Score > 0)
		{
			Scored.Add(FScored{Score, RowValue});
		}
	}

	Scored.Sort([](const FScored& A, const FScored& B) { return A.Score > B.Score; });

	TArray<TSharedPtr<FJsonValue>> Results;
	const int32 Count = FMath::Clamp(MaxResults, 1, 50);
	for (int32 i = 0; i < Scored.Num() && i < Count; i++) { Results.Add(Scored[i].Row); }

	return ToJsonString(Results);
}

FString UPCGExToolset::GetNode(const FString& NodeId)
{
	using namespace PCGExAssistant;

	const FString Trimmed = NodeId.TrimStartAndEnd();
	if (Trimmed.IsEmpty())
	{
		UKismetSystemLibrary::RaiseScriptError(TEXT("NodeId must not be empty."));
		return FString();
	}

	const TArray<TSharedPtr<FJsonObject>> PluginFiles = LoadPluginFiles();
	if (PluginFiles.IsEmpty())
	{
		UKismetSystemLibrary::RaiseScriptError(TEXT("Knowledge bundle unavailable: Knowledge/manifest.json or its plugin files are missing or unreadable."));
		return FString();
	}

	// One resolution path: entries first, then aliases to a primary id, cross-file
	// included. "nodes" is the pre-1.x name of the entries container; drop the
	// fallback once no bundle in the wild still uses it.
	auto FindEntry = [&PluginFiles](const FString& Id) -> TSharedPtr<FJsonObject>
	{
		for (const TSharedPtr<FJsonObject>& File : PluginFiles)
		{
			const TSharedPtr<FJsonObject>* Entries = nullptr;
			const TSharedPtr<FJsonObject>* Entry = nullptr;
			if (!File->TryGetObjectField(TEXT("entries"), Entries) && !File->TryGetObjectField(TEXT("nodes"), Entries))
			{
				continue;
			}
			if ((*Entries)->TryGetObjectField(*Id, Entry))
			{
				return *Entry;
			}
		}
		return nullptr;
	};

	TSharedPtr<FJsonObject> Entry = FindEntry(Trimmed);

	if (!Entry)
	{
		for (const TSharedPtr<FJsonObject>& File : PluginFiles)
		{
			const TSharedPtr<FJsonObject>* Aliases = nullptr;
			FString Primary;
			if (File->TryGetObjectField(TEXT("aliases"), Aliases) && (*Aliases)->TryGetStringField(*Trimmed, Primary))
			{
				Entry = FindEntry(Primary);
				break;
			}
		}
	}

	if (!Entry)
	{
		UKismetSystemLibrary::RaiseScriptError(FString::Printf(
			TEXT("Unknown node id '%s'. Ids are C++ settings class names (e.g. UPCGExBevelPathSettings); use SearchKnowledge to find one."), *Trimmed));
		return FString();
	}

	return ToJsonString(Entry.ToSharedRef());
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
	TArray<TSharedPtr<FJsonValue>> Results;

	for (TObjectIterator<UClass> It; It; ++It)
	{
		const UClass* Class = *It;
		if (!Class->IsChildOf(UPCGSettings::StaticClass())) { continue; }
		if (Class->HasAnyClassFlags(CLASS_Abstract | CLASS_Deprecated | CLASS_NewerVersionExists)) { continue; }

		FString Module = Class->GetOutermost()->GetName();
		if (!Module.RemoveFromStart(TEXT("/Script/"))) { continue; } // native classes only

		const bool bIsFamily =
			Module.StartsWith(TEXT("PCGEx")) ||
			Module.Contains(TEXT("Kweave")) ||
			Module.Contains(TEXT("Watabou"));
		if (!bIsFamily) { continue; }
		if (!ModuleFilter.IsEmpty() && !Module.Contains(ModuleFilter)) { continue; }

		// Display names are truth; class names are legacy and have misled before.
		const UPCGSettings* CDO = Class->GetDefaultObject<UPCGSettings>();
		FString DisplayName = CDO ? CDO->GetDefaultNodeTitle().ToString() : FString();
		if (DisplayName.IsEmpty()) { DisplayName = Class->GetName(); }

		const TSharedPtr<FJsonObject> Entry = MakeShared<FJsonObject>();
		// Prefixed C++ name: knowledge-bundle ids are U-prefixed, and this field is the join key.
		Entry->SetStringField(TEXT("class"), FString::Printf(TEXT("%s%s"), Class->GetPrefixCPP(), *Class->GetName()));
		Entry->SetStringField(TEXT("displayName"), DisplayName);
		Entry->SetStringField(TEXT("module"), Module);
		if (Class->HasAnyClassFlags(CLASS_Hidden))
		{
			// Hidden palette nodes are deprecated-but-compatible; flag rather than omit.
			Entry->SetBoolField(TEXT("hidden"), true);
		}
		Results.Add(MakeShared<FJsonValueObject>(Entry));
	}

	Results.Sort([](const TSharedPtr<FJsonValue>& A, const TSharedPtr<FJsonValue>& B)
	{
		return A->AsObject()->GetStringField(TEXT("displayName")) < B->AsObject()->GetStringField(TEXT("displayName"));
	});

	return PCGExAssistant::ToJsonString(Results);
}

FString UPCGExToolset::AddSubnodeChain(const FString& GraphPath, const FString& TargetNodeName, const FString& ChainJson)
{
	// Phase 3 scaffold: intentionally unimplemented. Wrap in an editor transaction
	// (GEditor->BeginTransaction / EndTransaction) when this becomes real; the
	// assistant's own undo buffer is off by default.
	UKismetSystemLibrary::RaiseScriptError(TEXT("AddSubnodeChain is not implemented yet."));
	return FString();
}
