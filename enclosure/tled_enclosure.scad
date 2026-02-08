// TLED Enclosure for DFRobot Beetle ESP32-C6 (DFR1117)
// Super mini friction-fit design with wire slit
//
// Board: 25mm x 20.5mm
// https://wiki.dfrobot.com/SKU_DFR1117_Beetle_ESP32_C6
//
// USB-C port is on the front edge (short side), centered.
// Wire slit on back for power/data wires.
//
// Lid fits on OUTSIDE of base (cap style).
//
// Print settings:
// - Layer height: 0.2mm
// - Infill: 20%
// - No supports needed

// ============== RENDER SELECTION ==============
// Set to "base", "lid", "assembly", "print_layout", or "closed"
RENDER_PART = "assembly";  // [assembly, base, lid, print_layout, closed]

// ============== PARAMETERS ==============

// Board dimensions (DFR1117 Beetle ESP32-C6)
board_w = 20.5;         // [15:0.5:30] Width (short edge - USB side)
board_d = 25.0;         // [20:0.5:35] Depth (long edge)
board_h = 6.0;          // [3:0.5:8] Height with components (reduced 1mm)
board_thick = 1.0;      // [0.8:0.1:1.6] PCB thickness

// Enclosure parameters
wall = 1.5;             // [1.0:0.1:3.0] Wall thickness
base_h = 1.5;           // [1.0:0.5:3.0] Base height (board sits on this)
corner_r = 2;           // [1:0.5:4] Corner radius
tol = 0.2;              // [0.1:0.05:0.4] Fit tolerance

// USB-C cutout (front wall - connector sticks out from PCB edge)
usbc_w = 10.0;          // [8:0.5:14] Width of opening
usbc_h = 4.5;           // [3:0.5:6] Height of opening
usbc_r = 1.5;           // [0.5:0.5:2.5] Corner radius for rounded rectangle

// Wire slit (back side)
slit_w = 7;             // [3:1:10] Wire slit width

// Ventilation holes (improved for ESP32-C6 thermal)
vent_d = 5.0;           // [3:0.5:6] Hole diameter
vent_cols = 2;          // [1:1:3] Columns (along short edge)
vent_rows = 3;          // [2:1:4] Rows (along long edge)

// Lid parameters (cap style - fits on outside)
lid_wall = 1.2;         // [1.0:0.1:2.0] Lid wall thickness
lid_overlap = 2.5;      // [2:0.5:6] How far lid overlaps base walls

// Snap-fit bumps (on base sides, notches in lid)
snap_bump_h = 0.3;      // [0.2:0.1:0.6] Bump height (how far it sticks out)
snap_bump_w = 6;        // [4:1:10] Bump width
snap_bump_z = 0.75;     // [0.5:0.25:2] Distance from top of base wall

// ============== CALCULATED ==============

inner_w = board_w + tol*2;
inner_d = board_d + tol*2;
total_w = inner_w + wall*2;
total_d = inner_d + wall*2;
inner_h = base_h + board_h;

// Lid outer dimensions (wraps around base)
lid_outer_w = total_w + lid_wall*2 + tol*2;
lid_outer_d = total_d + lid_wall*2 + tol*2;

$fn = 32;

// ============== MODULES ==============

module rounded_box(w, d, h, r) {
    hull() {
        translate([r, r, 0]) cylinder(r=r, h=h);
        translate([w-r, r, 0]) cylinder(r=r, h=h);
        translate([r, d-r, 0]) cylinder(r=r, h=h);
        translate([w-r, d-r, 0]) cylinder(r=r, h=h);
    }
}

module rounded_rect(w, h, r) {
    hull() {
        translate([r, r, 0]) circle(r=r);
        translate([w-r, r, 0]) circle(r=r);
        translate([r, h-r, 0]) circle(r=r);
        translate([w-r, h-r, 0]) circle(r=r);
    }
}

