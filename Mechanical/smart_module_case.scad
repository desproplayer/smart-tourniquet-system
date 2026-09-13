// ============================================================
// Smart Tourniquet System — Casing Smart Module
// Kelompok 19 — Desain Proyek Teknik Elektro, Komputer, Biomedik 2
//
// Dimensi luar: 120mm x 70mm x 35mm, ABS + gasket silikon IP54
// Cara pakai: buka di OpenSCAD (openscad.org), F6 untuk render,
// File > Export > Export as STL untuk cetak 3D.
// ============================================================

$fn = 64;

case_l = 120; case_w = 70; case_h = 35;
wall_t = 2.5; corner_r = 4;

gasket_groove_w = 3.2; gasket_groove_d = 1.5; gasket_groove_offset = 3;
screw_d = 2.2; screw_head_d = 4.2; screw_inset = 6;

oled_w = 30; oled_h = 14;
oled_pos = [case_l*0.28, case_w/2, 0];
btn_d = 8;
btn_pwr_pos  = [case_l*0.62, case_w*0.30, 0];
btn_start_pos= [case_l*0.62, case_w*0.70, 0];
led_d = 5; led_spacing = 10;
led_center_pos = [case_l*0.80, case_w/2, 0];
usb_c_w = 9.5; usb_c_h = 3.5;
usb_c_pos = [0, case_w*0.30, case_h*0.35];
pneumatic_port_d = 6;
pneumatic_port_pos = [0, case_w*0.70, case_h*0.35];
buzzer_d = 8;
buzzer_pos = [case_l*0.15, case_w/2, 0];

module rounded_rect(l, w, r) {
    hull() { for (x=[r,l-r]) for (y=[r,w-r]) translate([x,y,0]) circle(r=r); }
}
module case_shell_outer() { linear_extrude(height=case_h) rounded_rect(case_l, case_w, corner_r); }
module case_shell_inner() {
    translate([wall_t, wall_t, wall_t])
        linear_extrude(height=case_h)
            rounded_rect(case_l-2*wall_t, case_w-2*wall_t, max(corner_r-wall_t, 0.5));
}
module gasket_groove() {
    translate([gasket_groove_offset, gasket_groove_offset, case_h-gasket_groove_d])
        linear_extrude(height=gasket_groove_d+0.1)
            difference() {
                rounded_rect(case_l-2*gasket_groove_offset, case_w-2*gasket_groove_offset, corner_r);
                offset(delta=-gasket_groove_w)
                    rounded_rect(case_l-2*gasket_groove_offset, case_w-2*gasket_groove_offset, corner_r);
            }
}
module screw_hole(pos) {
    translate(pos) union() {
        cylinder(h=case_h+2, d=screw_d);
        translate([0,0,case_h-3]) cylinder(h=3.1, d1=screw_d, d2=screw_head_d);
    }
}
module screw_holes_all_corners() {
    for (p = [[screw_inset,screw_inset,-1],[case_l-screw_inset,screw_inset,-1],
              [screw_inset,case_w-screw_inset,-1],[case_l-screw_inset,case_w-screw_inset,-1]])
        screw_hole(p);
}
module top_panel_cutouts() {
    translate([oled_pos[0]-oled_w/2, oled_pos[1]-oled_h/2, case_h-wall_t-0.1])
        cube([oled_w, oled_h, wall_t+0.2]);
    translate([btn_pwr_pos[0], btn_pwr_pos[1], case_h-wall_t-0.1]) cylinder(h=wall_t+0.2, d=btn_d);
    translate([btn_start_pos[0], btn_start_pos[1], case_h-wall_t-0.1]) cylinder(h=wall_t+0.2, d=btn_d);
    for (i=[-1,0,1])
        translate([led_center_pos[0]+i*led_spacing, led_center_pos[1], case_h-wall_t-0.1])
            cylinder(h=wall_t+0.2, d=led_d);
    translate([buzzer_pos[0], buzzer_pos[1], case_h-wall_t-0.1]) cylinder(h=wall_t+0.2, d=buzzer_d);
}
module side_panel_cutouts() {
    translate([-0.1, usb_c_pos[1]-usb_c_w/2, usb_c_pos[2]-usb_c_h/2]) cube([wall_t+0.2, usb_c_w, usb_c_h]);
    translate([-0.1, pneumatic_port_pos[1], pneumatic_port_pos[2]])
        rotate([0,90,0]) cylinder(h=wall_t+0.2, d=pneumatic_port_d);
}
module smart_module_case() {
    difference() {
        case_shell_outer();
        case_shell_inner();
        gasket_groove();
        screw_holes_all_corners();
        top_panel_cutouts();
        side_panel_cutouts();
    }
}
smart_module_case();
