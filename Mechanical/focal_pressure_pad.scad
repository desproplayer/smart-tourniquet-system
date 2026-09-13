// ============================================================
// Smart Tourniquet System — Focal Pressure Pad (FPP)
// Kelompok 19 — Desain Proyek Teknik Elektro, Komputer, Biomedik 2
//
// Oval 60x40mm, ketebalan maks 15mm, kantung TPU + silikon medis A50.
// render_mode: "inflated" (visual+FEM) / "mold_bottom" / "mold_top"
// ============================================================

$fn = 96;

pad_length = 60; pad_width = 40; pad_max_thickness = 15;
connector_d = 4; connector_od = 7; connector_length = 8;

module oval_profile(l, w) { scale([l/2, w/2, 1]) circle(r=1, $fn=$fn); }

module inflated_pad() {
    union() {
        hull() {
            translate([0,0,0.1]) linear_extrude(height=0.1) oval_profile(pad_length*0.94, pad_width*0.94);
            translate([0,0,pad_max_thickness/2]) linear_extrude(height=0.1) oval_profile(pad_length, pad_width);
            translate([0,0,pad_max_thickness-0.1]) linear_extrude(height=0.1) oval_profile(pad_length*0.94, pad_width*0.94);
        }
        translate([pad_length/2-4, 0, pad_max_thickness/2])
            rotate([0,90,0]) cylinder(h=connector_length, d=connector_od);
    }
}
module inflated_pad_with_channel() {
    difference() {
        inflated_pad();
        translate([pad_length/2-4+connector_length-2, 0, pad_max_thickness/2])
            rotate([0,90,0]) cylinder(h=connector_length+4, d=connector_d);
    }
}
module mold_cavity_bottom() {
    block_margin = 6;
    block_l = pad_length + 2*block_margin;
    block_w = pad_width + 2*block_margin;
    block_h = pad_max_thickness/2 + block_margin;
    difference() {
        translate([-block_l/2, -block_w/2, 0]) cube([block_l, block_w, block_h]);
        translate([0,0,block_h]) scale([1,1,-1])
            hull() {
                linear_extrude(height=0.1) oval_profile(pad_length*0.94, pad_width*0.94);
                translate([0,0,pad_max_thickness/2-0.1]) linear_extrude(height=0.1) oval_profile(pad_length, pad_width);
            }
        translate([pad_length/2+block_margin/2, 0, -0.1]) cylinder(h=5, d=3);
        translate([-pad_length/2-block_margin/2, 0, -0.1]) cylinder(h=5, d=3);
    }
}
module mold_cavity_top() {
    mirror([0,0,1]) translate([0,0,-((pad_max_thickness/2)+6)]) mold_cavity_bottom();
}

render_mode = "inflated";
if (render_mode == "inflated") { inflated_pad_with_channel(); }
else if (render_mode == "mold_bottom") { mold_cavity_bottom(); }
else if (render_mode == "mold_top") { mold_cavity_top(); }
