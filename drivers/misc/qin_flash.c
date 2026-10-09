// SPDX-License-Identifier: GPL-2.0
/*
 * Qin 2 Pro flashlight driver.
 *
 * The stock DT ships a "flash-ic@63" node (sprd,flash-ocp8137 family:
 * OCP8137 / AW3641 / WD3124DA share i2c addr 0x63 and the same EN-pin
 * scheme) under i2c@70600000 (i2c1).
 *
 * The torch LED is driven either by the boost IC or directly by the
 * flash-*-en GPIOs through a transistor - both paths respond to the
 * same GPIOs (the pins are wired to the IC's HW-enable inputs on IC
 * variants, so asserting them is correct either way).  When the i2c
 * chip answers we also program its torch-current register; when the
 * camera bus is dead (unpowered sensors clamp SDA on this board) the
 * writes just fail and GPIO still does the job.
 *
 * Exposes led_classdev "flashlight" -> /sys/class/leds/flashlight.
 * The flash-sync GPIO is deliberately left alone (strobe timing only).
 */
#include <linux/module.h>
#include <linux/i2c.h>
#include <linux/gpio/consumer.h>
#include <linux/leds.h>
#include <linux/regulator/consumer.h>
#include <linux/of.h>
#include <linux/delay.h>

struct qin_flash {
	struct i2c_client *client;
	struct led_classdev cdev;
	struct gpio_desc *chip_en;
	struct gpio_desc *torch_en;
	struct gpio_desc *flash_en;
	struct regulator *vddio;
	struct regulator *vdda;
	bool have_ic;
};

/* OCP8137-style registers; AW3641/WD3124 use the same layout family. */
#define QIN_FLASH_REG_ENABLE	0x00
#define QIN_FLASH_REG_TCUR	0x02	/* torch current */
#define QIN_FLASH_MODE_TORCH	0x0c	/* hw torch enable on these ICs */

static int qin_flash_hw(struct qin_flash *f, int on)
{
	gpiod_set_value_cansleep(f->chip_en, 1);
	/* GPIO path: correct for dumb-LED and for the IC's HW torch-en. */
	gpiod_set_value_cansleep(f->torch_en, on);
	gpiod_set_value_cansleep(f->flash_en, 0);

	if (f->have_ic) {
		int ret;
		if (on) {
			i2c_smbus_write_byte_data(f->client, QIN_FLASH_REG_TCUR, 0x80);
			ret = i2c_smbus_write_byte_data(f->client,
					QIN_FLASH_REG_ENABLE, QIN_FLASH_MODE_TORCH);
		} else {
			ret = i2c_smbus_write_byte_data(f->client,
					QIN_FLASH_REG_ENABLE, 0x00);
		}
		if (ret)
			dev_dbg(&f->client->dev, "ic write failed: %d\n", ret);
	}
	if (!on)
		gpiod_set_value_cansleep(f->chip_en, 0);
	return 0;
}

static int qin_flash_brightness_set(struct led_classdev *cdev,
				    enum led_brightness brightness)
{
	struct qin_flash *f = container_of(cdev, struct qin_flash, cdev);
	return qin_flash_hw(f, brightness > 0);
}

static int qin_flash_probe(struct i2c_client *client,
			   const struct i2c_device_id *id)
{
	struct device *dev = &client->dev;
	struct qin_flash *flash;
	int ret, i, nonzero = 0;

	flash = devm_kzalloc(dev, sizeof(*flash), GFP_KERNEL);
	if (!flash)
		return -ENOMEM;
	flash->client = client;

	/* i2c pull-up / analog rails (optional - log only on failure). */
	flash->vddio = devm_regulator_get_optional(dev, "vddio");
	if (!IS_ERR(flash->vddio))
		regulator_enable(flash->vddio);
	flash->vdda = devm_regulator_get_optional(dev, "vdda");
	if (!IS_ERR(flash->vdda))
		regulator_enable(flash->vdda);

	flash->chip_en = devm_gpiod_get(dev, "flash-chip-en", GPIOD_OUT_LOW);
	if (IS_ERR(flash->chip_en))
		flash->chip_en = NULL;
	flash->torch_en = devm_gpiod_get(dev, "flash-torch-en", GPIOD_OUT_LOW);
	if (IS_ERR(flash->torch_en))
		flash->torch_en = NULL;
	flash->flash_en = devm_gpiod_get(dev, "flash-en", GPIOD_OUT_LOW);
	if (IS_ERR(flash->flash_en))
		flash->flash_en = NULL;

	/* wake the boost IC, then check whether i2c answers mean anything */
	gpiod_set_value_cansleep(flash->chip_en, 1);
	msleep(5);
	for (i = 0; i <= 0x0f; i++) {
		int v = i2c_smbus_read_byte_data(client, i);
		if (v > 0)
			nonzero = 1;
	}
	flash->have_ic = nonzero;
	dev_info(dev, "flash-ic probe: %s\n",
		 flash->have_ic ? "i2c chip present" :
		 "no usable i2c answer, GPIO drive only");

	flash->cdev.name = "flashlight";
	flash->cdev.brightness = LED_OFF;
	flash->cdev.max_brightness = 255;
	flash->cdev.brightness_set = qin_flash_brightness_set;
	ret = devm_led_classdev_register(dev, &flash->cdev);
	if (ret)
		dev_err(dev, "cannot register led\n");

	i2c_set_clientdata(client, flash);
	return ret;
}

static const struct of_device_id qin_flash_of_match[] = {
	{ .compatible = "sprd,flash-ocp8137" },
	{ }
};
MODULE_DEVICE_TABLE(of, qin_flash_of_match);

static const struct i2c_device_id qin_flash_id[] = {
	{ "ocp8137", 0 },
	{ }
};
MODULE_DEVICE_TABLE(i2c, qin_flash_id);

static struct i2c_driver qin_flash_driver = {
	.driver = {
		.name = "qin_flash",
		.of_match_table = qin_flash_of_match,
	},
	.probe = qin_flash_probe,
	.id_table = qin_flash_id,
};
module_i2c_driver(qin_flash_driver);

MODULE_DESCRIPTION("Qin 2 Pro flashlight (OCP8137/AW3641 or GPIO) driver");
MODULE_LICENSE("GPL");
