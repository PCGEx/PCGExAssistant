// Copyright 2026 Timothé Lapetite and contributors


#pragma once

#include "CoreMinimal.h"
#include "ToolsetRegistry/UToolsetRegistry.h"

#include "PCGExToolset.generated.h"

class UPCGGraph;
class UPCGNode;

/**
 * Expert knowledge and tooling for PCGEx (PCG Extended Toolkit) and its sibling plugins
 * (Valency, Clusters Sketch, ZoneGraph). PCGEx extends stock PCG with clusters
 * (vtx/edge connectivity structures), path processing, filter ensembles, tensors,
 * pathfinding, asset staging and hundreds of specialized nodes.
 * Use this toolset whenever a task involves a PCGEx node, a cluster or path workflow,
 * or choosing between PCGEx nodes: it can search the full node knowledge base
 * (purpose, mechanism, pin contracts, settings semantics, known traps), read concept
 * documentation, report which PCGEx plugins and versions are installed, and place
 * PCGEx nodes in a PCG graph. The stock PCG toolset cannot create or describe PCGEx
 * nodes: use CreateNode and AddSubnodes here for those, and the PCG toolset's
 * UpdateNode and ConnectNodePins for everything once a node exists.
 * It also reads and writes PCGEx data as JSON (property schemas, Tuple and Distribute Tuple
 * nodes, asset collections) through ListFormats, DescribeFormat, ExportJson and ImportJson.
 * This is NOT the right toolset for stock PCG questions; use it when node names carry
 * PCGEx category prefixes such as "Cluster :", "Path :", "Vtx :", or when stock PCG
 * lacks the needed operation.
 */
UCLASS(BlueprintType)
class PCGEXASSISTANT_API UPCGExToolset : public UToolsetDefinition
{
	GENERATED_BODY()

public:
	// ~ Knowledge (offline bundle: the PCGExKnowledge submodule under Knowledge/)

	/**
	 * Search the PCGEx knowledge base: every node and concept page across the installed
	 * PCGEx plugins, matched against display names, purpose statements and canonical
	 * situations. Call this first when unsure which PCGEx node or system fits a task.
	 * Describe the operation or problem in plain words; exact node names also work.
	 * Results of kind "entry" have full detail available via GetNode, with classification
	 * telling nodes apart from providers, data assets and shared settings surfaces; results
	 * of kind "concept" answer how-does-this-system-work questions and have their full
	 * documentation page available via GetConcept.
	 * Only classifications "node" and "provider" are placeable in a PCG graph; "blueprint_node"
	 * entries are Blueprint utility nodes and never belong in a PCG graph.
	 * @param Query Free-text description of the operation or problem, e.g. "collapse overlapping points" or "find shortest path on a cluster".
	 * @param MaxResults Maximum number of matches to return.
	 * @return JSON array of matches ordered by relevance: { id, name, plugin, kind: "entry"|"concept", classification?, purpose, situation?, url? }.
	 */
	UFUNCTION(meta = (AICallable), Category = "PCGEx|Knowledge")
	static FString SearchKnowledge(const FString& Query, const int32 MaxResults = 10);

	/**
	 * Fetch the full knowledge entry for one PCGEx node: what it is for, its mechanism,
	 * exact pin contract (including conditional pins), settings semantics with traps and
	 * interactions, and related nodes and concepts.
	 * @param NodeId The node's C++ settings class name (e.g. UPCGExBevelPathSettings), as returned by SearchKnowledge or seen in live reflection. Variant classes documented on a shared page resolve through aliases; settings surfaces inherited from a shared base are referenced by id and fetched with further GetNode calls.
	 * @return JSON object with the complete entry, or an error if the id is unknown. The entry's classification governs placeability: only "node" and "provider" go in a PCG graph.
	 */
	UFUNCTION(meta = (AICallable), Category = "PCGEx|Knowledge")
	static FString GetNode(const FString& NodeId);

	/**
	 * Fetch a concept documentation page: how a PCGEx system works (clusters, filters,
	 * staging, tensors, pathfinding, ...) rather than what one node does. Use it for
	 * "how does X work" and "how do these nodes fit together" questions, and cite its url.
	 * @param ConceptId The concept id as returned by SearchKnowledge for results of kind "concept", e.g. "working-with-pcgex/filters".
	 * @return JSON object: { id, title, plugin, description?, outline?, url?, body? }. The body is the page prose in markdown when the installed bundle carries it; **Bold Name** in it references a node by display name, and [Title](concept:id) references another concept.
	 */
	UFUNCTION(meta = (AICallable), Category = "PCGEx|Knowledge")
	static FString GetConcept(const FString& ConceptId);

