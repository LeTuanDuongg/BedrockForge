# Iron Chest core boundary

`hybrid::iron_chest::Container` owns exactly 54 native `ItemStack` values. It provides atomic revision-checked transactions for bulk insert, extract, split, merge and swap. Callers provide the item/variant stack-limit table; unknown items fail closed. Stack metadata stays opaque and must compare byte-for-byte to merge.

The version-1 save is deterministic, little-endian and CRC32 checked. It stores the core's 54 slots and transaction revision only. It is not Minecraft world persistence. Failed or unsupported loads leave the current container unchanged. Closed containers reject reads and transactions.

## Required engine integration

- A supported block registry and placeable custom-block identifier, including block placement/removal behavior.
- A block-entity factory and world ownership/lifetime callbacks for creating one core per placed chest and destroying it safely.
- Verified ItemStack import/export contracts, including identity, variant, count, metadata, copy/move ownership and destruction. Current static analysis does not establish callable constructors or safe conversion.
- Authoritative player/container transaction callbacks with slot limits, permissions, synchronization and rollback semantics. The standalone transaction model does not replace Bedrock's transaction engine.
- A documented or verified block-entity save/load hook that stores this deterministic byte payload in the world save.
- Actual chest-screen registration/navigation, player inventory binding, slot mapping, touchscreen input and close lifecycle.

No engine address is called here. Container, chest UI and persistence capabilities remain disabled. The core and its Android test executable are not Minecraft gameplay integration.
