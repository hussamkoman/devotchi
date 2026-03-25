union() {
	difference() {
		difference() {
			intersection() {
				hull() {
					translate(v = [-61.0, -38.5, 0]) {
						cylinder($fn = 48, center = true, h = 14.0, r = 4.0);
					}
					translate(v = [-61.0, 38.5, 0]) {
						cylinder($fn = 48, center = true, h = 14.0, r = 4.0);
					}
					translate(v = [61.0, -38.5, 0]) {
						cylinder($fn = 48, center = true, h = 14.0, r = 4.0);
					}
					translate(v = [61.0, 38.5, 0]) {
						cylinder($fn = 48, center = true, h = 14.0, r = 4.0);
					}
				}
				translate(v = [0, 0, 2.75]) {
					cube(center = true, size = [132.0, 87.0, 8.5]);
				}
			}
			translate(v = [0, 0, -2.3]) {
				difference() {
					hull() {
						translate(v = [-61.099999999999994, -38.6, 0]) {
							cylinder($fn = 48, center = true, h = 1.6, r = 3.2);
						}
						translate(v = [-61.099999999999994, 38.6, 0]) {
							cylinder($fn = 48, center = true, h = 1.6, r = 3.2);
						}
						translate(v = [61.099999999999994, -38.6, 0]) {
							cylinder($fn = 48, center = true, h = 1.6, r = 3.2);
						}
						translate(v = [61.099999999999994, 38.6, 0]) {
							cylinder($fn = 48, center = true, h = 1.6, r = 3.2);
						}
					}
					hull() {
						translate(v = [-60.9, -38.4, 0]) {
							cylinder($fn = 48, center = true, h = 1.8, r = 0.7999999999999998);
						}
						translate(v = [-60.9, 38.4, 0]) {
							cylinder($fn = 48, center = true, h = 1.8, r = 0.7999999999999998);
						}
						translate(v = [60.9, -38.4, 0]) {
							cylinder($fn = 48, center = true, h = 1.8, r = 0.7999999999999998);
						}
						translate(v = [60.9, 38.4, 0]) {
							cylinder($fn = 48, center = true, h = 1.8, r = 0.7999999999999998);
						}
					}
				}
			}
		}
		cube(center = true, size = [126.0, 81.0, 10.0]);
	}
	union() {
		translate(v = [-51.0, -28.5, 5.0]) {
			rotate(a = [180, 0, 0]) {
				difference() {
					cylinder($fn = 32, center = false, d = 5.0, h = 2.5);
					translate(v = [0, 0, -0.1]) {
						cylinder($fn = 24, center = false, d = 2.2, h = 2.7);
					}
				}
			}
		}
		translate(v = [51.0, -28.5, 5.0]) {
			rotate(a = [180, 0, 0]) {
				difference() {
					cylinder($fn = 32, center = false, d = 5.0, h = 2.5);
					translate(v = [0, 0, -0.1]) {
						cylinder($fn = 24, center = false, d = 2.2, h = 2.7);
					}
				}
			}
		}
		translate(v = [-51.0, 28.5, 5.0]) {
			rotate(a = [180, 0, 0]) {
				difference() {
					cylinder($fn = 32, center = false, d = 5.0, h = 2.5);
					translate(v = [0, 0, -0.1]) {
						cylinder($fn = 24, center = false, d = 2.2, h = 2.7);
					}
				}
			}
		}
		translate(v = [51.0, 28.5, 5.0]) {
			rotate(a = [180, 0, 0]) {
				difference() {
					cylinder($fn = 32, center = false, d = 5.0, h = 2.5);
					translate(v = [0, 0, -0.1]) {
						cylinder($fn = 24, center = false, d = 2.2, h = 2.7);
					}
				}
			}
		}
	}
}
