/*
 * fields.h
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

#ifndef FIELDS_H
#define FIELDS_H

#include <stddef.h>
#include <stdio.h>

#include "item.h"
#include "property.h"

enum FieldType
{
	Error, Property, FileName, FileSize
};

struct FieldCache
{
	short int    empty;
	unsigned int offset; // in bytes
	unsigned int size;   // in characters
};

struct FieldStruct
{
	enum FieldType    type;
	struct FieldCache cache;
	char              name[];
};

struct FieldListStruct
{
	unsigned int       fieldsMax;
	unsigned int       fieldsCount;
	unsigned int       colMax;
	unsigned int       colCount;
	struct FieldStruct **fieldsList;
	struct FieldStruct **columns;
};


struct FieldListStruct *fieldsInit(const char *fieldsList);
void fieldsFree(struct FieldListStruct *fields);
int fieldsPrintRow(const struct FieldListStruct *fields, const struct ItemStruct *item, const char *baseDir, FILE *fd);

#endif // FIELDS_H
