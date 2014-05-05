/*
 * utils.c
 * Copyright (C) 2013  Aleksey Andreev
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 *
 */

#include <stdlib.h>
#include <string.h>

#include "utils.h"

void reversew(wchar_t *s);

void uitow(unsigned long int n, wchar_t *s)
{
	unsigned long int i = 0;
	do
	{
		s[i++] = n % 10 + L'0';
	} while ((n /= 10) > 0);
	s[i] = L'\0';
	reversew(s);
}

void reversew(wchar_t *s)
{
	int i, j;
	wchar_t c;
	for (i = 0, j = wcslen(s) - 1; i < j; i++, j--)
	{
		c = s[i];
		s[i] = s[j];
		s[j] = c;
	}
}

wchar_t *makeWideCharString(const char *s, size_t len)
{
	if (len == 0)
		len = strlen(s);
	size_t sz = len * sizeof(char);
	int n;
	wchar_t wc;
	const char *ptr = s;
	size_t lenW = 0;
	while ((n = mbtowc(&wc, ptr, sz)) != 0)
	{
		if (n < 0)
			return NULL;
		sz -= n;
		ptr += n / sizeof(char);
		++lenW;
	}

	wchar_t *res = malloc((lenW + 1) * sizeof(wchar_t));
	if (res != NULL)
	{
		mbstowcs(res, s, lenW);
		res[lenW] = L'\0';
	}
	return res;
}
