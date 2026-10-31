# Visual Audio Lab project files

The structural editor reads and writes **`.valab`** projects as indented UTF-8 JSON. This is the implemented persistence contract, not an audio/DSP, plugin or scripting format. A project contains one or more named patches, stable nodes/connections, scalar parameter data, per-patch viewport navigation and the shell window/dock layout. The application still has no processing engine or parameter inspector/editor UI.

## File workflows

- **File > Save Project** (`Ctrl+S` on Windows, `Command+S` on macOS) writes the active path. An untitled project first asks for a `.valab` location.
- **Save Project As...** (`Ctrl/Command+Shift+S`) chooses a different location and changes the active path only after successful replacement. Existing compatible destinations require the native chooser's overwrite confirmation. Unsupported/corrupt targets are never overwritten, even with confirmation.
- **Load Project...** (`Ctrl/Command+O`) selects a file, then offers Save/Discard/Cancel if the current content has unsaved changes. Closing the window or choosing Quit uses the same protection. Cancelled pickers/consent and failed saves retain the active document; a failed load retains its content, path and undo history.
- **Recent Files** retains up to ten normalized local paths, newest first, across application restarts. Existing aliases are deduplicated using filesystem equivalence. Missing/unavailable entries are labelled, can be retried, and have an explicit remove action. Opening one uses the same dirty-document protection as Load.
- **New Patch** and the active-patch selector support multiple graphs in a single document. Undo/redo remains project-wide; graph edits reveal their owning patch. Adding a patch and setting a structural parameter use the existing bounded command journal. Loading a project clears the old journal and derives fresh monotonic identity counters.

Only successful saves mark content clean. Undoing back to saved content removes the unsaved marker. Selection is never serialized. Active patch, zoom/pan, window/dock positions, visibility, splitter proportions and selected tabs are saved on explicit Save, but navigation/layout changes alone do not mark content dirty or add undo entries. Unsaved navigation changes can therefore be discarded on close without prompting. Restoring a layout clamps the window into an available monitor work area; it does not restore fullscreen/minimized state. Local startup preferences remain separate from project layouts.

Recent paths are stored in machine-local `VisualAudioLab/recent-projects.json`, alongside the existing settings location. These are local user paths, not telemetry; they are not logged or included in portable project data. Recent-list write failures are reported separately after a completed project save/load and do not falsely turn that completed operation into a failure.

## Schema 1.0

Example (a two-node patch):

```json
{
  "version": { "major": 1, "minor": 0 },
  "minimumReader": { "major": 1, "minor": 0 },
  "requiredFeatures": [],
  "format": "VisualAudioLab",
  "name": "Example",
  "activePatch": "1",
  "patches": [
    {
      "id": "1",
      "name": "Patch 1",
      "viewport": { "zoom": 1.0, "x": 40.0, "y": 40.0 },
      "graph": {
        "nodes": [
          {
            "id": "1",
            "name": "Source",
            "position": { "x": 0.0, "y": 0.0 },
            "parameters": { "gain": 0.5, "enabled": true, "label": "Example", "unset": null }
          },
          {
            "id": "2",
            "name": "Destination",
            "position": { "x": 300.0, "y": 0.0 },
            "parameters": {}
          }
        ],
        "connections": [
          {
            "id": "3",
            "source": { "node": "1", "port": "out" },
            "destination": { "node": "2", "port": "in" }
          }
        ]
      }
    }
  ],
  "layout": {
    "window": { "x": 80, "y": 80, "width": 1100, "height": 720 },
    "panels": [
      { "id": "canvas", "dock": 1, "visible": true },
      { "id": "modules", "dock": 0, "visible": true },
      { "id": "explorer", "dock": 0, "visible": true },
      { "id": "properties", "dock": 2, "visible": true },
      { "id": "audio", "dock": 3, "visible": true }
    ],
    "ratios": [0.22, 0.24, 0.26],
    "selectedPanels": [1, 0, 3, 4]
  }
}
```

All shown fields are required. Additional patches use the same structure. Node and connection IDs are globally unique across patches; patch IDs use a separate namespace. IDs are canonical positive decimal **strings**, below `UINT64_MAX`, to avoid JSON number precision limits. Endpoints resolve within their containing patch. Node and patch names and the project name are non-blank, at most 128 UTF-8 bytes, without control characters.

