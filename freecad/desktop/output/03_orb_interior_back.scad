union() {
	color(alpha = 0.9, c = "WhiteSmoke") {
		translate(v = [0, 0, 102.0]) {
			rotate(a = [10.0, 0, 0]) {
				union() {
					intersection() {
						difference() {
							difference() {
								difference() {
									scale(v = [1.0, 0.7857142857142857, 0.39285714285714285]) {
										sphere($fn = 64, r = 70.0);
									}
									scale(v = [0.9642857142857143, 0.75, 0.35714285714285715]) {
										sphere($fn = 64, r = 70.0);
									}
								}
								translate(v = [0, -297.0, 0]) {
									cube(center = true, size = [1000.0, 500.0, 1000.0]);
								}
							}
							translate(v = [0, -51.0, 0]) {
								rotate(a = [90, 0, 0]) {
									cylinder($fn = 48, center = false, d = 20.4, h = 12.1);
								}
							}
						}
						translate(v = [0, 0, -250.0]) {
							cube(center = true, size = [1000.0, 1000.0, 500.0]);
						}
					}
					translate(v = [0, 0, -25.0]) {
						union() {
							translate(v = [51.0, 28.5, 0]) {
								difference() {
									cylinder($fn = 32, center = false, d = 5.0, h = 4.0);
									translate(v = [0, 0, -0.1]) {
										cylinder($fn = 24, center = false, d = 2.2, h = 4.2);
									}
								}
							}
							translate(v = [-51.0, 28.5, 0]) {
								difference() {
									cylinder($fn = 32, center = false, d = 5.0, h = 4.0);
									translate(v = [0, 0, -0.1]) {
										cylinder($fn = 24, center = false, d = 2.2, h = 4.2);
									}
								}
							}
							translate(v = [51.0, -28.5, 0]) {
								difference() {
									cylinder($fn = 32, center = false, d = 5.0, h = 4.0);
									translate(v = [0, 0, -0.1]) {
										cylinder($fn = 24, center = false, d = 2.2, h = 4.2);
									}
								}
							}
							translate(v = [-51.0, -28.5, 0]) {
								difference() {
									cylinder($fn = 32, center = false, d = 5.0, h = 4.0);
									translate(v = [0, 0, -0.1]) {
										cylinder($fn = 24, center = false, d = 2.2, h = 4.2);
									}
								}
							}
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
