// Voice Coil Actuator - 3D Printable Parts
// For Guitar Hero Bot Project
// 
// Designed to fit around Cherry MX compatible keyboard switches
// 
// Usage:
//   1. Open in OpenSCAD
//   2. Enable Customizer: View → Customizer (or View → Hide Customizer to toggle)
//   3. Select part from dropdown in "Part Selection" section
//   4. Render (F6) and Export as STL (File → Export → Export as STL)
//
// Alternative (without Customizer):
//   - Scroll to bottom of file
//   - Change PART = "assembly" to PART = "bobbin" (or housing, wire_guide, print_plate)
//
// Command line export:
//   openscad -o bobbin.stl -D 'PART="bobbin"' voice-coil-parts.scad
//   openscad -o housing.stl -D 'PART="housing"' voice-coil-parts.scad
//   openscad -o wire_guide.stl -D 'PART="wire_guide"' voice-coil-parts.scad
//
// Print settings:
//   - Layer height: 0.15-0.2mm
//   - Infill: 30-50%
//   - Material: PLA or PETG
//   - Supports: Yes for housing (magnet pockets)

// ============================================================
// CONFIGURATION PARAMETERS
// ============================================================

// Switch dimensions (Cherry MX compatible)
switch_width = 15.6;        // Switch body width
switch_depth = 15.6;        // Switch body depth  
stem_diameter = 4.0;        // MX stem cross diameter
stem_socket_dia = 4.3;      // Socket with clearance

// Magnet dimensions (N42 10x5x3mm blocks)
magnet_length = 10.0;
magnet_width = 5.0;
magnet_height = 3.0;
magnet_clearance = 0.2;     // Extra space for fit

// Steel yoke dimensions
yoke_thickness = 2.0;
yoke_width = 10.0;

// Coil dimensions
coil_wire_dia = 0.29;       // 30 AWG with enamel
coil_turns = 60;
coil_layers = 2;
coil_width = 3.0;           // Fits in magnet gap
coil_height = coil_layers * coil_wire_dia * (coil_turns/coil_layers/30);

// Air gap
air_gap = 3.5;              // Space between magnets

// General tolerances
print_tolerance = 0.2;      // General clearance for 3D printing
tight_tolerance = 0.1;      // For press fits

// Derived dimensions
housing_inner = switch_width + 2 * print_tolerance;
housing_wall = 2.0;
housing_outer = housing_inner + 2 * (magnet_width + housing_wall + air_gap/2);

// ============================================================
// HELPER MODULES
// ============================================================

// Rounded rectangle
module rounded_rect(w, d, h, r) {
    hull() {
        translate([r, r, 0]) cylinder(r=r, h=h, $fn=32);
        translate([w-r, r, 0]) cylinder(r=r, h=h, $fn=32);
        translate([r, d-r, 0]) cylinder(r=r, h=h, $fn=32);
        translate([w-r, d-r, 0]) cylinder(r=r, h=h, $fn=32);
    }
}

// Cherry MX stem cross shape (for socket)
module mx_stem_cross(height, clearance=0) {
    stem_long = 4.0 + clearance;
    stem_short = 1.2 + clearance;
    
    union() {
        // Vertical bar
        translate([-stem_short/2, -stem_long/2, 0])
            cube([stem_short, stem_long, height]);
        // Horizontal bar
        translate([-stem_long/2, -stem_short/2, 0])
            cube([stem_long, stem_short, height]);
    }
}

// ============================================================
// BOBBIN (Moving Part)
// ============================================================
// 
// The bobbin holds the voice coil and attaches to the switch stem.
// It moves up and down within the magnetic gap.
//
//              ┌─────────────────┐
//              │   Top surface   │
//              │  ┌───────────┐  │
//              │  │ Stem      │  │
//              │  │ socket    │  │
//              │  │    +      │  │
//              │  └───────────┘  │
//              │  ┌───────────┐  │
//              │  │░░░░░░░░░░░│  │ ← Coil groove
//              │  │░░░░░░░░░░░│  │
//              │  └───────────┘  │
//              └─────────────────┘

module bobbin() {
    bobbin_outer = 18;          // Outer dimension
    bobbin_inner = 10;          // Inner opening (clears switch top)
    bobbin_height = 8;          // Total height
    coil_groove_depth = 2.5;    // Depth of groove for wire
    coil_groove_width = coil_width + 0.5;  // Width with clearance
    stem_socket_depth = 4.5;    // How deep stem inserts
    wall = (bobbin_outer - bobbin_inner) / 2;
    
