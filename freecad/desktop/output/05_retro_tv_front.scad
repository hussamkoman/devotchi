union() {
	color(alpha = 0.95, c = "Ivory") {
		union() {
			union() {
				union() {
					difference() {
						difference() {
							difference() {
								difference() {
									difference() {
										difference() {
											hull() {
												translate(v = [-72.5, -47.5, 0]) {
													cylinder($fn = 48, center = true, h = 65.0, r = 10.0);
												}
												translate(v = [-72.5, 47.5, 0]) {
													cylinder($fn = 48, center = true, h = 65.0, r = 10.0);
												}
												translate(v = [72.5, -47.5, 0]) {
													cylinder($fn = 48, center = true, h = 65.0, r = 10.0);
												}
												translate(v = [72.5, 47.5, 0]) {
													cylinder($fn = 48, center = true, h = 65.0, r = 10.0);
												}
											}
											translate(v = [0, 0, 0]) {
												cube(center = true, size = [160.0, 110.0, 60.1]);
											}
										}
										translate(v = [0, 0, -31.25]) {
											hull() {
												translate(v = [-47.0, -25.4, 0]) {
													cylinder($fn = 48, center = true, h = 2.9, r = 8.0);
												}
												translate(v = [-47.0, 25.4, 0]) {
													cylinder($fn = 48, center = true, h = 2.9, r = 8.0);
												}
												translate(v = [47.0, -25.4, 0]) {
													cylinder($fn = 48, center = true, h = 2.9, r = 8.0);
												}
												translate(v = [47.0, 25.4, 0]) {
													cylinder($fn = 48, center = true, h = 2.9, r = 8.0);
												}
											}
										}
									}
									translate(v = [0, 0, -27.25]) {
										cube(center = true, size = [120.4, 75.4, 5.6]);
									}
								}
								union() {
									translate(v = [82.5, 7.5, -22.5]) {
										rotate(a = [0, 90, 0]) {
											cylinder($fn = 32, center = true, d = 10.0, h = 2.9);
										}
									}
									translate(v = [82.5, -7.5, -22.5]) {
										rotate(a = [0, 90, 0]) {
											cylinder($fn = 32, center = true, d = 10.0, h = 2.9);
										}
									}
								}
							}
							translate(v = [0, 57.5, -22.5]) {
								rotate(a = [90, 0, 0]) {
									cylinder($fn = 32, center = true, d = 8.0, h = 2.9);
								}
							}
						}
						union() {
							translate(v = [-82.5, -12.0, -27.75]) {
								rotate(a = [0, 90, 0]) {
									cylinder($fn = 16, center = true, d = 2.0, h = 2.9);
								}
							}
							translate(v = [-82.5, -8.0, -27.75]) {
								rotate(a = [0, 90, 0]) {
									cylinder($fn = 16, center = true, d = 2.0, h = 2.9);
								}
							}
							translate(v = [-82.5, -4.0, -27.75]) {
								rotate(a = [0, 90, 0]) {
									cylinder($fn = 16, center = true, d = 2.0, h = 2.9);
								}
							}
							translate(v = [-82.5, 0.0, -27.75]) {
								rotate(a = [0, 90, 0]) {
									cylinder($fn = 16, center = true, d = 2.0, h = 2.9);
								}
							}
							translate(v = [-82.5, 4.0, -27.75]) {
								rotate(a = [0, 90, 0]) {
									cylinder($fn = 16, center = true, d = 2.0, h = 2.9);
								}
							}
							translate(v = [-82.5, 8.0, -27.75]) {
								rotate(a = [0, 90, 0]) {
									cylinder($fn = 16, center = true, d = 2.0, h = 2.9);
								}
							}
							translate(v = [-82.5, 12.0, -27.75]) {
								rotate(a = [0, 90, 0]) {
									cylinder($fn = 16, center = true, d = 2.0, h = 2.9);
								}
							}
							translate(v = [-82.5, -12.0, -23.75]) {
								rotate(a = [0, 90, 0]) {
									cylinder($fn = 16, center = true, d = 2.0, h = 2.9);
								}
							}
							translate(v = [-82.5, -8.0, -23.75]) {
								rotate(a = [0, 90, 0]) {
									cylinder($fn = 16, center = true, d = 2.0, h = 2.9);
								}
							}
							translate(v = [-82.5, -4.0, -23.75]) {
								rotate(a = [0, 90, 0]) {
									cylinder($fn = 16, center = true, d = 2.0, h = 2.9);
								}
							}
							translate(v = [-82.5, 0.0, -23.75]) {
								rotate(a = [0, 90, 0]) {
									cylinder($fn = 16, center = true, d = 2.0, h = 2.9);
								}
							}
							translate(v = [-82.5, 4.0, -23.75]) {
								rotate(a = [0, 90, 0]) {
									cylinder($fn = 16, center = true, d = 2.0, h = 2.9);
								}
							}
							translate(v = [-82.5, 8.0, -23.75]) {
								rotate(a = [0, 90, 0]) {
									cylinder($fn = 16, center = true, d = 2.0, h = 2.9);
								}
							}
							translate(v = [-82.5, 12.0, -23.75]) {
								rotate(a = [0, 90, 0]) {
									cylinder($fn = 16, center = true, d = 2.0, h = 2.9);
								}
							}
							translate(v = [-82.5, -12.0, -19.75]) {
								rotate(a = [0, 90, 0]) {
									cylinder($fn = 16, center = true, d = 2.0, h = 2.9);
								}
							}
							translate(v = [-82.5, -8.0, -19.75]) {
								rotate(a = [0, 90, 0]) {
									cylinder($fn = 16, center = true, d = 2.0, h = 2.9);
								}
							}
							translate(v = [-82.5, -4.0, -19.75]) {
								rotate(a = [0, 90, 0]) {
									cylinder($fn = 16, center = true, d = 2.0, h = 2.9);
								}
							}
							translate(v = [-82.5, 0.0, -19.75]) {
								rotate(a = [0, 90, 0]) {
									cylinder($fn = 16, center = true, d = 2.0, h = 2.9);
								}
							}
							translate(v = [-82.5, 4.0, -19.75]) {
								rotate(a = [0, 90, 0]) {
									cylinder($fn = 16, center = true, d = 2.0, h = 2.9);
								}
							}
							translate(v = [-82.5, 8.0, -19.75]) {
								rotate(a = [0, 90, 0]) {
									cylinder($fn = 16, center = true, d = 2.0, h = 2.9);
								}
							}
							translate(v = [-82.5, 12.0, -19.75]) {
								rotate(a = [0, 90, 0]) {
									cylinder($fn = 16, center = true, d = 2.0, h = 2.9);
								}
							}
							translate(v = [-82.5, -12.0, -15.75]) {
								rotate(a = [0, 90, 0]) {
									cylinder($fn = 16, center = true, d = 2.0, h = 2.9);
								}
							}
							translate(v = [-82.5, -8.0, -15.75]) {
								rotate(a = [0, 90, 0]) {
									cylinder($fn = 16, center = true, d = 2.0, h = 2.9);
								}
							}
							translate(v = [-82.5, -4.0, -15.75]) {
								rotate(a = [0, 90, 0]) {
									cylinder($fn = 16, center = true, d = 2.0, h = 2.9);
								}
							}
							translate(v = [-82.5, 0.0, -15.75]) {
								rotate(a = [0, 90, 0]) {
									cylinder($fn = 16, center = true, d = 2.0, h = 2.9);
								}
							}
							translate(v = [-82.5, 4.0, -15.75]) {
								rotate(a = [0, 90, 0]) {
									cylinder($fn = 16, center = true, d = 2.0, h = 2.9);
								}
							}
							translate(v = [-82.5, 8.0, -15.75]) {
								rotate(a = [0, 90, 0]) {
									cylinder($fn = 16, center = true, d = 2.0, h = 2.9);
								}
							}
							translate(v = [-82.5, 12.0, -15.75]) {
								rotate(a = [0, 90, 0]) {
									cylinder($fn = 16, center = true, d = 2.0, h = 2.9);
								}
							}
							translate(v = [-82.5, -12.0, -11.75]) {
								rotate(a = [0, 90, 0]) {
									cylinder($fn = 16, center = true, d = 2.0, h = 2.9);
								}
							}
							translate(v = [-82.5, -8.0, -11.75]) {
								rotate(a = [0, 90, 0]) {
									cylinder($fn = 16, center = true, d = 2.0, h = 2.9);
								}
							}
							translate(v = [-82.5, -4.0, -11.75]) {
								rotate(a = [0, 90, 0]) {
									cylinder($fn = 16, center = true, d = 2.0, h = 2.9);
								}
							}
							translate(v = [-82.5, 0.0, -11.75]) {
								rotate(a = [0, 90, 0]) {
									cylinder($fn = 16, center = true, d = 2.0, h = 2.9);
								}
							}
							translate(v = [-82.5, 4.0, -11.75]) {
								rotate(a = [0, 90, 0]) {
									cylinder($fn = 16, center = true, d = 2.0, h = 2.9);
								}
							}
							translate(v = [-82.5, 8.0, -11.75]) {
								rotate(a = [0, 90, 0]) {
									cylinder($fn = 16, center = true, d = 2.0, h = 2.9);
								}
							}
							translate(v = [-82.5, 12.0, -11.75]) {
								rotate(a = [0, 90, 0]) {
									cylinder($fn = 16, center = true, d = 2.0, h = 2.9);
								}
							}
							translate(v = [-82.5, -12.0, -7.75]) {
								rotate(a = [0, 90, 0]) {
									cylinder($fn = 16, center = true, d = 2.0, h = 2.9);
								}
							}
							translate(v = [-82.5, -8.0, -7.75]) {
								rotate(a = [0, 90, 0]) {
									cylinder($fn = 16, center = true, d = 2.0, h = 2.9);
								}
							}
							translate(v = [-82.5, -4.0, -7.75]) {
								rotate(a = [0, 90, 0]) {
									cylinder($fn = 16, center = true, d = 2.0, h = 2.9);
								}
							}
							translate(v = [-82.5, 0.0, -7.75]) {
								rotate(a = [0, 90, 0]) {
									cylinder($fn = 16, center = true, d = 2.0, h = 2.9);
								}
							}
							translate(v = [-82.5, 4.0, -7.75]) {
								rotate(a = [0, 90, 0]) {
									cylinder($fn = 16, center = true, d = 2.0, h = 2.9);
								}
							}
							translate(v = [-82.5, 8.0, -7.75]) {
								rotate(a = [0, 90, 0]) {
									cylinder($fn = 16, center = true, d = 2.0, h = 2.9);
								}
							}
							translate(v = [-82.5, 12.0, -7.75]) {
								rotate(a = [0, 90, 0]) {
									cylinder($fn = 16, center = true, d = 2.0, h = 2.9);
								}
							}
							translate(v = [-82.5, -12.0, -3.75]) {
								rotate(a = [0, 90, 0]) {
									cylinder($fn = 16, center = true, d = 2.0, h = 2.9);
								}
							}
							translate(v = [-82.5, -8.0, -3.75]) {
								rotate(a = [0, 90, 0]) {
									cylinder($fn = 16, center = true, d = 2.0, h = 2.9);
								}
							}
							translate(v = [-82.5, -4.0, -3.75]) {
								rotate(a = [0, 90, 0]) {
									cylinder($fn = 16, center = true, d = 2.0, h = 2.9);
								}
							}
							translate(v = [-82.5, 0.0, -3.75]) {
								rotate(a = [0, 90, 0]) {
									cylinder($fn = 16, center = true, d = 2.0, h = 2.9);
								}
							}
							translate(v = [-82.5, 4.0, -3.75]) {
								rotate(a = [0, 90, 0]) {
									cylinder($fn = 16, center = true, d = 2.0, h = 2.9);
								}
							}
							translate(v = [-82.5, 8.0, -3.75]) {
								rotate(a = [0, 90, 0]) {
									cylinder($fn = 16, center = true, d = 2.0, h = 2.9);
								}
							}
							translate(v = [-82.5, 12.0, -3.75]) {
								rotate(a = [0, 90, 0]) {
									cylinder($fn = 16, center = true, d = 2.0, h = 2.9);
								}
							}
							translate(v = [-82.5, -12.0, 0.25]) {
								rotate(a = [0, 90, 0]) {
									cylinder($fn = 16, center = true, d = 2.0, h = 2.9);
								}
							}
							translate(v = [-82.5, -8.0, 0.25]) {
								rotate(a = [0, 90, 0]) {
									cylinder($fn = 16, center = true, d = 2.0, h = 2.9);
								}
							}
							translate(v = [-82.5, -4.0, 0.25]) {
								rotate(a = [0, 90, 0]) {
									cylinder($fn = 16, center = true, d = 2.0, h = 2.9);
								}
							}
							translate(v = [-82.5, 0.0, 0.25]) {
								rotate(a = [0, 90, 0]) {
									cylinder($fn = 16, center = true, d = 2.0, h = 2.9);
								}
							}
							translate(v = [-82.5, 4.0, 0.25]) {
								rotate(a = [0, 90, 0]) {
									cylinder($fn = 16, center = true, d = 2.0, h = 2.9);
								}
							}
							translate(v = [-82.5, 8.0, 0.25]) {
								rotate(a = [0, 90, 0]) {
									cylinder($fn = 16, center = true, d = 2.0, h = 2.9);
								}
							}
							translate(v = [-82.5, 12.0, 0.25]) {
								rotate(a = [0, 90, 0]) {
									cylinder($fn = 16, center = true, d = 2.0, h = 2.9);
								}
							}
							translate(v = [-82.5, -12.0, 4.25]) {
								rotate(a = [0, 90, 0]) {
									cylinder($fn = 16, center = true, d = 2.0, h = 2.9);
								}
							}
							translate(v = [-82.5, -8.0, 4.25]) {
								rotate(a = [0, 90, 0]) {
									cylinder($fn = 16, center = true, d = 2.0, h = 2.9);
								}
							}
							translate(v = [-82.5, -4.0, 4.25]) {
								rotate(a = [0, 90, 0]) {
									cylinder($fn = 16, center = true, d = 2.0, h = 2.9);
								}
							}
							translate(v = [-82.5, 0.0, 4.25]) {
								rotate(a = [0, 90, 0]) {
									cylinder($fn = 16, center = true, d = 2.0, h = 2.9);
								}
							}
							translate(v = [-82.5, 4.0, 4.25]) {
								rotate(a = [0, 90, 0]) {
									cylinder($fn = 16, center = true, d = 2.0, h = 2.9);
								}
							}
							translate(v = [-82.5, 8.0, 4.25]) {
								rotate(a = [0, 90, 0]) {
									cylinder($fn = 16, center = true, d = 2.0, h = 2.9);
								}
							}
							translate(v = [-82.5, 12.0, 4.25]) {
								rotate(a = [0, 90, 0]) {
									cylinder($fn = 16, center = true, d = 2.0, h = 2.9);
								}
							}
							translate(v = [-82.5, -12.0, 8.25]) {
								rotate(a = [0, 90, 0]) {
									cylinder($fn = 16, center = true, d = 2.0, h = 2.9);
								}
							}
							translate(v = [-82.5, -8.0, 8.25]) {
								rotate(a = [0, 90, 0]) {
									cylinder($fn = 16, center = true, d = 2.0, h = 2.9);
								}
							}
							translate(v = [-82.5, -4.0, 8.25]) {
								rotate(a = [0, 90, 0]) {
									cylinder($fn = 16, center = true, d = 2.0, h = 2.9);
								}
							}
							translate(v = [-82.5, 0.0, 8.25]) {
								rotate(a = [0, 90, 0]) {
									cylinder($fn = 16, center = true, d = 2.0, h = 2.9);
								}
							}
							translate(v = [-82.5, 4.0, 8.25]) {
								rotate(a = [0, 90, 0]) {
									cylinder($fn = 16, center = true, d = 2.0, h = 2.9);
								}
							}
							translate(v = [-82.5, 8.0, 8.25]) {
								rotate(a = [0, 90, 0]) {
									cylinder($fn = 16, center = true, d = 2.0, h = 2.9);
								}
							}
							translate(v = [-82.5, 12.0, 8.25]) {
								rotate(a = [0, 90, 0]) {
									cylinder($fn = 16, center = true, d = 2.0, h = 2.9);
								}
							}
						}
					}
					union() {
						translate(v = [-15.0, 65.0, 16.25]) {
							rotate(a = [90, 0, 0]) {
								cylinder($fn = 32, center = true, d = 4.0, h = 15.0);
							}
						}
						translate(v = [15.0, 65.0, 16.25]) {
							rotate(a = [90, 0, 0]) {
								cylinder($fn = 32, center = true, d = 4.0, h = 15.0);
							}
						}
					}
				}
				union() {
					translate(v = [-51.0, -28.5, -30.0]) {
						union() {
							translate(v = [0, 0, 0]) {
								difference() {
									cylinder($fn = 32, center = false, d = 5.0, h = 40.0);
									translate(v = [0, 0, -0.1]) {
										cylinder($fn = 24, center = false, d = 2.2, h = 40.2);
									}
								}
							}
						}
					}
					translate(v = [51.0, -28.5, -30.0]) {
						union() {
							translate(v = [0, 0, 0]) {
								difference() {
									cylinder($fn = 32, center = false, d = 5.0, h = 40.0);
									translate(v = [0, 0, -0.1]) {
										cylinder($fn = 24, center = false, d = 2.2, h = 40.2);
									}
								}
							}
						}
					}
					translate(v = [-51.0, 28.5, -30.0]) {
						union() {
							translate(v = [0, 0, 0]) {
								difference() {
									cylinder($fn = 32, center = false, d = 5.0, h = 40.0);
									translate(v = [0, 0, -0.1]) {
										cylinder($fn = 24, center = false, d = 2.2, h = 40.2);
									}
								}
							}
						}
					}
					translate(v = [51.0, 28.5, -30.0]) {
						union() {
							translate(v = [0, 0, 0]) {
								difference() {
									cylinder($fn = 32, center = false, d = 5.0, h = 40.0);
									translate(v = [0, 0, -0.1]) {
										cylinder($fn = 24, center = false, d = 2.2, h = 40.2);
									}
								}
							}
						}
					}
				}
			}
			union() {
				translate(v = [-35.0, -63.5, -22.5]) {
					rotate(a = [0, 0, -10.0]) {
						hull() {
							translate(v = [-5.5, -4.0, 0]) {
								cylinder($fn = 48, center = true, h = 20.0, r = 2.0);
							}
							translate(v = [-5.5, 4.0, 0]) {
								cylinder($fn = 48, center = true, h = 20.0, r = 2.0);
							}
							translate(v = [5.5, -4.0, 0]) {
								cylinder($fn = 48, center = true, h = 20.0, r = 2.0);
							}
							translate(v = [5.5, 4.0, 0]) {
								cylinder($fn = 48, center = true, h = 20.0, r = 2.0);
							}
						}
					}
				}
				translate(v = [35.0, -63.5, -22.5]) {
					rotate(a = [0, 0, 10.0]) {
						hull() {
							translate(v = [-5.5, -4.0, 0]) {
								cylinder($fn = 48, center = true, h = 20.0, r = 2.0);
							}
							translate(v = [-5.5, 4.0, 0]) {
								cylinder($fn = 48, center = true, h = 20.0, r = 2.0);
							}
							translate(v = [5.5, -4.0, 0]) {
								cylinder($fn = 48, center = true, h = 20.0, r = 2.0);
							}
							translate(v = [5.5, 4.0, 0]) {
								cylinder($fn = 48, center = true, h = 20.0, r = 2.0);
							}
						}
					}
				}
			}
		}
	}
	translate(v = [0, 0, -27.75]) {
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
