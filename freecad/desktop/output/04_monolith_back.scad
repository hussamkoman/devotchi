color(alpha = 0.85, c = "DarkGray") {
	difference() {
		difference() {
			translate(v = [0, 0, 4.35]) {
				hull() {
					translate(v = [-39.0, -64.5, 0]) {
						cylinder($fn = 48, center = true, h = 6.3, r = 3.0);
					}
					translate(v = [-39.0, 64.5, 0]) {
						cylinder($fn = 48, center = true, h = 6.3, r = 3.0);
					}
					translate(v = [39.0, -64.5, 0]) {
						cylinder($fn = 48, center = true, h = 6.3, r = 3.0);
					}
					translate(v = [39.0, 64.5, 0]) {
						cylinder($fn = 48, center = true, h = 6.3, r = 3.0);
					}
				}
			}
			translate(v = [0, 0, 3.3500000000000005]) {
				cube(center = true, size = [80.0, 131.0, 4.3999999999999995]);
			}
		}
		translate(v = [0, 0.7999999999999972, 6.35]) {
			cube(center = true, size = [50.6, 120.6, 2.4]);
		}
	}
}