    difference() {
        // Main body
        translate([-bobbin_outer/2, -bobbin_outer/2, 0])
            rounded_rect(bobbin_outer, bobbin_outer, bobbin_height, 2);
        
        // Inner opening (switch clearance)
        translate([-bobbin_inner/2, -bobbin_inner/2, -1])
            cube([bobbin_inner, bobbin_inner, bobbin_height + 2]);
        
        // Coil groove (rectangular channel around perimeter)
        coil_groove_inner = bobbin_inner + 0.5;
        coil_groove_outer = bobbin_outer - 1.0;
        
        difference() {
            translate([-(coil_groove_outer)/2, -(coil_groove_outer)/2, bobbin_height - coil_groove_depth])
                cube([coil_groove_outer, coil_groove_outer, coil_groove_depth + 1]);
            
            translate([-(coil_groove_inner)/2, -(coil_groove_inner)/2, bobbin_height - coil_groove_depth - 1])
                cube([coil_groove_inner, coil_groove_inner, coil_groove_depth + 3]);
        }
        
        // Stem socket (MX cross shape)
        translate([0, 0, -1])
            mx_stem_cross(stem_socket_depth + 1, 0.3);
        
        // Wire exit slots (two opposite sides)
        wire_slot_width = 2;
        wire_slot_depth = 3;
        translate([-bobbin_outer/2 - 1, -wire_slot_width/2, bobbin_height - wire_slot_depth])
            cube([wall + 2, wire_slot_width, wire_slot_depth + 1]);
        translate([bobbin_outer/2 - wall - 1, -wire_slot_width/2, bobbin_height - wire_slot_depth])
            cube([wall + 2, wire_slot_width, wire_slot_depth + 1]);
    }
    
    // Add stem alignment guides
    guide_height = 2;
    guide_width = 1.5;
    for (angle = [0, 90, 180, 270]) {
        rotate([0, 0, angle])
            translate([stem_socket_dia/2 + 0.5, -guide_width/2, 0])
                cube([1, guide_width, guide_height]);
    }
}

// ============================================================
// HOUSING (Fixed Part)
// ============================================================
//
// The housing holds the magnets and steel yokes, and mounts
// around the keyboard switch.
//
//    ┌─────────────────────────────┐
//    │ ┌───┐               ┌───┐   │
//    │ │Mag│    ┌─────┐    │Mag│   │ ← Magnet pockets
//    │ │   │    │     │    │   │   │
//    │ └───┘    │     │    └───┘   │
//    │          │Swtch│            │ ← Switch opening
//    │ ┌───┐    │     │    ┌───┐   │
//    │ │Mag│    │     │    │Mag│   │
//    │ │   │    └─────┘    │   │   │
//    │ └───┘               └───┘   │
//    │ ════════════════════════    │ ← Yoke slot
//    └─────────────────────────────┘

module housing() {
    housing_width = 24;
    housing_depth = 24;
    housing_height = 12;
    
    switch_opening = switch_width + 0.5;  // Clearance around switch
    
    // Magnet pocket dimensions (with clearance)
    mag_pocket_l = magnet_length + magnet_clearance;
    mag_pocket_w = magnet_width + magnet_clearance;
    mag_pocket_h = magnet_height + magnet_clearance;
    
    // Yoke slot dimensions
    yoke_slot_w = housing_width - 2;
    yoke_slot_d = yoke_width + 0.3;
    yoke_slot_h = yoke_thickness + 0.2;
    
    // Gap position (where coil moves)
    gap_width = air_gap;
    
    difference() {
        // Main body
        translate([-housing_width/2, -housing_depth/2, 0])
            rounded_rect(housing_width, housing_depth, housing_height, 3);
        
        // Switch opening (through hole)
        translate([-switch_opening/2, -switch_opening/2, -1])
            cube([switch_opening, switch_opening, housing_height + 2]);
        
        // Bobbin travel channel (wider than switch opening, for coil clearance)
        bobbin_channel = 19;  // Slightly larger than bobbin
        translate([-bobbin_channel/2, -bobbin_channel/2, housing_height - 6])
            cube([bobbin_channel, bobbin_channel, 7]);
        
        // Magnet pockets (4 magnets, 2 on each side of gap)
        // Left side magnets
        translate([-(switch_opening/2 + gap_width/2 + mag_pocket_w), -mag_pocket_l/2, housing_height - mag_pocket_h - 2])
            cube([mag_pocket_w, mag_pocket_l, mag_pocket_h + 0.5]);
        
        translate([-(switch_opening/2 + gap_width/2 + mag_pocket_w), -mag_pocket_l/2, 2])
            cube([mag_pocket_w, mag_pocket_l, mag_pocket_h + 0.5]);
        
        // Right side magnets
        translate([(switch_opening/2 + gap_width/2), -mag_pocket_l/2, housing_height - mag_pocket_h - 2])
            cube([mag_pocket_w, mag_pocket_l, mag_pocket_h + 0.5]);
            
        translate([(switch_opening/2 + gap_width/2), -mag_pocket_l/2, 2])
            cube([mag_pocket_w, mag_pocket_l, mag_pocket_h + 0.5]);
        
        // Bottom yoke slot
        translate([-yoke_slot_w/2, -yoke_slot_d/2, -0.1])
            cube([yoke_slot_w, yoke_slot_d, yoke_slot_h + 0.1]);
        
        // Top yoke slots (two pieces, on each side)
        translate([-(housing_width/2 - 1), -yoke_slot_d/2, housing_height - yoke_slot_h])
            cube([(housing_width - switch_opening)/2 - gap_width/2, yoke_slot_d, yoke_slot_h + 0.1]);
            
        translate([switch_opening/2 + gap_width/2 + 1, -yoke_slot_d/2, housing_height - yoke_slot_h])
            cube([(housing_width - switch_opening)/2 - gap_width/2, yoke_slot_d, yoke_slot_h + 0.1]);
        
        // Wire channel (groove for coil wires to exit)
        wire_channel_w = 3;
        wire_channel_h = 2;
        translate([-housing_width/2 - 1, -wire_channel_w/2, housing_height - 4])
            cube([housing_width + 2, wire_channel_w, wire_channel_h]);
    }
    
