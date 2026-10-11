// Copyright 2026 Timothé Lapetite and contributors


#include "PCGExAgentSkill.h"

#include "PCGExMediatorBridge.h"

#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"

namespace PCGExAgentSkillPrompt
{
	TSharedPtr<FJsonObject> Parse(const FString& Json)
	{
		TSharedPtr<FJsonObject> Object;
		if (!FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Json), Object)) { return nullptr; }
		return Object;
	}

	FString Pretty(const TSharedRef<FJsonObject>& Object)
	{
		FString Out;
		const TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&Out);
		FJsonSerializer::Serialize(Object, Writer);
		return Out;
	}

	FString ShortName(const FString& ClassPath)
	{
		int32 Dot = INDEX_NONE;
		return ClassPath.FindLastChar(TEXT('.'), Dot) ? ClassPath.Mid(Dot + 1) : ClassPath;
	}

	// One line per registered binding and action: what this editor can author right now.
	void AppendInventory(const FJsonObject& List, FString& Out)
	{
		const TArray<TSharedPtr<FJsonValue>>* Bindings = nullptr;
		if (List.TryGetArrayField(TEXT("bindings"), Bindings))
		{
			for (const TSharedPtr<FJsonValue>& Item : *Bindings)
			{
				const TSharedPtr<FJsonObject>* Binding = nullptr;
				if (!Item.IsValid() || !Item->TryGetObject(Binding)) { continue; }
				FString Class, Summary;
				(*Binding)->TryGetStringField(TEXT("class"), Class);
				(*Binding)->TryGetStringField(TEXT("summary"), Summary);
				Out += FString::Printf(TEXT("- Bound host %s (%s): %s\n"), *ShortName(Class), *Class, *Summary);
			}
		}

		const TArray<TSharedPtr<FJsonValue>>* Actions = nullptr;
		if (List.TryGetArrayField(TEXT("actions"), Actions))
		{
			for (const TSharedPtr<FJsonValue>& Item : *Actions)
			{
				const TSharedPtr<FJsonObject>* Action = nullptr;
				if (!Item.IsValid() || !Item->TryGetObject(Action)) { continue; }
				FString Id, Target, Summary;
				(*Action)->TryGetStringField(TEXT("id"), Id);
				(*Action)->TryGetStringField(TEXT("target"), Target);
				(*Action)->TryGetStringField(TEXT("summary"), Summary);
				Out += FString::Printf(TEXT("- Action %s on %s (RunAction): %s\n"), *Id, *ShortName(Target), *Summary);
			}
		}
	}

	// Each domain's guide verbatim, then its example documents pretty-printed.
	void AppendGuides(const FJsonObject& Guides, FString& Out)
	{
		const TArray<TSharedPtr<FJsonValue>>* Items = nullptr;
		if (!Guides.TryGetArrayField(TEXT("guides"), Items)) { return; }
		for (const TSharedPtr<FJsonValue>& Item : *Items)
		{
			const TSharedPtr<FJsonObject>* Guide = nullptr;
			if (!Item.IsValid() || !Item->TryGetObject(Guide)) { continue; }
			FString Domain, Text;
			(*Guide)->TryGetStringField(TEXT("domain"), Domain);
			(*Guide)->TryGetStringField(TEXT("text"), Text);

			TArray<FString> Hosts;
			const TArray<TSharedPtr<FJsonValue>>* HostValues = nullptr;
			if ((*Guide)->TryGetArrayField(TEXT("hosts"), HostValues))
			{
				for (const TSharedPtr<FJsonValue>& Host : *HostValues) { if (Host.IsValid()) { Hosts.Add(ShortName(Host->AsString())); } }
			}

			Out += FString::Printf(TEXT("\n%s GUIDE (hosts: %s)\n%s\n"), *Domain.ToUpper(), *FString::Join(Hosts, TEXT(", ")), *Text);

			const TArray<TSharedPtr<FJsonValue>>* Examples = nullptr;
			if (!(*Guide)->TryGetArrayField(TEXT("examples"), Examples)) { continue; }
			for (const TSharedPtr<FJsonValue>& Example : *Examples)
			{
				const TSharedPtr<FJsonObject>* Doc = nullptr;
				if (Example.IsValid() && Example->TryGetObject(Doc)) { Out += TEXT("Example ImportJson document:\n") + Pretty(Doc->ToSharedRef()) + TEXT("\n"); }
			}
		}
	}
}

