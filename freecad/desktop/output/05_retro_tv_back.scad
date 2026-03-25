color(alpha = 0.9, c = "WhiteSmoke") {
	difference() {
		difference() {
			difference() {
				difference() {
					difference() {
						difference() {
							difference() {
								difference() {
									difference() {
										translate(v = [0, 0, 29.375]) {
											hull() {
												translate(v = [-72.8, -47.8, 0]) {
													cylinder($fn = 48, center = true, h = 6.25, r = 9.5);
												}
												translate(v = [-72.8, 47.8, 0]) {
													cylinder($fn = 48, center = true, h = 6.25, r = 9.5);
												}
												translate(v = [72.8, -47.8, 0]) {
													cylinder($fn = 48, center = true, h = 6.25, r = 9.5);
												}
												translate(v = [72.8, 47.8, 0]) {
													cylinder($fn = 48, center = true, h = 6.25, r = 9.5);
												}
											}
										}
										translate(v = [0, -45.5, 29.375]) {
											linear_extrude(center = true, height = 6.65) {
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
									translate(v = [0, 39.5, 29.375]) {
										cube(center = true, size = [30.0, 2.5, 6.65]);
									}
								}
								translate(v = [0, 34.0, 29.375]) {
									cube(center = true, size = [30.0, 2.5, 6.65]);
								}
							}
							translate(v = [0, 28.5, 29.375]) {
								cube(center = true, size = [30.0, 2.5, 6.65]);
							}
						}
						translate(v = [0, 23.0, 29.375]) {
							cube(center = true, size = [30.0, 2.5, 6.65]);
						}
					}
					translate(v = [-72.0, -47.0, 29.375]) {
						cylinder($fn = 24, center = true, d = 2.5, h = 6.65);
					}
				}
				translate(v = [72.0, -47.0, 29.375]) {
					cylinder($fn = 24, center = true, d = 2.5, h = 6.65);
				}
			}
			translate(v = [-72.0, 47.0, 29.375]) {
				cylinder($fn = 24, center = true, d = 2.5, h = 6.65);
			}
		}
		translate(v = [72.0, 47.0, 29.375]) {
			cylinder($fn = 24, center = true, d = 2.5, h = 6.65);
		}
	}
}
