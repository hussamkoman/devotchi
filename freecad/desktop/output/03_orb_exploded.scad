union() {
	translate(v = [0, 0, 60]) {
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
	}
	translate(v = [0, 0, -20]) {
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
	}
	translate(v = [0, 0, -80]) {
		color(alpha = 0.85, c = "DimGray") {
			difference() {
				difference() {
					difference() {
						union() {
							difference() {
								difference() {
									difference() {
										cylinder($fn = 96, center = false, d = 80.0, h = 35.0);
										translate(v = [0, 0, 2.0]) {
											cylinder($fn = 96, center = false, d = 76.0, h = 33.1);
										}
									}
									translate(v = [0, 0, 2.0]) {
										cylinder($fn = 48, center = false, d = 30.0, h = 5.5);
									}
								}
								cylinder($fn = 32, center = false, d = 8.0, h = 45.2);
							}
							translate(v = [0, 0, 35.0]) {
								difference() {
									cylinder($fn = 48, center = false, d = 20.0, h = 10.0);
									translate(v = [0, 0, -0.1]) {
										cylinder($fn = 32, center = false, d = 8.0, h = 10.2);
									}
								}
							}
						}
						union() {
							translate(v = [-14.0, 40.0, 23.0]) {
								rotate(a = [90, 0, 0]) {
									cylinder($fn = 32, center = true, d = 8.0, h = 2.2);
								}
							}
							translate(v = [0.0, 40.0, 23.0]) {
								rotate(a = [90, 0, 0]) {
									cylinder($fn = 32, center = true, d = 8.0, h = 2.2);
								}
							}
							translate(v = [14.0, 40.0, 23.0]) {
								rotate(a = [90, 0, 0]) {
									cylinder($fn = 32, center = true, d = 8.0, h = 2.2);
								}
							}
						}
					}
					translate(v = [0, -40.0, 17.5]) {
						rotate(a = [90, 0, 0]) {
							linear_extrude(center = true, height = 2.2) {
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
				union() {
					translate(v = [-39.39231012048832, 6.945927106677217, 4.75]) {
						rotate(a = [0, 0, -80]) {
							cube(center = true, size = [3.0, 4.2, 8.0]);
						}
					}
					translate(v = [-34.64101615137754, 20.000000000000004, 4.75]) {
						rotate(a = [0, 0, -60]) {
							cube(center = true, size = [3.0, 4.2, 8.0]);
						}
					}
					translate(v = [-25.71150438746157, 30.64177772475912, 4.75]) {
						rotate(a = [0, 0, -40]) {
							cube(center = true, size = [3.0, 4.2, 8.0]);
						}
					}
					translate(v = [25.71150438746157, 30.64177772475912, 4.75]) {
						rotate(a = [0, 0, 40]) {
							cube(center = true, size = [3.0, 4.2, 8.0]);
						}
					}
					translate(v = [34.64101615137754, 20.000000000000004, 4.75]) {
						rotate(a = [0, 0, 60]) {
							cube(center = true, size = [3.0, 4.2, 8.0]);
						}
					}
					translate(v = [39.39231012048832, 6.945927106677217, 4.75]) {
						rotate(a = [0, 0, 80]) {
							cube(center = true, size = [3.0, 4.2, 8.0]);
						}
					}
				}
			}
		}
	}
	translate(v = [0, 0, 40]) {
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
	}
	translate(v = [0, 0, 20]) {
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
	translate(v = [0, 0, -60]) {
		translate(v = [0, 0, 3.75]) {
			color(alpha = 0.5, c = "Gold") {
				cylinder($fn = 48, center = true, d = 28.0, h = 3.5);
			}
		}
	}
}
