# HARDWARIO Monitor documentation

| Language | |
|---|---|
| English | [Documentation](en/index.md) |
| Čeština | [Dokumentace](cs/index.md) |
| Deutsch | [Dokumentation](de/index.md) |
| Slovenčina | [Dokumentácia](sk/index.md) |
| Polski | [Dokumentacja](pl/index.md) |

The reference pages are in English only: [Command line](en/cli.md), [Migrating from py-hardwario](en/cli-migration.md),
[MCP server](en/mcp.md).

## Rules for editing

- Keep the `<a id="…"></a>` lines unchanged when translating or editing a page. The app links to these anchors
  (`libs/core/src/docs_urls.cpp`, ADR-0259), they are the same English ids in every language, and the `docs_anchors`
  test checks them.
- On the English pages an `<a id>` can repeat the heading's own GitHub anchor (for example `no-j-link-found`). That is
  intended: the id is the same in every language, and the duplicate does no harm.