// Base - holds the board
module base() {
    difference() {
        // Main shell
        rounded_box(total_w, total_d, inner_h, corner_r);

        // Board pocket (hollows out the inside)
        translate([wall, wall, base_h])
            cube([inner_w, inner_d, board_h + 10]);

        // USB-C cutout (front wall, Y=0 side, aligned to top, rounded corners)
        usbc_x = (total_w - usbc_w) / 2;
        usbc_z = inner_h - usbc_h - 1.0;  // 1.0mm from top edge
        hull() {
            translate([usbc_x + usbc_r, -0.1, usbc_z + usbc_r])
                rotate([-90, 0, 0]) cylinder(r=usbc_r, h=wall+0.2);
            translate([usbc_x + usbc_w - usbc_r, -0.1, usbc_z + usbc_r])
                rotate([-90, 0, 0]) cylinder(r=usbc_r, h=wall+0.2);
            translate([usbc_x + usbc_r, -0.1, usbc_z + usbc_h - usbc_r])
                rotate([-90, 0, 0]) cylinder(r=usbc_r, h=wall+0.2);
            translate([usbc_x + usbc_w - usbc_r, -0.1, usbc_z + usbc_h - usbc_r])
                rotate([-90, 0, 0]) cylinder(r=usbc_r, h=wall+0.2);
        }

        // Wire slit (back, Y=max side)
        translate([(total_w - slit_w)/2, total_d - wall - 0.1, base_h])
            cube([slit_w, wall + 0.2, board_h + 10]);

        // Push-out hole (bottom, centered near wire slit)
        translate([total_w/2, total_d - wall - 5, -0.1])
            cylinder(d = 3, h = base_h + 0.2);

        // Side ventilation slots (left and right walls)
        vent_slot_w = 2;      // Slot width
        vent_slot_h = 4;      // Slot height
        vent_slot_spacing = 6; // Spacing between slots
        vent_slot_z = inner_h - vent_slot_h - 1.5;  // Align with top area

        // Left side vents (X=0)
        for (i = [0 : 2]) {
            translate([-0.1, total_d/2 - vent_slot_spacing + i * vent_slot_spacing - vent_slot_w/2, vent_slot_z])
                cube([wall + 0.2, vent_slot_w, vent_slot_h]);
        }

        // Right side vents (X=max)
        for (i = [0 : 2]) {
            translate([total_w - wall - 0.1, total_d/2 - vent_slot_spacing + i * vent_slot_spacing - vent_slot_w/2, vent_slot_z])
                cube([wall + 0.2, vent_slot_w, vent_slot_h]);
        }
    }

    // Snap-fit bumps on left and right walls only (near top)
    snap_z = inner_h - snap_bump_z;

    // Left side bump (X=0) - half embedded in wall
    hull() {
        translate([0, (total_d - snap_bump_w) / 2, snap_z])
            sphere(r=snap_bump_h, $fn=16);
        translate([0, (total_d + snap_bump_w) / 2, snap_z])
            sphere(r=snap_bump_h, $fn=16);
    }

    // Right side bump (X=max) - half embedded in wall
    hull() {
        translate([total_w, (total_d - snap_bump_w) / 2, snap_z])
            sphere(r=snap_bump_h, $fn=16);
        translate([total_w, (total_d + snap_bump_w) / 2, snap_z])
            sphere(r=snap_bump_h, $fn=16);
    }
}

// Lid - cap style (fits on outside of base)
module lid() {
    lid_top = wall;           // Top thickness
    lid_total_h = lid_top + lid_overlap;

    // Offset to center lid over base
    lid_offset = lid_wall + tol;

