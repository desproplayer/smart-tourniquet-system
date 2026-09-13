// ============================================================
// Smart Tourniquet System — Phantom Lengan (Validation Rig)
// Kelompok 19 — Desain Proyek Teknik Elektro, Komputer, Biomedik 2
// PIC: Grace Kezia Siregar
// render_mode: "visual" / "mold_bottom" / "fixture"
// ============================================================

$fn = 64;

limb_length = 250; limb_od = 80; bone_od = 20; bone_wall = 3;
artery_od = 4; artery_wall = 1; artery_offset_from_center = 22;
fixture_base_w = 140; fixture_base_d = 100; fixture_base_h = 10;
fixture_post_d = 25; fixture_post_h = 60;

module bone_pvc() {
    difference() {
        cylinder(h=limb_length, d=bone_od);
        translate([0,0,-1]) cylinder(h=limb_length+2, d=bone_od-2*bone_wall);
    }
}
module artery_tube(offset_angle=90) {
    x = artery_offset_from_center*cos(offset_angle);
    y = artery_offset_from_center*sin(offset_angle);
    translate([x,y,0])
        difference() {
            cylinder(h=limb_length, d=artery_od);
            translate([0,0,-1]) cylinder(h=limb_length+2, d=artery_od-2*artery_wall);
        }
}
module soft_tissue_solid() { cylinder(h=limb_length, d=limb_od); }

module phantom_arm_visual() {
    color("Bisque", 0.5)
        difference() {
            soft_tissue_solid();
            translate([0,0,-1]) cylinder(h=limb_length+2, d=bone_od+0.5);
            translate([artery_offset_from_center,0,-1]) cylinder(h=limb_length+2, d=artery_od+1);
        }
    color("Ivory") bone_pvc();
    color("Red") artery_tube(offset_angle=0);
}

module mold_half(is_top=false) {
    mold_wall = 5;
    difference() {
        cylinder(h=limb_length, d=limb_od+2*mold_wall);
        translate([0,0,-1]) cylinder(h=limb_length+2, d=limb_od);
        translate(is_top ? [-(limb_od/2+mold_wall+1), -(limb_od+2*mold_wall), -1]
                          : [-(limb_od/2+mold_wall+1), 0, -1])
            cube([limb_od+2*mold_wall+2, limb_od+2*mold_wall, limb_length+2]);
    }
}
module mold_alignment_holes() {
    translate([0,0,-1]) cylinder(h=limb_length+2, d=bone_od+0.3);
    translate([artery_offset_from_center,0,-1]) cylinder(h=limb_length+2, d=artery_od+0.3);
}
module mold_bottom_final() {
    difference() { mold_half(is_top=false); mold_alignment_holes(); }
}
module fixture_stand() {
    translate([-fixture_base_w/2, -fixture_base_d/2, 0]) cube([fixture_base_w, fixture_base_d, fixture_base_h]);
    for (side=[-1,1])
        translate([side*(limb_od*0.28), 0, fixture_base_h])
            rotate([0, side*20, 0]) cylinder(h=fixture_post_h, d=fixture_post_d/2);
}

render_mode = "visual";
if (render_mode == "visual") { phantom_arm_visual(); }
else if (render_mode == "mold_bottom") { mold_bottom_final(); }
else if (render_mode == "fixture") { fixture_stand(); }
