"""Allow a project socket limit in the pinned Ethernet 2.0.2 dependency.
Only the generated .pio library copy is changed.
"""
from pathlib import Path
Import("env")
header = Path(env.subst("$PROJECT_LIBDEPS_DIR")) / env.subst("$PIOENV") / "Ethernet/src/Ethernet.h"
if not header.is_file():
    raise RuntimeError("Ethernet 2.0.2 dependency missing; install project libraries first")
original = """#if defined(RAMEND) && defined(RAMSTART) && ((RAMEND - RAMSTART) <= 2048)
#define MAX_SOCK_NUM 4
#else
#define MAX_SOCK_NUM 8
#endif"""
replacement = "#ifndef MAX_SOCK_NUM\n" + original + "\n#endif // project socket limit"
source = header.read_text(encoding="utf-8")
if replacement not in source:
    if source.count(original) != 1:
        raise RuntimeError("Unexpected Ethernet.h socket configuration; review library version")
    header.write_text(source.replace(original, replacement, 1), encoding="utf-8", newline="")
