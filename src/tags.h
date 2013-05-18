/*
 * tags.h
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

#ifndef TAGS_H
#define TAGS_H

int tagsCreateIndex(void);
int tagsStatus(char **filesArray, unsigned int filesCount);
int tagsList(const char *fieldsStr, const char *whrPropStr);
int tagsShowProps(void);
int tagsUpdateFileInfo(char **filesArray, int filesCount, char *addPropStr, char *delPropStr, char *setPropStr, const char *whrPropStr);

#endif // TAGS_H
