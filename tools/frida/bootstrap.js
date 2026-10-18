// In-process bootstrap only. Requires an already-authorized Frida session in Minecraft.
// Does not patch engine code or call recovered engine methods.
let runtime = null;
let shutdown = null;
rpc.exports = {
    bootstrap(runtimePath, modPath) {
        if (Process.arch !== 'arm64') throw new Error('Expected ARM64 Minecraft process');
        if (!Process.findModuleByName('libminecraftpe.so')) throw new Error('Minecraft engine not loaded');
        if (!runtimePath.startsWith('/') || !modPath.startsWith('/')) throw new Error('Absolute device library paths required');
        runtime = Module.load(runtimePath);
        const probe = new NativeFunction(runtime.getExportByName('hybrid_probe'), 'pointer', []);
        const load = new NativeFunction(runtime.getExportByName('hybrid_load_mod'), 'int', ['pointer']);
        shutdown = new NativeFunction(runtime.getExportByName('hybrid_shutdown'), 'void', []);
        const identity = probe().readUtf8String();
        if (!identity.startsWith('engine_verified:')) throw new Error(identity);
        const result = load(Memory.allocUtf8String(modPath));
        return { identity, result, status: probe().readUtf8String(), gameplayActive: false };
    },
    shutdown() {
        if (shutdown) shutdown();
    }
};
