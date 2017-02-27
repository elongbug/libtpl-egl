/**
 * @file main.cpp
 * @brief TPL Test main entry point
 *
 * In main() function, test configurations are set from command line arguments.
 * Then, all tests will be run.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <getopt.h>

#include "gtest/gtest.h"
#include "tpl-test_base.h"
#include "tpl-test_wayland.h"

void
tpl_test_print_help()
{
	printf("\n\n=== How to setup test configurations ===\n\n");
	printf("--config.width=WIDTH	(default: 720)\n");
	printf("--config.height=HEIGHT	(default: 1280)\n");
	printf("--config.depth=DEPTH	(default: 24)\n");
	printf("\n\n");
}

Config
tpl_test_parse_arguments(int argc, char **argv)
{
	Config config;

	// Default configurations
	config.width = 720;
	config.height = 1280;
	config.depth = 24;

	// Check option
	int opt_width = 0;
	int opt_height = 0;
	int opt_depth = 0;

	struct option longopts[] = {
		{"help", no_argument, NULL, 'h'},
		{"config.width", required_argument, &opt_width, 1},
		{"config.height", required_argument, &opt_height, 1},
		{"config.depth", optional_argument, &opt_depth, 1},
		{NULL, 0, NULL, 0}
	};

	opterr = 0; // Disable argument error message

	// Parse arguments
	int opt;
	while ((opt = getopt_long_only(argc, argv, "", longopts, NULL)) != -1) {
		if (opt == 'h') {
			tpl_test_print_help();
			return config;
		} else if (opt_width == 1) {
			printf("Width set: %s\n", optarg);
			config.width = atoi(optarg);
			opt_width = 0;
		} else if (opt_height == 1) {
			printf("Height set: %s\n", optarg);
			config.height = atoi(optarg);
			opt_height = 0;
		} else if (opt_depth == 1) {
			printf("Depth set: %s\n", optarg);
			config.depth = atoi(optarg);
			opt_depth = 0;
		} else {
			break;
		}
	}

	return config;
}


int
main(int argc, char **argv)
{
	// Setup configurations
	TPLTestBase::config = tpl_test_parse_arguments(argc, argv);

	// Initialize backend
	TPLTestBase::backend = new TPLWayland();

	::testing::InitGoogleTest(&argc, argv);
	return RUN_ALL_TESTS();
}

