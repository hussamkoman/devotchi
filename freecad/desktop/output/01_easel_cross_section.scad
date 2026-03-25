intersection() {
	union() {
		union() {
			difference() {
				difference() {
					difference() {
						difference() {
							difference() {
								difference() {
									union() {
										union() {
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
												translate(v = [0, 0, -4.25]) {
													cube(center = true, size = [132.0, 87.0, 5.5]);
												}
											}
											translate(v = [0, 0, -2.1]) {
												difference() {
													hull() {
														translate(v = [-61.0, -38.5, 0]) {
															cylinder($fn = 48, center = true, h = 1.2, r = 2.8);
														}
														translate(v = [-61.0, 38.5, 0]) {
															cylinder($fn = 48, center = true, h = 1.2, r = 2.8);
														}
														translate(v = [61.0, -38.5, 0]) {
															cylinder($fn = 48, center = true, h = 1.2, r = 2.8);
														}
														translate(v = [61.0, 38.5, 0]) {
															cylinder($fn = 48, center = true, h = 1.2, r = 2.8);
														}
													}
													hull() {
														translate(v = [-61.0, -38.5, 0]) {
															cylinder($fn = 48, center = true, h = 1.4, r = 1.2);
														}
														translate(v = [-61.0, 38.5, 0]) {
															cylinder($fn = 48, center = true, h = 1.4, r = 1.2);
														}
														translate(v = [61.0, -38.5, 0]) {
															cylinder($fn = 48, center = true, h = 1.4, r = 1.2);
														}
														translate(v = [61.0, 38.5, 0]) {
															cylinder($fn = 48, center = true, h = 1.4, r = 1.2);
														}
													}
												}
											}
										}
										union() {
											translate(v = [-60.0, 15.75, -2.0]) {
												rotate(a = [0, 0, 90]) {
													union() {
														cube(center = true, size = [8.0, 1.0, 3.0]);
														translate(v = [0, -0.75, 1.5]) {
															cube(center = true, size = [8.0, 1.5, 1.0]);
														}
													}
												}
											}
											translate(v = [-60.0, -21.75, -2.0]) {
												rotate(a = [0, 0, 90]) {
													union() {
														cube(center = true, size = [8.0, 1.0, 3.0]);
														translate(v = [0, -0.75, 1.5]) {
															cube(center = true, size = [8.0, 1.5, 1.0]);
														}
													}
												}
											}
											translate(v = [60.0, 15.75, -2.0]) {
												rotate(a = [0, 0, -90]) {
													union() {
														cube(center = true, size = [8.0, 1.0, 3.0]);
														translate(v = [0, -0.75, 1.5]) {
															cube(center = true, size = [8.0, 1.5, 1.0]);
														}
													}
												}
											}
											translate(v = [60.0, -21.75, -2.0]) {
												rotate(a = [0, 0, -90]) {
													union() {
														cube(center = true, size = [8.0, 1.0, 3.0]);
														translate(v = [0, -0.75, 1.5]) {
															cube(center = true, size = [8.0, 1.5, 1.0]);
														}
													}
												}
											}
										}
									}
									cube(center = true, size = [126.0, 81.0, 10.0]);
								}
								translate(v = [0, -3.0, -4.5]) {
									cube(center = true, size = [120.4, 75.4, 5.2]);
								}
							}
							translate(v = [0, -3.0, -7.0]) {
								cube(center = true, size = [109.0, 65.8, 2.6]);
							}
						}
						translate(v = [0, -48.5, -7.0]) {
							union() {
								translate(v = [-14.0, 0, 0]) {
									cylinder($fn = 32, center = true, d = 8.0, h = 2.6);
								}
								translate(v = [0.0, 0, 0]) {
									cylinder($fn = 32, center = true, d = 8.0, h = 2.6);
								}
								translate(v = [14.0, 0, 0]) {
									cylinder($fn = 32, center = true, d = 8.0, h = 2.6);
								}
							}
						}
					}
					translate(v = [65.0, 31.5, -7.0]) {
						cylinder($fn = 16, center = true, d = 2.0, h = 2.6);
					}
				}
				translate(v = [0, -42.5, -2.75]) {
					rotate(a = [90, 0, 0]) {
						linear_extrude(center = true, height = 2.6) {
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
		}
		translate(v = [0, -3.0, -2.75]) {
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
		translate(v = [0, -3.0, 3.3]) {
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
	translate(v = [0, 250.0, 0]) {
		cube(center = true, size = [1000, 500, 1000]);
	}
}
