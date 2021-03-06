/*
 * file.c
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

#define _LARGEFILE64_SOURCE

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fnmatch.h>

#include "file.h"
#include "sha1.h"

#define FILES_INCREASE    10

int fileFilter(const struct dirent64 *ent);
int cmpsizefi(const void *fi1, const void *fi2);
int cmpsizehashfi(const void *fi1, const void *fi2);
int cmpfilepath(const void *fi1, const void *fi2);

void fileitemsFreeList(struct FileItem **list, unsigned int cnt);
int  fileitemsInsertItem(struct FileItemList *fil, struct FileItem *fi, enum FileItemMask mask);
void fileitemsPackItems(struct FileItemList *fil);
void fileitemsPackList(struct FileItem **list, unsigned int cnt);
void fileitemsRemoveDuplicates(struct FileItemList *fil);

const char *_pattern;

enum ErrorId fileInfo(const char *fileName, size_t *size, wchar_t *hash, int sizeHash)
{
	struct stat64 fstat;
	if (stat64(fileName, &fstat) == -1)
		return ErrorOther;

	if ((fstat.st_mode & S_IFMT) != S_IFREG)
	{
		if ((fstat.st_mode & S_IFMT) == S_IFDIR)
			return ErrorFileIsDir;
		return ErrorFileType;
	}

	if (fstat.st_size == 0)
		return ErrorFileIsEmpty;

	enum ErrorId res = ErrorNone;
	if (size != NULL)
		*size = fstat.st_size;
	if (hash != NULL)
	{
		FILE *fd = fopen64(fileName, "r");
		if (fd != NULL)
		{
			if (sizeHash > FILE_HASH_LEN)
			{
				char sHash[FILE_HASH_LEN + 1];
				if (sha1file(fd, sHash) != EXIT_SUCCESS)
					res = ErrorOther;
				mbstowcs(hash, sHash, FILE_HASH_LEN);
				hash[FILE_HASH_LEN] = L'\0';
			}
			else
				res = ErrorInternal;
			fclose(fd);
		}
		else
			res = ErrorOther;
	}

	return res;
}

void fileInfoError(const char *fileName, enum ErrorId err)
{
	switch (err)
	{
		case ErrorFileIsDir:
		case ErrorFileType:
			fprintf(stderr, "%s - file type error\n", fileName);
			break;
		case ErrorFileIsEmpty:
			fprintf(stderr, "Error: %s - file is empty\n", fileName);
		case ErrorOther:
			perror(fileName);
		default:
			fputs("Error: fileInfo failed\n", stderr);
	}
}

int dirList(const char *path, const char *pattern, DirEntry *dir)
{
	const char *path_ = path;
	if (path[0] == '\0')
		path_ = "./";
	_pattern = pattern;
	int cnt = scandir64(path_, dir, fileFilter, alphasort64);
	if (cnt >= 0)
		return cnt;

	perror("scandir");
	return -1;
}

int fileBaseNameOffset(char **filesArray, unsigned int filesCount)
{
	char dirStr[PATH_MAX];
	unsigned int dirLen = 0;
	dirStr[0] = '\0';
	unsigned int i;
	for (i = 0; i < filesCount; ++i)
	{
		char *fStr = filesArray[i];
		const char *pSrc = strrchr(fStr, '/');
		unsigned int len = 0;
		if (pSrc != NULL)
			len = pSrc - fStr + 1;
		if (i != 0)
		{
			if (len != dirLen || strncmp(dirStr, fStr, len) != 0)
			{
				fputs("Error: The files are not located in the same directory\n", stderr);
				return -1;
			}
		}
		else if (len != 0)
		{
			if (len >= PATH_MAX)
			{
				fputs("Error: path too long\n", stderr);
				return -1;
			}
			strncpy(dirStr, fStr, len);
			//dirStr[len] = '\0'; // not needed, because match the length and used strncmp
			dirLen = len;
		}
	}
	return dirLen;
}

wchar_t *fileBaseNameOffsetW(wchar_t *path)
{
	wchar_t *ch = wcsrchr(path, L'/');
	if (ch != NULL)
		return ++ch;
	return path;
}

struct FileItemList *fileitemsInitFromList(char **filesArray, unsigned int filesCount, unsigned int dirLen, enum FileItemMask mask)
{
	struct FileItemList *fil = malloc(sizeof(struct FileItemList));
	if (fil != NULL)
	{
		fil->nameOffset = dirLen;
		fil->filesMax   = 0;
		fil->filesCount = 0;
		fil->fileItems  = NULL;
		fil->dirsMax   = 0;
		fil->dirsCount = 0;
		fil->dirItems  = NULL;
		fil->packed = PackedYes;

		unsigned int i;
		for (i = 0; i < filesCount; ++i)
		{
			size_t fsz;
			enum ErrorId res = fileInfo(filesArray[i], &fsz, NULL, 0);
			if (res == ErrorNone || res == ErrorFileIsDir)
			{
				if ((res == ErrorNone && (mask & MaskFile)) || (res == ErrorFileIsDir && (mask & MaskDir)))
				{
					unsigned int len = strlen(filesArray[i]);
					if (len >= PATH_MAX)
					{
						fputs("Error: path too long\n", stderr);
						break;
					}
					struct FileItem *fi = malloc(sizeof(struct FileItem));
					if (fi == NULL)
					{
						fputs("Error: fileitemsInitFromList failed\n", stderr);
						break;
					}
					fi->size = fsz;
					size_t sz = mbstowcs(fi->path, filesArray[i], PATH_MAX);
					if (sz == (size_t) -1 || sz == PATH_MAX)
					{
						free(fi);
						fputs("Error: fileitemsInsertItem failed\n",stderr);
						break;
					}
					fi->name = fileBaseNameOffsetW(fi->path);
					fi->hash[0] = L'\0';
					fi->userData = 0;
					if (fileitemsInsertItem(fil, fi, mask) != EXIT_SUCCESS)
					{
						free(fi);
						fputs("Error: fileitemsInsertItem failed\n",stderr);
						break;
					}
				}
			}
			else
			{
				fileInfoError(filesArray[i], res);
				break;
			}
		}
		if (i < filesCount)
		{
			fileitemsFree(fil);
			fil = NULL;
		}
		else
			fileitemsRemoveDuplicates(fil);
	}
	return fil;
}

void fileitemsFree(struct FileItemList *fil)
{
	struct FileItem **list = fil->fileItems;
	if (list != NULL)
		fileitemsFreeList(list, fil->filesCount);

	list = fil->dirItems;
	if (list != NULL)
		fileitemsFreeList(list, fil->dirsCount);

	free(fil);
}

int fileitemsCalculateHashes(struct FileItemList *fil)
{
	struct FileItem **list = fil->fileItems;
	unsigned int cnt = fil->filesCount;
	for ( ; cnt != 0; ++list)
	{
		struct FileItem *fi = *list;
		if (fi != NULL)
		{
			char sPath[PATH_MAX];
			wcstombs(sPath, fi->name, PATH_MAX);
			enum ErrorId res = fileInfo(sPath, NULL, fi->hash, sizeof(fi->hash));
			if (res != ErrorNone)
			{
				fileInfoError(sPath, res);
				return EXIT_FAILURE;
			}
			--cnt;
		}
	}
	return EXIT_SUCCESS;
}

void fileitemsSort(struct FileItemList *fil, enum SortMethod method)
{
	if (fil->packed == PackedNo)
		fileitemsPackItems(fil);
	unsigned int cnt = fil->filesCount;
	if (cnt > 1)
	{
		switch (method)
		{
			case SortBySize:
				qsort(fil->fileItems, cnt, sizeof(struct FileItem *), cmpsizefi);
				break;
			case SortBySizeHash:
				qsort(fil->fileItems, cnt, sizeof(struct FileItem *), cmpsizehashfi);
				break;
			case SortByPath:
				qsort(fil->fileItems, cnt, sizeof(struct FileItem *), cmpfilepath);
		}
	}
}

void fileitemsRemoveItem(struct FileItemList *fil, struct FileItem **pfi, enum FileItemMask mask)
{
	free(*pfi);
	*pfi = NULL;
	unsigned int cnt;
	unsigned int offset;
	if (mask & MaskFile)
	{
		cnt    = --fil->filesCount;
		offset = pfi - fil->fileItems;
	}
	else
	{
		cnt    = --fil->dirsCount;
		offset = pfi - fil->dirItems;
	}
	if (fil->packed == PackedYes && cnt != offset)
		fil->packed = PackedNo;
}

/***************** private ******************************/