	// ~ Editor introspection

	/**
	 * List the PCGEx-family plugins present in this project and engine, with their versions.
	 * Call this before giving version-sensitive advice: the knowledge base shipped with this
	 * toolset matches the installed plugin versions.
	 * @return JSON array of { name, friendlyName, version, enabled }.
	 */
	UFUNCTION(meta = (AICallable), Category = "PCGEx|Introspection")
	static FString GetInstalledPCGExPlugins();

	/**
	 * List the PCGEx node types actually registered in this editor session, derived from
	 * live reflection rather than the knowledge base. Useful to confirm a node exists in
	 * the installed version before adding it to a graph.
	 * @param ModuleFilter Optional module name substring, e.g. "Valency"; empty lists all.
	 * @return JSON array of { class, displayName, module, hidden? } sorted by display name; hidden marks deprecated-but-compatible palette nodes.
	 */
	UFUNCTION(meta = (AICallable), Category = "PCGEx|Introspection")
	static FString ListRegisteredNodeTypes(const FString& ModuleFilter);

	/**
	 * The live, writable schema of a node settings class in this editor session: its pins
	 * as the installed version declares them and the JSON shape CreateNode and the PCG
	 * toolset's UpdateNode accept for its params, annotated with the knowledge base's
	 * semantics and traps where an entry exists. Read this before setting params or
	 * connecting pins: it is the ground truth for property names, types and pin labels.
	 * @param SettingsClass The C++ settings class name, e.g. UPCGExBevelPathSettings. Any UPCGSettings subclass is accepted, stock PCG included.
	 * @return JSON object: { class, displayName, module, hidden?, knowledge?: { id, name, url, classification, inherits? }, inputs: [{ label, types, status, multipleConnections, tooltip?, knowledge? }], outputs: [...], properties: JSON Schema of the writable params (each property may carry a knowledge object with semantics, trap, interactsWith, see, enum), advancedProperties: [names] }.
	 */
	UFUNCTION(meta = (AICallable), Category = "PCGEx|Introspection")
	static FString GetNodeSchema(const FString& SettingsClass);

	// ~ Actuation. Generic graph surgery on existing nodes (UpdateNode, ConnectNodePins,
	// RemoveNode, comments) belongs to the engine's PCG toolset and is not duplicated here.
	// What lives here is what that toolset cannot do: place nodes outside the PCG package,
	// and wire PCGEx provider ensembles in one go.

	/**
	 * Add a node of any settings class to a PCG graph, PCGEx nodes included. The stock PCG
	 * toolset's AddNode only knows nodes from the PCG package; use this for everything else.
	 * Read GetNodeSchema first for the exact param names and pin labels. Undoable.
	 * @param Graph The PCG graph asset to edit.
	 * @param SettingsClass The C++ settings class name, e.g. UPCGExBevelPathSettings (the knowledge base id).
	 * @param NodeName Optional unique object name for the node inside the graph; empty lets the engine pick one.
	 * @param JsonParams Optional JSON object of params to set, only the ones that differ from defaults. Shape per GetNodeSchema.
	 * @param NodeTitle Optional display title override; empty keeps the node's default title.
	 * @param XPosition Editor X position. Typical node size is 200.
	 * @param YPosition Editor Y position. Typical node size is 100.
	 * @return The created node, usable with the PCG toolset's ConnectNodePins, UpdateNode and GetNodeInfo.
	 */
	UFUNCTION(meta = (AICallable), Category = "PCGEx|Actuation")
	static UPCGNode* CreateNode(
		UPCGGraph* Graph,
		const FString& SettingsClass,
		const FString& NodeName = FString(),
		const FString& JsonParams = FString(),
		const FString& NodeTitle = FString(),
		const int32 XPosition = 0,
		const int32 YPosition = 0);

