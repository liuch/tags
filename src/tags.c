/*
 * tags.c
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
#include <stdio.h>
#include <string.h>
#include <linux/limits.h>

#include "tags.h"
#include "tagfile.h"
#include "file.h"
#include "fields.h"
#include "where.h"

int tagsCreateIndex(void)
{
	fprintf(stdout, "Initialization...\n");
	int res = tagfileCreateIndex();
	return res;
}

int tagsStatus(char **filesArray, unsigned int filesCount)
{
	int nameOffset = fileBaseNameOffset(filesArray, filesCount);
	if (nameOffset < 0)
		return EXIT_FAILURE;

	char sdir[PATH_MAX];
	if (nameOffset != 0)
		strncpy(sdir, filesArray[0], nameOffset);
	sdir[nameOffset] = '\0';
	struct TagFileStruct *tf = tagfileInit(sdir, NULL, ReadOnly);
	if (tf == NULL)
		return EXIT_FAILURE;
	if (tf->lastError != ErrorNone)
	{
		tagfileFree(tf);
		return EXIT_FAILURE;
	}

	int res = EXIT_FAILURE;
	struct FileItemList *fil = fileitemsInitFromList(filesArray, filesCount, nameOffset, MaskFile);
	if (fil != NULL)
	{
		fileitemsSort(fil, SortBySize);

		unsigned int fiCnt      = fil->filesCount;
		struct FileItem **pFi   = fil->fileItems;
		unsigned int szCurPos   = 0;
		unsigned int szStartPos = 0;
		size_t szCur            = pFi[szCurPos]->size;
		while (tf->lastError == ErrorNone)
		{
			if (!tagfileFindNextItemPosition(tf, szCur, NULL))
			{
				if (tf->lastError != ErrorNone)
					break;
				size_t sz = tf->curItemSize;
				for (++szCurPos; szCurPos < fiCnt && (szCur = pFi[szCurPos]->size) < sz; ++szCurPos)
					;
				if (szCurPos == fiCnt)
				{
					res = EXIT_SUCCESS;
					break;
				}
				szStartPos = szCurPos;
			}

			if (szCur == tf->curItemSize)
			{
				struct ItemStruct *item = tagfileItemLoad(tf);
				if (item == NULL)
					break;
				for ( ; szCurPos < fiCnt && pFi[szCurPos]->size == szCur; ++szCurPos)
					if (itemIsFileName(item, pFi[szCurPos]->name))
						pFi[szCurPos]->userData = item->propsCount + 1;
				szCurPos = szStartPos;
			}
		}
		if (tf->lastError == ErrorEOF)
			res = EXIT_SUCCESS;

		if (res == EXIT_SUCCESS)
		{
			unsigned int i;
			for (i = 0; i < fiCnt; ++i)
			{
				const struct FileItem *fi = pFi[i];
				if (fi->userData != 0)
					fprintf(stdout, "[%2i] %S\n", fi->userData - 1, fi->path);
				else
					fprintf(stdout, "[--] %S\n", fi->path);
			}
		}

		fileitemsFree(fil);
	}
	tagfileFree(tf);
	return res;
}

int tagsList(const wchar_t *fieldsStr, const wchar_t *whrPropStr)
{
	struct FieldListStruct *fields = NULL;
	if (fieldsStr == NULL)
	{
		fields = fieldsInit(L"@FileName");
		if (fields == NULL)
		{
			fputs("Error: fieldsInit failed\n", stderr);
			return EXIT_FAILURE;
		}
	}
	else if (wcscmp(fieldsStr, L"-") != 0)
	{
		fields = fieldsInit(fieldsStr);
		if (fields == NULL)
		{
			fputs("Error: incorrect list of fields\n", stderr);
			return EXIT_FAILURE;
		}
	}

	struct WhereStruct *whr = NULL;
	if (whrPropStr != NULL)
	{
		whr = whereInit(whrPropStr);
		if (whr == NULL)
		{
			if (fields != NULL)
				fieldsFree(fields);
			fprintf(stderr, "Error: incorrect where conditions\n");
			return EXIT_FAILURE;
		}
	}

	int res = EXIT_FAILURE;
	struct TagFileStruct *tf = tagfileInit(NULL, NULL, ReadOnly);
	if (tf != NULL)
	{
		res = tagfileList(tf, fields, whr);
		tagfileFree(tf);
	}

	if (whr != NULL)
		whereFree(whr);
	if (fields != NULL)
		fieldsFree(fields);
	return res;
}

int tagsShowProps(void)
{
	struct ItemStruct *item = itemInit(0, L"");
	if (item == NULL)
	{
		fputs("Error: itemInit failed\n", stderr);
		return EXIT_FAILURE;
	}

	int res = EXIT_FAILURE;
	struct TagFileStruct *tf = tagfileInit(NULL, NULL, ReadOnly);
	if (tf != NULL)
	{
		res = tagfileShowProps(tf, item);
		tagfileFree(tf);
	}

	if (res == EXIT_SUCCESS)
	{
		unsigned int i;
		unsigned int cnt = item->propsCount;
		for (i = 0; i < cnt; ++i)
		{
			struct PropertyStruct *prop = *itemGetPropArrayAddrByNum(item, i);
			const wchar_t *propName = propGetName(prop);
			fprintf(stdout, "%S\t%i\n", propName, prop->userData + 1);
			unsigned int subCount = prop->valCount;
			struct SubvalHandle **ps = propGetValueIndex(prop, ByUser);
			unsigned int k;
			for (k = 0; k < subCount; ++k)
			{
				const struct SubvalHandle *subval = ps[k];
				const wchar_t *str = subvalString(subval);
				if (*str != L'\0')
					fprintf(stdout, "  %S=%S\t%i\n", propName, subvalString(subval), subval->userData + 1);
			}
		}
	}

	itemFree(item);
	return res;
}

int tagsUpdateFileInfo(char **filesArray, int filesCount, wchar_t *addPropStr, wchar_t *delPropStr, wchar_t *setPropStr, const wchar_t *whrPropStr)
{
	int dirLen = fileBaseNameOffset(filesArray, filesCount);
	if (dirLen < 0)
		return EXIT_FAILURE;

	char sdir[PATH_MAX];
	if (dirLen > 0)
		strncpy(sdir, filesArray[0], dirLen);
	sdir[dirLen] = '\0';
	struct TagFileStruct *tf = tagfileInit(sdir, NULL, ReadWrite);
	if (tf == NULL)
		return EXIT_FAILURE;
	if (tf->lastError != ErrorNone)
	{
		tagfileFree(tf);
		return EXIT_FAILURE;
	}

	struct WhereStruct *whr = NULL;
	if (whrPropStr != NULL)
	{
		whr = whereInit(whrPropStr);
		if (whr == NULL)
		{
			fputs("Error: incorrect where conditions\n", stderr);
			tagfileFree(tf);
			return EXIT_FAILURE;
		}
	}

	int res = EXIT_FAILURE;
	struct FileItemList *fil = fileitemsInitFromList(filesArray, filesCount, dirLen, MaskFile);
	if (fil != NULL)
	{
		unsigned int fCount = fil->filesCount;
		if (fCount != 0)
		{
			// Stage one
			fileitemsSort(fil, SortBySize);
			struct FileItem **pFi = fil->fileItems;

			short int removeMode = 0;
			if (addPropStr == NULL && setPropStr == NULL && wcscmp(delPropStr, L"-") == 0)
				++removeMode;
			unsigned int updatedCnt = 0;
			unsigned int removedCnt = 0;
			unsigned int fiStartIdx = 0;
			size_t size             = pFi[0]->size;
			while (fiStartIdx < fCount)
			{
				if (tagfileFindNextItemPosition(tf, size, NULL))
				{
					struct ItemStruct *item = tagfileItemLoad(tf);
					if (item == NULL)
						goto free_resources;
					short int fnd = 0;
					short int flt = 0;
					if (whr != NULL)
						flt = whereIsFiltered(whr, item);
					unsigned int i;
					for (i = fiStartIdx; i < fCount; ++i)
					{
						struct FileItem *fi = pFi[i];
						if (fi != NULL)
						{
							if (size != fi->size)
								break;
							if (itemIsFileName(item, fi->name))
							{
								if (!flt)
								{
									if (!removeMode)
									{
										if (!fnd)
										{
											int tmpRes = EXIT_SUCCESS;
											if (setPropStr != NULL)
												if ((tmpRes = itemSetPropertiesRaw(item, setPropStr)) != EXIT_SUCCESS)
													fprintf(stderr, "Error: itemSetPropertyRaw failed in %S\n", setPropStr);
											if (tmpRes == EXIT_SUCCESS && addPropStr != NULL)
												if ((tmpRes = itemAddPropertiesRaw(item, addPropStr)) != EXIT_SUCCESS)
													fprintf(stderr, "Error: itemAddPropertyRaw failed in %S\n", addPropStr);
											if (tmpRes == EXIT_SUCCESS && delPropStr != NULL)
												if ((tmpRes = itemDelPropertiesRaw(item, delPropStr)) != EXIT_SUCCESS)
													fprintf(stderr, "Error: itemDelPropertyRaw failed in %S\n", delPropStr);
											if (tmpRes != EXIT_SUCCESS)
											{
												itemFree(item);
												goto free_resources;
											}
											++fnd;
										}
										++updatedCnt;
									}
									else
									{
										itemRemoveFileName(item, fi->name);
										++removedCnt;
									}
								}
								fileitemsRemoveItem(fil, pFi + i, MaskFile);
							}
						}
					}
					if (!removeMode || item->fileNameCount != 0)
						tagfileInsertItem(tf, item);
					itemFree(item);
					if (tf->lastError != ErrorNone && tf->lastError != ErrorEOF)
						goto free_resources;
				}
				else
				{
					if (tf->lastError != ErrorNone)
						break;
					size = tf->curItemSize;
					for (++fiStartIdx; fiStartIdx < fCount; ++fiStartIdx)
					{
						struct FileItem *fi = pFi[fiStartIdx];
						if (fi != NULL)
							if (fi->size >= size)
								break;
					}
					if (fiStartIdx == fCount)
						break;
				}
			}
			if (tf->lastError == ErrorEOF || tf->lastError == ErrorNone)
			{
				// Stage two
				res = EXIT_SUCCESS;
				unsigned int addCnt = fil->filesCount;
				if (addCnt != 0)
				{
					if ((addPropStr == NULL && setPropStr == NULL) || removeMode || whrPropStr != NULL)
						addCnt = 0;
					else
					{
						if ((res = fileitemsCalculateHashes(fil)) == EXIT_SUCCESS)
						{
							res = EXIT_FAILURE;
							fileitemsSort(fil, SortBySizeHash);
							if (tagfileSetAppendMode(tf) == ErrorNone)
							{
								struct FileItem **pFi = fil->fileItems;
								struct ItemStruct *item = NULL;
								short int fEof = 0;
								unsigned int i;
								for (i = 0; ; ++i)
								{
									struct FileItem *fi;
									if (i != addCnt)
										fi = pFi[i];

									if (i != 0 &&
										(i == addCnt || fi->size != item->fileSize || wcscmp(fi->hash, item->hash) != 0))
									{
										if (tagfileInsertItem(tf, item) != ErrorNone)
										{
											fputs("tagfileInsertItem error\n", stderr);
											break;
										}
										if (i == addCnt)
										{
											res = EXIT_SUCCESS;
											break;
										}
										itemFree(item);
										item = NULL;
									}

									short int fNew = 0;
									if (item == NULL)
									{
										++fNew;
										int fnd = 0;
										if (!fEof)
										{
											fnd = tagfileFindNextItemPosition(tf, fi->size, fi->hash);
											if (fnd)
											{
												item = tagfileItemLoad(tf);
												if (item == NULL)
												{
													fputs("tagfileItemLoad error\n", stderr);
													break;
												}
											}
											else if (tf->lastError == ErrorEOF)
											{
												++fEof;
											}
											else if (tf->lastError != ErrorNone)
											{
												fputs("tagfileFindNextPosition error\n", stderr);
												break;
											}
										}
										if (!fnd)
										{
											item = itemInit(fi->size, fi->hash);
											if (item == NULL)
											{
												fputs("itemInit error\n", stderr);
												break;
											}
										}
									}

									if (itemAddFileName(item, fi->name) != EXIT_SUCCESS)
									{
										fputs("itemAddFileName error\n", stderr);
										break;
									}
									if (fNew)
									{
										if (setPropStr != NULL)
											if (itemSetPropertiesRaw(item, setPropStr) != EXIT_SUCCESS)
											{
												fputs("itemSetPropertiesRaw error\n", stderr);
												break;
											}
										if (addPropStr != NULL)
											if (itemAddPropertiesRaw(item, addPropStr) != EXIT_SUCCESS)
											{
												fputs("itemAddPropertiesRaw error\n", stderr);
												break;
											}
									}
								}
								if (item != NULL)
									itemFree(item);
							}
						}
					}
				}
				if (res == EXIT_SUCCESS)
				{
					if (tagfileApplyModifications(tf) == ErrorNone)
					{
						if (addCnt != 0)
							fprintf(stdout, "Information was added for %i files\n", addCnt);
						if (updatedCnt != 0)
							fprintf(stdout, "Information was updated for %i files\n", updatedCnt);
						if (removedCnt != 0)
							fprintf(stdout, "Information was removed for %i files\n", removedCnt);
					}
					else
						res = EXIT_FAILURE;
				}
			}
		}
		else
			fprintf(stderr, "Error: no files to process\n");
free_resources:
		fileitemsFree(fil);
	}
	if (whr != NULL)
		whereFree(whr);
	tagfileFree(tf);
	return res;
}

int moveFile(char **filesArray)
{
	char oldDir[PATH_MAX]; *oldDir = '\0';
	char newDir[PATH_MAX]; *newDir = '\0';
	char *oldName = filesArray[0];
	char *newName = filesArray[1];
	if (strlen(oldName) >= PATH_MAX || strlen(newName) >= PATH_MAX)
	{
		fputs("Error: path too long\n", stderr);
		return EXIT_FAILURE;
	}
	int oldNameOffset = fileBaseNameOffset(&oldName, 1);
	int newNameOffset = fileBaseNameOffset(&newName, 1);
	if (oldNameOffset < 0 || newNameOffset < 0)
		return EXIT_FAILURE;
	if (oldNameOffset != 0)
	{
		strncpy(oldDir, oldName, oldNameOffset);
		oldDir[oldNameOffset] = '\0';
		oldName += oldNameOffset;
	}
	if (newNameOffset != 0)
	{
		strncpy(newDir, newName, newNameOffset);
		newDir[newNameOffset] = '\0';
		newName += newNameOffset;
	}
	wchar_t oldNameW[PATH_MAX];
	wchar_t newNameW[PATH_MAX];
	if (mbstowcs(oldNameW, oldName, PATH_MAX) == (size_t) -1 || mbstowcs(newNameW, newName, PATH_MAX) == (size_t) -1)
	{
		fputs("Error: filename is wrong\n", stderr);
		return EXIT_FAILURE;
	}

	int cmpDirs = (oldNameOffset == newNameOffset) ? strcmp(oldDir, newDir) : 1;
	int cmpNames = strcmp(oldName, newName);
	if (cmpDirs == 0 && cmpNames == 0)
	{
		fputs("Error: source and destination path must be different\n", stderr);
		return EXIT_FAILURE;
	}

	struct TagFileStruct *tfSrc = tagfileInit(oldDir, NULL, (cmpDirs == 0) ? ReadOnly : ReadWrite);
	if (tfSrc == NULL)
		return EXIT_FAILURE;
	if (tfSrc->lastError != ErrorNone)
	{
		tagfileFree(tfSrc);
		return EXIT_FAILURE;
	}
	struct TagFileStruct *tfDst = tfSrc;
	if (cmpDirs != 0)
	{
		tfDst = tagfileInit(newDir, NULL, ReadOnly);
		if (tfDst == NULL || tfDst->lastError != ErrorNone)
		{
			tagfileFree(tfSrc);
			if (tfDst != NULL)
				tagfileFree(tfDst);
			return EXIT_FAILURE;
		}
	}

	enum ErrorId res = ErrorNone;
	struct ItemStruct *itemTmp = tagfileGetItemByFileName(tfDst, newNameW);
	if (itemTmp == NULL)
	{
		if (tagfileReinit(tfDst, ReadWrite) == ErrorNone)
		{
			struct ItemStruct *itemSrc = tagfileGetItemByFileName(tfSrc, oldNameW);
			if (itemSrc != NULL)
			{
				struct ItemStruct *itemDst = NULL;
				if (tfSrc != tfDst)
				{
					if (!tagfileFindNextItemPosition(tfDst, itemSrc->fileSize, itemSrc->hash))
					{
						res = tfDst->lastError;
						if (res == ErrorEOF)
							res = ErrorNone;
					}
					else
					{
						itemDst = tagfileItemLoad(tfDst);
						if (itemDst != NULL)
						{
							if (!itemIsEqual(itemDst, itemSrc))
							{
								fputs("Error: such element is exists.", stderr);
								if (itemDst->fileNameCount != 0)
									fprintf(stderr, " File: %S\n", itemGetFileName(itemDst, 0));
								else
									fprintf(stderr, " File size: %zu, file hash: %S\n", itemDst->fileSize, itemDst->hash);
								itemFree(itemDst);
								itemDst = NULL;
								res = ErrorOther;
							}
						}
					}
				}
				if (res == ErrorNone)
				{
					itemRemoveFileName(itemSrc, oldNameW);
					if (tfDst != tfSrc)
					{
						if (itemSrc->fileNameCount != 0)
							res = tagfileInsertItem(tfSrc, itemSrc);
					}
					if (itemDst == NULL)
						itemDst = itemSrc;
					if (res == ErrorNone)
					{
						if (tfDst != tfSrc && itemDst == itemSrc)
							itemClearFileNames(itemDst);
						if ((res = itemAddFileName(itemDst, newNameW)) == ErrorNone)
							res = tagfileInsertItem(tfDst, itemDst);
					}
					if (itemDst != itemSrc)
						itemFree(itemDst);
				}
				itemFree(itemSrc);
			}
			else
			{
				res = tfSrc->lastError;
				if (res == ErrorEOF)
					fputs("Error: file not found\n", stderr);
			}
		}
	}
	else
	{
		res = ErrorOther;
		itemFree(itemTmp);
		fputs("Error: the file already exists.\n", stderr);
	}

	if (res == ErrorNone)
	{
		res = tagfileApplyModifications(tfDst);
		if (res == ErrorNone && tfSrc != tfDst)
			res = tagfileApplyModifications(tfSrc);
	}

	tagfileFree(tfSrc);
	if (tfDst != NULL && tfDst != tfSrc)
		tagfileFree(tfDst);
	return (res == ErrorNone) ? EXIT_SUCCESS : EXIT_FAILURE;
}
