exec(open('case.py').read().split('# EXPORT')[0])
def vol(s): 
    try: return s.val().Volume()
    except Exception: return 0.0
parts = {}
# display PCB (with holes) + glass module
pcb = blk(px, 0, z_pcb_front, front_z, pcb_w, pcb_h)
for sx in (-1,1):
    for sy in (-1,1): pcb = pcb.cut(cyl(px+sx*peg_xy[0], sy*peg_xy[1], 0, 20, 2.5))
glass = blk(px, glass_cy, lip_t + 0.1, z_pcb_front, glass_w, glass_h)
backcomp = blk(px, 0, front_z, front_z + comp_zone, 50, 40)
batt = blk(-38.25 + batt_x/2, 0, front_z + comp_zone + 0.0, front_z + comp_zone + batt_z, batt_x, batt_y)
fe = blk(fe_right_x - feather_w/2, fe_top_y - feather_l/2, fe_face_z - fe_pcb_t, fe_face_z, feather_w, feather_l)
for (u, v) in fe_holes: fe = fe.cut(cyl(fe_x(v), fe_y(u), 0, 30, 2.5))
fe_comp = (blk(fe_x(fe_usb_uv_v), fe_y(0) - 3, fe_face_z, fe_face_z + 2.8, 8, 6)
           .union(blk(fe_x(18), fe_y(10.8), fe_face_z, fe_face_z + 5.7, 9, 6))
           .union(blk(fe_x(fe_reset[1]), fe_y(fe_reset[0]), fe_face_z, fe_face_z + fe_sw_h, 4.6, 2.8))
           .union(blk(fe_x(fe_user[1]), fe_y(fe_user[0]), fe_face_z, fe_face_z + fe_sw_h, 4.6, 2.8)))
sws = None
for ysw in btn_pts:
    s_ = blk(bx, ysw, z_bf, z_bf + sw_body_h, sw_size, sw_size).union(cyl(bx, ysw, z_bf - sw_plunger, z_bf, 3.5))
    sws = s_ if sws is None else sws.union(s_)
case = shell.union(lid_asm)
for name, comp in (("display pcb", pcb), ("glass", glass), ("display back comps", backcomp), ("battery", batt),
                   ("feather", fe), ("feather comps", fe_comp), ("button switches", sws)):
    print(f"{name:20s} interference with case: {vol(case.intersect(comp)):8.2f} mm3")
print("battery vs feather:", vol(batt.intersect(fe.union(fe_comp))))
print("feather vs display back:", vol(fe.intersect(backcomp)))
# does the plunger touch nub? gap check
print("nub tip z", tongue_t + nub_h, "plunger tip z", z_bf - sw_plunger)

# visual: assembly with components, cut in half (section through USB/Feather x and through button column)
comps = pcb.union(glass).union(backcomp).union(batt).union(fe).union(fe_comp).union(sws)
full = case.union(comps)
cq.exporters.export(full, "viz_full.stl", tolerance=0.02, angularTolerance=0.2)
cutter = cq.Workplane("XY").box(200, 200, 200).translate((0, -100 + btn_pts[1], 0))   # keep y > btn_pts[1]
cq.exporters.export(full.cut(cutter), "viz_sectionY.stl", tolerance=0.02, angularTolerance=0.2)
cq.exporters.export(shell.union(comps), "viz_open.stl", tolerance=0.02, angularTolerance=0.2)
