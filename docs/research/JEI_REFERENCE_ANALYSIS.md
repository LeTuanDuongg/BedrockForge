# JEI 1.19.2 reference analysis

This analysis inspected the user-provided
`jei-1.19.2-forge-11.68.0.1086.jar` metadata, package layout and public API
signatures. It is a technical reference for an independent BedrockForge browser;
the JAR, Java implementation, textures, icons and other artwork are not copied
or bundled.

## Verified from the supplied JAR

`META-INF/mods.toml` identifies JEI `11.68.0.1086` as a Java FML mod targeting
Minecraft `1.19` through `1.19.2` and Forge `41.0.94` or newer. It also declares
the required `mezz_config` dependency and optional client configuration GUI.
That Java/Forge runtime contract is incompatible with BedrockForge's Android
native-module ABI; the `.jar` cannot be installed as a `.bfmod` or loaded by
Minecraft Bedrock.

The JAR exposes a rich Java API rather than only a screen. `IRecipeRegistration`
registers recipes and ingredient information. `IIngredientFilter` controls and
reports search results. `IIngredientListOverlay` exposes the visible ingredient
list and ingredient under the pointer. `IRecipeManager` queries typed recipe
categories, recipes and catalysts. `IJeiRuntime` composes those services with
recipe screens, bookmarks, ingredient management, key mappings and recipe
transfer. Other public packages cover recipe layouts, input routing, GUI
handlers, search and registration extensions.

The public API and internal package structure demonstrate these behavior groups:

- A persistent item/ingredient browser with filtering, ordering and pagination.
- Recipe category browsing, item-to-recipe and item-to-usage lookup.
- Plugin-provided ingredient types, recipe categories and recipe entries.
- Focus and hover context, bookmarks, tooltips and key/input handlers.
- Optional recipe transfer from the browser into a compatible crafting UI.
- Client UI rendering and search, with transfer work split across client/server
  packet classes.

These conclusions come from metadata, class/interface names and JVM signatures;
they do not claim runtime testing of this JAR.

## What Bedrock publicly exposes

Microsoft's current Script API documents `ItemTypes.getAll()` and each
`ItemType` exposes an identifier and localization key. This is a promising
catalog provider for registered items, subject to checking availability on the
user's exact Bedrock build. [ItemTypes API](https://learn.microsoft.com/en-us/minecraft/creator/scriptapi/minecraft/server/itemtypes?view=minecraft-bedrock-stable),
[ItemType API](https://learn.microsoft.com/en-us/minecraft/creator/scriptapi/minecraft/server/itemtype?view=minecraft-bedrock-stable)

The reviewed public Script API does not provide a documented global recipe
registry query equivalent to JEI's `IRecipeManager`. A newer documented
`RecipeCraftingContext` exposes recipe IDs valid for a player's active crafting
screen, but the reference marks this API pre-release. Recipe JSON in packs is
another possible provider when those pack files are available. These sources
do not establish full recipe discovery for all installed Bedrock content.
[RecipeCraftingContext](https://learn.microsoft.com/en-us/minecraft/creator/scriptapi/minecraft/server/recipecraftingcontext?view=minecraft-bedrock-stable),
[Recipe JSON reference](https://learn.microsoft.com/en-us/minecraft/creator/reference/content/recipereference/?view=minecraft-bedrock-stable)

Bedrock's `@minecraft/server-ui` provides dialog/form workflows; it does not
document a JEI-style persistent inventory-side overlay. JSON UI resource packs
can declare screens through `_ui_defs.json`, but Microsoft's cooperative add-on
guidance says JSON UI files are not cooperatively overridable. Therefore these
public APIs do not yet prove that BedrockForge can add a safe, composable pane
to the live inventory screen on Minecraft `1.26.30.5`.
[Server UI API](https://learn.microsoft.com/en-us/minecraft/creator/scriptapi/minecraft/server-ui/minecraft-server-ui?view=minecraft-bedrock-stable),
[JSON UI definitions](https://learn.microsoft.com/en-us/minecraft/creator/reference/content/jsonuireference/examples/jsonuicomponents/ui_defs?view=minecraft-bedrock-stable),
[Cooperative add-on guidance](https://learn.microsoft.com/en-us/minecraft/creator/documents/practices/guidelinesforbuildingcooperativeaddons?view=minecraft-bedrock-stable)

## BedrockForge adaptation

The user's reference layout can guide feature parity: a side catalog, compact
item grid, page controls, search field, and a separate recipe/uses view. We can
implement those behaviors with BedrockForge-owned controls, names, colors and
textures, adapted for touch. We must not copy JEI's texture files or source.

The framework already has logical item/recipe providers and searchable value
records, but it currently has no live Bedrock item provider and its `ui_open`
service returns `BF_UNSUPPORTED`. An in-game implementation therefore depends
on a verified GAL adapter that supplies all of:

1. A renderable, touch-interactive inventory overlay surface.
2. A live item catalog with localized names and safe icon resolution.
3. Recipe discovery with an explicit completeness/source declaration.
4. UI lifecycle and input routing that do not mutate inventory implicitly.

Until those capabilities pass device tests on the pinned game build, the JEI-like
browser is **PROTOTYPE_ONLY / BLOCKED by engine integration**. A native module
that merely registers catalog rows would not make an in-game UI. Recipe transfer
or item granting should remain disabled until separately verified transaction
capabilities exist. Any eventual gameplay package stays outside the public
system-only repository.
