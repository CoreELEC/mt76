// SPDX-License-Identifier: ISC
/* Copyright (C) 2026 signde */

#include <linux/firmware.h>
#include <linux/of.h>
#include <linux/sizes.h>
#include "mt7921.h"

#define MT7921_POWER_CHANNELS 234
#define MT7921_POWER_FILE_MAX SZ_1M

struct mt7921_power_table {
	const struct firmware *fw[2];
	s8 sku[2][MT7921_POWER_CHANNELS][MT7921_SKU_RATE_NUM];
	u8 alpha2[2];
	bool valid;
};

static const struct {
	const char *name;
	u8 offset;
	u8 count;
} mt7921_power_sections[] = {
	{ "cck", 0, 4 },
	{ "ofdm", 4, 8 },
	{ "ht20", 12, 8 },
	{ "ht40", 20, 9 },
	{ "vht20", 29, 10 },
	{ "vht40", 41, 10 },
	{ "vht80", 53, 10 },
	{ "vht160", 65, 10 },
	{ "ru26", 77, 12 },
	{ "ru52", 89, 12 },
	{ "ru106", 101, 12 },
	{ "ru242", 113, 12 },
	{ "ru484", 125, 12 },
	{ "ru996", 137, 12 },
	{ "ru996x2", 149, 12 },
};

static int mt7921_power_country(char *line, const u8 *alpha2)
{
	char *country, *end;
	bool match = false;

	end = strchr(line, ']');
	if (!end || end[1])
		return -EINVAL;
	*end = '\0';
	line++;
	while ((country = strsep(&line, ","))) {
		country = strim(country);
		if (strlen(country) != 2 ||
		    !((country[0] >= 'A' && country[0] <= 'Z' &&
		       country[1] >= 'A' && country[1] <= 'Z') ||
		      !strcmp(country, "00")))
			return -EINVAL;
		if (!memcmp(country, alpha2, 2))
			match = true;
	}

	return match;
}

static int mt7921_power_row(char *line, int section, bool band6,
			    s8 sku[][MT7921_SKU_RATE_NUM], bool store)
{
	char *token = strsep(&line, ",");
	s8 values[12];
	unsigned int channel;
	int i, count = mt7921_power_sections[section].count;

	if (strlen(token) != 5 || strncmp(token, "ch", 2) ||
	    kstrtouint(token + 2, 10, &channel) ||
	    !channel || channel >= MT7921_POWER_CHANNELS ||
	    (!band6 && channel > 177))
		return -EINVAL;

	for (i = 0; i < count; i++) {
		int value;

		token = strsep(&line, ",");
		if (!token)
			return -EINVAL;
		token = strim(token);
		if (!strcmp(token, "x")) {
			values[i] = S8_MAX;
			continue;
		}
		if (kstrtoint(token, 10, &value) ||
		    value < S8_MIN || value > S8_MAX)
			return -EINVAL;
		values[i] = value;
	}
	if (line)
		return -EINVAL;
	if (store)
		memcpy(sku[channel] + mt7921_power_sections[section].offset,
		       values, count);

	return channel;
}

static int mt7921_power_parse(const struct firmware *fw, const u8 *alpha2,
			      bool band6, s8 sku[][MT7921_SKU_RATE_NUM])
{
	unsigned long channels[BITS_TO_LONGS(MT7921_POWER_CHANNELS)];
	char *buf, *cursor, *line;
	bool version = false, country = false, active = false, found = false;
	u16 required = band6 ? BIT(1) | GENMASK(13, 8) :
			       GENMASK(13, 0) & ~BIT(7);
	u16 sections = 0;
	int section = -1, rows = 0, ret = -EINVAL;

	if (!fw->size || fw->size > MT7921_POWER_FILE_MAX ||
	    memchr(fw->data, '\0', fw->size))
		return -EINVAL;
	buf = kmemdup_nul(fw->data, fw->size, GFP_KERNEL);
	if (!buf)
		return -ENOMEM;
	cursor = buf;
	memset(sku, S8_MAX, MT7921_POWER_CHANNELS * MT7921_SKU_RATE_NUM);

	while ((line = strsep(&cursor, "\n"))) {
		char *comment = strchr(line, '#');
		int i;

		if (comment)
			*comment = '\0';
		line = strim(line);
		if (!*line)
			continue;
		if (!version) {
			if (strcmp(line, "<Ver:02>"))
				goto out;
			version = true;
			continue;
		}
		if (*line == '[') {
			if (section >= 0 || (country && (sections & required) != required))
				goto out;
			i = mt7921_power_country(line, alpha2);
			if (i < 0 || (i && found))
				goto out;
			active = i;
			found |= active;
			country = true;
			sections = 0;
			continue;
		}
		if (!country)
			goto out;
		if (!strncmp(line, "</", 2)) {
			size_t len = strlen(line);

			if (section < 0 || !rows || len < 4 ||
			    line[len - 1] != '>')
				goto out;
			line[len - 1] = '\0';
			if (strcmp(line + 2, mt7921_power_sections[section].name))
				goto out;
			section = -1;
			continue;
		}
		if (*line == '<') {
			char *comma = strchr(line, ',');
			size_t len = strlen(line);

			if (section >= 0 || !comma || line[len - 1] != '>')
				goto out;
			*comma = '\0';
			for (i = 0; i < ARRAY_SIZE(mt7921_power_sections); i++)
				if (!strcmp(strim(line + 1), mt7921_power_sections[i].name))
					break;
			if (i == ARRAY_SIZE(mt7921_power_sections) ||
			    (sections & BIT(i)) ||
			    (band6 && i != 1 && i < 8))
				goto out;
			section = i;
			sections |= BIT(i);
			bitmap_zero(channels, MT7921_POWER_CHANNELS);
			rows = 0;
			continue;
		}
		if (section < 0)
			goto out;
		i = mt7921_power_row(line, section, band6, sku, active);
		if (i < 0 || test_and_set_bit(i, channels))
			goto out;
		rows++;
	}
	if (version && country && (sections & required) == required && section < 0)
		ret = found ? 0 : -ENOENT;
out:
	kfree(buf);
	return ret;
}

