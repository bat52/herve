/*
 * Copyright (c) 2024 Herve Project Contributors
 * SPDX-License-Identifier: Apache-2.0
 *
 * Board initialization for the Herve RISC-V ISS.
 */

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/init.h>

static int herve_board_init(const struct device *dev)
{
	ARG_UNUSED(dev);

	/* No board-specific initialization needed for Herve.
	 * All peripherals are memory-mapped and handled by the ISS.
	 */
	return 0;
}

SYS_INIT(herve_board_init, PRE_KERNEL_1, CONFIG_KERNEL_INIT_PRIORITY_DEFAULT);