int fileFilter(const struct dirent64* ent)
{
	const char *name = ent->d_name;
	if (strcmp(name, ".") == 0 || strcmp(name, "..") == 0)
		return 0;

	int res = fnmatch(_pattern, name, 0);
	if (res == FNM_NOMATCH)
		return 0;

	return 1;
}

int cmpsizefi(const void *fi1, const void *fi2)
{
	size_t sz1 = (*(const struct FileItem **)fi1)->size;
	size_t sz2 = (*(const struct FileItem **)fi2)->size;
	if (sz1 < sz2)
		return -1;
	if (sz1 > sz2)
		return 1;
	return 0;
}

int cmpsizehashfi(const void* fi1, const void* fi2)
{
	int res = cmpsizefi(fi1, fi2);
	if (res == 0)
	{
		const wchar_t *hash1 = (*(const struct FileItem **)fi1)->hash;
		const wchar_t *hash2 = (*(const struct FileItem **)fi2)->hash;
		res = wcscmp(hash1, hash2);
	}
	return res;
}

int cmpfilepath(const void *fi1, const void *fi2)
{
	int res = cmpsizefi(fi1, fi2);
	if (res == 0)
	{
		const wchar_t *path1 = (*(const struct FileItem **)fi1)->path;
		const wchar_t *path2 = (*(const struct FileItem **)fi2)->path;
		res = wcscmp(path1, path2);
	}
	return res;
}

