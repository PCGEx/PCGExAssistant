// Copyright 2026 Timothé Lapetite and contributors


#include "PCGExToolset.h"

#include "PCGExKnowledgeSearch.h"

#include "Dom/JsonObject.h"
#include "HAL/FileManager.h"
#include "Interfaces/IPluginManager.h"
#include "Kismet/KismetSystemLibrary.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "PCGGraph.h"
#include "PCGNode.h"
#include "PCGPin.h"
#include "PCGSettings.h"
#include "ScopedTransaction.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "StructUtils/PropertyBag.h"
#include "ToolsetRegistry/ToolsetLibrary.h"
#include "UObject/Package.h"
#include "UObject/UObjectIterator.h"

#define LOCTEXT_NAMESPACE "PCGExToolset"

namespace PCGExAssistant
{
#pragma region Bundle access

	// The offline knowledge bundle (rendered from the documentation card store) is the
	// PCGExKnowledge submodule pinned under Knowledge/, so data matches the installed
	// versions. Layout: manifest.json (bundle + plugin versions), search.json (flat
	// search surface, the only file SearchKnowledge loads), concepts.json (concept pages,
	// loaded by GetConcept), and one per-plugin file of full node entries loaded lazily
	// by GetNode, resolved through a single entries-then-aliases lookup (inherits/see/
	// alias ids all resolve there, cross-file included). The per-plugin file set comes
	// from manifest.plugins[].file, never a hardcoded list: it grows and shrinks per
	// render (a plugin whose only cards are parked produces no file).
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

	TSharedPtr<FJsonObject> ParseJsonObject(const FString& Json)
	{
		TSharedPtr<FJsonObject> Object;
		if (!FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Json), Object)) { return nullptr; }
		return Object;
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

	// One resolution path: entries first, then aliases to a primary id, cross-file
	// included. "nodes" is the pre-1.x name of the entries container; drop the
	// fallback once no bundle in the wild still uses it.
	TSharedPtr<FJsonObject> FindEntry(const TArray<TSharedPtr<FJsonObject>>& PluginFiles, const FString& Id)
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
	}

	TSharedPtr<FJsonObject> ResolveEntry(const TArray<TSharedPtr<FJsonObject>>& PluginFiles, const FString& Id)
	{
		if (const TSharedPtr<FJsonObject> Entry = FindEntry(PluginFiles, Id)) { return Entry; }

		for (const TSharedPtr<FJsonObject>& File : PluginFiles)
		{
			const TSharedPtr<FJsonObject>* Aliases = nullptr;
			FString Primary;
			if (File->TryGetObjectField(TEXT("aliases"), Aliases) && (*Aliases)->TryGetStringField(*Id, Primary))
			{
				return FindEntry(PluginFiles, Primary);
			}
		}
		return nullptr;
	}

	TSharedPtr<FJsonObject> FindConcept(const FString& Id)
	{
		const TSharedPtr<FJsonValue> Root = LoadJsonFile(TEXT("concepts.json"));
		const TArray<TSharedPtr<FJsonValue>>* Rows = nullptr;
		if (!Root || !Root->TryGetArray(Rows)) { return nullptr; }

		for (const TSharedPtr<FJsonValue>& RowValue : *Rows)
		{
			const TSharedPtr<FJsonObject>* Row = nullptr;
			FString RowId;
			if (RowValue && RowValue->TryGetObject(Row) && (*Row)->TryGetStringField(TEXT("id"), RowId) && RowId == Id)
			{
				return *Row;
			}
		}
		return nullptr;
	}

#pragma endregion

