"""Explicit tick-differential noise candidates; see README.md before enabling.

None is enabled automatically: whole-object/stack masks can hide real bugs.
Shadow's two-original-run noise discovery is independent of these candidates.
"""
from dataclasses import dataclass


@dataclass(frozen=True)
class Mask:
    where: object  # symbol name or guest address
    size: int
    reason: str


MASKS = (
    Mask("D_800F53C0", 0xE8,
         "effect request from func_8007138C copied by func_800CF218: "
         "contains unwritten host-stack bytes; also contains meaningful fields"),
    Mask(0x801F8000, 0x8000,
         "candidate dead guest stack tail; only valid when the scenario proves it unread"),
    Mask("last_vsync", 16,
         "boot_runtime.c wall-clock timespec; size and unique symbol identity "
         "must be confirmed against the target, not inferred from this name"),
)


def resolve(symbols, *, masks=(), sizes=None):
    """Return digest RPC (address, size) pairs for explicitly selected masks.

Missing symbols are skipped. `sizes` supplies verified target object sizes;
required for symbolic masks, so a PSX or LP64 size is never silently reused.
The caller must supply the correct file-local last_vsync address (nm contains
multiple unrelated symbols with that name).
"""
    sizes = sizes or {}
    out = []
    for mask in masks:
        address = mask.where if isinstance(mask.where, int) else symbols.get(mask.where)
        if address is None:
            continue
        size = mask.size if isinstance(mask.where, int) else sizes.get(mask.where)
        if not isinstance(size, int) or size <= 0:
            raise ValueError(f"verified target size required for {mask.where}")
        if not isinstance(address, int) or address < 0:
            raise ValueError("invalid mask address")
        out.append((address, size))
    return out
