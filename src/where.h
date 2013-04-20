/*
 * where.h
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

#ifndef WHERE_H
#define WHERE_H

#include "property.h"
#include "item.h"

struct WhereStruct
{
	unsigned int condMax;
	unsigned int condCount;
	struct PropertyStruct **conditions;
};

struct WhereStruct *whereInit(const char *whereStr);
void whereFree(struct WhereStruct *whr);
int whereIsFiltered(const struct WhereStruct *whr, struct ItemStruct *item);

#endif // WHERE_H