#pragma region Reflection

	// Prefixed C++ name: knowledge-bundle ids are U-prefixed, and this field is the join key.
	FString ClassId(const UClass* Class)
	{
		return FString::Printf(TEXT("%s%s"), Class->GetPrefixCPP(), *Class->GetName());
	}

	FString ModuleName(const UClass* Class)
	{
		FString Module = Class->GetOutermost()->GetName();
		Module.RemoveFromStart(TEXT("/Script/"));
		return Module;
	}

	FString DisplayName(const UClass* Class)
	{
		// Display names are truth; class names are legacy and have misled before.
		const UPCGSettings* CDO = Class->GetDefaultObject<UPCGSettings>();
		FString Name = CDO ? CDO->GetDefaultNodeTitle().ToString() : FString();
		return Name.IsEmpty() ? Class->GetName() : Name;
	}

	struct FResolvedClass
	{
		UClass* Class = nullptr;
		FString Error;
	};

	// Accepts the bundle id (U-prefixed), the bare class name, or an object path. Deprecated
	// classes are refused outright; hidden ones are left to the caller to judge.
	FResolvedClass ResolveSettingsClass(const FString& Name)
	{
		FResolvedClass Out;

		const FString Trimmed = Name.TrimStartAndEnd();
		if (Trimmed.IsEmpty())
		{
			Out.Error = TEXT("SettingsClass must not be empty.");
			return Out;
		}

		UClass* Class = FindFirstObject<UClass>(*Trimmed, EFindFirstObjectOptions::NativeFirst);
		if (!Class && Trimmed.Len() > 1 && Trimmed[0] == TEXT('U'))
		{
			Class = FindFirstObject<UClass>(*Trimmed.Mid(1), EFindFirstObjectOptions::NativeFirst);
		}

		if (!Class)
		{
			Out.Error = FString::Printf(TEXT("Unknown settings class '%s'. Use the C++ class name (e.g. UPCGExBevelPathSettings); ListRegisteredNodeTypes lists what this editor has."), *Trimmed);
			return Out;
		}
		if (!Class->IsChildOf(UPCGSettings::StaticClass()))
		{
			Out.Error = FString::Printf(TEXT("'%s' is not a PCG settings class."), *ClassId(Class));
			return Out;
		}
		if (Class->HasAnyClassFlags(CLASS_Abstract))
		{
			Out.Error = FString::Printf(TEXT("'%s' is abstract and cannot be placed; pick a concrete subclass."), *ClassId(Class));
			return Out;
		}
		if (Class->HasAnyClassFlags(CLASS_Deprecated | CLASS_NewerVersionExists))
		{
			Out.Error = FString::Printf(TEXT("'%s' is deprecated; search the knowledge base for its replacement."), *ClassId(Class));
			return Out;
		}

		Out.Class = Class;
		return Out;
	}

	const TCHAR* PinStatusName(const EPCGPinStatus Status)
	{
		switch (Status)
		{
		case EPCGPinStatus::Required: return TEXT("required");
		case EPCGPinStatus::Advanced: return TEXT("advanced");
		case EPCGPinStatus::OverrideOrUserParam: return TEXT("override");
		case EPCGPinStatus::Normal: return TEXT("normal");
		default:
			checkNoEntry();
			return TEXT("normal");
		}
	}

	const TSharedPtr<FJsonObject>* FindKnowledgePin(const TArray<TSharedPtr<FJsonValue>>* KnowledgePins, const FName Label)
	{
		if (!KnowledgePins) { return nullptr; }
		const FString LabelString = Label.ToString();
		for (const TSharedPtr<FJsonValue>& PinValue : *KnowledgePins)
		{
			const TSharedPtr<FJsonObject>* Pin = nullptr;
			FString PinLabel;
			if (PinValue && PinValue->TryGetObject(Pin) && (*Pin)->TryGetStringField(TEXT("label"), PinLabel) && PinLabel.Equals(LabelString, ESearchCase::IgnoreCase))
			{
				return Pin;
			}
		}
		return nullptr;
	}

	TArray<TSharedPtr<FJsonValue>> PinsToJson(const TArray<FPCGPinProperties>& Pins, const TArray<TSharedPtr<FJsonValue>>* KnowledgePins)
	{
		TArray<TSharedPtr<FJsonValue>> Out;
		for (const FPCGPinProperties& Pin : Pins)
		{
			if (Pin.bInvisiblePin) { continue; }

			const TSharedPtr<FJsonObject> Obj = MakeShared<FJsonObject>();
			Obj->SetStringField(TEXT("label"), Pin.Label.ToString());
			Obj->SetStringField(TEXT("types"), Pin.AllowedTypes.ToDisplayText().ToString());
			Obj->SetStringField(TEXT("status"), PinStatusName(Pin.PinStatus));
			Obj->SetBoolField(TEXT("multipleConnections"), Pin.AllowsMultipleConnections());
#if WITH_EDITORONLY_DATA
			if (!Pin.Tooltip.IsEmpty()) { Obj->SetStringField(TEXT("tooltip"), Pin.Tooltip.ToString()); }
#endif
			if (const TSharedPtr<FJsonObject>* Knowledge = FindKnowledgePin(KnowledgePins, Pin.Label))
			{
				Obj->SetObjectField(TEXT("knowledge"), *Knowledge);
			}
			Out.Add(MakeShared<FJsonValueObject>(Obj));
		}
		return Out;
	}

	// The node's own editable properties, nearest class first, stopping below UPCGSettings
	// whose members (seed, execution flags, ...) are not node params.
	TArray<FProperty*> WritableProperties(const UClass* Class, TArray<FString>& OutAdvanced)
	{
		TArray<FProperty*> Out;
		for (const UStruct* Current = Class; Current && Current != UPCGSettings::StaticClass(); Current = Current->GetSuperStruct())
		{
			for (TFieldIterator<FProperty> It(Current, EFieldIteratorFlags::ExcludeSuper, EFieldIteratorFlags::ExcludeDeprecated); It; ++It)
			{
				FProperty* Property = *It;
				if (!Property->HasAnyPropertyFlags(CPF_Edit) || Property->HasAnyPropertyFlags(CPF_EditConst)) { continue; }
				if (Property->HasAnyPropertyFlags(CPF_AdvancedDisplay)) { OutAdvanced.Add(Property->GetName()); }
				Out.Add(Property);
			}
		}
		return Out;
	}

	// JSON Schema of the writable params, in the shape UToolsetLibrary::SetObjectProperties
	// consumes. Built through a property bag so struct- and object-typed params render the
	// same way the engine's own toolsets render them.
	TSharedPtr<FJsonObject> PropertiesSchema(const TArray<FProperty*>& Properties)
	{
		TArray<FPropertyBagPropertyDesc> Descs;
		Descs.Reserve(Properties.Num());
		for (const FProperty* Property : Properties) { Descs.Emplace(Property->GetFName(), Property); }

		FInstancedPropertyBag Bag;
		Bag.AddProperties(Descs);

		const UPropertyBag* BagStruct = Bag.GetPropertyBagStruct();
		if (!BagStruct) { return MakeShared<FJsonObject>(); }

		const TSharedPtr<FJsonObject> Schema = ParseJsonObject(UToolsetLibrary::ListStructProperties(BagStruct, /*bUserVisiblePropertiesOnly=*/false));
		return Schema ? Schema : MakeShared<FJsonObject>();
	}

	// Copies the authored layer of a bundle setting onto the schema of the same property.
	void AnnotateProperties(const TSharedPtr<FJsonObject>& Schema, const TSharedPtr<FJsonObject>& Entry)
	{
		const TSharedPtr<FJsonObject>* SchemaProperties = nullptr;
		const TSharedPtr<FJsonObject>* Settings = nullptr;
		if (!Schema->TryGetObjectField(TEXT("properties"), SchemaProperties) || !Entry->TryGetObjectField(TEXT("settings"), Settings)) { return; }

		struct FCopiedField
		{
			const TCHAR* Source;
			const TCHAR* Target;
		};
		static const FCopiedField CopiedFields[] = {
			{TEXT("semantics"), TEXT("semantics")},
			{TEXT("trap"), TEXT("trap")},
			{TEXT("interacts_with"), TEXT("interactsWith")},
			{TEXT("see"), TEXT("see")},
			{TEXT("enum"), TEXT("enum")},
			{TEXT("edit_condition"), TEXT("editCondition")},
			{TEXT("tooltip"), TEXT("tooltip")},
		};

		for (const TPair<FString, TSharedPtr<FJsonValue>>& Pair : (*SchemaProperties)->Values)
		{
			const TSharedPtr<FJsonObject>* PropertySchema = nullptr;
			const TSharedPtr<FJsonObject>* Setting = nullptr;
			if (!Pair.Value || !Pair.Value->TryGetObject(PropertySchema) || !(*Settings)->TryGetObjectField(Pair.Key, Setting)) { continue; }

			const TSharedPtr<FJsonObject> Knowledge = MakeShared<FJsonObject>();
			for (const FCopiedField& Field : CopiedFields)
			{
				if (const TSharedPtr<FJsonValue> Value = (*Setting)->TryGetField(Field.Source)) { Knowledge->SetField(Field.Target, Value); }
			}
			if (Knowledge->Values.Num() > 0) { (*PropertySchema)->SetObjectField(TEXT("knowledge"), Knowledge); }
		}
	}

