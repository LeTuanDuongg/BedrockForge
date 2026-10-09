"""Desktop Python 3 reference binding to the real native core.

This is a trusted scripting SDK prototype, not an Android embedded interpreter
or a security sandbox. Mod code receives value objects and integer handle IDs.
"""
import ctypes as C
from dataclasses import dataclass

class ModError(RuntimeError):
    def __init__(self, code):
        super().__init__(f"BedrockForge status {code}")
        self.code = code

class _Item(C.Structure):
    _fields_ = [("id", C.c_char*128), ("name", C.c_char*128),
                ("category", C.c_char*64), ("icon", C.c_char*256), ("max_stack", C.c_uint32)]
class _Stack(C.Structure):
    _fields_ = [("item", C.c_char*128), ("metadata", C.c_char*256), ("count", C.c_uint32)]
class _Container(C.Structure):
    _fields_ = [("handle", C.c_uint64), ("identity", C.c_char*128),
                ("slots", C.c_uint32), ("revision", C.c_uint64)]
class _Recipe(C.Structure):
    _fields_ = [("id", C.c_char*128), ("provider", C.c_char*64),
                ("output", _Stack), ("ingredient_count", C.c_uint32), ("ingredients", _Stack*9)]

def _check(result):
    if result: raise ModError(result)
def _encoded(value, limit):
    data = value.encode("utf-8")
    if b"\0" in data or len(data) >= limit: raise ValueError("invalid or oversized string")
    return data
def _uint(value, bits=32):
    if not isinstance(value, int) or not 0 <= value < 2**bits: raise ValueError("integer range")
    return value

@dataclass(frozen=True)
class Container:
    handle: int
    identity: str
    slots: int
    revision: int

class Host:
    def __init__(self, library, data_root):
        self._lib = C.CDLL(str(library))
        signatures = {
            "host_create": (C.c_uint64, [C.c_char_p]),
            "host_destroy": (None, [C.c_uint64]),
            "bind": (C.c_uint64, [C.c_uint64, C.c_char_p]),
            "capability": (C.c_int32, [C.c_uint64, C.c_char_p]),
            "item_register": (C.c_int32, [C.c_uint64, C.POINTER(_Item)]),
            "item_at": (C.c_int32, [C.c_uint64, C.c_uint32, C.POINTER(_Item)]),
            "container": (C.c_int32, [C.c_uint64, C.c_char_p, C.c_uint32, C.POINTER(_Container)]),
            "read": (C.c_int32, [C.c_uint64, C.c_uint64, C.c_uint32, C.POINTER(_Stack)]),
            "deposit": (C.c_int32, [C.c_uint64, C.c_uint64, C.c_uint32, C.POINTER(_Stack), C.c_uint64]),
            "withdraw": (C.c_int32, [C.c_uint64, C.c_uint64, C.c_uint32, C.c_uint32, C.c_uint64, C.POINTER(_Stack)]),
            "recipe_register": (C.c_int32, [C.c_uint64, C.POINTER(_Recipe)]),
            "recipe_at": (C.c_int32, [C.c_uint64, C.c_uint32, C.POINTER(_Recipe)]),
            "start_native": (C.c_int32, [C.c_uint64, C.c_uint32, C.POINTER(C.c_void_p)]),
        }
        for name, (ret, args) in signatures.items():
            fn = getattr(self._lib, "bf_script_"+name)
            fn.restype, fn.argtypes = ret, args
        self._token = self._lib.bf_script_host_create(_encoded(str(data_root), 1024))
        if not self._token: raise ModError(5)
        self._native_mods=[]
    def load_native_mods(self, libraries):
        """Trusted host bootstrap; pointers remain inside this binding implementation."""
        if self._native_mods: raise ValueError("native mods already started")
        loaded=[C.CDLL(str(path)) for path in libraries]
        entries=[]
        for lib in loaded:
            lib.bf_mod_entry.restype=C.c_void_p
            lib.bf_mod_entry.argtypes=[]
            entries.append(lib.bf_mod_entry())
        descriptors=(C.c_void_p*len(entries))(*entries)
        _check(self._lib.bf_script_start_native(self._token,len(entries),descriptors))
        self._native_mods=loaded
    def mod(self, owner):
        token = self._lib.bf_script_bind(self._token, _encoded(owner, 128))
        if not token: raise ModError(1)
        return ModAPI(self, token)
    def close(self):
        if self._token:
            self._lib.bf_script_host_destroy(self._token)
            self._token = 0
            self._native_mods=[]
    def __enter__(self): return self
    def __exit__(self, *_): self.close()