static void mt7921_power_release(void *data)
{
	release_firmware(data);
}

int mt7921_load_power_limits(struct mt7921_dev *dev)
{
	static const char * const files[] = {
		"mediatek/TxPwrLimit_MT79x1.dat",
		"mediatek/TxPwrLimit6G_MT79x1.dat",
	};
	struct mt7921_power_table *table;
	struct device *device = dev->mt76.dev;
	const char *board;
	int i, ret;

	if (!mt76_is_sdio(&dev->mt76) ||
	    of_property_read_string(of_root, "coreelec-dt-id", &board) ||
	    strcmp(board, "t7_pop1-g_amazon_3rd_gen_cube"))
		return 0;

	table = devm_kzalloc(device, sizeof(*table), GFP_KERNEL);
	if (!table)
		return -ENOMEM;
	for (i = 0; i < ARRAY_SIZE(files); i++) {
		ret = request_firmware(&table->fw[i], files[i], device);
		if (ret) {
			dev_warn(device, "board power limits unavailable: %s (%d)\n",
				 files[i], ret);
			return 0;
		}
		ret = devm_add_action_or_reset(device, mt7921_power_release,
					      (void *)table->fw[i]);
		if (ret)
			return ret;
	}
	dev->power_table = table;
	return 0;
}

bool mt7921_has_power_limits(struct mt7921_dev *dev)
{
	return dev->power_table && dev->power_table->valid;
}

void mt7921_update_power_limits(struct mt7921_dev *dev, const u8 *alpha2)
{
	struct mt7921_power_table *table = dev->power_table;
	bool fallback[2] = {};
	int i, ret;

	if (!table || (table->valid && !memcmp(table->alpha2, alpha2, 2)))
		return;
	table->valid = false;
	for (i = 0; i < ARRAY_SIZE(table->fw); i++) {
		ret = mt7921_power_parse(table->fw[i], alpha2, i, table->sku[i]);
		if (ret == -ENOENT) {
			fallback[i] = true;
			ret = mt7921_power_parse(table->fw[i], "00", i, table->sku[i]);
		}
		if (ret) {
			dev_warn(dev->mt76.dev, "invalid board power table %d (%d)\n", i, ret);
			return;
		}
	}
	memcpy(table->alpha2, alpha2, 2);
	table->valid = true;
	dev_info(dev->mt76.dev, "board power limits: country %c%c, tables %s/%s\n",
		 alpha2[0], alpha2[1], fallback[0] ? "00" : "matched",
		 fallback[1] ? "00" : "matched");
}

void mt7921_apply_power_limits(struct mt76_dev *mdev,
			       struct ieee80211_channel *chan, s8 *sku)
{
	struct mt7921_dev *dev = container_of(mdev, struct mt7921_dev, mt76);
	struct mt7921_power_table *table = dev->power_table;
	const s8 *limits;
	int i;

	if (!mt7921_has_power_limits(dev) ||
	    chan->hw_value >= MT7921_POWER_CHANNELS)
		return;
	limits = table->sku[chan->band == NL80211_BAND_6GHZ][chan->hw_value];
	for (i = 0; i < MT7921_SKU_RATE_NUM; i++)
		sku[i] = min(sku[i], limits[i]);
}
EXPORT_SYMBOL_GPL(mt7921_apply_power_limits);