#pragma endregion

#pragma region Graph edits

	void RaiseError(const FString& Message)
	{
		UKismetSystemLibrary::RaiseScriptError(Message);
	}

	// Places one node the way the engine's PCG toolset does, minus the transaction, which the
	// caller owns so that multi-node operations roll back as a unit. Returns null with OutError
	// set; the graph is left as it was.
	UPCGNode* PlaceNode(UPCGGraph* Graph, UClass* Class, const FString& NodeName, const FString& JsonParams, const FString& NodeTitle, const int32 X, const int32 Y, FString& OutError)
	{
		UPCGSettings* DefaultSettings = nullptr;
		UPCGNode* Node = Graph->AddNodeOfType(Class, DefaultSettings);
		if (!Node)
		{
			OutError = FString::Printf(TEXT("The graph refused to create a '%s' node."), *ClassId(Class));
			return nullptr;
		}

		auto Fail = [Graph, Node, &OutError](const FString& Message) -> UPCGNode*
		{
			Graph->RemoveNode(Node);
			OutError = Message;
			return nullptr;
		};

		if (!NodeTitle.IsEmpty()) { Node->NodeTitle = FName(*NodeTitle); }

		if (!NodeName.IsEmpty())
		{
			// A failed real rename asserts; the test pass reports name clashes gracefully.
			if (!Node->Rename(*NodeName, nullptr, REN_Test) || !Node->Rename(*NodeName))
			{
				return Fail(FString::Printf(TEXT("Could not name the node '%s'; the name is probably already in use in this graph."), *NodeName));
			}
		}

		if (!JsonParams.IsEmpty() && !UToolsetLibrary::SetObjectProperties(Node->GetSettings(), JsonParams, EBypassContainerCheck::Yes))
		{
			return Fail(FString::Printf(TEXT("Could not apply params to '%s'. Check names and value shapes against GetNodeSchema('%s')."), *DisplayName(Class), *ClassId(Class)));
		}

		Node->SetNodePosition(X, Y);
		return Node;
	}

	void NotifyGraphEdited(UPCGGraph* Graph)
	{
		[[maybe_unused]] const bool bDirty = Graph->MarkPackageDirty();
		Graph->ForceNotificationForEditor(EPCGChangeType::ExternalModification);
	}

	FString PinLabels(const TArray<TObjectPtr<UPCGPin>>& Pins)
	{
		TArray<FString> Labels;
		for (const UPCGPin* Pin : Pins) { if (Pin) { Labels.Add(Pin->Properties.Label.ToString()); } }
		return FString::Join(Labels, TEXT(", "));
	}

	TSharedPtr<FJsonObject> NodeToJson(const UPCGNode* Node)
	{
		const TSharedPtr<FJsonObject> Obj = MakeShared<FJsonObject>();
		Obj->SetStringField(TEXT("name"), Node->GetName());
		Obj->SetStringField(TEXT("path"), Node->GetPathName());
		if (const UPCGSettings* Settings = Node->GetSettings())
		{
			Obj->SetStringField(TEXT("class"), ClassId(Settings->GetClass()));
			Obj->SetStringField(TEXT("displayName"), Node->GetNodeTitle(EPCGNodeTitleType::ListView).ToString());
		}
		return Obj;
	}

