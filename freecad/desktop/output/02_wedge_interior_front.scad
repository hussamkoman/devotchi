color(alpha = 0.9, c = "LightSteelBlue") {
	difference() {
		difference() {
			difference() {
				difference() {
					difference() {
						difference() {
							hull() {
								translate(v = [0, -45.0, 5.0]) {
									hull() {
										translate(v = [-72.51, -0.009999999999999787, 0]) {
											cylinder($fn = 48, center = true, h = 10.0, r = 4.99);
										}
										translate(v = [-72.51, 0.009999999999999787, 0]) {
											cylinder($fn = 48, center = true, h = 10.0, r = 4.99);
										}
										translate(v = [72.51, -0.009999999999999787, 0]) {
											cylinder($fn = 48, center = true, h = 10.0, r = 4.99);
										}
										translate(v = [72.51, 0.009999999999999787, 0]) {
											cylinder($fn = 48, center = true, h = 10.0, r = 4.99);
										}
									}
								}
								translate(v = [0, 30.0, 20.0]) {
									hull() {
										translate(v = [-72.5, -15.0, 0]) {
											cylinder($fn = 48, center = true, h = 40.0, r = 5.0);
										}
										translate(v = [-72.5, 15.0, 0]) {
											cylinder($fn = 48, center = true, h = 40.0, r = 5.0);
										}
										translate(v = [72.5, -15.0, 0]) {
											cylinder($fn = 48, center = true, h = 40.0, r = 5.0);
										}
										translate(v = [72.5, 15.0, 0]) {
											cylinder($fn = 48, center = true, h = 40.0, r = 5.0);
										}
									}
								}
							}
							hull() {
								translate(v = [0, -45.0, 5.0]) {
									cube(center = true, size = [151.0, 6.1, 6.1]);
								}
								translate(v = [0, 30.0, 20.0]) {
									cube(center = true, size = [151.0, 36.1, 36.1]);
								}
							}
						}
						translate(v = [0, 0.0, 25.0]) {
							rotate(a = [-16.69924423399362, 0, 0]) {
								translate(v = [0, 0, -10.0]) {
									cube(center = true, size = [120.4, 75.4, 20.0]);
								}
							}
						}
					}
					translate(v = [0, 0.0, 25.0]) {
						rotate(a = [-16.69924423399362, 0, 0]) {
							translate(v = [0, 0, -7.0]) {
								cube(center = true, size = [109.0, 65.8, 14.0]);
							}
						}
					}
				}
				union() {
					translate(v = [-14.0, -50.0, 5.0]) {
						rotate(a = [90, 0, 0]) {
							cylinder($fn = 32, center = true, d = 8.0, h = 2.4);
						}
					}
					translate(v = [0.0, -50.0, 5.0]) {
						rotate(a = [90, 0, 0]) {
							cylinder($fn = 32, center = true, d = 8.0, h = 2.4);
						}
					}
					translate(v = [14.0, -50.0, 5.0]) {
						rotate(a = [90, 0, 0]) {
							cylinder($fn = 32, center = true, d = 8.0, h = 2.4);
						}
					}
				}
			}
			translate(v = [0, 50.0, 20.0]) {
				rotate(a = [90, 0, 0]) {
					linear_extrude(center = true, height = 2.4) {
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
		translate(v = [0.0, 28.0, 1.9]) {
			cylinder($fn = 48, center = false, d = 32.0, h = 5.2);
		}
	}
}