	/**
	 * Create a set of PCGEx provider nodes (filters, heuristics, tensors, samplers, ...) and
	 * wire them into a target node's input pin in one undoable operation, laid out beside
	 * the target. Subnodes feed the target pin by default; a subnode can instead feed another
	 * subnode declared before it, for nested ensembles. Each subnode must expose exactly one
	 * output pin unless outputPin names which to use. Fails as a whole if any node cannot be
	 * created or any connection is incompatible; nothing is left behind.
	 * @param Graph The PCG graph asset to edit.
	 * @param TargetNode The existing node receiving the ensemble.
	 * @param TargetPinLabel The target's input pin label, e.g. "Filters" or "Heuristics"; read it from GetNode or GetNodeSchema.
	 * @param SubnodesJson JSON array of { class, name?, params?, title?, feeds?, pin?, outputPin? }: class is the settings class name; params a JSON object per GetNodeSchema; feeds the name of an earlier subnode to connect into instead of the target; pin that subnode's input pin label (required when feeds is set); outputPin which output pin to connect from when the subnode has several.
	 * @return JSON object: { target: { name, path }, nodes: [{ name, class, displayName, path }], edges: [{ from, fromPin, to, toPin }] }.
	 */
	UFUNCTION(meta = (AICallable), Category = "PCGEx|Actuation")
	static FString AddSubnodes(UPCGGraph* Graph, UPCGNode* TargetNode, const FString& TargetPinLabel, const FString& SubnodesJson);

	// ~ Authoring. PCGEx data as JSON through the PCGExMediator formats: a document is
	// { "format", "version", "data" }; an object document's data is { "class", "members": { <member>: <body> } }.
	// Served by PCGExtendedToolkit's UPCGExMediatorLibrary, reached by reflection: this plugin ships alone.

	/**
	 * List the JSON formats and the object bindings the PCGEx mediator registers in this editor
	 * session: which structs have a JSON shape (property schemas, override sets, collection
	 * entries, ...) and which asset or node classes can be exported and imported as a whole,
	 * with the members that travel. Call this before DescribeFormat when unsure what exists.
	 * @return JSON object: { formats: [{ id, version, struct, summary }], bindings: [{ class, members, summary }] }.
	 */
	UFUNCTION(meta = (AICallable), Category = "PCGEx|Authoring")
	static FString ListFormats();

	/**
	 * The JSON Schema (draft 2020-12) of a mediator format or of a bound class's object document.
	 * Read it before writing a document for ImportJson: it is the ground truth for key names, value
	 * shapes (vectors as arrays, rotators as {pitch, yaw, roll}, enums by name) and the per-type
	 * value shapes of property schemas ($defs/values).
	 * @param FormatIdOrClass A format id from ListFormats (e.g. pcgex.property-schema) or a bound class name or path (e.g. UPCGExTupleSettings).
	 * @return JSON Schema object, or an error when nothing matches.
	 */
	UFUNCTION(meta = (AICallable), Category = "PCGEx|Authoring")
	static FString DescribeFormat(const FString& FormatIdOrClass);

	/**
	 * Export a bound object as a JSON document: an asset by object path (a Property Schema asset,
	 * an asset collection) or a node's settings as "<graph object path>:<node title>" (Tuple,
	 * Distribute Tuple). The result is what ImportJson accepts back, so edit and re-import it.
	 * @param Target Object path of the asset, or <graph object path>:<node title or name> for a node.
	 * @return The JSON document { format, version, data }, or an error with diagnostics when the target does not resolve or has no binding.
	 */
	UFUNCTION(meta = (AICallable), Category = "PCGEx|Authoring")
	static FString ExportJson(const FString& Target);

	/**
	 * Import a JSON document into a bound object, undoable. Members present in the document
	 * replace the host's (a property schema document is the whole schema; an override set is the
	 * full enabled set); members absent are left alone. Nothing changes unless the whole document
	 * decodes, and every problem is reported with its path in the document. Read DescribeFormat
	 * first; ExportJson gives a correct starting document.
	 * @param Target Object path of the asset, or <graph object path>:<node title or name> for a node.
	 * @param Json The document, as produced by ExportJson or written to DescribeFormat's schema.
	 * @return JSON object { ok, diagnostics: [{ severity, path, message }] }; on failure the call also raises an error carrying the diagnostics.
	 */
	UFUNCTION(meta = (AICallable), Category = "PCGEx|Authoring")
	static FString ImportJson(const FString& Target, const FString& Json);
};
