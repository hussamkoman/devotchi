union() {
	union() {
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
		color(alpha = 0.85, c = "SlateGray") {
			union() {
				difference() {
					difference() {
						translate(v = [0, 0, 1.0]) {
							hull() {
								translate(v = [-72.5, -45.0, 0]) {
									cylinder($fn = 48, center = true, h = 2.0, r = 4.8);
								}
								translate(v = [-72.5, 45.0, 0]) {
									cylinder($fn = 48, center = true, h = 2.0, r = 4.8);
								}
								translate(v = [72.5, -45.0, 0]) {
									cylinder($fn = 48, center = true, h = 2.0, r = 4.8);
								}
								translate(v = [72.5, 45.0, 0]) {
									cylinder($fn = 48, center = true, h = 2.0, r = 4.8);
								}
							}
						}
						translate(v = [0.0, 28.0, -0.1]) {
							union() {
								translate(v = [-4.0, -12.0, 0]) {
									cylinder($fn = 16, center = true, d = 2.0, h = 20);
								}
								translate(v = [0.0, -12.0, 0]) {
									cylinder($fn = 16, center = true, d = 2.0, h = 20);
								}
								translate(v = [4.0, -12.0, 0]) {
									cylinder($fn = 16, center = true, d = 2.0, h = 20);
								}
								translate(v = [-8.0, -8.0, 0]) {
									cylinder($fn = 16, center = true, d = 2.0, h = 20);
								}
								translate(v = [-4.0, -8.0, 0]) {
									cylinder($fn = 16, center = true, d = 2.0, h = 20);
								}
								translate(v = [0.0, -8.0, 0]) {
									cylinder($fn = 16, center = true, d = 2.0, h = 20);
								}
								translate(v = [4.0, -8.0, 0]) {
									cylinder($fn = 16, center = true, d = 2.0, h = 20);
								}
								translate(v = [8.0, -8.0, 0]) {
									cylinder($fn = 16, center = true, d = 2.0, h = 20);
								}
								translate(v = [-12.0, -4.0, 0]) {
									cylinder($fn = 16, center = true, d = 2.0, h = 20);
								}
								translate(v = [-8.0, -4.0, 0]) {
									cylinder($fn = 16, center = true, d = 2.0, h = 20);
								}
								translate(v = [-4.0, -4.0, 0]) {
									cylinder($fn = 16, center = true, d = 2.0, h = 20);
								}
								translate(v = [0.0, -4.0, 0]) {
									cylinder($fn = 16, center = true, d = 2.0, h = 20);
								}
								translate(v = [4.0, -4.0, 0]) {
									cylinder($fn = 16, center = true, d = 2.0, h = 20);
								}
								translate(v = [8.0, -4.0, 0]) {
									cylinder($fn = 16, center = true, d = 2.0, h = 20);
								}
								translate(v = [12.0, -4.0, 0]) {
									cylinder($fn = 16, center = true, d = 2.0, h = 20);
								}
								translate(v = [-12.0, 0.0, 0]) {
									cylinder($fn = 16, center = true, d = 2.0, h = 20);
								}
								translate(v = [-8.0, 0.0, 0]) {
									cylinder($fn = 16, center = true, d = 2.0, h = 20);
								}
								translate(v = [-4.0, 0.0, 0]) {
									cylinder($fn = 16, center = true, d = 2.0, h = 20);
								}
								translate(v = [0.0, 0.0, 0]) {
									cylinder($fn = 16, center = true, d = 2.0, h = 20);
								}
								translate(v = [4.0, 0.0, 0]) {
									cylinder($fn = 16, center = true, d = 2.0, h = 20);
								}
								translate(v = [8.0, 0.0, 0]) {
									cylinder($fn = 16, center = true, d = 2.0, h = 20);
								}
								translate(v = [12.0, 0.0, 0]) {
									cylinder($fn = 16, center = true, d = 2.0, h = 20);
								}
								translate(v = [-12.0, 4.0, 0]) {
									cylinder($fn = 16, center = true, d = 2.0, h = 20);
								}
								translate(v = [-8.0, 4.0, 0]) {
									cylinder($fn = 16, center = true, d = 2.0, h = 20);
								}
								translate(v = [-4.0, 4.0, 0]) {
									cylinder($fn = 16, center = true, d = 2.0, h = 20);
								}
								translate(v = [0.0, 4.0, 0]) {
									cylinder($fn = 16, center = true, d = 2.0, h = 20);
								}
								translate(v = [4.0, 4.0, 0]) {
									cylinder($fn = 16, center = true, d = 2.0, h = 20);
								}
								translate(v = [8.0, 4.0, 0]) {
									cylinder($fn = 16, center = true, d = 2.0, h = 20);
								}
								translate(v = [12.0, 4.0, 0]) {
									cylinder($fn = 16, center = true, d = 2.0, h = 20);
								}
								translate(v = [-8.0, 8.0, 0]) {
									cylinder($fn = 16, center = true, d = 2.0, h = 20);
								}
								translate(v = [-4.0, 8.0, 0]) {
									cylinder($fn = 16, center = true, d = 2.0, h = 20);
								}
								translate(v = [0.0, 8.0, 0]) {
									cylinder($fn = 16, center = true, d = 2.0, h = 20);
								}
								translate(v = [4.0, 8.0, 0]) {
									cylinder($fn = 16, center = true, d = 2.0, h = 20);
								}
								translate(v = [8.0, 8.0, 0]) {
									cylinder($fn = 16, center = true, d = 2.0, h = 20);
								}
								translate(v = [-4.0, 12.0, 0]) {
									cylinder($fn = 16, center = true, d = 2.0, h = 20);
								}
								translate(v = [0.0, 12.0, 0]) {
									cylinder($fn = 16, center = true, d = 2.0, h = 20);
								}
								translate(v = [4.0, 12.0, 0]) {
									cylinder($fn = 16, center = true, d = 2.0, h = 20);
								}
							}
						}
					}
					union() {
						translate(v = [65.5, 40.0, -0.1]) {
							cylinder($fn = 24, center = false, d = 2.5, h = 2.2);
						}
						translate(v = [-65.5, 40.0, -0.1]) {
							cylinder($fn = 24, center = false, d = 2.5, h = 2.2);
						}
						translate(v = [65.5, -40.0, -0.1]) {
							cylinder($fn = 24, center = false, d = 2.5, h = 2.2);
						}
						translate(v = [-65.5, -40.0, -0.1]) {
							cylinder($fn = 24, center = false, d = 2.5, h = 2.2);
						}
					}
				}
				translate(v = [0, 0, 2.0]) {
					union() {
						translate(v = [59.0, 28.5, 0]) {
							difference() {
								cylinder($fn = 32, center = false, d = 5.0, h = 7.0);
								translate(v = [0, 0, -0.1]) {
									cylinder($fn = 24, center = false, d = 2.2, h = 7.2);
								}
							}
						}
						translate(v = [-43.0, 28.5, 0]) {
							difference() {
								cylinder($fn = 32, center = false, d = 5.0, h = 7.0);
								translate(v = [0, 0, -0.1]) {
									cylinder($fn = 24, center = false, d = 2.2, h = 7.2);
								}
							}
						}
						translate(v = [59.0, -28.5, 0]) {
							difference() {
								cylinder($fn = 32, center = false, d = 5.0, h = 7.0);
								translate(v = [0, 0, -0.1]) {
									cylinder($fn = 24, center = false, d = 2.2, h = 7.2);
								}
							}
						}
						translate(v = [-43.0, -28.5, 0]) {
							difference() {
								cylinder($fn = 32, center = false, d = 5.0, h = 7.0);
								translate(v = [0, 0, -0.1]) {
									cylinder($fn = 24, center = false, d = 2.2, h = 7.2);
								}
							}
						}
					}
				}
				translate(v = [65.5, 40.0, 2.0]) {
					difference() {
						cylinder($fn = 32, center = false, d = 6.0, h = 5.0);
						translate(v = [0, 0, -0.1]) {
							cylinder($fn = 24, center = false, d = 2.5, h = 5.2);
						}
					}
				}
				translate(v = [-65.5, 40.0, 2.0]) {
					difference() {
						cylinder($fn = 32, center = false, d = 6.0, h = 5.0);
						translate(v = [0, 0, -0.1]) {
							cylinder($fn = 24, center = false, d = 2.5, h = 5.2);
						}
					}
				}
				translate(v = [65.5, -40.0, 2.0]) {
					difference() {
						cylinder($fn = 32, center = false, d = 6.0, h = 5.0);
						translate(v = [0, 0, -0.1]) {
							cylinder($fn = 24, center = false, d = 2.5, h = 5.2);
						}
					}
				}
				translate(v = [-65.5, -40.0, 2.0]) {
					difference() {
						cylinder($fn = 32, center = false, d = 6.0, h = 5.0);
						translate(v = [0, 0, -0.1]) {
							cylinder($fn = 24, center = false, d = 2.5, h = 5.2);
						}
					}
				}
				translate(v = [65.5, 38.0, 0]) {
					color(alpha = 0.8, c = "DimGray") {
						cylinder($fn = 32, center = false, d = 8.0, h = 1.5);
					}
				}
				translate(v = [-65.5, 38.0, 0]) {
					color(alpha = 0.8, c = "DimGray") {
						cylinder($fn = 32, center = false, d = 8.0, h = 1.5);
					}
				}
				translate(v = [65.5, -38.0, 0]) {
					color(alpha = 0.8, c = "DimGray") {
						cylinder($fn = 32, center = false, d = 8.0, h = 1.5);
					}
				}
				translate(v = [-65.5, -38.0, 0]) {
					color(alpha = 0.8, c = "DimGray") {
						cylinder($fn = 32, center = false, d = 8.0, h = 1.5);
					}
				}
			}
		}
	}
	translate(v = [0, 0.0, 20.75]) {
		rotate(a = [-16.69924423399362, 0, 0]) {
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
	translate(v = [8.0, 0, 9.8]) {
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
	translate(v = [0.0, 28.0, 4.25]) {
		color(alpha = 0.5, c = "Gold") {
			cylinder($fn = 48, center = true, d = 28.0, h = 3.5);
		}
	}
}
