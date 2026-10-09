# Creative tab API design

This document defines the framework-side contract only. It does not ship a
gameplay mod, resource pack, Minecraft UI override or target-specific engine
integration.

The proposed `CreativeTabRegistry` uses stable tab IDs, localized title keys,
icon item IDs, ordered item IDs and a registry generation. Pagination is a
presentation concern; selecting an item must use a verified engine transaction
capability. Display names and engine collection indices are never item identity.

The Android GAL currently has no verified Creative registry or grant adapter.
It must report `game.creative_tabs.v1` as unavailable until a target-specific
resolver and selection transaction have passed device tests. A script-facing
API will expose copied IDs and value records, never engine pointers.
