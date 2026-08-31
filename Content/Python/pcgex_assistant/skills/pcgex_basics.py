# Copyright 2026 Timothé Lapetite and contributors
# Released under the MIT license https://opensource.org/license/MIT/

import unreal

from toolset_registry.agent_skill import agent_skill

_INSTRUCTIONS = """\
GROUNDING (ALWAYS)
- Never answer PCGEx questions from your own training knowledge. It is sparse and
  frequently wrong about this toolkit. Every claim about a PCGEx node's purpose,
  pins, settings or behavior must come from the PCGExAssistant.PCGExToolset tools.
- Start with SearchKnowledge (free-text; matches node names, purposes and canonical
  situations across all installed PCGEx plugins). Fetch detail with GetNode.
- GetNode returns source-verified data: purpose, mechanism, exact pin contract
  (including conditional pins), per-setting semantics, known traps, and a
  documentation url worth citing to the user.

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
  of kind "concept" carry documentation urls; use and cite them.
- Version-sensitive advice: call GetInstalledPCGExPlugins first. The knowledge
  base shipped with the assistant matches the installed plugin versions.
- Confirm a node exists in this install with ListRegisteredNodeTypes before
  proposing it; hidden entries are deprecated, do not recommend them.
- Editing graphs: use the PCG toolset (PCGToolset.PCGToolset) to create nodes and
  wire pins, passing the class name from this knowledge base as the node type.
  Read the pin contract from GetNode BEFORE connecting anything: many PCGEx nodes
  have conditional pins that only exist under specific settings.

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
    stock PCG, or touches any node whose name carries a PCGEx category prefix such
    as "Cluster :", "Path :", "Vtx :", "Filter :". PCGEx questions must be answered
    from its knowledge-base toolset, never from prior knowledge. Not needed for
    purely stock PCG work involving no PCGEx nodes."""

    instructions = _INSTRUCTIONS
