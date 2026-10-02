#!/usr/bin/env python3
"""Fails on data defined in more than one of the given binaries (executable, plugins).

A library statically linked into several plugins keeps a copy of its globals in each, so
state meant to be process-wide (a logger registry, a cache, a hook) silently splits per
plugin -- as spdlog's registry did. `nm` can't tell a constant from mutable data, so known
harmless duplicates are allowed below, each with its reason; anything else is an error.
"""

import collections
import os
import re
import subprocess
import sys

DATA_SYMBOL_TYPES = set("BbDdu")
COMPILER_EMITTED = re.compile(r"^(typeinfo|vtable|VTT|guard variable|construction vtable) ")

ALLOWED = [
    (r"^flecs::",
     "flecs's C++ headers keep per-binary type caches and entity ids; SyncFlecsOsApi and "
     "name lookup reconcile them (z13_plugin_smoke)"),
    (r"^std::", "standard-library constants (ranges CPOs, in_place tags); libstdc++ is shared"),
    (r"^(__|DW\.ref\.|completed\.\d+$|_TLS_MODULE_BASE_$)", "toolchain bookkeeping in every ELF object"),
    (r"^rfl::Field<", "reflect-cpp field-name constants"),
    (r"^flatbuffers::(kHashFunctions\d+$|data<|\(anonymous namespace\)::TokenToString\(int\)::tokens$|Parser::)",
     "FlatBuffers constants and parser scratch; each binary parses its own schemas"),
    (r"^flexbuffers::\w+::Empty\w*\(\)::\w+$", "FlexBuffers empty-value constants"),
    (r"^boost::(system::(detail::\w+_cat_holder<void>::instance$|error_code::location\(\) const::loc$|"
     r"error_category::init_stdcat\(\) const::mx_$)|filesystem::|container::(std_)?piecewise_construct)",
     "Boost.Filesystem/System linked by lib_core and raylib_module: error categories compare "
     "by id, and nothing changes the path locale"),
    (r"^\(anonymous namespace\)::g_(path_locale|path_locale_deleter|dot_path|dot_dot_path)$",
     "Boost.Filesystem path globals, as above"),
    (r"^z13::(\w+::)*k[A-Z]\w*$", "constexpr constants from our headers"),
    (r"::data\(\)::bfbsData$", "binary FlatBuffers schemas embedded by flatc"),
]


def DataSymbols(binary):
    output = subprocess.run(
        ["nm", "--demangle", "--defined-only", binary], capture_output=True, text=True, check=True).stdout
    for line in output.splitlines():
        parts = line.split(" ", 2)
        if len(parts) == 3 and parts[1] in DATA_SYMBOL_TYPES and not COMPILER_EMITTED.match(parts[2]):
            yield parts[2]


def main(binaries):
    owners = collections.defaultdict(set)
    for binary in binaries:
        for symbol in DataSymbols(binary):
            owners[symbol].add(os.path.basename(binary))

    allowed = [(re.compile(pattern), reason) for pattern, reason in ALLOWED]
    unexpected = sorted(
        (symbol, sorted(where)) for symbol, where in owners.items()
        if len(where) > 1 and not any(pattern.search(symbol) for pattern, _ in allowed))
    for symbol, where in unexpected:
        print(f"::error::{symbol} is defined in {', '.join(where)}")
    if unexpected:
        print(f"{len(unexpected)} data symbols are duplicated across binaries: make the library that "
              "defines them shared (triplets/), or allow them here with a reason if they're constants.")
        return 1
    print(f"no unexpected duplicated data across {len(binaries)} binaries")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
