intersection() {
	union() {
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
		translate(v = [0, 0, 25.0]) {
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
		translate(v = [0, -5, 10.8]) {
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
		translate(v = [0.0, 30.0, 4.25]) {
			color(alpha = 0.5, c = "Gold") {
				cylinder($fn = 48, center = true, d = 28.0, h = 3.5);
			}
		}
	}
	translate(v = [250.0, 0, 0]) {
		cube(center = true, size = [500, 1000, 1000]);
	}
}
