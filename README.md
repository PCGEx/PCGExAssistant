# PCGEx | Assistant

AI agent integration for the [PCGEx](https://pcgex.gitbook.io/pcgex/) ecosystem. The plugin registers a PCGEx toolset with Unreal's experimental Toolset Registry and ships an offline, source-verified knowledge base covering every documented PCGEx node: purpose, mechanism, exact pin contracts, settings semantics and known traps, matched to the installed plugin versions.

Any MCP client (Claude Code, Cursor, VS Code, and others) connected to the engine's Model Context Protocol server can then search that knowledge base, inspect the installed PCGEx plugins, and ground its answers in what the nodes actually do instead of guessing.

## Setup

1. Enable the **PCGEx Assistant** and **Model Context Protocol** plugins (both require an editor restart).
2. In **Editor Preferences > Model Context Protocol**: enable **Auto Start Server**. For the most direct experience, disable **Enable Tool Search** so PCGEx tools appear as first-class tools in your client.
3. Point your MCP client at the editor, either by letting the engine write the client configuration for you, or manually:

```json
{ "mcpServers": { "unreal": { "type": "http", "url": "http://127.0.0.1:8000/mcp" } } }
```

For Claude Code, save this as `.mcp.json` at your project root. With the editor running, ask a PCGEx question and watch it answer from the knowledge base.

## Tools

| Tool | What it does |
| --- | --- |
| `SearchKnowledge` | Free-text search across every documented node and concept page of the installed PCGEx plugins. |
| `GetNode` | The full knowledge entry for one node: purpose, mechanism, pin contract, settings semantics, traps, documentation link. |
| `GetInstalledPCGExPlugins` | Names and versions of the PCGEx-family plugins in the project. |
| `ListRegisteredNodeTypes` | Live reflection over the node types actually registered in this editor session. |

## Knowledge data

The knowledge base is rendered from the same verified card store that produces the PCGEx documentation, and is also published as raw JSON at [PCGEx/PCGExKnowledge](https://github.com/PCGEx/PCGExKnowledge) for anyone who wants to build their own tooling on it.

## Links

- Documentation: https://pcgex.gitbook.io/pcgex/
- Discord: https://discord.gg/mde2vC5gbE
