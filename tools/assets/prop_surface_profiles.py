"""Authored constant-PBR surfaces for the industrial, facility and lab kits.

Bindings use generator material identities, never inferred runtime colors.
Color overrides are sRGB reflectance colors for exposed metals; other surfaces
retain the kit palette. Importers and Blender authors share these bindings.
"""

SURFACES = {
    "paint": (0.0, .34, None),
    "dark_paint": (0.0, .28, None),
    "steel": (.90, .26, (184, 193, 200)),
    "aluminium": (1.0, .23, (205, 213, 219)),
    "copper": (1.0, .24, (212, 153, 104)),
    "plastic": (0.0, .36, None),
    "glass": (0.0, .12, None),
    "rubber": (0.0, .88, None),
    "fabric": (0.0, .92, None),
    "mineral": (0.0, .86, None),
    "wood": (0.0, .58, None),
    "board": (0.0, .72, None),
    "ceramic": (0.0, .52, None),
    "label": (0.0, .70, None),
}

INDUSTRIAL = {
    "crate": ("paint", "steel"),
    "barrier": ("mineral", "dark_paint"),
    "short_wall": ("mineral", "dark_paint"),
    "railing": ("paint", "steel"),
    "lamp_post": ("paint", "steel"),
    "vent_unit": ("paint", "steel"),
    "workbench": ("wood", "steel"),
    "ammo_container": ("paint", "dark_paint"),
    "industrial_pillar": ("paint", "steel"),
    "pipe_module": ("steel", "dark_paint"),
    "power_unit": ("paint", "steel"),
    "gate_frame": ("paint", "steel"),
    "control_cabinet": ("paint", "steel"),
}
ARCHITECTURAL = {
    "beam": ("paint", "steel"),
    "support": ("paint", "steel"),
    "wall": ("mineral", "dark_paint"),
    "doorway": ("mineral", "steel"),
    "pipe_straight": ("steel", "dark_paint"),
    "pipe_elbow": ("steel", "dark_paint"),
    "pipe_tee": ("steel", "dark_paint"),
    "service_panel": ("paint", "steel"),
    "cable_tray": ("steel", "dark_paint"),
    "floor_hatch": ("steel", "dark_paint"),
}
FACILITY = {
    "desk": ("wood", "steel"),
    "chair": ("fabric", "steel"),
    "monitor": ("plastic", "dark_paint"),
    "command_table": ("paint", "steel"),
    "low_cabinet": ("paint", "steel"),
    "bench": ("wood", "steel"),
    "terminal": ("paint", "steel"),
}
LAB = ("stand", "case", "board", "cpu", "memory", "compute", "cooling",
       "display", "keyboard")
LAB_BINDINGS = dict(zip(("lab_" + str(i) for i in range(7)),
                       ("aluminium", "plastic", "glass", "copper", "board",
                        "ceramic", "rubber")))

ASSET_BINDINGS = {}
for name, (body, frame) in INDUSTRIAL.items():
    asset = "rf_" + name
    if name == "crate":
        bindings = {asset + "_flat_body": body, asset + "_flat_frame": frame}
    else:
        bindings = {asset + "_flat_0": body, asset + "_flat_1": frame,
                    asset + "_flat_2": "glass" if name == "lamp_post" else "paint"}
    bindings[asset + "_albedo"] = "label"
    ASSET_BINDINGS[asset] = bindings
for prefix, family in (("rf_arch_", ARCHITECTURAL), ("rf_facility_", FACILITY)):
    for name, (body, frame) in family.items():
        asset = prefix + name
        bindings = {asset + "_body": body, asset + "_frame": frame}
        if prefix == "rf_facility_":
            bindings[asset + "_signal"] = "glass"
        ASSET_BINDINGS[asset] = bindings
for name in LAB:
    ASSET_BINDINGS["rf_lab_computer_" + name] = LAB_BINDINGS.copy()

MATERIAL_BINDINGS = {name: surface for bindings in ASSET_BINDINGS.values()
                     for name, surface in bindings.items()}


def linear_rgb(srgb):
    return tuple(v / 255 / 12.92 if v / 255 <= .04045 else
                 ((v / 255 + .055) / 1.055) ** 2.4 for v in srgb)


def material_profile(name):
    surface = MATERIAL_BINDINGS.get(name)
    return SURFACES[surface] if surface else None
