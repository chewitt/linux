// SPDX-License-Identifier: GPL-2.0
/*
 * Auxiliary Display Driver for TM16XX and compatible LED controllers
 *
 * Copyright (C) 2024 Jean-François Lessard
 *
 * This driver supports various LED controller chips, including TM16XX family,
 * FD6XX family, PT6964, and HBS658. It provides support for both I2C and SPI interfaces.
 */

#include <linux/bitfield.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/slab.h>
#include <linux/i2c.h>
#include <linux/spi/spi.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/leds.h>
#include <linux/delay.h>
#include <linux/workqueue.h>
#include <linux/map_to_7segment.h>

#define TM16XX_DRIVER_NAME "tm16xx"
#define TM16XX_DEVICE_NAME "display"
#define DIGIT_SEGMENTS 7
#define MIN_SEGMENT 0
#define MAX_SEGMENT 7 /* data stored as 8 bits (u8) */

/* Common bit field definitions */
// clang-format off
/* Command type bits (bits 7-6) */
#define TM16XX_CMD_TYPE_MASK    GENMASK(7, 6)
#define TM16XX_CMD_MODE         0
#define TM16XX_CMD_DATA         BIT(6)
#define TM16XX_CMD_CTRL         BIT(7)
#define TM16XX_CMD_ADDR         (BIT(7) | BIT(6))

/* Mode command grid settings (bits 1-0) */
#define TM16XX_MODE_GRID_MASK   GENMASK(1, 0)
#define TM16XX_MODE_4GRIDS      0
#define TM16XX_MODE_5GRIDS      BIT(0)
#define TM16XX_MODE_6GRIDS      BIT(1)
#define TM16XX_MODE_7GRIDS      (BIT(1) | BIT(0))

/* Data command settings */
#define TM16XX_DATA_ADDR_MASK   BIT(2)
#define TM16XX_DATA_ADDR_AUTO   0
#define TM16XX_DATA_ADDR_FIXED  BIT(2)
#define TM16XX_DATA_MODE_MASK   GENMASK(1, 0)
#define TM16XX_DATA_WRITE       0
#define TM16XX_DATA_READ        BIT(1)

/* Control command settings */
#define TM16XX_CTRL_ON          BIT(3)
#define TM16XX_CTRL_BR_MASK     GENMASK(2, 0)

/* TM1618 specific constants */
#define TM1618_BYTE1_MASK       GENMASK(4, 0)
#define TM1618_BYTE1_RSHIFT     0
#define TM1618_BYTE2_MASK       (~TM1618_BYTE1_MASK)
#define TM1618_BYTE2_RSHIFT     2

/* I2C controller addresses and control settings */
#define TM1650_CMD_CTRL         0x48
#define TM1650_CMD_ADDR         0x68
#define TM1650_CTRL_BR_MASK     GENMASK(6, 4)
#define TM1650_CTRL_ON          BIT(0)
#define TM1650_CTRL_SEG_MASK    BIT(3)
#define TM1650_CTRL_SEG8_MODE   0
#define TM1650_CTRL_SEG7_MODE   BIT(3)

#define FD655_CMD_CTRL          0x48
#define FD655_CMD_ADDR          0x66
#define FD655_CTRL_BR_MASK      GENMASK(6, 5)
#define FD655_CTRL_ON           BIT(0)

#define FD6551_CMD_CTRL         0x48
#define FD6551_CTRL_BR_MASK     GENMASK(3, 1)
#define FD6551_CTRL_ON          BIT(0)