#pragma endregion
}

#pragma region Knowledge

FString UPCGExToolset::SearchKnowledge(const FString& Query, const int32 MaxResults)
{
	using namespace PCGExAssistant;

	const FString Trimmed = Query.TrimStartAndEnd();
	if (Trimmed.IsEmpty())
	{
		RaiseError(TEXT("Query must not be empty."));
		return FString();
	}

	const TSharedPtr<FJsonValue> Root = LoadJsonFile(TEXT("search.json"));
	if (!Root)
	{
		RaiseError(TEXT("Knowledge bundle unavailable: Knowledge/search.json is missing or unreadable."));
		return FString();
	}

	return ToJsonString(PCGExKnowledgeSearch::Search(Root, Trimmed, FMath::Clamp(MaxResults, 1, 50)));
}

FString UPCGExToolset::GetNode(const FString& NodeId)
{
	using namespace PCGExAssistant;

	const FString Trimmed = NodeId.TrimStartAndEnd();
	if (Trimmed.IsEmpty())
	{
		RaiseError(TEXT("NodeId must not be empty."));
		return FString();
	}

	const TArray<TSharedPtr<FJsonObject>> PluginFiles = LoadPluginFiles();
	if (PluginFiles.IsEmpty())
	{
		RaiseError(TEXT("Knowledge bundle unavailable: Knowledge/manifest.json or its plugin files are missing or unreadable."));
		return FString();
	}

	const TSharedPtr<FJsonObject> Entry = ResolveEntry(PluginFiles, Trimmed);
	if (!Entry)
	{
		if (FindConcept(Trimmed))
		{
			RaiseError(FString::Printf(TEXT("'%s' is a concept id, not a node id; use GetConcept."), *Trimmed));
		}
		else
		{
			RaiseError(FString::Printf(TEXT("Unknown node id '%s'. Ids are C++ settings class names (e.g. UPCGExBevelPathSettings); use SearchKnowledge to find one."), *Trimmed));
		}
		return FString();
	}

	return ToJsonString(Entry.ToSharedRef());
}