void fileitemsFreeList(struct FileItem **list, unsigned int cnt)
{
	unsigned int i;
	for (i = 0; cnt != 0; ++i)
	{
		struct FileItem *fi = list[i];
		if (fi != NULL)
		{
			free(fi);
			--cnt;
		}
	}
	free(list);
}

int fileitemsInsertItem(struct FileItemList *fil, struct FileItem *fi, enum FileItemMask mask)
{
	unsigned int    *pMax;
	unsigned int    *pCnt;
	struct FileItem ***pList;
	if (mask & MaskFile)
	{
		pMax  = &fil->filesMax;
		pCnt  = &fil->filesCount;
		pList = &fil->fileItems;
	}
	else
	{
		pMax  = &fil->dirsMax;
		pCnt  = &fil->dirsCount;
		pList = &fil->dirItems;
	}

	unsigned int max = *pMax;
	if (*pCnt == max)
	{
		struct FileItem **pNew;
		if (*pCnt == 0)
			pNew = malloc(sizeof(struct FileItem *) * FILES_INCREASE);
		else
			pNew = realloc(*pList, sizeof(struct FileItem *) * (max + FILES_INCREASE));
		if (pNew == NULL)
			return EXIT_FAILURE;

		bzero(&pNew[max], sizeof(struct FileItem *) * FILES_INCREASE);
		pNew[max] = fi;
		*pList    = pNew;
		*pMax     += FILES_INCREASE;
		++*pCnt;
		return EXIT_SUCCESS;
	}

	struct FileItem **list = *pList + *pCnt;
	if (fil->packed == PackedNo)
	{
		for (list = *pList; ; ++list)
			if (*list == NULL)
				break;
		if ((unsigned int)(list - *pList) == *pCnt)
			fil->packed = PackedYes;
	}
	*list = fi;
	++*pCnt;
	return EXIT_SUCCESS;
}

void fileitemsPackItems(struct FileItemList *fil)
{
	unsigned int cnt = fil->filesCount;
	if (cnt != 0)
		fileitemsPackList(fil->fileItems, cnt);
	cnt = fil->dirsCount;
	if (cnt != 0)
		fileitemsPackList(fil->dirItems, cnt);
	fil->packed = PackedYes;
}

void fileitemsPackList(struct FileItem **list, unsigned int cnt)
{
	struct FileItem **souPtr = list;
	struct FileItem **desPtr = souPtr;
	do
	{
		struct FileItem *fi = *souPtr;
		if (fi != NULL)
		{
			if (souPtr != desPtr)
			{
				*desPtr = fi;
				*souPtr = NULL;
			}
			++desPtr;
			--cnt;
		}
		++souPtr;
	} while (cnt != 0);
}

int sha1file (FILE *fd, char sha1_str[41])
{
	SHA1_CTX ctx;
	SHA1Init(&ctx);
	unsigned char buff[4096];
	do
	{
		size_t cnt = fread(buff, 1, sizeof(buff), fd);
		if (cnt != sizeof(buff) && ferror(fd) != 0)
		{
			perror("read");
			return EXIT_FAILURE;
		}
		SHA1Update(&ctx, buff, cnt);
	} while (feof(fd) == 0);
	unsigned char hash[20];
	SHA1Final(hash, &ctx);
	char *j = sha1_str;
	unsigned int i;
	for (i = 0; i < 20; i++) {
		unsigned char chr1 = hash[i];
		unsigned char chr2 = chr1 & 15;
		chr1 >>= 4;
		if (chr1 <= 9) {
			chr1 += 48;
		} else {
			chr1 += 87;
		}
		*j = chr1;
		++j;
		if (chr2 <= 9) {
			chr2 += 48;
		} else {
			chr2 += 87;
		}
		*j = chr2;
		++j;
	}
	*j = '\0';
	return EXIT_SUCCESS;
}

void fileitemsRemoveDuplicates(struct FileItemList *fil)
{
	fileitemsSort(fil, SortByPath);
	unsigned int cnt = fil->filesCount;
	struct FileItem *fi_1 = fil->fileItems[0];
	unsigned int i;
	for (i = 1; i < cnt; ++i)
	{
		struct FileItem *fi_2 = fil->fileItems[i];
		if (wcscmp(fi_1->path, fi_2->path) == 0)
			fileitemsRemoveItem(fil, fil->fileItems + i, MaskFile);
		else
			fi_1 = fi_2;
	}
	if (fil->packed == PackedNo)
		fileitemsPackItems(fil);
}
