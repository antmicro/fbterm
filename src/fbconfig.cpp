/*
 *   Copyright © 2008 dragchan <zgchan317@gmail.com>
 *
 *   This program is free software; you can redistribute it and/or
 *   modify it under the terms of the GNU General Public License
 *   as published by the Free Software Foundation; either version 2
 *   of the License, or (at your option) any later version.
 *
 *   This program is distributed in the hope that it will be useful,
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *   GNU General Public License for more details.
 *
 *   You should have received a copy of the GNU General Public License
 *   along with this program; if not, write to the Free Software
 *   Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 *
 */

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <fcntl.h>
#include <sys/stat.h>
#include "fbconfig.h"

DEFINE_INSTANCE_DEFAULT(Config)

Config::Config()
{
	mConfigBuf = 0;
	mConfigEntrys = 0;

	s8 name[64];
	snprintf(name, sizeof(name), "%s/%s", getenv("HOME"), ".fbtermrc");

	struct stat cstat;
	if (stat(name, &cstat) == -1) {
		create_config_file(name);
		if (stat(name, &cstat) == -1) return;
	}

	if (cstat.st_size > 10240) return;

	s32 fd = open(name, O_RDONLY);
	if (fd == -1) return;

	mConfigBuf = new char[cstat.st_size + 1];
	mConfigBuf[cstat.st_size] = 0;

	read(fd, mConfigBuf, cstat.st_size);
	close(fd);

	s8 *end, *start = mConfigBuf;
	do {
		end = strchr(start, '\n');
		if (end) *end = 0;
		parseOption(start);
		if (end) start = end + 1;
	} while (end && *start);
}

Config::~Config()
{
	if (mConfigBuf) delete[] mConfigBuf;

	OptionEntry *next, *cur = mConfigEntrys;
	while (cur) {
		next = cur->next;
		delete cur;
		cur = next;
	}
}

void Config::getOption(const s8 *key, s8 *val, u32 len)
{
	if (!val) return;
	*val = 0;

	OptionEntry *entry = getEntry(key);
	if (!entry || !entry->val) return;

	u32 val_len = strlen(entry->val);
	if (--len > val_len) len = val_len;
	memcpy(val, entry->val, len);
	val[len] = 0;
}

void Config::getOption(const s8 *key, s32 &val)
{
	OptionEntry *entry = getEntry(key);
	if (!entry || !entry->val) return;

	s8 *tail;
	s32 a = strtol(entry->val, &tail, 10);

	if (!*tail) val = a;
}

void Config::getOption(const s8 *key, bool &val)
{
	OptionEntry *entry = getEntry(key);
	if (!entry || !entry->val) return;

	if (!strcmp(entry->val, "yes")) val = true;
	else if (!strcmp(entry->val, "no")) val = false;
}

Config::OptionEntry *Config::getEntry(const s8 *key)
{
	if (!key) return 0;

	OptionEntry *entry = mConfigEntrys;
	while (entry) {
		if (!strcmp(key, entry->key)) break;
		entry = entry->next;
	}

	return entry;
}

void Config::parseOption(s8 *str)
{
	s8 *cur = str, *end = str + strlen(str) - 1;
	while (*cur == ' ') cur++;

	if (!*cur || *cur == '#') return;

	s8 *key = cur;
	while (*cur && *cur != '=') cur++;
	if (!*cur) return;
	*cur = 0;

	s8 *val = ++cur;
	cur = end;
	while (*cur == ' ') cur--;
	if (cur < val) return;
	*(cur + 1) = 0;

	OptionEntry *entry = new OptionEntry;
	entry->key = key;
	entry->val = val;
	entry->next = mConfigEntrys;
	mConfigEntrys = entry;
}

void Config::create_config_file(const s8 *file)
{
	static const s8 default_config[] =
		"# Configuration for fbterm\n"
		"\n"
		"# Lines starting with '#' are ignored.\n"
		"# Note that end-of-line comments are NOT supported, comments must be on a line of their own.\n"
		"\n\n"
		"# font family/pixelsize used by fbterm, multiple font families must be seperated by ','\n"
		"font_family=mono\n"
		"font_size=12\n"
		"\n"
		"# default color of foreground/background text\n"
		"# available colors: 0 = black, 1 = red, 2 = green, 3 = brown, 4 = blue, 5 = magenta, 6 = cyan, 7 = white\n"
		"color_foreground=7\n"
		"color_background=0\n"
		"\n"
		"# max scroll-back history lines of every window, value must be [0 - 65535], 0 means disable it\n"
		"history_lines=1000\n"
		"\n"
		"# up to 5 additional text encodings, multiple encodings must be seperated by ','\n"
		"# run 'iconv --list' to get available encodings.\n"
		"text_encoding=\n"
		;


	s32 fd = open(file, O_WRONLY | O_CREAT, S_IRUSR | S_IWUSR);
	write(fd, default_config, sizeof(default_config) - 1);
	close(fd);
}