FString UPCGExToolset::GetConcept(const FString& ConceptId)
{
	using namespace PCGExAssistant;

	const FString Trimmed = ConceptId.TrimStartAndEnd();
	if (Trimmed.IsEmpty())
	{
		RaiseError(TEXT("ConceptId must not be empty."));
		return FString();
	}

	if (!LoadJsonFile(TEXT("concepts.json")))
	{
		RaiseError(TEXT("Knowledge bundle unavailable: Knowledge/concepts.json is missing or unreadable."));
		return FString();
	}

	const TSharedPtr<FJsonObject> Concept = FindConcept(Trimmed);
	if (!Concept)
	{
		RaiseError(FString::Printf(TEXT("Unknown concept id '%s'. Concept ids come from SearchKnowledge results of kind \"concept\"."), *Trimmed));
		return FString();
	}

	return ToJsonString(Concept.ToSharedRef());
}

#pragma endregion

#pragma region Introspection

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
	using namespace PCGExAssistant;

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

		const TSharedPtr<FJsonObject> Entry = MakeShared<FJsonObject>();
		Entry->SetStringField(TEXT("class"), ClassId(Class));
		Entry->SetStringField(TEXT("displayName"), DisplayName(Class));
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

	return ToJsonString(Results);
}

FString UPCGExToolset::GetNodeSchema(const FString& SettingsClass)
{
	using namespace PCGExAssistant;

	const FResolvedClass Resolved = ResolveSettingsClass(SettingsClass);
	if (!Resolved.Class)
	{
		RaiseError(Resolved.Error);
		return FString();
	}

	UClass* Class = Resolved.Class;
	const UPCGSettings* CDO = Class->GetDefaultObject<UPCGSettings>();
	if (!CDO)
	{
		RaiseError(FString::Printf(TEXT("'%s' has no default object to read pins from."), *ClassId(Class)));
		return FString();
	}

	// The bundle entry is optional: stock PCG classes and undocumented nodes still get a schema.
	const TSharedPtr<FJsonObject> Entry = ResolveEntry(LoadPluginFiles(), ClassId(Class));

	const TSharedPtr<FJsonObject> Out = MakeShared<FJsonObject>();
	Out->SetStringField(TEXT("class"), ClassId(Class));
	Out->SetStringField(TEXT("displayName"), DisplayName(Class));
	Out->SetStringField(TEXT("module"), ModuleName(Class));
	if (Class->HasAnyClassFlags(CLASS_Hidden)) { Out->SetBoolField(TEXT("hidden"), true); }

	const TArray<TSharedPtr<FJsonValue>>* KnowledgeInputs = nullptr;
	const TArray<TSharedPtr<FJsonValue>>* KnowledgeOutputs = nullptr;
	if (Entry)
	{
		const TSharedPtr<FJsonObject> Knowledge = MakeShared<FJsonObject>();
		for (const TCHAR* Field : {TEXT("id"), TEXT("name"), TEXT("url"), TEXT("classification"), TEXT("inherits")})
		{
			if (const TSharedPtr<FJsonValue> Value = Entry->TryGetField(Field)) { Knowledge->SetField(Field, Value); }
		}
		Out->SetObjectField(TEXT("knowledge"), Knowledge);

		const TSharedPtr<FJsonObject>* Pins = nullptr;
		if (Entry->TryGetObjectField(TEXT("pins"), Pins))
		{
			(*Pins)->TryGetArrayField(TEXT("inputs"), KnowledgeInputs);
			(*Pins)->TryGetArrayField(TEXT("outputs"), KnowledgeOutputs);
		}
	}

	Out->SetArrayField(TEXT("inputs"), PinsToJson(CDO->InputPinProperties(), KnowledgeInputs));
	Out->SetArrayField(TEXT("outputs"), PinsToJson(CDO->OutputPinProperties(), KnowledgeOutputs));

	TArray<FString> Advanced;
	const TSharedPtr<FJsonObject> Schema = PropertiesSchema(WritableProperties(Class, Advanced));
	if (Entry) { AnnotateProperties(Schema, Entry); }
	Out->SetObjectField(TEXT("properties"), Schema);

	TArray<TSharedPtr<FJsonValue>> AdvancedValues;
	for (const FString& Name : Advanced) { AdvancedValues.Add(MakeShared<FJsonValueString>(Name)); }
	Out->SetArrayField(TEXT("advancedProperties"), AdvancedValues);

	return ToJsonString(Out.ToSharedRef());
}

