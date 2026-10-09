# Game Abstraction Layer và components

GAL major 1 là contract adapter, hiện chỉ có `UnavailableAdapter` trả `false`
cho validate và capability set rỗng. Input profile gồm exact game version, ABI,
game-library hash. Không có offset/symbol nào được giả định.

| Component | Value contract dự kiến | Capability | Trạng thái |
|---|---|---|---|
| ItemRegistry | namespaced ID, localized name, stack limit, metadata codec | game.items.v1 | Blocked by game integration |
| CreativeTabRegistry | tab ID, localized title key, icon item ID, ordered item IDs, registry generation | game.creative_tabs.v1 | Proposed after Inner Core/Horizon audit; native resolver/transactions unavailable |
| BlockRegistry | ID, states, placement and removal intents | game.blocks.v1 | Not implemented |
| Inventory | owner/world/container ID, slot, expected revision, transaction ID | game.inventory.v1 | Blocked by game integration |
| RecipeProvider | provider/version, output, ingredients, category, completeness | game.recipes.v1 | Blocked by game integration |
| CustomUI | view token, declarative controls, touch intent, text input | game.ui.v1 | Blocked by game integration |
| Persistence | mod/world scope, schema, commit generation | core.persistence.v1 | Implemented and tested desktop journal |
| Events/Simulation | subscription token, copied event value, tick number | core.events.v1 | Implemented and tested manual ticks |
| Messaging | mod channel, side, sender identity, payload and correlation ID | game.messaging.v1 | Not implemented |

Components bind to opaque world/entity tokens. Token validity ends on world
close, disconnect or owner disable; resolver must reject stale generations.
Client views are snapshots. Server owns inventory state; client request includes
revision/transaction ID. Client-provided player/owner IDs must be validated
against authenticated session by future transport. No network implementation
or authorization is currently claimed.

Available native SDK services use `core.*` capabilities and copied structs.
They are **logical services**, not Minecraft components. `ui_open`/`message`
return BF_UNSUPPORTED. Registry registration changes framework catalog only.
Native ABI recipe record supports up to nine concrete ingredients; tags,
alternatives, furnace/brewing timing and arbitrary recipe shapes need a later
versioned representation.

World containers require placement/world lifecycle, full lossless item codecs,
validated vanilla inventory ownership and crash-safe transfer integration before
real items may enter logical storage. Registry viewers require verified provider
data and render/touch/keyboard bindings. Each capability must ship with exact
profile, reproducible device results and safe failure tests.

Creative tabs must resolve stable item IDs through the versioned adapter. Display
names and Equipment collection indices are not item identities. Pagination belongs
to the view; Creative item acquisition belongs to the validated engine transaction
path. See [Inner Core/Horizon research](../research/INNER_CORE_HORIZON_ANALYSIS.md).
