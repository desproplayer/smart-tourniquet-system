// ============================================================
// Smart Tourniquet System — Bladder Housing (Backing Plate)
// Kelompok 19 — Desain Proyek Teknik Elektro, Komputer, Biomedik 2
//
// Ukuran luar: 190mm x 70mm (ditentukan tim, membentang sepanjang strap)
// ============================================================

$fn = 96;

housing_outer_l = 190; housing_outer_w = 70; housing_corner_r = 6;
pad_length = 60; pad_width = 40; pad_max_thickness = 15;
housing_wall = 2.5; housing_depth = pad_max_thickness + 3;
foam_layer_thickness = 2;
strap_slot_h = 3; strap_slot_w = 60; strap_slot_inset = 8;
tube_channel_d = 6; tube_strain_relief_length = 14;
alignment_mark_w = 2; alignment_mark_length = 20;

module rounded_rect_centered(l, w, r) {
    hull() { for (x=[-(l/2-r),(l/2-r)]) for (y=[-(w/2-r),(w/2-r)]) translate([x,y,0]) circle(r=r); }
}
module oval_profile(l, w) { scale([l/2, w/2, 1]) circle(r=1, $fn=$fn); }

module housing_shell() {
    difference() {
        linear_extrude(height=housing_depth) rounded_rect_centered(housing_outer_l, housing_outer_w, housing_corner_r);
        translate([0,0,housing_wall]) linear_extrude(height=housing_depth)
            oval_profile(pad_length+2*foam_layer_thickness+4, pad_width+2*foam_layer_thickness+4);
    }
}
module retention_rim() {
    rim_height = 2.5; rim_lip = 1.5;
    translate([0,0,housing_depth-rim_height])
        difference() {
            linear_extrude(height=rim_height) oval_profile(pad_length+2*foam_layer_thickness+4, pad_width+2*foam_layer_thickness+4);
            translate([0,0,-0.1]) linear_extrude(height=rim_height+0.2)
                oval_profile(pad_length+2*foam_layer_thickness+4-2*rim_lip, pad_width+2*foam_layer_thickness+4-2*rim_lip);
        }
}
module strap_slots() {
    for (side=[-1,1])
        translate([side*(housing_outer_l/2-strap_slot_inset-6), 0, housing_depth/2-strap_slot_h/2])
            cube([12, strap_slot_w, strap_slot_h], center=true);
}
module pneumatic_channel() {
    translate([housing_outer_l*0.28, housing_outer_w/2-2, housing_depth/2])
        rotate([90,0,0]) cylinder(h=foam_layer_thickness+housing_wall+tube_strain_relief_length, d=tube_channel_d);
}
module strain_relief_funnel() {
    translate([housing_outer_l*0.28, housing_outer_w/2+tube_strain_relief_length-4, housing_depth/2])
        rotate([90,0,0]) cylinder(h=4, d1=tube_channel_d, d2=tube_channel_d+4);
}
module alignment_marks() {
    for (side=[-1,1])
        translate([side*(pad_length/2+foam_layer_thickness+2), 0, housing_depth-0.5])
            cube([alignment_mark_w, alignment_mark_length, 1], center=true);
}
module bladder_housing() {
    difference() {
        union() { housing_shell(); retention_rim(); }
        strap_slots(); pneumatic_channel(); alignment_marks();
    }
    strain_relief_funnel();
}
bladder_housing();
