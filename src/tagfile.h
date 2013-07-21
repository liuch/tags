/*
 * tagfile.h
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

#ifndef TAGFILE_H
#define TAGFILE_H

#include <stdio.h>

#include "file.h"
#include "item.h"
#include "errors.h"
#include "where.h"
#include "fields.h"

enum TagFileMode {ReadOnly, ReadWrite};

struct TagFileStruct
{
	FILE             *fd;
	char             *dirPath;
	unsigned int     dirLen;
	char             *filePath;
	char             *fileName;
	enum TagFileMode mode;
	enum ErrorId     lastError;
	unsigned int     curLineNum;
	char             *readBuffer;
	size_t           curItemSize;
	char             *curItemHash;
	int              findFlag;
	FILE             *fdModif;
	FILE             *fdInsert;
};

int tagfileCreateIndex();
struct TagFileStruct *tagfileInit(const char *dPath, const char *fName, enum TagFileMode mode);
enum ErrorId tagfileReinit(struct TagFileStruct *tf, enum TagFileMode mode);
void tagfileFree(struct TagFileStruct *tf);
int tagfileFindNextItemPosition(struct TagFileStruct *tf, size_t sz, const char *hash);
struct ItemStruct *tagfileItemLoad(struct TagFileStruct *tf);
int tagfileList(struct TagFileStruct *tf, struct FieldListStruct *fields, const struct WhereStruct *whr);
int tagfileShowProps(struct TagFileStruct *tf, struct ItemStruct *item);
enum ErrorId tagfileSetAppendMode(struct TagFileStruct *tf);
enum ErrorId tagfileInsertItem(struct TagFileStruct *tf, const struct ItemStruct *item);
enum ErrorId tagfileApplyModifications(struct TagFileStruct *tf);
struct ItemStruct *tagfileGetItemByFileName(struct TagFileStruct *tf, const char *fileName);

#endif // TAGFILE_H
