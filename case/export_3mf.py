"""Bambu Studio project files for the three plates: python export_3mf.py   (run case.py first: it writes the STLs)

  case_front.3mf    shell (filament 1, body) + panel (filament 2, accent), ONE object with two parts, front face down
  case_back.3mf     lid (1) + wordmark (2), outer face down
  case_buttons.3mf  caps (3) + legends (1), head down

Format copied from a 3MF written by Bambu Studio 2.x (3D/3dmodel.model with one object made of components, the meshes in
3D/Objects/object_1.model, Metadata/model_settings.config with the part names and extruders, Metadata/project_settings.config
with the printer / process / filament presets). The presets are the A1 0.4 nozzle + 0.20 mm Standard + Bambu PETG HF system
profiles of the installed Bambu Studio, resolved into one flat dictionary (needs Bambu Studio installed; without it the files
still open, with the printer settings of the current project).
Objects are centred on the 256 x 256 mm A1 plate; part positions are exactly those of the STL files, so the inlay sits in its pockets.
"""
import json, os, sys, zipfile
import numpy as np
import trimesh

APP = "/Applications/BambuStudio.app/Contents/Resources/profiles/BBL"
FILAMENT_COLOURS = ["#EEE4CD", "#24563E", "#E87028"]       # 1 body (cream), 2 panel / wordmark (forest), 3 caps (orange)
PLATE = (128.0, 128.0)
VERSION = "02.04.00.70"

NS = ('xmlns="http://schemas.microsoft.com/3dmanufacturing/core/2015/02" xmlns:BambuStudio="http://schemas.bambulab.com/package/2021" '
      'xmlns:p="http://schemas.microsoft.com/3dmanufacturing/production/2015/06" requiredextensions="p"')

def load_profile(kind, name):
    d = json.load(open(os.path.join(APP, kind, name + ".json")))
    if d.get("inherits"):
        base = load_profile(kind, d["inherits"])
        base.update(d)
        d = base
    return d

def project_settings():
    """flat A1 / PETG preset dictionary in the layout of project_settings.config, or None without Bambu Studio"""
    if not os.path.isdir(APP):
        return None
    machine = load_profile("machine", "Bambu Lab A1 0.4 nozzle")
    process = load_profile("process", "0.20mm Standard @BBL A1")
    fil = load_profile("filament", "Bambu PETG HF @BBL A1")
    n = len(FILAMENT_COLOURS)
    skip = {"type", "name", "from", "instantiation", "inherits", "setting_id", "filament_id", "compatible_printers",
            "compatible_printers_condition", "print_compatible_printers", "version", "is_custom_defined", "renamed_from"}
    cfg = {}
    for k, v in {**machine, **process}.items():
        if k not in skip: cfg[k] = v
    for k, v in fil.items():                       # every filament key is a per-filament vector
        if k in skip: continue
        v = v if isinstance(v, list) else [v]
        cfg[k] = (v * n)[:n] if len(v) == 1 else v[:1] * n
    cfg["filament_colour"] = FILAMENT_COLOURS
    cfg["filament_settings_id"] = ["Bambu PETG HF @BBL A1"] * n
    cfg["filament_ids"] = [fil.get("filament_id", "GFG02")] * n if isinstance(fil.get("filament_id"), str) else ["GFG02"] * n
    cfg["filament_map"] = ["1"] * n
    cfg["default_filament_colour"] = [""] * n
    cfg["printer_settings_id"] = "Bambu Lab A1 0.4 nozzle"
    cfg["print_settings_id"] = "0.20mm Standard @BBL A1"
    cfg["printer_model"] = machine.get("printer_model", "Bambu Lab A1")
    cfg["curr_bed_type"] = "Textured PEI Plate"
    cfg["different_settings_to_system"] = [""] * (n + 2)
    for k in ("solid_infill_filament", "sparse_infill_filament", "wall_filament"):   # profiles say 0 ("default"), projects say 1
        cfg[k] = "1"
    cfg["tree_support_wall_count"] = "0"
    cfg["from"] = "project"; cfg["name"] = "project_settings"; cfg["version"] = "02.00.02.01"
    return cfg

def mesh_xml(oid, m, uuid):
    v = m.vertices; f = m.faces
    out = [f' <object id="{oid}" p:UUID="{uuid}" type="model">', '  <mesh>', '   <vertices>']
    out += [f'    <vertex x="{x:.6g}" y="{y:.6g}" z="{z:.6g}"/>' for x, y, z in v]
    out += ['   </vertices>', '   <triangles>']
    out += [f'    <triangle v1="{a}" v2="{b}" v3="{c}"/>' for a, b, c in f]
    out += ['   </triangles>', '  </mesh>', ' </object>']
    return "\n".join(out)

