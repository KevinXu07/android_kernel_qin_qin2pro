// SPDX-License-Identifier: GPL-2.0
/*
 * Qin 2 Pro OCP8137 torch/flash LED driver.
 *
 * The OCP8137 flash IC sits on i2c@70900000 (addr 0x63) and is gated by
 * four AP GPIOs described by the stock DTS node "flash-ic@63":
 *   flash-chip-en-gpios  - main chip enable / power
 *   flash-torch-en-gpios - torch mode enable
 *   flash-en-gpios       - strobe (flash mode) enable
 *   flash-sync-gpios     - sync input, kept low
 *
 * For camera torch use we only need chip-en + torch-en: the OCP8137
 * lights the LED at the hardware default torch current.  Exposed as a
 * led_classdev named "flashlight" so userspace can drive it through
 * /sys/class/leds/flashlight/brightness.
 */

#include <linux/gpio/consumer.h>
#include <linux/i2c.h>
#include <linux/leds.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/slab.h>

struct qin_flash {
	struct i2c_client *client;
	struct led_classdev cdev;
	struct gpio_desc *chip_en;
	struct gpio_desc *torch_en;
	struct gpio_desc *flash_en;
	struct gpio_desc *sync;
};

static void qin_flash_set(struct qin_flash *flash, int on)
{
	gpiod_set_value_cansleep(flash->chip_en, on);
	gpiod_set_value_cansleep(flash->torch_en, on);
}

static int qin_flash_brightness_set(struct led_classdev *cdev,
				    enum led_brightness brightness)
{
	struct qin_flash *flash =
		container_of(cdev, struct qin_flash, cdev);

	qin_flash_set(flash, brightness ? 1 : 0);
	return 0;
}

static int qin_flash_probe(struct i2c_client *client,
			   const struct i2c_device_id *id)
{
	struct device *dev = &client->dev;
	struct qin_flash *flash;
	int ret;

	flash = devm_kzalloc(dev, sizeof(*flash), GFP_KERNEL);
	if (!flash)
		return -ENOMEM;

	flash->client = client;
	flash->chip_en = devm_gpiod_get(dev, "flash-chip-en", GPIOD_OUT_LOW);
	if (IS_ERR(flash->chip_en)) {
		dev_err(dev, "cannot get flash-chip-en gpio\n");
		return PTR_ERR(flash->chip_en);
	}

	flash->torch_en = devm_gpiod_get(dev, "flash-torch-en", GPIOD_OUT_LOW);
	if (IS_ERR(flash->torch_en)) {
		dev_err(dev, "cannot get flash-torch-en gpio\n");
		return PTR_ERR(flash->torch_en);
	}

	/* Optional lines: claim them low so nothing floats. */
	flash->flash_en = devm_gpiod_get(dev, "flash-en", GPIOD_OUT_LOW);
	if (IS_ERR(flash->flash_en))
		flash->flash_en = NULL;
	flash->sync = devm_gpiod_get(dev, "flash-sync", GPIOD_OUT_LOW);
	if (IS_ERR(flash->sync))
		flash->sync = NULL;

	flash->cdev.name = "flashlight";
	flash->cdev.max_brightness = 1;
	flash->cdev.brightness_set_blocking = qin_flash_brightness_set;
	flash->cdev.flags = LED_CORE_SUSPENDRESUME;

	ret = devm_led_classdev_register(dev, &flash->cdev);
	if (ret) {
		dev_err(dev, "cannot register led\n");
		return ret;
	}

	i2c_set_clientdata(client, flash);
	dev_info(dev, "qin flash torch registered\n");
	return 0;
}

static int qin_flash_remove(struct i2c_client *client)
{
	struct qin_flash *flash = i2c_get_clientdata(client);

	qin_flash_set(flash, 0);
	return 0;
}

static const struct of_device_id qin_flash_of_match[] = {
	{ .compatible = "sprd,flash-ocp8137" },
	{ }
};
MODULE_DEVICE_TABLE(of, qin_flash_of_match);

static struct i2c_driver qin_flash_driver = {
	.driver = {
		.name = "qin-flash-ocp8137",
		.of_match_table = qin_flash_of_match,
	},
	.probe = qin_flash_probe,
	.remove = qin_flash_remove,
};
module_i2c_driver(qin_flash_driver);

MODULE_DESCRIPTION("Qin 2 Pro OCP8137 torch LED driver");
MODULE_LICENSE("GPL");
