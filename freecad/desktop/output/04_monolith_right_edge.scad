union() {
	color(alpha = 0.9, c = "LightGray") {
		difference() {
			difference() {
				difference() {
					difference() {
						difference() {
							hull() {
								translate(v = [-39.0, -64.5, 0]) {
									cylinder($fn = 48, center = true, h = 15.0, r = 3.0);
								}
								translate(v = [-39.0, 64.5, 0]) {
									cylinder($fn = 48, center = true, h = 15.0, r = 3.0);
								}
								translate(v = [39.0, -64.5, 0]) {
									cylinder($fn = 48, center = true, h = 15.0, r = 3.0);
								}
								translate(v = [39.0, 64.5, 0]) {
									cylinder($fn = 48, center = true, h = 15.0, r = 3.0);
								}
							}
							translate(v = [0, 0, -0.9]) {
								cube(center = true, size = [80.0, 131.0, 11.1]);
							}
						}
						translate(v = [0, 0, -3.0]) {
							cube(center = true, size = [75.4, 120.4, 5.1]);
						}
					}
					translate(v = [0, 0, -6.5]) {
						cube(center = true, size = [65.8, 109.0, 2.2]);
					}
				}
				union() {
					translate(v = [42.0, -12.0, 0]) {
						rotate(a = [0, 90, 0]) {
							cylinder($fn = 32, center = true, d = 8.0, h = 2.2);
						}
					}
					translate(v = [42.0, 0.0, 0]) {
						rotate(a = [0, 90, 0]) {
							cylinder($fn = 32, center = true, d = 8.0, h = 2.2);
						}
					}
					translate(v = [42.0, 12.0, 0]) {
						rotate(a = [0, 90, 0]) {
							cylinder($fn = 32, center = true, d = 8.0, h = 2.2);
						}
					}
				}
			}
			translate(v = [0, -67.5, 0]) {
				rotate(a = [90, 0, 0]) {
					linear_extrude(center = true, height = 15.2) {
						hull() {
							translate(v = [-2.84, 0]) {
								circle($fn = 32, r = 2.0300000000000002);
							}
							translate(v = [2.84, 0]) {
								circle($fn = 32, r = 2.0300000000000002);
							}
						}
					}
				}
			}
		}
	}
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
	color(alpha = 0.75, c = "Gray") {
		translate(v = [0, -59.5, 7.5]) {
			rotate(a = [-45, 0, 0]) {
				translate(v = [0, 60.0, 0]) {
					hull() {
						translate(v = [-23.0, -58.0, 0]) {
							cylinder($fn = 32, center = true, h = 2.0, r = 2.0);
						}
						translate(v = [-23.0, 58.0, 0]) {
							cylinder($fn = 32, center = true, h = 2.0, r = 2.0);
						}
						translate(v = [23.0, -58.0, 0]) {
							cylinder($fn = 32, center = true, h = 2.0, r = 2.0);
						}
						translate(v = [23.0, 58.0, 0]) {
							cylinder($fn = 32, center = true, h = 2.0, r = 2.0);
						}
					}
				}
			}
		}
	}
}