#pragma endregion

#pragma region Actuation

UPCGNode* UPCGExToolset::CreateNode(UPCGGraph* Graph, const FString& SettingsClass, const FString& NodeName, const FString& JsonParams, const FString& NodeTitle, const int32 XPosition, const int32 YPosition)
{
	using namespace PCGExAssistant;

	if (!Graph)
	{
		RaiseError(TEXT("Graph not found."));
		return nullptr;
	}

	const FResolvedClass Resolved = ResolveSettingsClass(SettingsClass);
	if (!Resolved.Class)
	{
		RaiseError(Resolved.Error);
		return nullptr;
	}
	if (Resolved.Class->HasAnyClassFlags(CLASS_Hidden))
	{
		RaiseError(FString::Printf(TEXT("'%s' is a hidden palette node (deprecated but still loadable); place its current replacement instead."), *ClassId(Resolved.Class)));
		return nullptr;
	}

	FScopedTransaction Transaction(LOCTEXT("CreateNode", "PCGEx Assistant: Create Node"));
	Graph->Modify();

	FString Error;
	UPCGNode* Node = PlaceNode(Graph, Resolved.Class, NodeName, JsonParams, NodeTitle, XPosition, YPosition, Error);
	if (!Node)
	{
		Transaction.Cancel();
		RaiseError(Error);
		return nullptr;
	}

	NotifyGraphEdited(Graph);
	return Node;
}