class ModAPI:
    def __init__(self, host, token): self._host, self._token = host, token
    def capability(self, name):
        return self._host._lib.bf_script_capability(self._token, _encoded(name, 1024)) == 0
    def register_item(self, identifier, name, category="", max_stack=64):
        item = _Item(_encoded(identifier,128), _encoded(name,128), _encoded(category,64), b"", _uint(max_stack))
        _check(self._host._lib.bf_script_item_register(self._token, C.byref(item)))
    def search(self, query="", category=""):
        _encoded(query,1024); _encoded(category,64)
        items=[]; index=0
        while True:
            item=_Item(); result=self._host._lib.bf_script_item_at(self._token,index,C.byref(item))
            if result == 2: break
            _check(result); index+=1
            data={k:getattr(item,k).decode("utf-8") for k in ("id","name","category","icon")}
            data["max_stack"]=item.max_stack
            if (not category or category==data["category"]) and (query.casefold() in data["id"].casefold() or query.casefold() in data["name"].casefold()): items.append(data)
        return items
    def container(self, key, slots):
        out=_Container(); _check(self._host._lib.bf_script_container(self._token,_encoded(key,1024),_uint(slots),C.byref(out)))
        return Container(out.handle,out.identity.decode("utf-8"),out.slots,out.revision)
    def read(self, container, slot):
        out=_Stack(); _check(self._host._lib.bf_script_read(self._token,container.handle,_uint(slot),C.byref(out)))
        return {"item":out.item.decode("utf-8"),"metadata":out.metadata.decode("utf-8"),"count":out.count}
    def deposit(self, container, slot, identifier, count, metadata=""):
        stack=_Stack(_encoded(identifier,128),_encoded(metadata,256),_uint(count))
        _check(self._host._lib.bf_script_deposit(self._token,container.handle,_uint(slot),C.byref(stack),_uint(container.revision,64)))
    def withdraw(self, container, slot, count):
        out=_Stack()
        _check(self._host._lib.bf_script_withdraw(self._token,container.handle,_uint(slot),_uint(count),_uint(container.revision,64),C.byref(out)))
        return {"item":out.item.decode(),"metadata":out.metadata.decode(),"count":out.count}
    def register_recipe(self, identifier, output, ingredients):
        if not 1 <= len(ingredients) <= 9: raise ValueError("recipe ingredient count")
        def make_stack(value): return _Stack(_encoded(value["item"],128),_encoded(value.get("metadata",""),256),_uint(value["count"]))
        recipe=_Recipe()
        recipe.id=_encoded(identifier,128);recipe.provider=_encoded(identifier.split(':',1)[0],64)
        recipe.output=make_stack(output);recipe.ingredient_count=len(ingredients)
        for index,value in enumerate(ingredients): recipe.ingredients[index]=make_stack(value)
        _check(self._host._lib.bf_script_recipe_register(self._token,C.byref(recipe)))
    def recipes(self, identifier, usage=False):
        results=[];index=0
        def value(s):return {"item":s.item.decode(),"metadata":s.metadata.decode(),"count":s.count}
        while True:
            recipe=_Recipe();status=self._host._lib.bf_script_recipe_at(self._token,index,C.byref(recipe))
            if status==2:break
            _check(status);index+=1
            ingredients=[value(recipe.ingredients[i]) for i in range(recipe.ingredient_count)]
            if (usage and any(s['item']==identifier for s in ingredients)) or (not usage and recipe.output.item.decode()==identifier):
                results.append({"id":recipe.id.decode(),"provider":recipe.provider.decode(),"output":value(recipe.output),"ingredients":ingredients})
        return results
