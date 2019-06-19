/*
 * RNG driver for TX4939 Random Number Generators (RNG)
 *
 * Copyright (C) 2009 Atsushi Nemoto <anemo@mba.ocn.ne.jp>
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 */
#include <linux/err.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/delay.h>
#include <linux/io.h>
#include <linux/platform_device.h>
#include <linux/hw_random.h>
#include <linux/gfp.h>

#define TX4939_RNG_RCSR		0x00000000
#define TX4939_RNG_ROR(n)	(0x00000018 + (n) * 8)

#define TX4939_RNG_RCSR_INTE	0x00000008
#define TX4939_RNG_RCSR_RST	0x00000004
#define TX4939_RNG_RCSR_FIN	0x00000002
#define TX4939_RNG_RCSR_ST	0x00000001

struct tx4939_rng {
	struct hwrng rng;
	void __iomem *base;
};

static void rng_io_start(void)
{
#ifndef CONFIG_64BIT
	/*
	 * readq is reading a 64-bit register using a 64-bit load.  On
	 * a 32-bit kernel however interrupts or any other processor
	 * exception would clobber the upper 32-bit of the processor
	 * register so interrupts need to be disabled.
	 */
	local_irq_disable();
#endif
}

static void rng_io_end(void)
{
#ifndef CONFIG_64BIT
	local_irq_enable();
#endif
}

static u64 read_rng(void __iomem *base, unsigned int offset)
{
	return ____raw_readq(base + offset);
}

static void write_rng(u64 val, void __iomem *base, unsigned int offset)
{
	return ____raw_writeq(val, base + offset);
}

static int tx4939_rng_read(struct hwrng *rng, void *data, size_t max, bool wait)
{
	u64 *out = data;
	int present, retries = 20, i;
	struct tx4939_rng *rngdev = container_of(rng, struct tx4939_rng, rng);

	if (WARN_ON(max < sizeof(u64)))
		return -EINVAL;

	rng_io_start();
	present = !(read_rng(rngdev->base, TX4939_RNG_RCSR)
			& TX4939_RNG_RCSR_ST);
	rng_io_end();

	while (wait && !present && retries--) {
		/* 90 bus clock cycles by default for generation */
		ndelay(90 * 5);

		rng_io_start();
		present = !(read_rng(rngdev->base, TX4939_RNG_RCSR)
			& TX4939_RNG_RCSR_ST);
		rng_io_end();
	}

	if (!present)
		return 0;

	max = min((size_t)3, max / sizeof(u64));

	rng_io_start();
	for (i = 0; i < max; i++)
		out[i] = read_rng(rngdev->base, TX4939_RNG_ROR(i));
	/* Start RNG */
	write_rng(TX4939_RNG_RCSR_ST, rngdev->base, TX4939_RNG_RCSR);
	rng_io_end();

	return i * sizeof(u64);
}

static int __init tx4939_rng_probe(struct platform_device *dev)
{
	struct tx4939_rng *rngdev;
	struct resource *r;
	int i;
	u64 flush[3];

	rngdev = devm_kzalloc(&dev->dev, sizeof(*rngdev), GFP_KERNEL);
	if (!rngdev)
		return -ENOMEM;
	r = platform_get_resource(dev, IORESOURCE_MEM, 0);
	rngdev->base = devm_ioremap_resource(&dev->dev, r);
	if (IS_ERR(rngdev->base))
		return PTR_ERR(rngdev->base);

	rngdev->rng.name = dev_name(&dev->dev);
	rngdev->rng.read = tx4939_rng_read;

	rng_io_start();
	/* Reset RNG */
	write_rng(TX4939_RNG_RCSR_RST, rngdev->base, TX4939_RNG_RCSR);
	write_rng(0, rngdev->base, TX4939_RNG_RCSR);
	/* Start RNG */
	write_rng(TX4939_RNG_RCSR_ST, rngdev->base, TX4939_RNG_RCSR);
	rng_io_end();
	/*
	 * Drop first two results.  From the datasheet:
	 * The quality of the random numbers generated immediately
	 * after reset can be insufficient.  Therefore, do not use
	 * random numbers obtained from the first and second
	 * generations; use the ones from the third or subsequent
	 * generation.
	 */
	for (i = 0; i < 2; i++)
		if (!tx4939_rng_read(&rngdev->rng, flush, sizeof(flush), 1))
			return -EIO;

	platform_set_drvdata(dev, rngdev);
	return devm_hwrng_register(&dev->dev, &rngdev->rng);
}

static struct platform_driver tx4939_rng_driver = {
	.driver		= {
		.name	= "tx4939-rng",
	},
};

module_platform_driver_probe(tx4939_rng_driver, tx4939_rng_probe);

MODULE_DESCRIPTION("H/W Random Number Generator (RNG) driver for TX4939");
MODULE_LICENSE("GPL");
