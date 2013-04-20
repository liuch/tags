/*
 * file.h
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

#ifndef FILE_H
#define FILE_H

#define __USE_GNU

#include <stdio.h>
#include <sys/stat.h>
#include <dirent.h>

#include "errors.h"

enum PackStatus { PackedNo, PackedYes };

struct FileItemList
{
	unsigned int    nameOffset;
	unsigned int    filesCount;
	unsigned int    filesMax;
	struct FileItem **fileItems;
	unsigned int    dirsCount;
	unsigned int    dirsMax;
	struct FileItem **dirItems;
	enum PackStatus packed;
};

struct FileItem
{
	size_t         size;
	char           path[PATH_MAX];
	char           *name;
	char           hash[41];
	unsigned int   userData;
};

enum FileItemMask { MaskFile = 1, MaskDir = 2 };
enum SortMethod { SortBySize, SortBySizeHash, SortByPath };

typedef struct dirent64 ** DirEntry;

enum ErrorId fileInfo(const char *fileName, size_t *size, char *hash, int sizeHash);
void fileInfoError(const char *fileName, enum ErrorId err);
int dirList(const char *path, const char *pattern, DirEntry *dir);
int fileBaseNameOffset(char **filesArray, unsigned int filesCount);
int sha1file(FILE *fd, char[41]);

struct FileItemList *fileitemsInitFromList(char **filesArray, unsigned int filesCount, unsigned int dirLen, enum FileItemMask mask);
void fileitemsFree(struct FileItemList *fil);
int  fileitemsCalculateHashes(struct FileItemList *fil);
void fileitemsSort(struct FileItemList *fil, enum SortMethod method);
void fileitemsRemoveItem(struct FileItemList *fil, struct FileItem **pfi, enum FileItemMask mask);

#endif // FILE_H
