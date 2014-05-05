/*
 * property.h
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

#ifndef PROPERTY_H
#define PROPERTY_H

#include <wchar.h>

enum PropSubvalOrder
{
	None, ByValue, ByUser
};

struct PropertyStruct
{
	unsigned int         maxSize;
	unsigned int         curSize;
	unsigned int         blkCount;
	unsigned int         valuesOffset;
	unsigned int         valCount;
	struct SubvalHandle  **arrayDirect;
	struct SubvalHandle  **arrayByValue;
	struct SubvalHandle  **arrayByUser;
	unsigned int         userData;
};

enum SubvalStatus { NotUsed, Used };

struct SubvalHandle
{
	unsigned int       subvalSize;
	enum SubvalStatus  subvalStatus;
	unsigned int       userData;
};

struct PropertyStruct *propInit(const wchar_t *name, const wchar_t *value);
void propFree(struct PropertyStruct *prop);
const wchar_t *propGetName(const struct PropertyStruct *prop);
int propIsEmpty(struct PropertyStruct *prop);
int propIsEqualValue(struct PropertyStruct *prop1, struct PropertyStruct *prop2);
const wchar_t *propGetSubval(struct PropertyStruct *prop, unsigned int num, enum PropSubvalOrder order);
struct SubvalHandle  **propGetValueIndex(struct PropertyStruct *prop, enum PropSubvalOrder order);
int propAddSubvalues(struct PropertyStruct **pp, const wchar_t *value);
struct SubvalHandle *propIsSubval(const struct PropertyStruct *prop, const wchar_t *subval);
struct PropertyStruct *propAddSubval(struct PropertyStruct *prop, const wchar_t *value);
int propDelSubvalues(struct PropertyStruct **pp, const wchar_t *value);
const wchar_t *subvalString(const struct SubvalHandle *subval);

#endif // PROPERTY_H
