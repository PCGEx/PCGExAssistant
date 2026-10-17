![UE 5.8](https://img.shields.io/badge/UE-5.8-darkgreen)

<h1 align="center">PCGEx | Assistant</h1>

<p align="center">
  <strong>AI agent integration for the PCGEx ecosystem</strong><br>
  The PCGEx knowledge base, the live editor, PCGEx nodes in your graphs and PCGEx data as JSON, exposed to any MCP client through the engine's Model Context Protocol server.
</p>

<p align="center">
  <a href="https://pcgex.gitbook.io/pcgex/ai">Documentation</a> •
  <a href="https://discord.gg/mde2vC5gbE">Discord</a>
</p>

The plugin registers a PCGEx toolset with Unreal's experimental Toolset Registry and ships an offline, source-verified knowledge base covering every documented node of PCGEx and its sibling plugins (Valency, Cluster Sketch, ZoneGraph, Kweave): purpose, mechanism, exact pin contracts, settings semantics and known traps, matched to the installed plugin versions. Any MCP client (Claude Code, Cursor, VS Code, and others) connected to the engine's Model Context Protocol server can then search that knowledge base, inspect the installed PCGEx plugins, place PCGEx nodes in a graph, author PCGEx data as JSON, and ground its answers in what the nodes actually do instead of guessing.

> [!IMPORTANT]
> **Authoring PCGEx data as JSON needs the [PCGEx Toolset](https://github.com/PCGEx/PCGExToolset) in the project.** The Assistant reaches it by reflection and never links it: without it the knowledge, introspection and graph tools work and the skill reports authoring as unavailable. From PCGEx 0.80 the FAB packages carry the toolset (PCGEx Pro carries the Valency and Cluster Sketch toolsets as well); from source it is its own repository.

## Requirements

- **Unreal Engine 5.8**.
- **[PCGExtendedToolkit](https://github.com/Nebukam/PCGExtendedToolkit)**.
- The engine's experimental **Toolset Registry** plugin, enabled automatically as a dependency, and its **Model Context Protocol** plugin for clients to connect.

Editor-only: nothing ships in cooked builds. The plugin is off by default; enable it in **Edit > Plugins**.

## Getting it

**From FAB**: install [PCGEx Assistant](https://www.fab.com/listings/030a9f2c-d1cb-4637-90ba-92fd39da6e94) to your 5.8 engine next to PCGEx 0.80 or later, which ships the toolset inside its package.

**From source**: the knowledge base is the [PCGExKnowledge](https://github.com/PCGEx/PCGExKnowledge) repository, pinned as the `Knowledge/` submodule. Clone with it, or the plugin loads and raises `Knowledge bundle unavailable`:

```bash
git clone --recurse-submodules https://github.com/PCGEx/PCGExAssistant Plugins/PCGExAssistant
```

As a submodule of your project, then fetch the nested one:

```bash
git submodule add https://github.com/PCGEx/PCGExAssistant Plugins/PCGExAssistant
git submodule update --init --recursive
```

The toolkit's `5.8` branch pairs with this plugin; the Assistant itself tracks `main`. The full checklist, with the toolset and its Pro siblings, is in the [documentation](https://pcgex.gitbook.io/pcgex/ai/getting-started/from-source).

## Setup

1. Enable the **PCGEx Assistant** and **Model Context Protocol** plugins (both require an editor restart).
2. In **Editor Preferences > Plugins > Model Context Protocol**: enable **Auto Start Server**. For the most direct experience, disable **Enable Tool Search** so PCGEx tools appear as first-class tools in your client.
3. Point your MCP client at the editor, either by letting the engine write the client configuration for you, or manually:

```json
{ "mcpServers": { "unreal": { "type": "http", "url": "http://127.0.0.1:8000/mcp" } } }
```

For Claude Code, save this as `.mcp.json` at your project root, or run `claude mcp add --transport http unreal http://127.0.0.1:8000/mcp`. With the editor running, ask a PCGEx question and watch it answer from the knowledge base.

## Tools

The toolset ships with a skill the client fetches: static grounding rules, plus an authoring section composed at every fetch from what the editor binds right now (hosts, actions and per-domain guides from the installed toolsets).

**Knowledge**, from the offline bundle:

| Tool | What it does |
| --- | --- |
| `SearchKnowledge` | Free-text search across every documented node and concept page of the installed PCGEx plugins. |
| `GetNode` | The full knowledge entry for one node: purpose, mechanism, pin contract, settings semantics, traps, documentation link. |
| `GetConcept` | A concept documentation page (clusters, filters, staging, tensors, ...) with its outline, link and, when the bundle carries it, its full prose. |

**Introspection**, from the running editor:

| Tool | What it does |
| --- | --- |
| `GetInstalledPCGExPlugins` | Names and versions of the PCGEx-family plugins in the project. |
| `ListRegisteredNodeTypes` | Live reflection over the node types actually registered in this editor session. |
| `GetNodeSchema` | The live, writable schema of a node class: pins as the installed version declares them and the JSON shape its params accept, annotated from the knowledge base. |

**Graphs**, what the engine's PCG toolset cannot do for PCGEx nodes:

| Tool | What it does |
| --- | --- |
| `CreateNode` | Adds a node of any settings class to a PCG graph. The engine's own PCG toolset only knows nodes from the PCG package; this covers PCGEx and everything else. |
| `AddSubnodes` | Creates a set of provider nodes (filters, heuristics, tensors, samplers) and wires them into a target node in one undoable operation. |

Editing an existing node (params, connections, comments, removal) is left to the engine's PCG toolset, whose tools work on any node once it exists.

**Authoring**, served by PCGEx Toolset through reflection:

| Tool | What it does |
| --- | --- |
| `ListFormats` | Which assets and nodes are bound, which document formats exist, which actions are registered. |
| `DescribeFormat` | The JSON Schema of a format, of an action's params, of a bound class's whole document, or of any struct by path. |
| `DescribeTarget` | What one existing instance offers: resolved properties, defaults, choices and locks. |
| `ExportJson` | A bound asset or node as a sparse document that re-imports unchanged. |
| `ImportJson` | Applies a document to a bound target as one undoable transaction; nothing applies unless the whole document decodes. |
| `CreateAsset` | Creates an asset of a class with a factory, unsaved, ready for `ImportJson`. |
| `SaveAsset` | Writes an asset's package to disk. |
| `Undo` | Reverts the editor's last transaction, an import being one. |
| `RunAction` | Runs a registered action on a target, undoable (align choices, build a sketch, rebuild a Bonding Rules kit, ...). |
| `Open` | Brings a target in front of the user: asset editor, graph node, level actor, or a domain's own mode. |

Every target is an asset's object path or `<graph path>:<node>`, and also accepts `@focused` (what the user is looking at) and `@selected` (the level or Content Browser selection).

## Knowledge data

The knowledge base is rendered from the same verified card store that produces the PCGEx documentation, and is also published as raw JSON at [PCGEx/PCGExKnowledge](https://github.com/PCGEx/PCGExKnowledge) for anyone who wants to build their own tooling on it.

## Links

- Documentation: https://pcgex.gitbook.io/pcgex/ai
- PCGEx: https://pcgex.gitbook.io/pcgex/
- Discord: https://discord.gg/mde2vC5gbE
