color(alpha = 0.85, c = "SlateGray") {
	union() {
		difference() {
			difference() {
				difference() {
					difference() {
						difference() {
							translate(v = [0, 0, 1.0]) {
								hull() {
									translate(v = [-72.5, -45.0, 0]) {
										cylinder($fn = 48, center = true, h = 2.0, r = 4.7);
									}
									translate(v = [-72.5, 45.0, 0]) {
										cylinder($fn = 48, center = true, h = 2.0, r = 4.7);
									}
									translate(v = [72.5, -45.0, 0]) {
										cylinder($fn = 48, center = true, h = 2.0, r = 4.7);
									}
									translate(v = [72.5, 45.0, 0]) {
										cylinder($fn = 48, center = true, h = 2.0, r = 4.7);
									}
								}
							}
							translate(v = [0.0, 30.0, 0]) {
								union() {
									translate(v = [-6.0, -10.0, 0]) {
										cylinder($fn = 16, center = true, d = 2.0, h = 20);
									}
									translate(v = [-2.0, -10.0, 0]) {
										cylinder($fn = 16, center = true, d = 2.0, h = 20);
									}
									translate(v = [2.0, -10.0, 0]) {
										cylinder($fn = 16, center = true, d = 2.0, h = 20);
									}
									translate(v = [6.0, -10.0, 0]) {
										cylinder($fn = 16, center = true, d = 2.0, h = 20);
									}
									translate(v = [-10.0, -6.0, 0]) {
										cylinder($fn = 16, center = true, d = 2.0, h = 20);
									}
									translate(v = [-6.0, -6.0, 0]) {
										cylinder($fn = 16, center = true, d = 2.0, h = 20);
									}
									translate(v = [-2.0, -6.0, 0]) {
										cylinder($fn = 16, center = true, d = 2.0, h = 20);
									}
									translate(v = [2.0, -6.0, 0]) {
										cylinder($fn = 16, center = true, d = 2.0, h = 20);
									}
									translate(v = [6.0, -6.0, 0]) {
										cylinder($fn = 16, center = true, d = 2.0, h = 20);
									}
									translate(v = [10.0, -6.0, 0]) {
										cylinder($fn = 16, center = true, d = 2.0, h = 20);
									}
									translate(v = [-10.0, -2.0, 0]) {
										cylinder($fn = 16, center = true, d = 2.0, h = 20);
									}
									translate(v = [-6.0, -2.0, 0]) {
										cylinder($fn = 16, center = true, d = 2.0, h = 20);
									}
									translate(v = [-2.0, -2.0, 0]) {
										cylinder($fn = 16, center = true, d = 2.0, h = 20);
									}
									translate(v = [2.0, -2.0, 0]) {
										cylinder($fn = 16, center = true, d = 2.0, h = 20);
									}
									translate(v = [6.0, -2.0, 0]) {
										cylinder($fn = 16, center = true, d = 2.0, h = 20);
									}
									translate(v = [10.0, -2.0, 0]) {
										cylinder($fn = 16, center = true, d = 2.0, h = 20);
									}
									translate(v = [-10.0, 2.0, 0]) {
										cylinder($fn = 16, center = true, d = 2.0, h = 20);
									}
									translate(v = [-6.0, 2.0, 0]) {
										cylinder($fn = 16, center = true, d = 2.0, h = 20);
									}
									translate(v = [-2.0, 2.0, 0]) {
										cylinder($fn = 16, center = true, d = 2.0, h = 20);
									}
									translate(v = [2.0, 2.0, 0]) {
										cylinder($fn = 16, center = true, d = 2.0, h = 20);
									}
									translate(v = [6.0, 2.0, 0]) {
										cylinder($fn = 16, center = true, d = 2.0, h = 20);
									}
									translate(v = [10.0, 2.0, 0]) {
										cylinder($fn = 16, center = true, d = 2.0, h = 20);
									}
									translate(v = [-10.0, 6.0, 0]) {
										cylinder($fn = 16, center = true, d = 2.0, h = 20);
									}
									translate(v = [-6.0, 6.0, 0]) {
										cylinder($fn = 16, center = true, d = 2.0, h = 20);
									}
									translate(v = [-2.0, 6.0, 0]) {
										cylinder($fn = 16, center = true, d = 2.0, h = 20);
									}
									translate(v = [2.0, 6.0, 0]) {
										cylinder($fn = 16, center = true, d = 2.0, h = 20);
									}
									translate(v = [6.0, 6.0, 0]) {
										cylinder($fn = 16, center = true, d = 2.0, h = 20);
									}
									translate(v = [10.0, 6.0, 0]) {
										cylinder($fn = 16, center = true, d = 2.0, h = 20);
									}
									translate(v = [-6.0, 10.0, 0]) {
										cylinder($fn = 16, center = true, d = 2.0, h = 20);
									}
									translate(v = [-2.0, 10.0, 0]) {
										cylinder($fn = 16, center = true, d = 2.0, h = 20);
									}
									translate(v = [2.0, 10.0, 0]) {
										cylinder($fn = 16, center = true, d = 2.0, h = 20);
									}
									translate(v = [6.0, 10.0, 0]) {
										cylinder($fn = 16, center = true, d = 2.0, h = 20);
									}
								}
							}
						}
						translate(v = [-62.5, -35.0, -0.1]) {
							cylinder($fn = 24, d = 2.5, h = 2.2);
						}
					}
					translate(v = [62.5, -35.0, -0.1]) {
						cylinder($fn = 24, d = 2.5, h = 2.2);
					}
				}
				translate(v = [-62.5, 35.0, -0.1]) {
					cylinder($fn = 24, d = 2.5, h = 2.2);
				}
			}
			translate(v = [62.5, 35.0, -0.1]) {
				cylinder($fn = 24, d = 2.5, h = 2.2);
			}
		}
		translate(v = [0, -5, 2.0]) {
			union() {
				translate(v = [-51.0, -28.5, 0]) {
					difference() {
						cylinder($fn = 32, center = false, d = 5.0, h = 8.0);
						translate(v = [0, 0, -0.1]) {
							cylinder($fn = 24, center = false, d = 2.2, h = 8.2);
						}
					}
				}
				translate(v = [51.0, -28.5, 0]) {
					difference() {
						cylinder($fn = 32, center = false, d = 5.0, h = 8.0);
						translate(v = [0, 0, -0.1]) {
							cylinder($fn = 24, center = false, d = 2.2, h = 8.2);
						}
					}
				}
				translate(v = [-51.0, 28.5, 0]) {
					difference() {
						cylinder($fn = 32, center = false, d = 5.0, h = 8.0);
						translate(v = [0, 0, -0.1]) {
							cylinder($fn = 24, center = false, d = 2.2, h = 8.2);
						}
					}
				}
				translate(v = [51.0, 28.5, 0]) {
					difference() {
						cylinder($fn = 32, center = false, d = 5.0, h = 8.0);
						translate(v = [0, 0, -0.1]) {
							cylinder($fn = 24, center = false, d = 2.2, h = 8.2);
						}
					}
				}
			}
		}
		union() {
			translate(v = [-65.5, -38.0, 0]) {
				color(alpha = 0.8, c = "DimGray") {
					cylinder($fn = 32, center = false, d = 10.0, h = 2.0);
				}
			}
			translate(v = [65.5, -38.0, 0]) {
				color(alpha = 0.8, c = "DimGray") {
					cylinder($fn = 32, center = false, d = 10.0, h = 2.0);
				}
			}
			translate(v = [-65.5, 38.0, 0]) {
				color(alpha = 0.8, c = "DimGray") {
					cylinder($fn = 32, center = false, d = 10.0, h = 2.0);
				}
			}
			translate(v = [65.5, 38.0, 0]) {
				color(alpha = 0.8, c = "DimGray") {
					cylinder($fn = 32, center = false, d = 10.0, h = 2.0);
				}
			}
		}
	}
}
