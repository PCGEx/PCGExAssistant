// Copyright 2026 Timothé Lapetite and contributors


#pragma once

#include "CoreMinimal.h"
#include "ToolsetRegistry/UToolsetRegistry.h"

#include "PCGExToolset.generated.h"

class UPCGGraph;
class UPCGNode;

/**
 * Expert knowledge and tooling for PCGEx (PCG Extended Toolkit) and its sibling plugins
 * (Valency, Clusters Sketch, ZoneGraph), with two jobs: working with PCGEx nodes in PCG graphs,
 * and authoring PCGEx data as JSON (the assets and data-hosting nodes ListFormats reports; the
 * PCGEx skill carries each domain's guide). PCGEx extends stock PCG with clusters (vtx/edge
 * connectivity structures), path
 * processing, filter ensembles, tensors, pathfinding, asset staging and hundreds of specialized nodes.
 * Nodes: it searches the full node knowledge base (purpose, mechanism, pin contracts, settings
 * semantics, known traps), reads concept documentation, reports which PCGEx plugins and versions
 * are installed, and places PCGEx nodes in a PCG graph. The stock PCG toolset cannot create or
 * describe PCGEx nodes: use CreateNode and AddSubnodes here for those, and the PCG toolset's
 * UpdateNode and ConnectNodePins for everything once a node exists.
 * Data: ListFormats and DescribeFormat tell which assets and nodes have a JSON document and its
 * schema, DescribeTarget what one instance offers (resolved properties, defaults, choices),
 * ExportJson and ImportJson read and write the document, CreateAsset and SaveAsset create and
 * persist assets, RunAction runs editor actions on a target, and Undo reverts the last edit.
 * Use it whenever a task mentions PCGEx, involves a cluster or path workflow, asks to create,
 * fill or edit PCGEx data (assets or data-hosting nodes), names a node with a PCGEx category
 * prefix such as "Cluster :", "Path :", "Vtx :", or needs an operation stock PCG lacks. It is NOT
 * the right toolset for stock PCG questions.
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
	 * Params whose struct has a PCGEx mediator format are described in that format's shape, the one UpdateNode and
	 * ImportJson accept, marked by a $comment naming the format; their raw reflection is never shown.
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
	// Served by PCGExToolset's UPCGExMediatorLibrary, reached by reflection: this plugin ships alone.

	/**
	 * List the JSON formats and the object bindings the PCGEx mediator registers in this editor
	 * session: which structs have a JSON shape (property schemas, override sets, collection
	 * entries, ...) and which asset or node classes can be exported and imported as a whole,
	 * with the members that travel, and the registered actions (id, target, summary; DescribeFormat(<action id>)
	 * has an action's params schema). Call this before DescribeFormat when unsure what exists.
	 * @return JSON object: { formats: [{ id, version, struct, summary }], bindings: [{ class, members, summary }], actions: [{ id, target, summary }] }.
	 */
	UFUNCTION(meta = (AICallable), Category = "PCGEx|Authoring")
	static FString ListFormats();

	/**
	 * The JSON Schema (draft 2020-12) of a mediator format, of an action's params, of a bound class's
	 * object document, or of any struct by path. Read it before writing a document for ImportJson or
	 * params for RunAction: it is the ground truth for key names, value shapes (vectors as arrays,
	 * rotators as {pitch, yaw, roll}, enums by name) and the per-type value shapes of property schemas
	 * ($defs/values). Schemas collapse nested bodies (component descriptors, variations, embedded
	 * property schemas) to a one-line fragment whose $comment names what to pass here for the full
	 * shape; ask for it only when you need to author those fields.
	 * @param FormatIdOrClass A format id or an action id from ListFormats (e.g. pcgex.property-schema, pcgex.sketch.build), a bound class name or path (e.g. UPCGExTupleSettings), or a struct path from a collapsed fragment (e.g. /Script/PCG.PCGSoftISMComponentDescriptor).
	 * @return JSON Schema object, or an error when nothing matches.
	 */
	UFUNCTION(meta = (AICallable), Category = "PCGEx|Authoring")
	static FString DescribeFormat(const FString& FormatIdOrClass);

	/**
	 * Export a bound object as a JSON document: an asset by object path (a Property Schema asset,
	 * an asset collection) or a node's settings as "<graph object path>:<node title>" (Tuple,
	 * Distribute Tuple). The result is what ImportJson accepts back, so edit and re-import it.
	 * @param Target Object path of the asset, <graph object path>:<node title or name> for a node, or a target alias written with a leading at sign: focused (what the user is looking at) or selected (the level or Content Browser selection); the PCGEx skill spells them.
	 * @return The JSON document { format, version, data }, or an error with diagnostics when the target does not resolve or has no binding.
	 */
	UFUNCTION(meta = (AICallable), Category = "PCGEx|Authoring")
	static FString ExportJson(const FString& Target);

	/**
	 * Import a JSON document into a bound object, undoable (one editor transaction; see Undo).
	 * Members present in the document replace the host's, members absent are left alone. Within a
	 * member: a property schema document is the whole schema and an override set is the full
	 * enabled set, while array members (collection entries, Tuple rows) merge BY POSITION: row i
	 * of the document merges into row i of the host, rows beyond the host's count are created,
	 * rows the document lacks are removed. Inside an entry an absent key keeps the existing value
	 * (or the default on a new row), so a partial entry is safe and the sparse ExportJson output
	 * is the normal starting point. Nothing changes unless the whole document decodes; every
	 * problem is reported with its path in the document (e.g. data/members/Entries/1/Weight).
	 * @param Target Object path of the asset, <graph object path>:<node title or name> for a node, or a target alias written with a leading at sign: focused (what the user is looking at) or selected (the level or Content Browser selection); the PCGEx skill spells them.
	 * @param Json The document, as produced by ExportJson or written to DescribeFormat's schema.
	 * @return JSON object { ok, diagnostics: [{ severity, path, message }] }; on failure the call also raises an error carrying the diagnostics.
	 */
	UFUNCTION(meta = (AICallable), Category = "PCGEx|Authoring")
	static FString ImportJson(const FString& Target, const FString& Json);

	/**
	 * Create a PCGEx asset so it can be filled with ImportJson: a Property Schema asset
	 * (UPCGExPropertySchemaAsset) or any asset collection class (UPCGExMeshCollection,
	 * UPCGExActorCollection, UPCGExOmniCollection, ...). Any class with an asset factory works. The
	 * asset is created unsaved and marked dirty; call SaveAsset to write it to disk.
	 * @param ClassNameOrPath Class name (UPCGExMeshCollection or PCGExMeshCollection) or class path.
	 * @param PackagePath Long package path of the folder, e.g. /Game/PCGEx/Collections.
	 * @param AssetName The asset's name, without extension.
	 * @return JSON object { ok, path, class } with the new asset's object path (the Target for ImportJson); an existing asset, an unknown class or a bad path raises an error.
	 */
	UFUNCTION(meta = (AICallable), Category = "PCGEx|Authoring")
	static FString CreateAsset(const FString& ClassNameOrPath, const FString& PackagePath, const FString& AssetName);

	/**
	 * Save an asset's package to disk after CreateAsset or ImportJson; nothing written by those is on
	 * disk until this runs (or the user saves).
	 * @param Target Object path of the asset.
	 * @return JSON object { ok, path }; raises an error when the target does not resolve or saving fails.
	 */
	UFUNCTION(meta = (AICallable), Category = "PCGEx|Authoring")
	static FString SaveAsset(const FString& Target);

	/**
	 * Undo the editor's most recent transaction. An ImportJson is one transaction, so this reverts a
	 * whole import; it also undoes a user's last manual edit if that came after, so read the returned
	 * title before relying on it.
	 * @return JSON object { ok, title } naming the undone transaction; raises an error when there is nothing to undo.
	 */
	UFUNCTION(meta = (AICallable), Category = "PCGEx|Authoring")
	static FString Undo();

	/**
	 * What one bound target offers right now, as data rather than a type schema: for a schema host (Property
	 * Schema asset, asset collection, Tuple or Distribute Tuple node) every resolved property with its name, type,
	 * declaring asset, effective default and its effective choices and lock after every import patch. Read it before
	 * writing override values or picks against that host: DescribeFormat cannot know an instance's choices.
	 * @param Target Object path of the asset, <graph object path>:<node title or name> for a node, or a target alias written with a leading at sign: focused (what the user is looking at) or selected (the level or Content Browser selection); the PCGEx skill spells them.
	 * @return JSON object { target, class, properties: [{ name, type, declaredBy, overridden, default, choices?: { locked, items: [{ label, value }] }, patched? }], imports }; falls back to the class's object schema when the binding describes no instance; an error when the target does not resolve or has no binding.
	 */
	UFUNCTION(meta = (AICallable), Category = "PCGEx|Authoring")
	static FString DescribeTarget(const FString& Target);

	/**
	 * Run a registered mediator action on a bound target, undoable (one transaction). Actions are editor levers no
	 * document expresses; ListFormats lists them under "actions" with their target class and summary, and
	 * DescribeFormat(<action id>) is the params schema to read before the first call. Examples: "pcgex.align-choices"
	 * with params { "property": "<name>" } snaps every enabled row whose value is none of that property's choices onto
	 * the default choice (on a Property Schema asset it sweeps every LOADED importer, on any other host only that
	 * host's rows); the "pcgex.sketch.*" actions author Cluster Sketch geometry (build, move, remove, ...).
	 * @param ActionId The action id from ListFormats, e.g. pcgex.align-choices or pcgex.sketch.build.
	 * @param Target Object path of the asset, <graph object path>:<node title or name> for a node, or a target alias written with a leading at sign: focused (what the user is looking at) or selected (the level or Content Browser selection); the PCGEx skill spells them.
	 * @param ParamsJson JSON object of params per DescribeFormat(<action id>); empty for none.
	 * @return JSON object { ok, ...action result, diagnostics }, the result shape being the one the action's params schema describes; raises an error when the action, target or params are wrong.
	 */
	UFUNCTION(meta = (AICallable), Category = "PCGEx|Authoring")
	static FString RunAction(const FString& ActionId, const FString& Target, const FString& ParamsJson = FString());

	/**
	 * Bring a target in front of the user so they can watch the edits land: an asset opens in its editor (through the
	 * Content Browser's own activation path, and the browser syncs to it), a node opens its graph editor and is focused,
	 * a level component selects its actor and frames it, and a Cluster Sketch component or payload also enters the
	 * Cluster Sketch level mode. Chain it after CreateAsset and before RunAction / ImportJson for live review.
	 * @param Target Object path, <graph object path>:<node title or name>, or a target alias (at sign + focused, at sign + selected).
	 * @return JSON object { ok, target, opened } saying how it was shown; raises an error when the target does not resolve or cannot be opened.
	 */
	UFUNCTION(meta = (AICallable), Category = "PCGEx|Authoring")
	static FString Open(const FString& Target);

	/**
	 * Spawn an actor of a class into the open level, undoable. Mechanics only: a class with creation rules of its own
	 * (a Valency context volume or cage) goes through its domain action (ListFormats "actions"), which this call would
	 * bypass. DescribeTarget on the level alias (at sign + level) lists every host actor and component in the level.
	 * @param ClassNameOrPath Class path, short name or C++ name of an actor class.
	 * @param TransformJson World transform in the dialect: { "location": [x, y, z], "rotation": [pitch, yaw, roll] or a quaternion, "scale": [x, y, z] }, each optional; "" for identity.
	 * @param Label Actor label to set; "" keeps the generated name.
	 * @return JSON object { ok, actor, label, class, bound } or a failure object with diagnostics.
	 */
	UFUNCTION(meta = (AICallable), Category = "PCGEx|Authoring")
	static FString SpawnActor(const FString& ClassNameOrPath, const FString& TransformJson = FString(), const FString& Label = FString());
};