    difference() {
        // Outer shell of lid (cap shape)
        translate([-lid_offset, -lid_offset, 0])
            rounded_box(lid_outer_w, lid_outer_d, lid_total_h, corner_r + lid_wall);

        // Hollow out to fit over base walls
        translate([0, 0, lid_top])
            rounded_box(total_w + tol*2, total_d + tol*2, lid_overlap + 0.1, corner_r);

        // USB-C cutout (front wall of lid skirt)
        // Match the base's rounded rectangle shape, moved down 1mm to align
        usbc_lid_x = (total_w - usbc_w) / 2;  // Centered on base width
        translate([usbc_lid_x, -lid_offset - 0.1, lid_top + 1.0])
            hull() {
                translate([usbc_r, 0, usbc_r])
                    rotate([-90, 0, 0]) cylinder(r=usbc_r, h=lid_wall + tol + 0.2);
                translate([usbc_w - usbc_r, 0, usbc_r])
                    rotate([-90, 0, 0]) cylinder(r=usbc_r, h=lid_wall + tol + 0.2);
                translate([usbc_r, 0, usbc_h - usbc_r])
                    rotate([-90, 0, 0]) cylinder(r=usbc_r, h=lid_wall + tol + 0.2);
                translate([usbc_w - usbc_r, 0, usbc_h - usbc_r])
                    rotate([-90, 0, 0]) cylinder(r=usbc_r, h=lid_wall + tol + 0.2);
            }

        // Wire slit cutout (back wall of lid + top)
        slit_x = (total_w - slit_w) / 2;
        // Through the back wall (full overlap height)
        translate([slit_x, total_d + tol - 0.1, lid_top])
            cube([slit_w, lid_wall + tol + 0.2, lid_overlap + 0.2]);
        // Through the top surface for wire access
        translate([slit_x, total_d - wall + tol, -0.1])
            cube([slit_w, lid_wall + wall + tol*2 + 0.2, lid_top + 0.2]);

        // Top vent holes
        vent_margin = 5;
        vent_x_spacing = (inner_w - vent_margin*2) / max(vent_cols - 1, 1);
        vent_y_spacing = (inner_d - vent_margin*2) / max(vent_rows - 1, 1);
        for (x = [0 : vent_cols - 1]) {
            for (y = [0 : vent_rows - 1]) {
                translate([
                    wall + vent_margin + x * vent_x_spacing,
                    wall + vent_margin + y * vent_y_spacing,
                    -0.1
                ])
                cylinder(d = vent_d, h = lid_top + 0.2);
            }
        }

        // Snap-fit grooves on left and right inner walls only
        // Groove near bottom of skirt to catch bump when lid slides on
        snap_groove_h = snap_bump_h * 3;        // Height of the groove
        snap_notch_z = lid_total_h - snap_bump_z - snap_groove_h;  // Near opening of skirt
        snap_groove_depth = snap_bump_h + tol;  // How deep the groove cuts into wall

        // Left side groove (indent on inner wall at X=0)
        // Hollow starts at X=0, so cut from X=-0.1 into the wall
        translate([-0.1, (total_d - snap_bump_w) / 2 - tol, snap_notch_z])
            cube([snap_groove_depth + 0.2, snap_bump_w + tol*2, snap_groove_h]);

        // Right side groove (indent on inner wall at X=max)
        // Hollow ends at X=total_w+tol*2, so cut from there into the wall
        translate([total_w + tol*2 - 0.1, (total_d - snap_bump_w) / 2 - tol, snap_notch_z])
            cube([snap_groove_depth + 0.2, snap_bump_w + tol*2, snap_groove_h]);
    }
}

// ============== VIEWS ==============

module assembly() {
    color("DodgerBlue") base();
    // Lid floats above, positioned to show cap-style fit
    color("LimeGreen") translate([0, 0, inner_h + 5 - lid_overlap]) lid();
}

module print_layout() {
    base();
    // Lid printed upside down, offset to the right
    translate([total_w + 10, lid_wall + tol, wall])
        rotate([180, 0, 0])
        lid();
}

module closed() {
    // Lid sits on top, overlapping the base walls
    // Rotate 180° around X to flip, 180° around Z to fix front/back
    base();
    translate([24, 0, inner_h + wall])
        rotate([180, 0, 180])
        lid();
}

// ============== RENDER ==============

if (RENDER_PART == "base") base();
else if (RENDER_PART == "lid") lid();
else if (RENDER_PART == "print_layout") print_layout();
else if (RENDER_PART == "closed") closed();
else assembly();

// ============== INFO ==============
echo("=== TLED Enclosure ===");
echo(str("Base outer: ", total_w, " x ", total_d, " x ", inner_h, " mm"));
echo(str("Lid outer: ", lid_outer_w, " x ", lid_outer_d, " x ", wall + lid_overlap, " mm"));
echo(str("Inner pocket: ", inner_w, " x ", inner_d, " mm"));
echo(str("Total closed height: ", inner_h, " mm"));