    // Add mounting tabs with screw holes
    tab_width = 6;
    tab_length = 4;
    tab_height = 2;
    screw_hole_dia = 2.5;  // For M2.5 screws
    
    for (angle = [0, 180]) {
        rotate([0, 0, angle])
            translate([housing_width/2, -tab_width/2, 0]) {
                difference() {
                    cube([tab_length, tab_width, tab_height]);
                    translate([tab_length/2, tab_width/2, -1])
                        cylinder(d=screw_hole_dia, h=tab_height + 2, $fn=24);
                }
            }
    }
}

// ============================================================
// WIRE GUIDE / STRAIN RELIEF
// ============================================================

module wire_guide() {
    guide_width = 8;
    guide_depth = 5;
    guide_height = 4;
    wire_hole_dia = 1.5;
    
    difference() {
        // Body
        translate([-guide_width/2, 0, 0])
            cube([guide_width, guide_depth, guide_height]);
        
        // Wire holes
        translate([-2, -1, guide_height/2])
            rotate([-90, 0, 0])
                cylinder(d=wire_hole_dia, h=guide_depth + 2, $fn=16);
        
        translate([2, -1, guide_height/2])
            rotate([-90, 0, 0])
                cylinder(d=wire_hole_dia, h=guide_depth + 2, $fn=16);
        
        // Clip slot (to attach to housing)
        translate([-guide_width/2 - 1, guide_depth - 1.5, -1])
            cube([guide_width + 2, 1.5, 2.5]);
    }
}

// ============================================================
// ASSEMBLY VISUALIZATION
// ============================================================

module assembly() {
    // Housing (transparent for visualization)
    color("gray", 0.5)
        housing();
    
    // Bobbin (in up position)
    color("blue", 0.8)
        translate([0, 0, 8])
            bobbin();
    
    // Magnets (red = north up, blue = south up for visualization)
    // Left side
    color("red")
        translate([-(switch_width/2 + air_gap/2 + magnet_width/2), 0, 12 - magnet_height - 2])
            translate([-magnet_width/2, -magnet_length/2, 0])
                cube([magnet_width, magnet_length, magnet_height]);
    
    color("blue")
        translate([-(switch_width/2 + air_gap/2 + magnet_width/2), 0, 2])
            translate([-magnet_width/2, -magnet_length/2, 0])
                cube([magnet_width, magnet_length, magnet_height]);
    
    // Right side
    color("red")
        translate([(switch_width/2 + air_gap/2 + magnet_width/2), 0, 12 - magnet_height - 2])
            translate([-magnet_width/2, -magnet_length/2, 0])
                cube([magnet_width, magnet_length, magnet_height]);
                
    color("blue")
        translate([(switch_width/2 + air_gap/2 + magnet_width/2), 0, 2])
            translate([-magnet_width/2, -magnet_length/2, 0])
                cube([magnet_width, magnet_length, magnet_height]);
    
    // Steel yokes
    color("silver")
        translate([-11, -yoke_width/2, 0])
            cube([22, yoke_width, yoke_thickness]);
    
    // Wire guide
    color("green")
        translate([0, -15, 8])
            wire_guide();
}

// ============================================================
// RENDER SELECTION
// ============================================================
// 
// Change PART to select what to render:
//   "assembly"    - All parts together (visualization only)
//   "bobbin"      - Moving part with coil groove
//   "housing"     - Fixed part with magnet pockets
//   "wire_guide"  - Strain relief clip
//   "print_plate" - All printable parts arranged for printing
//
// In OpenSCAD: Use the Customizer panel (View → Customizer)
// Or change the value below directly:

/* [Part Selection] */
PART = "housing"; // ["assembly", "bobbin", "housing", "wire_guide", "print_plate"]

// Render the selected part
if (PART == "assembly") {
    assembly();
} else if (PART == "bobbin") {
    bobbin();
} else if (PART == "housing") {
    housing();
} else if (PART == "wire_guide") {
    wire_guide();
} else if (PART == "print_plate") {
    // All printable parts laid out for printing
    translate([-20, 0, 0]) bobbin();
    translate([20, 0, 0]) housing();
    translate([0, 25, 0]) wire_guide();
}
