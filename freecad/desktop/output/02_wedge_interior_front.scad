color(alpha = 0.85, c = "LightSteelBlue") {
	difference() {
		difference() {
			difference() {
				difference() {
					difference() {
						difference() {
							difference() {
								difference() {
									hull() {
										translate(v = [0, -48.0, 5.0]) {
											hull() {
												translate(v = [-75.51, -0.010000000000000009, 0]) {
													cylinder($fn = 48, center = true, h = 10.0, r = 1.99);
												}
												translate(v = [-75.51, 0.010000000000000009, 0]) {
													cylinder($fn = 48, center = true, h = 10.0, r = 1.99);
												}
												translate(v = [75.51, -0.010000000000000009, 0]) {
													cylinder($fn = 48, center = true, h = 10.0, r = 1.99);
												}
												translate(v = [75.51, 0.010000000000000009, 0]) {
													cylinder($fn = 48, center = true, h = 10.0, r = 1.99);
												}
											}
										}
										translate(v = [0, 48.0, 20.0]) {
											hull() {
												translate(v = [-75.51, -0.010000000000000009, 0]) {
													cylinder($fn = 48, center = true, h = 40.0, r = 1.99);
												}
												translate(v = [-75.51, 0.010000000000000009, 0]) {
													cylinder($fn = 48, center = true, h = 40.0, r = 1.99);
												}
												translate(v = [75.51, -0.010000000000000009, 0]) {
													cylinder($fn = 48, center = true, h = 40.0, r = 1.99);
												}
												translate(v = [75.51, 0.010000000000000009, 0]) {
													cylinder($fn = 48, center = true, h = 40.0, r = 1.99);
												}
											}
										}
									}
									hull() {
										translate(v = [0, -47.5, 5.0]) {
											cube(center = true, size = [151.0, 1.0, 6.0]);
										}
										translate(v = [0, 47.5, 20.0]) {
											cube(center = true, size = [151.0, 1.0, 36.0]);
										}
									}
								}
								translate(v = [0, 0.0, 35.0]) {
									rotate(a = [-16.69924423399362, 0, 0]) {
										cube(center = true, size = [109.0, 65.8, 25]);
									}
								}
							}
							translate(v = [0, 0.0, 30.0]) {
								rotate(a = [-16.69924423399362, 0, 0]) {
									cube(center = true, size = [120.4, 75.4, 15.5]);
								}
							}
						}
						translate(v = [-16.0, -50.0, 6.0]) {
							rotate(a = [90, 0, 0]) {
								cylinder($fn = 32, center = true, d = 8.0, h = 3.0);
							}
						}
					}
					translate(v = [0.0, -50.0, 6.0]) {
						rotate(a = [90, 0, 0]) {
							cylinder($fn = 32, center = true, d = 8.0, h = 3.0);
						}
					}
				}
				translate(v = [16.0, -50.0, 6.0]) {
					rotate(a = [90, 0, 0]) {
						cylinder($fn = 32, center = true, d = 8.0, h = 3.0);
					}
				}
			}
			translate(v = [0, 50.0, 20.0]) {
				rotate(a = [90, 0, 0]) {
					linear_extrude(center = true, height = 3.0) {
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
		translate(v = [-65.5, -50.0, 4.0]) {
			rotate(a = [90, 0, 0]) {
				cylinder($fn = 16, center = true, d = 2.0, h = 3.0);
			}
		}
	}
}
