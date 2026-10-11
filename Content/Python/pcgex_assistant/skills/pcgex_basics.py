# Copyright 2026 Timothé Lapetite and contributors
# Released under the MIT license https://opensource.org/license/MIT/

import unreal

from toolset_registry.agent_skill import agent_skill

_INSTRUCTIONS = """\
GROUNDING (ALWAYS)
- Never answer PCGEx questions from your own training knowledge. It is sparse and
  frequently wrong about this toolkit. Every claim about a PCGEx node's purpose,
  pins, settings or behavior must come from the PCGExAssistant.PCGExToolset tools.
- Start with SearchKnowledge (free-text; describe the operation or problem in plain
  words, it matches node names, purposes and canonical situations across all
  installed PCGEx plugins). Fetch detail with GetNode for entries and GetConcept for
  concept pages.
- GetNode returns source-verified data: purpose, mechanism, exact pin contract
  (including conditional pins), per-setting semantics, known traps, and a
  documentation url worth citing to the user.
- GetConcept returns a system's documentation page (clusters, filters, staging,
  tensors, pathfinding, ...) with its prose when the bundle carries it; cite its url.

IDS
- Entry ids are C++ settings class names, e.g. UPCGExBevelPathSettings for
  "Path : Bevel". The same name is what ListRegisteredNodeTypes reports from live
  reflection and what PCG graph tooling needs to place the node. Variant and
  helper class names resolve through aliases; pass them to GetNode as-is.
- Settings inherited from a shared base are not repeated per node: follow the
  entry's "inherits" list with further GetNode calls when a setting is not found.

PLACEABILITY
- Only classifications "node" and "provider" may be placed in a PCG graph.
- "blueprint_node" entries are Blueprint utility nodes; never place them in a
  PCG graph. "asset" entries are data assets created in the Content Browser.
  "shared_struct" entries document settings blocks, not placeable things.

WORKFLOW
- Which node fits: SearchKnowledge, then GetNode on the best hits, then recommend
  by canonical situation, not by name similarity.
- How a system works (clusters, filter ensembles, staging, tensors): search hits
  of kind "concept" resolve through GetConcept; use and cite them.
- Version-sensitive advice: call GetInstalledPCGExPlugins first. The knowledge
  base shipped with the assistant matches the installed plugin versions.
- Confirm a node exists in this install with ListRegisteredNodeTypes before
  proposing it; hidden entries are deprecated, do not recommend them.

EDITING GRAPHS
- The stock PCG toolset (PCGToolset.PCGToolset) cannot create or describe PCGEx
  nodes: its AddNode and GetNativeNodeSchema only know nodes from the PCG package.
  Create PCGEx nodes with CreateNode here, passing the class name from this
  knowledge base; wire provider ensembles (filters, heuristics, tensors, samplers)
  into a node with AddSubnodes in one call.
- Once a node exists, the PCG toolset's UpdateNode, ConnectNodePins, GetNodeInfo
  and RemoveNode work on it like on any node; CreateNode returns the node for them.
- Read GetNodeSchema BEFORE setting params or connecting pins: it is the live,
  writable truth for property names, JSON value shapes and pin labels in the
  installed version, annotated with the knowledge base's semantics and traps.
  Many PCGEx nodes have conditional pins that only exist under specific settings;
  GetNode's pin contract says which.

AUTHORING DATA AS JSON
- PCGEx data is authored as JSON documents: Property Schema assets, asset
  collections (mesh, actor, omni, ...) and the Tuple / Distribute Tuple nodes.
  "Create a schema with a float and an enum", "add entries to a collection",
  "fill this Tuple" are authoring tasks; never answer them with Content Browser
  instructions.
- Chain: ListFormats (which classes and structs are bound) -> DescribeFormat (the
  JSON Schema of a format or bound class) and DescribeTarget (one instance's
  resolved properties, defaults and choices) -> CreateAsset when the asset does
  not exist -> ImportJson -> SaveAsset. Undo reverts the last import whole.
- Start from ExportJson when the target exists: its output is sparse (values at
  struct default are omitted) and ImportJson accepts it back, so edit and
  re-import. Nothing applies unless the whole document decodes; diagnostics
  carry the path of every problem.
- Merge rules: members present in the document replace the host's, absent
  members are untouched. A property schema document is the whole schema and an
  override set is the full enabled set. Array members (collection entries, Tuple
  rows) merge BY POSITION: rows beyond the host are created, rows the document
  lacks are removed, and inside a row an absent key keeps the existing value.
- A node whose class ListFormats reports as bound (Tuple, Distribute Tuple) is
  edited with ExportJson / ImportJson, targeted as "<graph object path>:<node
  title or name>". Do not follow GetNodeSchema into UpdateNode for those members:
  it reports their raw reflected shape, which UpdateNode rejects.
- Values travel in the mediator dialect: enums by enumerator name, vectors as
  arrays, rotators as {pitch, yaw, roll}; DescribeFormat is the truth for each.

VOCABULARY
- Cluster, not graph. Vtx, not vertex. PCGEx, not PCGEX. Node display names carry
  category prefixes ("Path : Bevel", "Cluster : Connect"); use display names when
  talking to the user, class names when calling tools.
"""


@agent_skill
class PCGExBasicsSkill(unreal.AgentSkill):
    """Foundation skill for PCGEx (PCG Extended Toolkit) and its sibling plugins
    (Valency, Cluster Sketch, ZoneGraph). Use this skill whenever a task mentions
    PCGEx, involves clusters (vtx/edge connectivity over points), path processing,
    pathfinding on point data, filter ensembles, tensors, or asset staging beyond
    stock PCG, touches any node whose name carries a PCGEx category prefix such
    as "Cluster :", "Path :", "Vtx :", "Filter :", or asks to create, fill or edit
    PCGEx data (property schemas, asset collections, Tuple and Distribute Tuple
    nodes), which this toolset authors as JSON. PCGEx questions must be answered
    from its knowledge-base toolset, never from prior knowledge. Not needed for
    purely stock PCG work involving no PCGEx nodes."""

    instructions = _INSTRUCTIONS