FString UPCGExToolset::AddSubnodes(UPCGGraph* Graph, UPCGNode* TargetNode, const FString& TargetPinLabel, const FString& SubnodesJson)
{
	using namespace PCGExAssistant;

	if (!Graph)
	{
		RaiseError(TEXT("Graph not found."));
		return FString();
	}
	if (!TargetNode || TargetNode->GetGraph() != Graph)
	{
		RaiseError(TEXT("TargetNode not found in that graph."));
		return FString();
	}
	if (!TargetNode->GetInputPin(FName(*TargetPinLabel)))
	{
		RaiseError(FString::Printf(TEXT("Target node has no input pin '%s'. Its input pins are: %s."), *TargetPinLabel, *PinLabels(TargetNode->GetInputPins())));
		return FString();
	}

	// Every item is validated before anything is created, so a bad list costs no edit.
	struct FSubnode
	{
		UClass* Class = nullptr;
		FString Name;
		FString Title;
		FString Params;
		FString OutputPin;
		FString Pin;
		int32 Feeds = INDEX_NONE; // Index into the list, or INDEX_NONE for the target.
		int32 Depth = 1;
	};
	TArray<FSubnode> Subnodes;

	TSharedPtr<FJsonValue> Root;
	const TArray<TSharedPtr<FJsonValue>>* Items = nullptr;
	if (!FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(SubnodesJson), Root) || !Root || !Root->TryGetArray(Items) || Items->IsEmpty())
	{
		RaiseError(TEXT("SubnodesJson must be a non-empty JSON array of { class, name?, params?, title?, feeds?, pin?, outputPin? }."));
		return FString();
	}

	for (int32 i = 0; i < Items->Num(); i++)
	{
		const TSharedPtr<FJsonObject>* Item = nullptr;
		if (!(*Items)[i] || !(*Items)[i]->TryGetObject(Item))
		{
			RaiseError(FString::Printf(TEXT("Subnode %d is not a JSON object."), i));
			return FString();
		}

		FSubnode& Subnode = Subnodes.AddDefaulted_GetRef();

		FString ClassName;
		(*Item)->TryGetStringField(TEXT("class"), ClassName);
		const FResolvedClass Resolved = ResolveSettingsClass(ClassName);
		if (!Resolved.Class)
		{
			RaiseError(FString::Printf(TEXT("Subnode %d: %s"), i, *Resolved.Error));
			return FString();
		}
		if (Resolved.Class->HasAnyClassFlags(CLASS_Hidden))
		{
			RaiseError(FString::Printf(TEXT("Subnode %d: '%s' is a hidden palette node (deprecated but still loadable); use its current replacement."), i, *ClassId(Resolved.Class)));
			return FString();
		}
		Subnode.Class = Resolved.Class;

		(*Item)->TryGetStringField(TEXT("name"), Subnode.Name);
		(*Item)->TryGetStringField(TEXT("title"), Subnode.Title);
		(*Item)->TryGetStringField(TEXT("outputPin"), Subnode.OutputPin);
		(*Item)->TryGetStringField(TEXT("pin"), Subnode.Pin);

		if (!Subnode.Name.IsEmpty())
		{
			for (int32 j = 0; j < i; j++)
			{
				if (Subnodes[j].Name == Subnode.Name)
				{
					RaiseError(FString::Printf(TEXT("Subnode %d: the name '%s' is used twice."), i, *Subnode.Name));
					return FString();
				}
			}
		}

		const TSharedPtr<FJsonObject>* Params = nullptr;
		if ((*Item)->TryGetObjectField(TEXT("params"), Params)) { Subnode.Params = ToJsonString(Params->ToSharedRef()); }
		else if ((*Item)->HasField(TEXT("params")))
		{
			RaiseError(FString::Printf(TEXT("Subnode %d: params must be a JSON object."), i));
			return FString();
		}

		FString Feeds;
		if ((*Item)->TryGetStringField(TEXT("feeds"), Feeds) && !Feeds.IsEmpty())
		{
			// Only earlier items may be fed: creation order then doubles as wiring order, and no cycle is expressible.
			for (int32 j = 0; j < i; j++)
			{
				if (!Subnodes[j].Name.IsEmpty() && Subnodes[j].Name == Feeds) { Subnode.Feeds = j; }
			}
			if (Subnode.Feeds == INDEX_NONE)
			{
				RaiseError(FString::Printf(TEXT("Subnode %d feeds '%s', which is not the name of an earlier subnode in the list."), i, *Feeds));
				return FString();
			}
			if (Subnode.Pin.IsEmpty())
			{
				RaiseError(FString::Printf(TEXT("Subnode %d feeds '%s' but names no pin; set pin to that subnode's input pin label."), i, *Feeds));
				return FString();
			}
			Subnode.Depth = Subnodes[Subnode.Feeds].Depth + 1;
		}
	}

	int32 TargetX = 0;
	int32 TargetY = 0;
	TargetNode->GetNodePosition(TargetX, TargetY);
	constexpr int32 ColumnSpacing = 400;
	constexpr int32 RowSpacing = 160;

	FScopedTransaction Transaction(LOCTEXT("AddSubnodes", "PCGEx Assistant: Add Subnodes"));
	Graph->Modify();

	TArray<UPCGNode*> Created;
	auto Abort = [Graph, &Created, &Transaction](const FString& Message) -> FString
	{
		for (int32 i = Created.Num() - 1; i >= 0; i--) { Graph->RemoveNode(Created[i]); }
		Transaction.Cancel();
		RaiseError(Message);
		return FString();
	};

	TArray<TSharedPtr<FJsonValue>> NodesJson;
	TArray<TSharedPtr<FJsonValue>> EdgesJson;

	for (int32 i = 0; i < Subnodes.Num(); i++)
	{
		const FSubnode& Subnode = Subnodes[i];

		FString Error;
		UPCGNode* Node = PlaceNode(Graph, Subnode.Class, Subnode.Name, Subnode.Params, Subnode.Title, TargetX - ColumnSpacing * Subnode.Depth, TargetY + RowSpacing * i, Error);
		if (!Node) { return Abort(FString::Printf(TEXT("Subnode %d: %s"), i, *Error)); }
		Created.Add(Node);

		UPCGPin* FromPin = nullptr;
		if (!Subnode.OutputPin.IsEmpty())
		{
			FromPin = Node->GetOutputPin(FName(*Subnode.OutputPin));
			if (!FromPin) { return Abort(FString::Printf(TEXT("Subnode %d: '%s' has no output pin '%s'. Its output pins are: %s."), i, *DisplayName(Subnode.Class), *Subnode.OutputPin, *PinLabels(Node->GetOutputPins()))); }
		}
		else
		{
			const TArray<TObjectPtr<UPCGPin>>& Outputs = Node->GetOutputPins();
			if (Outputs.Num() != 1 || !Outputs[0])
			{
				return Abort(FString::Printf(TEXT("Subnode %d: '%s' has %d output pins (%s); set outputPin to pick one."), i, *DisplayName(Subnode.Class), Outputs.Num(), *PinLabels(Outputs)));
			}
			FromPin = Outputs[0];
		}

		UPCGNode* ToNode = Subnode.Feeds == INDEX_NONE ? TargetNode : Created[Subnode.Feeds];
		const FName ToLabel = FName(Subnode.Feeds == INDEX_NONE ? *TargetPinLabel : *Subnode.Pin);
		UPCGPin* ToPin = ToNode->GetInputPin(ToLabel);
		if (!ToPin)
		{
			return Abort(FString::Printf(TEXT("Subnode %d: '%s' has no input pin '%s'. Its input pins are: %s."), i, *ToNode->GetName(), *ToLabel.ToString(), *PinLabels(ToNode->GetInputPins())));
		}
		if (!ToPin->IsCompatible(FromPin))
		{
			return Abort(FString::Printf(TEXT("Subnode %d: output '%s' (%s) is not compatible with input '%s' (%s) on '%s'."), i,
				*FromPin->Properties.Label.ToString(), *FromPin->Properties.AllowedTypes.ToDisplayText().ToString(),
				*ToLabel.ToString(), *ToPin->Properties.AllowedTypes.ToDisplayText().ToString(), *ToNode->GetName()));
		}
		if (!ToPin->CanConnect(FromPin))
		{
			return Abort(FString::Printf(TEXT("Subnode %d: input '%s' on '%s' takes a single connection and is already connected."), i, *ToLabel.ToString(), *ToNode->GetName()));
		}

		Graph->AddLabeledEdge(Node, FromPin->Properties.Label, ToNode, ToLabel);

		const TSharedPtr<FJsonObject> Edge = MakeShared<FJsonObject>();
		Edge->SetStringField(TEXT("from"), Node->GetName());
		Edge->SetStringField(TEXT("fromPin"), FromPin->Properties.Label.ToString());
		Edge->SetStringField(TEXT("to"), ToNode->GetName());
		Edge->SetStringField(TEXT("toPin"), ToLabel.ToString());
		EdgesJson.Add(MakeShared<FJsonValueObject>(Edge));
		NodesJson.Add(MakeShared<FJsonValueObject>(NodeToJson(Node)));
	}

	NotifyGraphEdited(Graph);

	const TSharedPtr<FJsonObject> Out = MakeShared<FJsonObject>();
	Out->SetObjectField(TEXT("target"), NodeToJson(TargetNode));
	Out->SetArrayField(TEXT("nodes"), NodesJson);
	Out->SetArrayField(TEXT("edges"), EdgesJson);
	return ToJsonString(Out.ToSharedRef());
}

#pragma endregion

#undef LOCTEXT_NAMESPACE