UPCGExAgentSkill::UPCGExAgentSkill()
{
	Description =
		TEXT("Foundation skill for PCGEx (PCG Extended Toolkit) and its sibling plugins (Valency, Cluster Sketch, ZoneGraph). ")
		TEXT("Use this skill whenever a task mentions PCGEx, involves clusters (vtx/edge connectivity over points), path processing, ")
		TEXT("pathfinding on point data, filter ensembles, tensors, or asset staging beyond stock PCG, touches any node whose name ")
		TEXT("carries a PCGEx category prefix such as \"Cluster :\", \"Path :\", \"Vtx :\", \"Filter :\", or asks to create, fill or ")
		TEXT("edit PCGEx data (the assets and data-hosting nodes the PCGEx toolset authors as JSON). PCGEx questions must be answered ")
		TEXT("from its knowledge-base toolset, never from prior knowledge. Not needed for purely stock PCG work involving no PCGEx nodes.");

	Instructions =
		TEXT("GROUNDING (ALWAYS)\n")
		TEXT("- Never answer PCGEx questions from your own training knowledge. It is sparse and frequently wrong about this toolkit. ")
		TEXT("Every claim about a PCGEx node's purpose, pins, settings or behavior must come from the PCGExAssistant.PCGExToolset tools.\n")
		TEXT("- Start with SearchKnowledge (free-text; describe the operation or problem in plain words, it matches node names, purposes ")
		TEXT("and canonical situations across all installed PCGEx plugins). Fetch detail with GetNode for entries and GetConcept for concept pages.\n")
		TEXT("- GetNode returns source-verified data: purpose, mechanism, exact pin contract (including conditional pins), per-setting ")
		TEXT("semantics, known traps, and a documentation url worth citing to the user.\n")
		TEXT("- GetConcept returns a system's documentation page (clusters, filters, staging, tensors, pathfinding, ...) with its prose ")
		TEXT("when the bundle carries it; cite its url.\n")
		TEXT("\nIDS\n")
		TEXT("- Entry ids are C++ settings class names, e.g. UPCGExBevelPathSettings for \"Path : Bevel\". The same name is what ")
		TEXT("ListRegisteredNodeTypes reports from live reflection and what PCG graph tooling needs to place the node. Variant and helper ")
		TEXT("class names resolve through aliases; pass them to GetNode as-is.\n")
		TEXT("- Settings inherited from a shared base are not repeated per node: follow the entry's \"inherits\" list with further GetNode ")
		TEXT("calls when a setting is not found.\n")
		TEXT("\nPLACEABILITY\n")
		TEXT("- Only classifications \"node\" and \"provider\" may be placed in a PCG graph.\n")
		TEXT("- \"blueprint_node\" entries are Blueprint utility nodes; never place them in a PCG graph. \"asset\" entries are data assets ")
		TEXT("created in the Content Browser or through CreateAsset. \"shared_struct\" entries document settings blocks, not placeable things.\n")
		TEXT("\nWORKFLOW\n")
		TEXT("- Which node fits: SearchKnowledge, then GetNode on the best hits, then recommend by canonical situation, not by name similarity.\n")
		TEXT("- How a system works (clusters, filter ensembles, staging, tensors): search hits of kind \"concept\" resolve through GetConcept; use and cite them.\n")
		TEXT("- Version-sensitive advice: call GetInstalledPCGExPlugins first. The knowledge base shipped with the assistant matches the installed plugin versions.\n")
		TEXT("- Confirm a node exists in this install with ListRegisteredNodeTypes before proposing it; hidden entries are deprecated, do not recommend them.\n")
		TEXT("\nEDITING GRAPHS\n")
		TEXT("- The stock PCG toolset (PCGToolset.PCGToolset) cannot create or describe PCGEx nodes: its AddNode and GetNativeNodeSchema only ")
		TEXT("know nodes from the PCG package. Create PCGEx nodes with CreateNode here, passing the class name from this knowledge base; wire ")
		TEXT("provider ensembles (filters, heuristics, tensors, samplers) into a node with AddSubnodes in one call.\n")
		TEXT("- Once a node exists, the PCG toolset's UpdateNode, ConnectNodePins, GetNodeInfo and RemoveNode work on it like on any node; ")
		TEXT("CreateNode returns the node for them.\n")
		TEXT("- Read GetNodeSchema BEFORE setting params or connecting pins: it is the live, writable truth for property names, JSON value ")
		TEXT("shapes and pin labels in the installed version, annotated with the knowledge base's semantics and traps. Many PCGEx nodes ")
		TEXT("have conditional pins that only exist under specific settings; GetNode's pin contract says which.\n")
		TEXT("\nAUTHORING DATA AS JSON\n")
		TEXT("- PCGEx data (assets and data-hosting nodes) is authored as JSON documents through this toolset, never through Content Browser ")
		TEXT("instructions. The hosts, actions and per-domain rules are listed below, live from the installed toolkit.\n")
		TEXT("- Chain: ListFormats (what is bound) -> DescribeFormat (the JSON Schema of a format or bound class) and DescribeTarget (one ")
		TEXT("instance's resolved properties, defaults and choices) -> CreateAsset when the asset does not exist -> ImportJson -> SaveAsset. ")
		TEXT("Undo reverts the last import whole. RunAction runs a registered action on a target, undoable.\n")
		TEXT("- Start from ExportJson when the target exists: its output is sparse (values at struct default are omitted) and ImportJson ")
		TEXT("accepts it back, so edit and re-import. Nothing applies unless the whole document decodes; diagnostics carry the path of every problem.\n")
		TEXT("- Merge rules: members present in the document replace the host's, absent members are untouched. Array members merge BY POSITION: ")
		TEXT("rows beyond the host are created, rows the document lacks are removed, and inside a row an absent key keeps the existing value.\n")
		TEXT("- A node whose class is a bound host is edited with ExportJson / ImportJson, targeted as \"<graph object path>:<node title or ")
		TEXT("name>\". GetNodeSchema describes its mediator-backed params in the document shape (marked by a $comment naming the format), ")
		TEXT("which the PCG toolset's UpdateNode accepts as well.\n")
		TEXT("- Values travel in the mediator dialect: enums by enumerator name, vectors as arrays, rotators as {pitch, yaw, roll}; ")
		TEXT("DescribeFormat is the truth for each.\n")
		TEXT("\nVOCABULARY\n")
		TEXT("- Cluster, not graph. Vtx, not vertex. PCGEx, not PCGEX. Node display names carry category prefixes (\"Path : Bevel\", ")
		TEXT("\"Cluster : Connect\"); use display names when talking to the user, class names when calling tools.\n");
}

FString UPCGExAgentSkill::GeneratePrompt_Implementation(const FString& InitialInstructions) const
{
	using namespace PCGExAssistant;
	using namespace PCGExAgentSkillPrompt;

	FString Out = InitialInstructions;
	Out += TEXT("\nAUTHORING: WHAT THIS EDITOR BINDS (live from the PCGEx mediator)\n");

	FString Result, Error;
	if (!MediatorBridge::Call(TEXT("ListFormats"), {}, Result, Error) || MediatorBridge::IsFailure(Result, Error))
	{
		Out += TEXT("- PCGEx data authoring is unavailable in this editor: ") + Error + TEXT("\n");
		return Out;
	}
	if (const TSharedPtr<FJsonObject> List = Parse(Result)) { AppendInventory(*List, Out); }

	if (!MediatorBridge::Call(TEXT("DescribeGuides"), {}, Result, Error) || MediatorBridge::IsFailure(Result, Error))
	{
		Out += TEXT("- No domain guides: ") + Error + TEXT("\n");
		return Out;
	}
	if (const TSharedPtr<FJsonObject> Guides = Parse(Result)) { AppendGuides(*Guides, Out); }
	return Out;
}
