// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#pragma once

#include "CoreMinimal.h"
#include "ToolsetRegistry/UToolsetRegistry.h"

#include "PCGExToolset.generated.h"

/**
 * Expert knowledge and tooling for PCGEx (PCG Extended Toolkit) and its sibling plugins
 * (Valency, Clusters Sketch, ZoneGraph). PCGEx extends stock PCG with clusters
 * (vtx/edge connectivity structures), path processing, filter ensembles, tensors,
 * pathfinding, asset staging and hundreds of specialized nodes.
 * Use this toolset whenever a task involves a PCGEx node, a cluster or path workflow,
 * or choosing between PCGEx nodes: it can search the full node knowledge base
 * (purpose, mechanism, pin contracts, settings semantics, known traps) and report
 * which PCGEx plugins and versions are installed.
 * This is NOT the right toolset for stock PCG questions; use it when node names carry
 * PCGEx category prefixes such as "Cluster :", "Path :", "Vtx :", or when stock PCG
 * lacks the needed operation.
 */
UCLASS(BlueprintType)
class PCGEXASSISTANT_API UPCGExToolset : public UToolsetDefinition
{
	GENERATED_BODY()

public:
	// ~ Phase 1 : Knowledge (offline bundle: the PCGExKnowledge submodule under Knowledge/)

	/**
	 * Search the PCGEx knowledge base: every node and concept page across the installed
	 * PCGEx plugins, matched against display names, purpose statements and canonical
	 * situations. Call this first when unsure which PCGEx node or system fits a task.
	 * Results of kind "node" have full detail available via GetNode; results of kind
	 * "concept" answer how-does-this-system-work questions and carry a documentation url.
	 * @param Query Free-text description of the operation or problem, e.g. "collapse overlapping points" or "find shortest path on a cluster".
	 * @param MaxResults Maximum number of matches to return.
	 * @return JSON array of matches ordered by relevance: { id, name, plugin, kind: "node"|"concept", purpose, situation, url? }.
	 */
	UFUNCTION(meta = (AICallable), Category = "PCGEx|Knowledge")
	static FString SearchKnowledge(const FString& Query, const int32 MaxResults = 10);

	/**
	 * Fetch the full knowledge entry for one PCGEx node: what it is for, its mechanism,
	 * exact pin contract (including conditional pins), settings semantics with traps and
	 * interactions, and related nodes and concepts.
	 * @param NodeId The node's C++ settings class name (e.g. UPCGExBevelPathSettings), as returned by SearchKnowledge or seen in live reflection. Variant classes documented on a shared page resolve through aliases; settings surfaces inherited from a shared base are referenced by id and fetched with further GetNode calls.
	 * @return JSON object with the complete entry, or an error if the id is unknown.
	 */
	UFUNCTION(meta = (AICallable), Category = "PCGEx|Knowledge")
	static FString GetNode(const FString& NodeId);

	// ~ Phase 2 : Editor introspection

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
	 * the installed version before adding it to a graph via the PCG toolset.
	 * @param ModuleFilter Optional module name substring, e.g. "Valency"; empty lists all.
	 * @return JSON array of { class, displayName, module }.
	 */
	UFUNCTION(meta = (AICallable), Category = "PCGEx|Introspection")
	static FString ListRegisteredNodeTypes(const FString& ModuleFilter);

	// ~ Phase 3 : Actuation (scaffold only; generic graph surgery belongs to the engine's
	// PCGToolset (AddNode, ConnectNodePins, ...) and is not duplicated here. PCGEx-specific
	// actuation means multi-node idioms: factory/subnode chains, filter ensembles, cluster
	// pipelines.)

	/**
	 * Build a PCGEx subnode chain (filters, heuristics, tensors, ...) feeding a target node
	 * in one operation, wired the way PCGEx expects.
	 * @param GraphPath Asset path of the PCG graph to edit.
	 * @param TargetNodeName Node whose subnode input pin receives the chain.
	 * @param ChainJson JSON array describing each subnode: type and settings.
	 * @return JSON summary of the created nodes and connections.
	 */
	UFUNCTION(meta = (Experimental, AICallable), Category = "PCGEx|Actuation")
	static FString AddSubnodeChain(const FString& GraphPath, const FString& TargetNodeName, const FString& ChainJson);
};
