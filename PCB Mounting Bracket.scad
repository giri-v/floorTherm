pL = 94; //pcb Length
pW = 63.7; //pcb Width

bL = pL + 2; // bracket Length
bW = pW +2.3; //bracket Width
bH = 80; //bracket Height
bT = 6; //bracket Thickness

hDX = 83.7; //hole Distance X
hDY = 53.3; //hole Distance Y
hD = 3; //hole Diameter for M4 standoff
lHDX = 74; //lower Hole Distance X
lHDY = 46; //lower Hole Distance Y

difference (){cube([bL, bW, bH], center = true);
translate([0,0,0]) cube([bL - 2*bT, bW, bH - 2*bT], center = true);
    translate([hDX/2, hDY/2, (bH-bT)/2]) cylinder(h=bT, r=hD/2, center = true);
    translate([hDX/2, -hDY/2, (bH-bT)/2]) cylinder(h=bT, r=hD/2, center = true);
    translate([-hDX/2, hDY/2, (bH-bT)/2]) cylinder(h=bT, r=hD/2, center = true);
    translate([-hDX/2, -hDY/2, (bH-bT)/2]) cylinder(h=bT, r=hD/2, center = true);

    translate([lHDX/2, lHDY/2, -(bH-bT)/2]) cylinder(h=bT, r=hD/2, center = true);
    translate([lHDX/2, -lHDY/2, -(bH-bT)/2]) cylinder(h=bT, r=hD/2, center = true);
    translate([-lHDX/2, lHDY/2, -(bH-bT)/2]) cylinder(h=bT, r=hD/2, center = true);
    translate([-lHDX/2, -lHDY/2, -(bH-bT)/2]) cylinder(h=bT, r=hD/2, center = true);
}
