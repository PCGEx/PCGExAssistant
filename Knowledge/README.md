# Knowledge bundle

This folder receives the offline render of the PCGEx documentation card store: one JSON dataset per plugin (node identity, purpose, mechanism, pin contracts, settings semantics, related pages), produced by the `render-bundle` target of the documentation automation pipeline and published to its own public repository.

The copy shipped here is what `UPCGExToolset::SearchNodes` / `GetNode` serve, so the assistant's knowledge always matches the installed plugin versions.

Nothing in this folder is hand-edited.
