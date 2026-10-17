# Experimental Button Duration Mod

`hybridmcpe:button-duration-experiment` is a separate Mod ABI 1 module. Its `start` prepares a passive hook; explicit control enables the 200-tick behavior or disables and removes the hook. Runtime suspend calls `stop`, which unhooks; resume prepares it again and restores the user's enabled state. Failed unhook throws through the existing runtime guard so its library handle stays loaded.

The module consumes one private bridge symbol: `hybrid_engine_button_schedule_126504_control(operation)`. Operations are `0` observe/install, `1` enable, and `2` disable/unhook. The bridge owns Shadowhook, AArch64 addresses, callsite and vtable checks, and the exact engine fingerprint gate. This is experimental and supports only Minecraft 1.26.50.4 ARM64 with SHA-256 `fac2be5b17ef79419ebd66fe4f9e362a48d7b2067d5e1f649e65c36edf2927e1`.

The boundary can support later mods that need version-pinned hooks, but it is not a stable gameplay API. Each hook still needs its own verified ABI, object lifetime, thread and cleanup contract. No block registration contract is established by this change.