#define TM16XX_CTRL_BRIGHTNESS(enabled, value, prefix) \
	((enabled) ? \
	 (FIELD_PREP(prefix##_CTRL_BR_MASK, (value)) | prefix##_CTRL_ON) : \
	 0)
// clang-format on

static char *default_value;
module_param(default_value, charp, 0644);
MODULE_PARM_DESC(default_value, "Default display value to initialize");

static SEG7_CONVERSION_MAP(map_seg7, MAP_ASCII7SEG_ALPHANUM);

/* Forward declarations */
struct tm16xx_display;

/**
 * DOC: struct tm16xx_controller - Controller-specific operations
 * @max_grids: Maximum number of grids supported by the controller
 * @max_brightness: Maximum brightness level supported by the controller
 * @init: Configures the controller mode and brightness
 * @data: Writes display data to the controller
 *
 * This structure holds function pointers for controller-specific operations.
 */
struct tm16xx_controller {
	const u8 max_grids;
	const u8 max_brightness;
	int (* const init)(struct tm16xx_display *display);
	int (* const data)(struct tm16xx_display *display, u8 index, u8 data);
};

/**
 * struct tm16xx_led - LED information
 * @cdev: LED class device
 * @grid: Grid index of the LED
 * @segment: Segment index of the LED
 */
struct tm16xx_led {
	struct led_classdev cdev;
	u8 grid;
	u8 segment;
};

/**
 * struct tm16xx_digit - Digit information
 * @grid: Grid index of the digit
 * @value: Current value of the digit
 */
struct tm16xx_digit {
	u8 grid;
	char value;
};

/**
 * struct tm16xx_display - Main driver structure
 * @dev: Pointer to device structure
 * @controller: Pointer to controller-specific operations
 * @client: Union of I2C and SPI client structures
 * @main_led: LED class device for the entire display
 * @leds: Array of individual LEDs
 * @num_leds: Number of LEDs
 * @digits: Array of digits
 * @num_digits: Number of digits
 * @segment_mapping: Segment mapping array
 * @digit_bitmask: Bitmask for setting digit values
 * @display_data: Display data buffer
 * @display_data_len: Length of display data buffer
 * @lock: Mutex for concurrent access protection
 * @flush_init: Work structure for brightness update
 * @flush_display: Work structure for display update
 * @flush_status: Result of the last flush work
 * @transpose_display_data: Flag indicating if segments and grids should be transposed when writing data
 */
struct tm16xx_display {
	struct device *dev;
	const struct tm16xx_controller *controller;
	union {
		struct i2c_client *i2c;
		struct spi_device *spi;
	} client;
	struct led_classdev main_led;
	struct tm16xx_led *leds;
	int num_leds;
	struct tm16xx_digit *digits;
	int num_digits;
	u8 segment_mapping[DIGIT_SEGMENTS];
	u8 digit_bitmask;
	u8 *display_data;
	size_t display_data_len;
	struct mutex lock; /* protect shared display state in work functions */
	struct work_struct flush_init;
	struct work_struct flush_display;
	int flush_status;
	bool transpose_display_data;
};

/**
 * tm16xx_i2c_write - Write data to I2C client
 * @display: Pointer to tm16xx_display structure
 * @data: Data to write
 * @len: Length of data
 *
 * Return: Number of bytes written or negative error code
 */
static int tm16xx_i2c_write(struct tm16xx_display *display, u8 *data, size_t len)
{
	dev_dbg(display->dev, "i2c_write %*ph", (char)len, data);

	struct i2c_msg msg = {
		.addr = data[0] >> 1,
		.flags = 0,
		.len = len - 1,
		.buf = &data[1],
	};
	int ret;

	ret = i2c_transfer(display->client.i2c->adapter, &msg, 1);
	if (ret < 0)
		return ret;

	return (ret == 1) ? len : -EIO;
}

/**
 * tm16xx_spi_write - Write data to SPI client
 * @display: Pointer to tm16xx_display structure
 * @data: Data to write
 * @len: Length of data
 *
 * Return: Number of bytes written or negative error code
 */
static int tm16xx_spi_write(struct tm16xx_display *display, u8 *data, size_t len)
{
	dev_dbg(display->dev, "spi_write %*ph", (char)len, data);

	struct spi_device *spi = display->client.spi;

	return spi_write(spi, data, len);
}

/* Controller-specific functions */
static int tm1628_init(struct tm16xx_display *display)
{
	const enum led_brightness brightness = display->main_led.brightness;
	const u8 num_grids = display->transpose_display_data ? DIGIT_SEGMENTS :
							       display->display_data_len;
	u8 cmd;
	int ret;

	/* Set mode command based on grid count */
	cmd = TM16XX_CMD_MODE;
	if (num_grids <= 4)
		cmd |= TM16XX_MODE_4GRIDS;
	else if (num_grids == 5)
		cmd |= TM16XX_MODE_5GRIDS;
	else if (num_grids == 6)
		cmd |= TM16XX_MODE_6GRIDS;
	else
		cmd |= TM16XX_MODE_7GRIDS;

	ret = tm16xx_spi_write(display, &cmd, 1);
	if (ret < 0)
		return ret;

	/* Set data command */
	cmd = TM16XX_CMD_DATA | TM16XX_DATA_ADDR_AUTO | TM16XX_DATA_WRITE;
	ret = tm16xx_spi_write(display, &cmd, 1);
	if (ret < 0)
		return ret;

	/* Set control command with brightness */
	cmd = TM16XX_CMD_CTRL |
	      TM16XX_CTRL_BRIGHTNESS(brightness, brightness - 1, TM16XX);
	ret = tm16xx_spi_write(display, &cmd, 1);
	if (ret < 0)
		return ret;

	return 0;
}

static int tm1618_data(struct tm16xx_display *display, u8 index, u8 data)
{
	u8 cmds[3];

	cmds[0] = TM16XX_CMD_ADDR + index * 2;
	cmds[1] = (data & TM1618_BYTE1_MASK) >> TM1618_BYTE1_RSHIFT;
	cmds[2] = (data & TM1618_BYTE2_MASK) >> TM1618_BYTE2_RSHIFT;

	return tm16xx_spi_write(display, cmds, ARRAY_SIZE(cmds));
}

static int tm1628_data(struct tm16xx_display *display, u8 index, u8 data)
{
	u8 cmds[3];

	cmds[0] = TM16XX_CMD_ADDR + index * 2;
	cmds[1] = data; // SEG 1 to 8
	cmds[2] = 0; // SEG 9 to 14

	return tm16xx_spi_write(display, cmds, ARRAY_SIZE(cmds));
}

static int tm1650_init(struct tm16xx_display *display)
{
	u8 cmds[2];
	const enum led_brightness brightness = display->main_led.brightness;

	cmds[0] = TM1650_CMD_CTRL;
	cmds[1] = TM16XX_CTRL_BRIGHTNESS(brightness, brightness, TM1650) |
		  TM1650_CTRL_SEG8_MODE;

	return tm16xx_i2c_write(display, cmds, ARRAY_SIZE(cmds));
}

static int tm1650_data(struct tm16xx_display *display, u8 index, u8 data)
{
	u8 cmds[2];

	cmds[0] = TM1650_CMD_ADDR + index * 2;
	cmds[1] = data; // SEG 1 to 8

	return tm16xx_i2c_write(display, cmds, ARRAY_SIZE(cmds));
}

static int fd655_init(struct tm16xx_display *display)
{
	u8 cmds[2];
	const enum led_brightness brightness = display->main_led.brightness;

	cmds[0] = FD655_CMD_CTRL;
	cmds[1] = TM16XX_CTRL_BRIGHTNESS(brightness, brightness % 3, FD655);

	return tm16xx_i2c_write(display, cmds, ARRAY_SIZE(cmds));
}

static int fd655_data(struct tm16xx_display *display, u8 index, u8 data)
{
	u8 cmds[2];

	cmds[0] = FD655_CMD_ADDR + index * 2;
	cmds[1] = data; // SEG 1 to 8

	return tm16xx_i2c_write(display, cmds, ARRAY_SIZE(cmds));
}

static int fd6551_init(struct tm16xx_display *display)
{
	u8 cmds[2];
	const enum led_brightness brightness = display->main_led.brightness;

	cmds[0] = FD6551_CMD_CTRL;
	cmds[1] = TM16XX_CTRL_BRIGHTNESS(brightness, ~(brightness - 1), FD6551);

	return tm16xx_i2c_write(display, cmds, ARRAY_SIZE(cmds));
}

static void hbs658_swap_nibbles(u8 *data, size_t len)
{
	for (size_t i = 0; i < len; i++)
		data[i] = (data[i] << 4) | (data[i] >> 4);
}

static int hbs658_init(struct tm16xx_display *display)
{
	const enum led_brightness brightness = display->main_led.brightness;
	u8 cmd;
	int ret;

	/* Set data command */
	cmd = TM16XX_CMD_DATA | TM16XX_DATA_ADDR_AUTO | TM16XX_DATA_WRITE;
	hbs658_swap_nibbles(&cmd, 1);
	ret = tm16xx_spi_write(display, &cmd, 1);
	if (ret < 0)
		return ret;

	/* Set control command with brightness */
	cmd = TM16XX_CMD_CTRL |
	      TM16XX_CTRL_BRIGHTNESS(brightness, brightness - 1, TM16XX);
	hbs658_swap_nibbles(&cmd, 1);
	ret = tm16xx_spi_write(display, &cmd, 1);
	if (ret < 0)
		return ret;

	return 0;
}

static int hbs658_data(struct tm16xx_display *display, u8 index, u8 data)
{
	u8 cmds[2];

	cmds[0] = TM16XX_CMD_ADDR + index * 2;
	cmds[1] = data;

	hbs658_swap_nibbles(cmds, ARRAY_SIZE(cmds));
	return tm16xx_spi_write(display, cmds, ARRAY_SIZE(cmds));
}

/* Controller definitions */
static const struct tm16xx_controller tm1618_controller = {
	.max_grids = 7,
	.max_brightness = 8,
	.init = tm1628_init,
	.data = tm1618_data,
};

static const struct tm16xx_controller tm1628_controller = {
	.max_grids = 7,
	.max_brightness = 8,
	.init = tm1628_init,
	.data = tm1628_data,
};

static const struct tm16xx_controller tm1650_controller = {
	.max_grids = 4,
	.max_brightness = 8,
	.init = tm1650_init,
	.data = tm1650_data,
};

static const struct tm16xx_controller fd655_controller = {
	.max_grids = 5,
	.max_brightness = 3,
	.init = fd655_init,
	.data = fd655_data,
};

static const struct tm16xx_controller fd6551_controller = {
	.max_grids = 5,
	.max_brightness = 8,
	.init = fd6551_init,
	.data = fd655_data,
};

static const struct tm16xx_controller hbs658_controller = {
	.max_grids = 5,
	.max_brightness = 8,
	.init = hbs658_init,
	.data = hbs658_data,
};

/**
 * tm16xx_ascii_to_segments - Convert ASCII character to segment pattern
 * @display: Pointer to tm16xx_display structure
 * @c: ASCII character to convert
 *
 * Return: Segment pattern for the given ASCII character
 */
static u8 tm16xx_ascii_to_segments(struct tm16xx_display *display, char c)
{
	u8 standard_segments, mapped_segments = 0;
	int i;

	standard_segments = map_to_seg7(&map_seg7, c);

	for (i = 0; i < DIGIT_SEGMENTS; i++) {
		if (standard_segments & BIT(i))
			mapped_segments |= BIT(display->segment_mapping[i]);
	}

	return mapped_segments;
}

/**
 * tm16xx_display_flush_init - Work function to update controller configuration (mode and brightness)
 * @work: Pointer to work_struct
 */
static void tm16xx_display_flush_init(struct work_struct *work)
{
	struct tm16xx_display *display =
		container_of(work, struct tm16xx_display, flush_init);
	int ret;

	if (display->controller->init) {
		mutex_lock(&display->lock);
		ret = display->controller->init(display);
		display->flush_status = ret;
		mutex_unlock(&display->lock);
		if (ret < 0)
			dev_err(display->dev, "Failed to set brightness: %d\n", ret);
	}
}

/**
 * tm16xx_display_flush_data - Work function to update display data
 * @work: Pointer to work_struct
 */
static void tm16xx_display_flush_data(struct work_struct *work)
{
	struct tm16xx_display *display =
		container_of(work, struct tm16xx_display, flush_display);
	int i, ret = 0;

	mutex_lock(&display->lock);

	if (display->controller->data) {
		for (i = 0; i < display->display_data_len; i++) {
			ret = display->controller->data(display, i,
							display->display_data[i]);
			if (ret < 0) {
				dev_err(display->dev,
					"Failed to write display data: %d\n", ret);
				break;
			}
		}
	}

	display->flush_status = ret;
	mutex_unlock(&display->lock);
}

/**
 * tm16xx_display_flush_data_transposed - Transposed flush work function
 * @work: Pointer to work_struct
 */
static void tm16xx_display_flush_data_transposed(struct work_struct *work)
{
	struct tm16xx_display *display =
		container_of(work, struct tm16xx_display, flush_display);
	int i, j, ret = 0;
	u8 transposed_data;

	mutex_lock(&display->lock);

	if (display->controller->data) {
		/* Write operations based on number of segments */
		for (i = MIN_SEGMENT; i <= MAX_SEGMENT; i++) {
			/* Gather bits from each grid for this segment */
			transposed_data = 0;
			for (j = 0; j < display->display_data_len; j++) {
				if (display->display_data[j] & BIT(i))
					transposed_data |= BIT(MAX_SEGMENT - j);
			}

			ret = display->controller->data(display, i, transposed_data);
			if (ret < 0) {
				dev_err(display->dev,
					"Failed to write transposed data: %d\n", ret);
				break;
			}
		}
	}

	display->flush_status = ret;
	mutex_unlock(&display->lock);
}

/**
 * tm16xx_display_remove - Remove the display
 * @display: Pointer to tm16xx_display structure
 */
static void tm16xx_display_remove(struct tm16xx_display *display)
{
	memset(display->display_data, 0x00, display->display_data_len);
	schedule_work(&display->flush_display);
	flush_work(&display->flush_display);

	display->main_led.brightness = LED_OFF;
	schedule_work(&display->flush_init);
	flush_work(&display->flush_init);

	dev_info(display->dev, "Display turned off\n");
}

/**
 * tm16xx_brightness_set - Set brightness of the display
 * @led_cdev: Pointer to led_classdev
 * @brightness: Brightness value to set
 */
static void tm16xx_brightness_set(struct led_classdev *led_cdev,
				  enum led_brightness brightness)
{
	struct tm16xx_display *display = dev_get_drvdata(led_cdev->dev->parent);

	led_cdev->brightness = brightness;
	schedule_work(&display->flush_init);
}

/**
 * tm16xx_led_set - Set state of an individual LED
 * @led_cdev: Pointer to led_classdev
 * @value: Value to set (on/off)
 */
static void tm16xx_led_set(struct led_classdev *led_cdev, enum led_brightness value)
{
	struct tm16xx_led *led = container_of(led_cdev, struct tm16xx_led, cdev);
	struct tm16xx_display *display = dev_get_drvdata(led_cdev->dev->parent);

	if (value)
		display->display_data[led->grid] |= (1 << led->segment);
	else
		display->display_data[led->grid] &= ~(1 << led->segment);

	schedule_work(&display->flush_display);
}

/**
 * tm16xx_value_show - Show current display value
 * @dev: Pointer to device structure
 * @attr: Pointer to device attribute structure
 * @buf: Buffer to write the display value
 *
 * Return: Number of bytes written to buffer
 */
static ssize_t tm16xx_value_show(struct device *dev, struct device_attribute *attr,
				 char *buf)
{
	struct led_classdev *led_cdev = dev_get_drvdata(dev);
	struct tm16xx_display *display = dev_get_drvdata(led_cdev->dev->parent);
	int i;

	for (i = 0; i < display->num_digits && i < PAGE_SIZE - 1; i++)
		buf[i] = display->digits[i].value;

	buf[i++] = '\n';

	return i;
}

/**
 * tm16xx_value_store - Store new display value
 * @dev: Pointer to device structure
 * @attr: Pointer to device attribute structure
 * @buf: Buffer containing the new display value
 * @count: Number of bytes in buffer
 *
 * Return: Number of bytes written or negative error code
 */
static ssize_t tm16xx_value_store(struct device *dev, struct device_attribute *attr,
				  const char *buf, size_t count)
{
	struct led_classdev *led_cdev = dev_get_drvdata(dev);
	struct tm16xx_display *display = dev_get_drvdata(led_cdev->dev->parent);
	struct tm16xx_digit *digit;
	int i;
	u8 data;

	for (i = 0; i < display->num_digits; i++) {
		digit = &display->digits[i];

		if (i < count && buf[i] != '\n') {
			digit->value = buf[i];
			data = tm16xx_ascii_to_segments(display, digit->value);
		} else {
			digit->value = 0;
			data = 0;
		}

		display->display_data[digit->grid] =
			(display->display_data[digit->grid] & ~display->digit_bitmask) |
			(data & display->digit_bitmask);
	}

	schedule_work(&display->flush_display);

	return count;
}

/**
 * tm16xx_num_digits_show - Show the number of digits in the display
 * @dev: The device struct
 * @attr: The device_attribute struct
 * @buf: The output buffer
 *
 * This function returns the number of digits in the display.
 *
 * Return: Number of bytes written to buf
 */
static ssize_t tm16xx_num_digits_show(struct device *dev, struct device_attribute *attr,
				      char *buf)
{
	struct led_classdev *led_cdev = dev_get_drvdata(dev);
	struct tm16xx_display *display = dev_get_drvdata(led_cdev->dev->parent);

	return sprintf(buf, "%d\n", display->num_digits);
}

static int tm16xx_parse_int_array(const char *buf, int **array)
{
	int *values, value, count = 0, len;
	const char *ptr = buf;

	while (sscanf(ptr, "%d %n", &value, &len) == 1) {
		count++;
		ptr += len;
	}

	if (count == 0) {
		*array = NULL;
		return 0;
	}

	values = kmalloc(count * sizeof(*values), GFP_KERNEL);
	if (!values)
		return -ENOMEM;

	ptr = buf;
	count = 0;
	while (sscanf(ptr, "%d %n", &value, &len) == 1) {
		values[count++] = value;
		ptr += len;
	}

	*array = values;
	return count;
}

/**
 * tm16xx_segments_show - Show the current segment mapping
 * @dev: The device struct
 * @attr: The device_attribute struct
 * @buf: The output buffer
 *
 * This function returns the current segment mapping of the display.
 *
 * Return: Number of bytes written to buf
 */
static ssize_t tm16xx_segments_show(struct device *dev, struct device_attribute *attr,
				    char *buf)
{
	struct led_classdev *led_cdev = dev_get_drvdata(dev);
	struct tm16xx_display *display = dev_get_drvdata(led_cdev->dev->parent);
	int i, count = 0;

	for (i = 0; i < DIGIT_SEGMENTS; i++)
		count += sprintf(buf + count, "%d ", display->segment_mapping[i]);

	count += sprintf(buf + count, "\n");

	return count;
}

/**
 * tm16xx_segments_store - Set a new segment mapping
 * @dev: The device struct
 * @attr: The device_attribute struct
 * @buf: The input buffer
 * @count: Number of bytes in buf
 *
 * This function sets a new segment mapping for the display.
 * It validates that each value is within the valid range, and that
 * the number of received values matches the number of segments.
 *
 * Return: count on success, negative errno on failure
 */
static ssize_t tm16xx_segments_store(struct device *dev, struct device_attribute *attr,
				     const char *buf, size_t count)
{
	struct led_classdev *led_cdev = dev_get_drvdata(dev);
	struct tm16xx_display *display = dev_get_drvdata(led_cdev->dev->parent);
	int *array, ret, i;

	ret = tm16xx_parse_int_array(buf, &array);
	if (ret < 0)
		return ret;

	if (ret != DIGIT_SEGMENTS) {
		kfree(array);
		return -EINVAL;
	}

	for (i = 0; i < DIGIT_SEGMENTS; i++) {
		if (array[i] < MIN_SEGMENT || array[i] > MAX_SEGMENT) {
			kfree(array);
			return -EINVAL;
		}
	}

	display->digit_bitmask = 0;
	for (i = 0; i < DIGIT_SEGMENTS; i++) {
		display->segment_mapping[i] = (u8)array[i];
		display->digit_bitmask |= BIT(display->segment_mapping[i]);
	}

	kfree(array);
	return count;
}

/**
 * tm16xx_digits_show - Show the current digit ordering
 * @dev: The device struct
 * @attr: The device_attribute struct
 * @buf: The output buffer
 *
 * This function returns the current ordering of digits in the display.
 *
 * Return: Number of bytes written to buf
 */
static ssize_t tm16xx_digits_show(struct device *dev, struct device_attribute *attr,
				  char *buf)
{
	struct led_classdev *led_cdev = dev_get_drvdata(dev);
	struct tm16xx_display *display = dev_get_drvdata(led_cdev->dev->parent);
	int i, count = 0;

	for (i = 0; i < display->num_digits; i++)
		count += sprintf(buf + count, "%d ", display->digits[i].grid);

	count += sprintf(buf + count, "\n");

	return count;
}

/**
 * tm16xx_digits_store - Set a new digit ordering
 * @dev: The device struct
 * @attr: The device_attribute struct
 * @buf: The input buffer
 * @count: Number of bytes in buf
 *
 * This function sets a new ordering of digits for the display.
 * It validates that all values match the original digit grid indexes,
 * and that the number of received values matches the number of digits.
 *
 * Return: count on success, negative errno on failure
 */
static ssize_t tm16xx_digits_store(struct device *dev, struct device_attribute *attr,
				   const char *buf, size_t count)
{
	struct led_classdev *led_cdev = dev_get_drvdata(dev);
	struct tm16xx_display *display = dev_get_drvdata(led_cdev->dev->parent);
	int *array, ret, i, j;
	bool found;

	ret = tm16xx_parse_int_array(buf, &array);
	if (ret < 0)
		return ret;

	if (ret != display->num_digits) {
		kfree(array);
		return -EINVAL;
	}

	for (i = 0; i < display->num_digits; i++) {
		found = false;

		for (j = 0; j < display->num_digits; j++) {
			if (display->digits[i].grid == array[j]) {
				found = true;
				break;
			}
		}

		if (!found) {
			kfree(array);
			return -EINVAL;
		}
	}

	for (i = 0; i < display->num_digits; i++)
		display->digits[i].grid = (u8)array[i];

	kfree(array);
	return count;
}

/**
 * tm16xx_map_seg7_show - Show the current 7-segment character map
 * @dev: The device struct
 * @attr: The device_attribute struct
 * @buf: The output buffer
 *
 * This function returns the current 7-segment character map.
 *
 * Return: Number of bytes written to buf
 */
static ssize_t tm16xx_map_seg7_show(struct device *dev, struct device_attribute *attr,
				    char *buf)
{
	memcpy(buf, &map_seg7, sizeof(map_seg7));
	return sizeof(map_seg7);
}

/**
 * tm16xx_map_seg7_store - Set a new 7-segment character map
 * @dev: The device struct
 * @attr: The device_attribute struct
 * @buf: The input buffer
 * @cnt: Number of bytes in buf
 *
 * This function sets a new 7-segment character map.
 *
 * Return: cnt on success, negative errno on failure
 */
static ssize_t tm16xx_map_seg7_store(struct device *dev, struct device_attribute *attr,
				     const char *buf, size_t cnt)
{
	if (cnt != sizeof(map_seg7))
		return -EINVAL;
	memcpy(&map_seg7, buf, cnt);
	return cnt;
}

// clang-format off
static DEVICE_ATTR(value, 0644, tm16xx_value_show, tm16xx_value_store);
static DEVICE_ATTR(num_digits, 0444, tm16xx_num_digits_show, NULL);
static DEVICE_ATTR(segments, 0644, tm16xx_segments_show, tm16xx_segments_store);
static DEVICE_ATTR(digits, 0644, tm16xx_digits_show, tm16xx_digits_store);
static DEVICE_ATTR(map_seg7, 0644, tm16xx_map_seg7_show, tm16xx_map_seg7_store);

static struct attribute *tm16xx_main_led_attrs[] = {
	&dev_attr_value.attr,
	&dev_attr_num_digits.attr,
	&dev_attr_segments.attr,
	&dev_attr_digits.attr,
	&dev_attr_map_seg7.attr,
	NULL,
};
ATTRIBUTE_GROUPS(tm16xx_main_led);
// clang-format on

/**
 * tm16xx_display_init - Initialize the display
 * @display: Pointer to tm16xx_display structure
 *
 * Return: 0 on success, negative error code on failure
 */
static int tm16xx_display_init(struct tm16xx_display *display)
{
	schedule_work(&display->flush_init);
	flush_work(&display->flush_init);
	if (display->flush_status < 0)
		return display->flush_status;

	if (default_value && strlen(default_value) > 0) {
		tm16xx_value_store(display->main_led.dev, NULL, default_value,
				   strlen(default_value));
	} else {
		memset(display->display_data, 0xFF, display->display_data_len);
		schedule_work(&display->flush_display);
		flush_work(&display->flush_display);
		memset(display->display_data, 0x00, display->display_data_len);
		if (display->flush_status < 0)
			return display->flush_status;
	}

	return 0;
}

/**
 * tm16xx_parse_dt - Parse device tree data
 * @dev: Pointer to device structure
 * @display: Pointer to tm16xx_display structure
 *
 * Return: 0 on success, negative error code on failure
 */
static int tm16xx_parse_dt(struct device *dev, struct tm16xx_display *display)
{
	struct fwnode_handle *child;
	int ret, i, max_grid = 0;
	u8 *digits;

	display->transpose_display_data =
		device_property_read_bool(dev, "titanmec,transposed");

	ret = device_property_count_u8(dev, "titanmec,digits");
	if (ret < 0) {
		dev_err(dev, "Failed to count 'titanmec,digits' property: %d\n", ret);
		return ret;
	}

	display->num_digits = ret;
	dev_dbg(dev, "Number of digits: %d\n", display->num_digits);

	digits = devm_kcalloc(dev, display->num_digits, sizeof(*digits), GFP_KERNEL);
	if (!digits)
		return -ENOMEM;

	ret = device_property_read_u8_array(dev, "titanmec,digits", digits,
					    display->num_digits);
	if (ret < 0) {
		dev_err(dev, "Failed to read 'titanmec,digits' property: %d\n", ret);
		return ret;
	}

	display->digits = devm_kcalloc(dev, display->num_digits, sizeof(*display->digits),
				       GFP_KERNEL);
	if (!display->digits)
		return -ENOMEM;

	for (i = 0; i < display->num_digits; i++) {
		if (digits[i] >= display->controller->max_grids) {
			dev_err(dev, "Digit grid %d exceeds controller max_grids %d\n",
				digits[i], display->controller->max_grids);
			return -EINVAL;
		}

		display->digits[i].grid = digits[i];
		max_grid = umax(max_grid, digits[i]);
	}

	devm_kfree(dev, digits);

	ret = device_property_read_u8_array(dev, "titanmec,segment-mapping",
					    display->segment_mapping, DIGIT_SEGMENTS);
	if (ret < 0) {
		dev_err(dev, "Failed to read 'titanmec,segment-mapping' property: %d\n",
			ret);
		return ret;
	}

	display->digit_bitmask = 0;
	for (i = 0; i < DIGIT_SEGMENTS; i++) {
		if (display->segment_mapping[i] < MIN_SEGMENT ||
		    display->segment_mapping[i] > MAX_SEGMENT) {
			dev_err(dev,
				"Invalid 'titanmec,segment-mapping' value: %d (must be between %d and %d)\n",
				display->segment_mapping[i], MIN_SEGMENT, MAX_SEGMENT);
			return -EINVAL;
		}

		display->digit_bitmask |= BIT(display->segment_mapping[i]);
	}

	display->num_leds = 0;
	device_for_each_child_node(dev, child) {
		u32 reg[2];

		ret = fwnode_property_read_u32_array(child, "reg", reg, 2);
		if (ret < 0) {
			dev_err(dev, "Failed to read 'reg' property of led node: %d\n",
				ret);
			fwnode_handle_put(child);
			return ret;
		}

		if (reg[0] >= display->controller->max_grids) {
			dev_err(dev, "LED grid %d exceeds controller max_grids %d\n",
				reg[0], display->controller->max_grids);
			fwnode_handle_put(child);
			return -EINVAL;
		}

		if (reg[1] < MIN_SEGMENT || reg[1] > MAX_SEGMENT) {
			dev_err(dev,
				"LED segment %d is invalid (must be between %d and %d)\n",
				reg[1], MIN_SEGMENT, MAX_SEGMENT);
			fwnode_handle_put(child);
			return -EINVAL;
		}

		max_grid = umax(max_grid, reg[0]);
		display->num_leds++;
	}

	dev_dbg(dev, "Number of LEDs: %d\n", display->num_leds);

	display->display_data_len = max_grid + 1;
	dev_dbg(dev, "Number of display grids: %zu\n", display->display_data_len);

	display->display_data = devm_kcalloc(dev, display->display_data_len,
					     sizeof(*display->display_data), GFP_KERNEL);
	if (!display->display_data)
		return -ENOMEM;

	return 0;
}

/**
 * tm16xx_probe - Probe function for tm16xx devices
 * @display: Pointer to tm16xx_display structure
 *
 * Return: 0 on success, negative error code on failure
 */
static int tm16xx_probe(struct tm16xx_display *display)
{
	struct device *dev = display->dev;
	struct fwnode_handle *child;
	int ret, i;

	ret = tm16xx_parse_dt(dev, display);
	if (ret < 0) {
		dev_err(dev, "Failed to parse device tree: %d\n", ret);
		return ret;
	}

	mutex_init(&display->lock);
	INIT_WORK(&display->flush_init, tm16xx_display_flush_init);

	/* Initialize work structure with appropriate flush function */
	if (display->transpose_display_data) {
		INIT_WORK(&display->flush_display, tm16xx_display_flush_data_transposed);
		dev_info(display->dev, "Operating in transposed mode\n");
	} else {
		INIT_WORK(&display->flush_display, tm16xx_display_flush_data);
	}

	display->main_led.name = TM16XX_DEVICE_NAME;
	display->main_led.brightness = display->controller->max_brightness;
	display->main_led.max_brightness = display->controller->max_brightness;
	display->main_led.brightness_set = tm16xx_brightness_set;
	display->main_led.groups = tm16xx_main_led_groups;
	display->main_led.flags = LED_RETAIN_AT_SHUTDOWN | LED_CORE_SUSPENDRESUME;

	ret = devm_led_classdev_register(dev, &display->main_led);
	if (ret < 0) {
		dev_err(dev, "Failed to register main LED: %d\n", ret);
		return ret;
	}

	display->leds =
		devm_kcalloc(dev, display->num_leds, sizeof(*display->leds), GFP_KERNEL);
	if (!display->leds)
		return -ENOMEM;

	i = 0;
	device_for_each_child_node(dev, child) {
		struct tm16xx_led *led = &display->leds[i];
		struct led_init_data led_init = {
			.fwnode = child,
			.devicename = display->main_led.name,
			.devname_mandatory = true,
		};
		u32 reg[2];

		ret = fwnode_property_read_u32_array(child, "reg", reg, 2);
		if (ret < 0) {
			fwnode_handle_put(child);
			dev_err(dev, "Failed to read LED reg property: %d\n", ret);
			return ret;
		}

		led->grid = reg[0];
		led->segment = reg[1];

		led->cdev.max_brightness = 1;
		led->cdev.brightness_set = tm16xx_led_set;
		led->cdev.flags = LED_RETAIN_AT_SHUTDOWN | LED_CORE_SUSPENDRESUME;

		ret = devm_led_classdev_register_ext(dev, &led->cdev, &led_init);
		if (ret < 0) {
			fwnode_handle_put(child);
			dev_err(dev, "Failed to register LED %s: %d\n", led->cdev.name,
				ret);
			return ret;
		}

		i++;
	}

	ret = tm16xx_display_init(display);
	if (ret < 0) {
		dev_err(display->dev, "Failed to initialize display: %d\n", ret);
		return ret;
	}

	dev_info(display->dev, "Display initialized successfully\n");
	return 0;
}

/* SPI specific code */
static int tm16xx_spi_probe(struct spi_device *spi)
{
	const struct tm16xx_controller *controller;
	struct tm16xx_display *display;
	int ret;

	controller = of_device_get_match_data(&spi->dev);
	if (!controller)
		return -EINVAL;

	display = devm_kzalloc(&spi->dev, sizeof(*display), GFP_KERNEL);
	if (!display)
		return -ENOMEM;

	display->client.spi = spi;
	display->dev = &spi->dev;
	display->controller = controller;

	spi_set_drvdata(spi, display);

	ret = tm16xx_probe(display);
	if (ret)
		return ret;

	return 0;
}

static void tm16xx_spi_remove(struct spi_device *spi)
{
	struct tm16xx_display *display = spi_get_drvdata(spi);

	tm16xx_display_remove(display);
}

// clang-format off
static const struct of_device_id tm16xx_spi_of_match[] = {
	{ .compatible = "titanmec,tm1618", .data = &tm1618_controller },
	{ .compatible = "titanmec,tm1620", .data = &tm1628_controller },
	{ .compatible = "titanmec,tm1628", .data = &tm1628_controller },
	{ .compatible = "fdhisi,fd620", .data = &tm1628_controller },
	{ .compatible = "fdhisi,fd628", .data = &tm1628_controller },
	{ .compatible = "icore,aip1618", .data = &tm1618_controller },
	{ .compatible = "icore,aip1628", .data = &tm1628_controller },
	{ .compatible = "princeton,pt6964", .data = &tm1628_controller },
	{ .compatible = "winrise,hbs658", .data = &hbs658_controller },
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, tm16xx_spi_of_match);

static const struct spi_device_id tm16xx_spi_id[] = {
	{ "tm1618", 0 },
	{ "tm1620", 0 },
	{ "tm1628", 0 },
	{ "fd620", 0 },
	{ "fd628", 0 },
	{ "aip1618", 0 },
	{ "aip1628", 0 },
	{ "pt6964", 0 },
	{ "hbs658", 0 },
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(spi, tm16xx_spi_id);
// clang-format on

static struct spi_driver tm16xx_spi_driver = {
	.driver = {
		.name = TM16XX_DRIVER_NAME,
		.of_match_table = tm16xx_spi_of_match,
	},
	.probe = tm16xx_spi_probe,
	.remove = tm16xx_spi_remove,
	.shutdown = tm16xx_spi_remove,
	.id_table = tm16xx_spi_id,
};

/* I2C specific code */
static int tm16xx_i2c_probe(struct i2c_client *client)
{
	const struct tm16xx_controller *controller;
	struct tm16xx_display *display;
	int ret;

	controller = of_device_get_match_data(&client->dev);
	if (!controller)
		return -EINVAL;

	display = devm_kzalloc(&client->dev, sizeof(*display), GFP_KERNEL);
	if (!display)
		return -ENOMEM;

	display->client.i2c = client;
	display->dev = &client->dev;
	display->controller = controller;

	i2c_set_clientdata(client, display);

	ret = tm16xx_probe(display);
	if (ret)
		return ret;

	return 0;
}

static void tm16xx_i2c_remove(struct i2c_client *client)
{
	struct tm16xx_display *display = i2c_get_clientdata(client);

	tm16xx_display_remove(display);
}

// clang-format off
static const struct of_device_id tm16xx_i2c_of_match[] = {
	{ .compatible = "titanmec,tm1650", .data = &tm1650_controller },
	{ .compatible = "icore,aip650", .data = &tm1650_controller },
	{ .compatible = "fdhisi,fd650", .data = &tm1650_controller },
	{ .compatible = "fdhisi,fd6551", .data = &fd6551_controller },
	{ .compatible = "fdhisi,fd655", .data = &fd655_controller },
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, tm16xx_i2c_of_match);

static const struct i2c_device_id tm16xx_i2c_id[] = {
	{ "tm1650", 0 },
	{ "aip650", 0 },
	{ "fd650", 0 },
	{ "fd6551", 0 },
	{ "fd655", 0 },
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(i2c, tm16xx_i2c_id);
// clang-format on

static struct i2c_driver tm16xx_i2c_driver = {
	.driver = {
		.name = TM16XX_DRIVER_NAME,
		.of_match_table = tm16xx_i2c_of_match,
	},
	.probe = tm16xx_i2c_probe,
	.remove = tm16xx_i2c_remove,
	.shutdown = tm16xx_i2c_remove,
	.id_table = tm16xx_i2c_id,
};

static int __init tm16xx_init(void)
{
	int ret;

	ret = spi_register_driver(&tm16xx_spi_driver);
	if (ret)
		return ret;

	ret = i2c_add_driver(&tm16xx_i2c_driver);
	if (ret) {
		spi_unregister_driver(&tm16xx_spi_driver);
		return ret;
	}

	return 0;
}

static void __exit tm16xx_exit(void)
{
	i2c_del_driver(&tm16xx_i2c_driver);
	spi_unregister_driver(&tm16xx_spi_driver);
}

module_init(tm16xx_init);
module_exit(tm16xx_exit);

MODULE_AUTHOR("Jean-François Lessard");
MODULE_DESCRIPTION("TM16XX Compatible LED Display Controllers");
MODULE_LICENSE("GPL");
MODULE_ALIAS("spi:tm16xx");
MODULE_ALIAS("i2c:tm16xx");
