union() {
	color(alpha = 0.95, c = "Ivory") {
		translate(v = [0, 0, 102.0]) {
			rotate(a = [10.0, 0, 0]) {
				intersection() {
					difference() {
						difference() {
							scale(v = [1.0, 0.7857142857142857, 0.39285714285714285]) {
								sphere($fn = 64, r = 70.0);
							}
							scale(v = [0.9642857142857143, 0.75, 0.35714285714285715]) {
								sphere($fn = 64, r = 70.0);
							}
						}
						translate(v = [0, 0, 26.25]) {
							cube(center = true, size = [109.4, 66.2, 10.2]);
						}
					}
					translate(v = [0, 0, 250.0]) {
						cube(center = true, size = [1000.0, 1000.0, 500.0]);
					}
				}
			}
		}
	}
	translate(v = [0, 0, 102.0]) {
		rotate(a = [10.0, 0, 0]) {
			translate(v = [0, 0, 22.75]) {
				union() {
					color(alpha = 0.5, c = "DodgerBlue") {
						cube(center = true, size = [120.0, 75.0, 4.5]);
					}
					color(alpha = 0.7, c = "LightBlue") {
						translate(v = [0, 0, 1.75]) {
							cube(center = true, size = [108.0, 64.8, 1.0]);
						}
					}
				}
			}
		}
	}
	translate(v = [0, 0, 102.0]) {
		rotate(a = [10.0, 0, 0]) {
			translate(v = [0, 0, 18.7]) {
				union() {
					color(alpha = 0.5, c = "ForestGreen") {
						cube(center = true, size = [110.0, 65.0, 1.6]);
					}
					color(alpha = 0.6, c = "DarkGreen") {
						translate(v = [30.0, 17.5, 2.0]) {
							cube(center = true, size = [15.4, 20.5, 2.4]);
						}
					}
				}
			}
		}
	}
}
