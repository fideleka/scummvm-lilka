idf_component_register(
	SRCS
		"main.cpp"
		"launch_manifest.cpp"
		"esp-graphics.cpp"
		"esp-mixer.cpp"
		"posixesp-fs-factory.cpp"
		"mmc.c"
		"esp-mutex.cpp"
		"tdeck_board.c"
	INCLUDE_DIRS "."
	PRIV_REQUIRES
		driver
		esp_driver_i2s
		esp_driver_sdspi
		esp_driver_spi
		esp_lcd
		esp_hw_support
		esp_timer
		fatfs
		sdmmc
		vfs
		blkcache
		scummvm
	EMBED_FILES "loading.png"
)

target_compile_options(${COMPONENT_LIB} PRIVATE -DHAVE_CONFIG_H)

idf_build_set_property(COMPILE_OPTIONS "-Wno-error=reorder" APPEND)