def write_3mf(path, obj_name, parts, settings):
    """parts: [(name, stl file, extruder)]; the first part decides the object's extruder"""
    meshes = []
    for name, stl, ext in parts:
        m = trimesh.load(stl)
        m.merge_vertices()
        meshes.append(m)
    lo = np.min([m.bounds[0] for m in meshes], axis=0); hi = np.max([m.bounds[1] for m in meshes], axis=0)
    shift = np.array([-(lo[0] + hi[0]) / 2, -(lo[1] + hi[1]) / 2, 0.0])      # object origin on the xy centre, z = 0 stays the bed
    for m in meshes: m.apply_translation(shift)
    tx, ty = PLATE
    ident = "1 0 0 0 1 0 0 0 1 0 0 0"
    sub = ['<?xml version="1.0" encoding="UTF-8"?>', f'<model unit="millimeter" xml:lang="en-US" {NS}>',
           ' <metadata name="BambuStudio:3mfVersion">1</metadata>', ' <resources>']
    for i, m in enumerate(meshes, 1):
        sub.append(mesh_xml(i, m, f"0001{i:04d}-81cb-4c03-9d28-80fed5dfa1dc"))
    sub += [' </resources>', ' <build/>', '</model>']
    comps = "\n".join(f'    <component p:path="/3D/Objects/object_1.model" objectid="{i}" p:UUID="0002{i:04d}-b206-40ff-9872-83e8017abed1" transform="{ident}"/>'
                      for i in range(1, len(meshes) + 1))
    main = ['<?xml version="1.0" encoding="UTF-8"?>', f'<model unit="millimeter" xml:lang="en-US" {NS}>',
            f' <metadata name="Application">BambuStudio-{VERSION}</metadata>', ' <metadata name="BambuStudio:3mfVersion">1</metadata>',
            f' <metadata name="Title">{obj_name}</metadata>', ' <resources>',
            '  <object id="3" p:UUID="00000001-61cb-4c03-9d28-80fed5dfa1dc" type="model">', '   <components>', comps, '   </components>', '  </object>',
            ' </resources>', ' <build p:UUID="2c7c17d8-22b5-4d84-8835-1976022ea369">',
            f'  <item objectid="3" p:UUID="00000003-b1ec-4553-aec9-835e5b724bb4" transform="1 0 0 0 1 0 0 0 1 {tx} {ty} 0" printable="1"/>',
            ' </build>', '</model>']
    cfg = ['<?xml version="1.0" encoding="UTF-8"?>', '<config>', '  <object id="3">', f'    <metadata key="name" value="{obj_name}"/>',
           f'    <metadata key="extruder" value="{parts[0][2]}"/>', f'    <metadata face_count="{sum(len(m.faces) for m in meshes)}"/>']
    for i, ((name, stl, ext), m) in enumerate(zip(parts, meshes), 1):
        cfg += [f'    <part id="{i}" subtype="normal_part">', f'      <metadata key="name" value="{name}"/>',
                '      <metadata key="matrix" value="1 0 0 0 0 1 0 0 0 0 1 0 0 0 0 1"/>',
                f'      <metadata key="source_file" value="{os.path.basename(stl)}"/>', f'      <metadata key="source_object_id" value="{i}"/>',
                '      <metadata key="source_volume_id" value="0"/>', f'      <metadata key="extruder" value="{ext}"/>',
                f'      <mesh_stat face_count="{len(m.faces)}" edges_fixed="0" degenerate_facets="0" facets_removed="0" facets_reversed="0" backwards_edges="0"/>',
                '    </part>']
    cfg += ['  </object>', '  <plate>', '    <metadata key="plater_id" value="1"/>', '    <metadata key="plater_name" value=""/>',
            '    <metadata key="locked" value="false"/>', '    <model_instance>', '      <metadata key="object_id" value="3"/>',
            '      <metadata key="instance_id" value="0"/>', '      <metadata key="identify_id" value="709"/>', '    </model_instance>', '  </plate>',
            '  <assemble>', f'   <assemble_item object_id="3" instance_id="0" transform="1 0 0 0 1 0 0 0 1 {tx} {ty} 0" offset="0 0 0" />',
            '  </assemble>', '</config>']
    types = ('<?xml version="1.0" encoding="UTF-8"?>\n<Types xmlns="http://schemas.openxmlformats.org/package/2006/content-types">\n'
             ' <Default Extension="rels" ContentType="application/vnd.openxmlformats-package.relationships+xml"/>\n'
             ' <Default Extension="model" ContentType="application/vnd.ms-package.3dmanufacturing-3dmodel+xml"/>\n'
             ' <Default Extension="png" ContentType="image/png"/>\n <Default Extension="gcode" ContentType="text/x.gcode"/>\n</Types>')
    rels = ('<?xml version="1.0" encoding="UTF-8"?>\n<Relationships xmlns="http://schemas.openxmlformats.org/package/2006/relationships">\n'
            ' <Relationship Target="/3D/3dmodel.model" Id="rel-1" Type="http://schemas.microsoft.com/3dmanufacturing/2013/01/3dmodel"/>\n</Relationships>')
    rels3d = ('<?xml version="1.0" encoding="UTF-8"?>\n<Relationships xmlns="http://schemas.openxmlformats.org/package/2006/relationships">\n'
              ' <Relationship Target="/3D/Objects/object_1.model" Id="rel-1" Type="http://schemas.microsoft.com/3dmanufacturing/2013/01/3dmodel"/>\n</Relationships>')
    with zipfile.ZipFile(path, "w", zipfile.ZIP_DEFLATED) as z:
        z.writestr("[Content_Types].xml", types)
        z.writestr("_rels/.rels", rels)
        z.writestr("3D/3dmodel.model", "\n".join(main))
        z.writestr("3D/_rels/3dmodel.model.rels", rels3d)
        z.writestr("3D/Objects/object_1.model", "\n".join(sub))
        z.writestr("Metadata/model_settings.config", "\n".join(cfg))
        if settings is not None:
            z.writestr("Metadata/project_settings.config", json.dumps(settings, indent=4))
    print(f"{path}: {len(parts)} parts, centred at {PLATE}, bbox {np.round(hi - lo, 1)} mm")

if __name__ == "__main__":
    st = project_settings()
    write_3mf("case_front.3mf", "front", [("shell", "case_shell.stl", 1), ("panel", "case_shell_inlay.stl", 2)], st)
    write_3mf("case_back.3mf", "back", [("lid", "case_lid.stl", 1), ("wordmark", "case_lid_inlay.stl", 2)], st)
    write_3mf("case_buttons.3mf", "buttons", [("caps", "case_buttons.stl", 3), ("legends", "case_buttons_inlay.stl", 1)], st)
