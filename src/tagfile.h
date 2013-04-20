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

enum TagFileMode {ReadOnly, ReadWrite};
enum TagFileEof {EofNo, EofYes};
enum TagFileType {TypeNormal, TypeFd};

struct TagFileStruct
{
	enum TagFileType type;
	FILE             *fd;
	char             *dirPath;
	unsigned int     dirLen;
	char             *filePath;
	char             *fileName;
	enum TagFileMode mode;
	enum ErrorId     lastError;
	unsigned int     curLineNum;
	char             *readBuffer;
	enum TagFileEof  eof;
};

int tagfileCreate();
int tagfileStatus(char **filesArray, unsigned int filesCount);
FILE *tagfileAddFile(FILE *fdSou, struct FileItemList *fil, char *addPropStr, char *setPropStr);
int tagfileUpdateFileInfo(char **filesArray, int filesCount, char *addPropStr, char *delPropStr, char *setPropStr, const char *whrPropStr);
int tagfileListStart(const char *fieldsStr, const char *whrPropStr);
int tagfileShowPropsStart(const char *path, struct ItemStruct *item);

#endif // TAGFILE_H