Parameters are a JSON object with nonempty keys up to 128 UTF-8 bytes and values of `null`, boolean, finite binary64 number or string. They are structural data only. Collections, processor definitions and parameter automation are not supported value types and are rejected rather than being silently reinterpreted. The service exposes reversible scalar edits; no DSP interpretation is attached.

Node origins are bounded to +/-1,000,000 patch units. Zoom is 0.025-3.0 and viewport offsets are bounded to +/-3,000,000 pixels. Connections must be output-to-input, with valid node references, no self-links/cycles/duplicate wires and at most one incoming wire per input. Outputs allow fan-out. These are the same rules as editor-created graphs.

Dock numbers are `0 = left`, `1 = centre`, `2 = right`, `3 = bottom`. Canvas visibility must be true. Ratios are left/right/bottom, finite and within 0.1-0.5. `selectedPanels` contains one known-panel index for each dock (left/centre/right/bottom), with indices matching the panel list above and `-1` for none. Invalid or unavailable selected tabs fall back to an existing tab without losing any panel. Window width is 800-7680, height 600-4320, and x/y within +/-1,000,000; usable placement is reconciled with the current displays.

## Compatibility and unknown content

`version.major` identifies a breaking schema. Reader 1.0 accepts major 1 and a nonnegative future minor only if `minimumReader` is exactly 1.0 and `requiredFeatures` is empty. A future writer must raise the minimum reader or list a required feature when old readers cannot safely edit its semantics. Other major versions, higher required readers/features, unsupported known types and malformed references fail before replacing active content.

Compatible unknown properties are retained at root, version/reader, layout/window/panel, patch/viewport/graph, node/position and connection/endpoint levels. Known entities are merged by stable ID on read-edit-save, not reconstructed from only recognized properties. Unknown future panel records are retained as inert data. Compatible future version numbers are retained rather than downgraded. Save As carries the same unknown content into the copy. Deliberately deleting a known node/connection removes that entity and its attached metadata from the file. The session retains its loaded merge template separately from the last written source, so undo can restore attached metadata even after the deletion was saved. Reloading starts a new session/history and uses the newly loaded file as its template.

Preservation is **semantic JSON**, not original byte formatting, key order or numeric spelling. The supported JSON data model is objects with unique keys, arrays, strings without NUL, booleans, null, signed 64-bit integer literals and finite binary64 decimals with at most 17 significant digits. Excess integer/decimal precision, duplicate keys, NUL strings, nesting beyond 64 levels and files over 64 MiB are rejected to prevent silent loss by the JSON adapter. Extension authors needing arbitrary-precision numbers should use strings. Fields requiring unsupported semantics must not be presented as compatible additive metadata.

Known numeric fields stored as binary64 values reject integer literals outside the exactly representable integer range of -9007199254740991 to 9007199254740991. Unknown integer extension fields retain the signed 64-bit range without conversion to binary64.

## Failure and replacement policy

The reader parses and validates an independent candidate, including all patches/topology, before replacing the workspace. Saves validate the complete snapshot, merge supported source data, then decode the prepared output to verify exact known-state round-trip before writing. A sibling temporary file is fully written and flushed before native replacement. Write/flush/replace failures leave the original and the active project/path/dirty state intact. Temporary files are cleaned by their scoped owner.

The active file's loaded/saved source is checked against disk on Save; a second check immediately before replacement catches changes during preparation. Removed or externally changed active files require Load or Save As at a different location. Unsupported/corrupt destinations are protected from Save As as well. This is a local single-writer workflow, not a cross-process locking/CAS protocol, network-filesystem transaction, crash-recovery journal or power-loss durability guarantee. Keep external backups for important work; concurrent editing of the same file is not supported.

## Automated coverage

`Persistence.Projects` exercises complete multi-patch/parameter/layout round trips, Unicode paths, unknown-field merges, additive/breaking versions, invalid endpoints/cycles/IDs/types, malformed and ambiguous JSON, dirty/history/path protection, external modification, bounded/deduplicated persistent recent files and explicit storage failures. `Presentation.Projects` exercises registered Save and UI load/save endpoints, restored docking/viewport and asynchronous Cancel/Save-before-close behavior with real storage. Other graph/editor/shell/settings suites remain registered with CTest. Native picker and platform integration must also be qualified on their actual operating systems.
