/*
 * tagfile.c
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

#include <stdlib.h>
#include <errno.h>
#include <unistd.h>
#include <string.h>
#include <wctype.h>

#include "tagfile.h"
#include "sha1.h"
#include "file.h"
#include "item.h"
#include "common.h"
#include "utils.h"

#define READ_BUFFER_INCREASE   200
#define READ_BUFFER_MAX_LENGTH 50000

const char tagFileName[] = "tags.info";
#define tagFileNameLen     9

int tagfileScanItemHeader(const wchar_t *str, size_t *pSz, wchar_t **pHash);
enum ErrorId tagfileWritingTail(struct TagFileStruct *tf);
int tagfileItemOutput(FILE *fd, const struct ItemStruct *item);
FILE *tagfileGetReadFd(const struct TagFileStruct *tf);
FILE *tagfileGetWriteFd(const struct TagFileStruct *tf);
struct TagFileStruct *tagfileInitStruct(enum TagFileMode mode);
static enum ErrorId initPath(struct TagFileStruct *tf, const wchar_t *dPath, const wchar_t *fName);
static enum ErrorId updateCharPath(struct TagFileStruct *tf);
enum ErrorId tagfileAllocateReadBuffer(struct TagFileStruct *tf);
struct TagFileStruct *tagfileCloneForSubdir(const struct TagFileStruct *tf, const char *subdir);
enum ErrorId tagfileOpen(struct TagFileStruct *tf);
enum ErrorId tagfileReadHeader(struct TagFileStruct *tf);
void tagfileClose(struct TagFileStruct *tf);
enum ErrorId tagfileReadString(struct TagFileStruct *tf);
enum ErrorId tagfileWriteString(struct TagFileStruct *tf, const wchar_t *str);
enum ErrorId tagfileItemBodyLoad(struct TagFileStruct *tf, struct ItemStruct *item);
struct ItemStruct *tagfileGetNextItem(struct TagFileStruct *tf);
enum ErrorId tagfileOpenTempWriteFile(struct TagFileStruct *tf);
void tagfileCloseTemporaryFiles(struct TagFileStruct *tf);


int tagfileCreateIndex()
{
	int res = EXIT_FAILURE;
	if (access(tagFileName, F_OK) == -1 && errno == ENOENT)
	{
		FILE *fd = fopen(tagFileName, "w");
		if (fd != NULL)
		{
			if (fputs("!tags-info", fd) != EOF && fputs("\n!version=0.1\n!format=simple\n\n", fd) != EOF)
			{
				fputs("Ok\n", stdout);
				res = EXIT_SUCCESS;
			}
			fclose(fd);
		}
		if (res != EXIT_SUCCESS)
			perror("tagfile");
	}
	else
		fprintf(stderr, "Error: file %s already exists\n", tagFileName);

	return res;
}

struct TagFileStruct *tagfileInit(const char* dPath, const char* fName, enum TagFileMode mode)
{
	struct TagFileStruct *tf = tagfileInitStruct(mode);
	if (tf != NULL)
	{
		enum ErrorId res = ErrorOther;
		wchar_t *_dir = NULL;
		if (dPath == NULL || (_dir = makeWideCharString(dPath, 0)) != NULL)
		{
			wchar_t *_fname = NULL;
			if (fName == NULL || (_fname = makeWideCharString(fName, 0)) != NULL)
			{
				res = initPath(tf, _dir, _fname);
				if (res == ErrorNone)
				{
					res = updateCharPath(tf);
					if (res == ErrorNone)
					{
						res = tagfileOpen(tf);
						if (res == ErrorNone)
							res = tagfileReadHeader(tf);
					}
				}
				if (_fname != NULL)
					free(_fname);
			}
			if (_dir != NULL)
				free(_dir);
		}
		if (res != ErrorNone && res != ErrorNotFound)
		{
			tagfileFree(tf);
			tf = NULL;
		}
	}
	if (tf == NULL)
		fputs("Error: tagfileInit failed\n", stderr);

	return tf;
}

enum ErrorId tagfileReinit(struct TagFileStruct *tf, enum TagFileMode mode)
{
	if (tf->mode == ReadWrite)
		tagfileCloseTemporaryFiles(tf);
	tf->mode          = mode;
	tf->lastError     = ErrorNone;
	tf->curLineNum    = 0;
	tf->readBuffer.pointer[0] = L'\0';
	tf->curItemSize   = 0;
	tf->curItemHash   = NULL;
	tf->findFlag      = 0;
	if (fseek(tf->fd, 0L, SEEK_SET) != -1)
	{
		if (mode == ReadWrite)
			if (tagfileOpenTempWriteFile(tf) != ErrorNone)
				return tf->lastError;
	}
	else
		tf->lastError = ErrorInternal;
	return tf->lastError;
}

void tagfileFree(struct TagFileStruct *tf)
{
	tagfileCloseTemporaryFiles(tf);
	tagfileClose(tf);
	if (tf->dirPath != NULL)
		free(tf->dirPath);
	if (tf->dirPathChar != NULL)
		free(tf->dirPathChar);
	if (tf->filePath != NULL)
		free(tf->filePath);
	if (tf->filePathChar != NULL)
		free(tf->filePathChar);
	free(tf);
}

int tagfileFindNextItemPosition(struct TagFileStruct *tf, size_t sz, const wchar_t *hash)
{
	if (feof(tagfileGetReadFd(tf)))
	{
		tf->lastError = ErrorEOF;
		return 0;
	}

	wchar_t *buff = tf->readBuffer.pointer;
	short fWrite = 0;
	if (tf->mode == ReadWrite)
		++fWrite;

	if (tf->curLineNum == 0 || tf->findFlag)
	{
		if (fWrite && tf->curLineNum != 0)
			if (tagfileWriteString(tf, buff) != ErrorNone)
				return 0;
		if (tagfileReadString(tf) != ErrorNone)
			return 0;
	}

	do
	{
		if (buff[0] == L'[')
		{
			if (tagfileScanItemHeader(buff, &tf->curItemSize, &tf->curItemHash) != EXIT_SUCCESS)
			{
				tf->lastError = ErrorInvalidIndex;
				return 0;
			}
			int sizeCmp = 0;
			int hashCmp = 0;
			if (sz != 0)
			{
				sizeCmp = sz - tf->curItemSize;
				if (hash != NULL)
					hashCmp = wcsncmp(hash, tf->curItemHash, FILE_HASH_LEN);
			}
			if (sizeCmp == 0)
			{
				if (hashCmp == 0)
				{
					tf->findFlag = 1;
					return 1;
				}
				if (hashCmp < 0)
					return 0;
			}
			else if (sz < tf->curItemSize)
				return 0;
		}

		if (fWrite && tagfileWriteString(tf, buff) != ErrorNone)
			break;
	} while (tagfileReadString(tf) == ErrorNone);

	return 0;
}

struct ItemStruct *tagfileItemLoad(struct TagFileStruct *tf)
{
	struct ItemStruct *item = itemInit(tf->curItemSize, tf->curItemHash);
	if (item == NULL)
	{
		tf->lastError = ErrorInvalidIndex;
		fputs("Error: tagsItemInit failed\n", stderr);
		return NULL;
	}

	if (tagfileItemBodyLoad(tf, item) == ErrorNone || tf->lastError == ErrorEOF)
		return item;

	itemFree(item);
	return NULL;
}

int tagfileList(struct TagFileStruct *tf, struct FieldListStruct *fields, const struct WhereStruct *whr)
{
	int res = EXIT_SUCCESS;
	if (tf->lastError == ErrorNone)
	{
		struct ItemStruct *item = NULL;
		while ((item = tagfileGetNextItem(tf)) != NULL)
		{
			int fltr = (whr == NULL) ? 0 : whereIsFiltered(whr, item);
			if (!fltr)
			{
				if (fields != NULL)
					res = fieldsPrintRow(fields, item, tf->dirPath, stdout);
				else
					res = tagfileItemOutput(stdout, item);
			}
			itemFree(item);
			if (res != EXIT_SUCCESS)
				return EXIT_FAILURE;
		}
		if (tf->lastError != ErrorEOF && tf->lastError != ErrorNone)
			res = EXIT_FAILURE;
	}
	tagfileClose(tf);

	if ((flags & RecurFlag) != 0)
	{
		DirEntry dir;
		int cnt = dirList(tf->dirPathChar, "*", &dir);
		if (cnt == -1)
			return EXIT_FAILURE;
		int i;
		for (i = 0; i < cnt; ++i)
		{
			struct dirent64 *de = dir[i];
			if (res == EXIT_SUCCESS && de->d_type == DT_DIR)
			{
				struct TagFileStruct *tf_ = tagfileCloneForSubdir(tf, de->d_name);
				if (tf_ == NULL)
					res = EXIT_FAILURE;
				else
				{
					if (tf_->lastError != ErrorNone && tf_->lastError != ErrorNotFound)
						res = EXIT_FAILURE;
					else
						res = tagfileList(tf_, fields, whr);
					tagfileFree(tf_);
				}
			}
			free(de);
		}
		free(dir);
	}

	return res;
}

int tagfileShowProps(struct TagFileStruct *tf, struct ItemStruct *item)
{
	int res = EXIT_SUCCESS;

	if (tf->lastError == ErrorNone)
	{
		struct ItemStruct *item2;
		while ((item2 = tagfileGetNextItem(tf)) != NULL)
		{
			itemClearFileNames(item2);
			if (itemMerge(item, item2) != EXIT_SUCCESS)
			{
				itemFree(item2);
				fputs("Error: itemMerge failed\n", stderr);
				return EXIT_FAILURE;
			}
			if (tf->lastError == ErrorEOF)
				break;
		}
		if (tf->lastError != ErrorEOF && tf->lastError != ErrorNone)
			return EXIT_FAILURE;
	}
	tagfileClose(tf);

	if ((flags & RecurFlag) != 0)
	{
		DirEntry dir;
		int cnt = dirList(tf->dirPathChar, "*", &dir);
		int i;
		for (i = 0; i < cnt; ++i)
		{
			struct dirent64 *de = dir[i];
			if (res == EXIT_SUCCESS && de->d_type == DT_DIR)
			{
				struct TagFileStruct *tf_ = tagfileCloneForSubdir(tf, de->d_name);
				if (tf_ != NULL)
				{
					if (tf_->lastError != ErrorNone && tf_->lastError != ErrorNotFound)
						res = EXIT_FAILURE;
					else
						res = tagfileShowProps(tf_, item);
					tagfileFree(tf_);
				}
				else
					res = EXIT_FAILURE;
			}
			free(de);
		}
		free(dir);
	}

	return res;
}

enum ErrorId tagfileSetAppendMode(struct TagFileStruct *tf)
{
	if (tagfileWritingTail(tf) == ErrorNone)
	{
		if (fseek(tf->fdModif, 0L, SEEK_SET) == 0 && (tf->fdInsert = tmpfile()) != NULL)
		{
			tf->curLineNum = 0;
			tf->lastError = ErrorNone;
		}
		else
		{
			tf->lastError = ErrorOther;
			perror("tmpfile");
		}
	}
	return tf->lastError;
}

enum ErrorId tagfileInsertItem(struct TagFileStruct *tf, const struct ItemStruct *item)
{
	enum ErrorId res = ErrorOther;

	FILE *fdWrite = tagfileGetWriteFd(tf);
	if (tagfileItemOutput(fdWrite, item) == EXIT_SUCCESS)
		if (fputwc(L'\n', fdWrite) != WEOF)
			res = ErrorNone;

	tf->lastError = res;
	return res;
}

enum ErrorId tagfileApplyModifications(struct TagFileStruct *tf)
{
	if (tagfileWritingTail(tf) != ErrorNone)
		return tf->lastError;

	enum ErrorId res = ErrorOther;
	FILE *fd = tagfileGetWriteFd(tf);
	unsigned int pathLen = strlen(tf->filePathChar);
	if (pathLen + 4 + 1 <= PATH_MAX)
	{
		char tmpName[PATH_MAX];
		strcpy(tmpName, tf->filePathChar);
		strcpy(tmpName + pathLen, ".tmp");
		FILE *fdTmp = fopen(&tmpName[0], "w");
		if (fdTmp != NULL)
		{
			fseek(fd, 0L, SEEK_SET);
			char buff[4096];
			while (1)
			{
				size_t cntRead = fread(buff, 1, sizeof(buff) * sizeof(char), fd);
				if (ferror(fd))
					break;
				if (cntRead != 0)
				{
					size_t cntWrite = fwrite(buff, 1, cntRead, fdTmp);
					if (cntRead != cntWrite || ferror(fdTmp))
						break;
				}
				if (feof(fd))
				{
					res = ErrorNone;
					fflush(fdTmp);
					break;
				}
			}
			if (fclose(fdTmp) == EOF)
				res = ErrorOther;

			if (res == ErrorNone)
			{
				tagfileClose(tf);
				res = ErrorOther;
				char bakName[PATH_MAX];
				strcpy(bakName, tf->filePathChar);
				strcpy(bakName + pathLen, ".bak");
				if (rename(tf->filePathChar, bakName) == 0)
				{
					sync();
					if (rename(tmpName, tf->filePathChar) == 0)
					{
						sync();
						unlink(bakName);
						res = ErrorNone;
					}
					else
						perror("indexfile");
				}
				else
					perror("bakfile");
			}
			else
				perror("tmpfile");
			if (res != ErrorNone)
				unlink(tmpName);
		}
		else
			perror("tmpfile");
	}
	tf->lastError = res;
	return res;
}

struct ItemStruct *tagfileGetItemByFileName(struct TagFileStruct *tf, const wchar_t *fileName)
{
	struct ItemStruct *item;
	enum TagFileMode mode = tf->mode;
	while ((item = tagfileGetNextItem(tf)) != NULL)
	{
		if (itemIsFileName(item, fileName))
			return item;
		if (mode == ReadWrite)
		{
			enum ErrorId res = tagfileInsertItem(tf, item);
			itemFree(item);
			if (res != ErrorNone)
				break;
		}
	}
	if (tf->lastError != ErrorEOF)
		fputs("Error: tagfileGetItemByFileName failed\n", stderr);
	return NULL;
}

/******************************* Private ******************************/

int tagfileScanItemHeader(const wchar_t *str, size_t *pSz, wchar_t **pHash)
{
	if (str[0] != L'[')
		return EXIT_FAILURE;

	int len = wcslen(str);
	if (len < FILE_HASH_LEN + 4 || str[--len] != L']')
		return EXIT_FAILURE;

	wchar_t *sep = wcschr(str, L':');
	if (sep == NULL || sep - str < 2)
		return EXIT_FAILURE;

	int c = 0;
	const wchar_t *p = str + 1;
	do
	{
		if (!iswdigit(*p))
			return EXIT_FAILURE;
		++c;
	} while (++p != sep);

	if (c > 20)
		return EXIT_FAILURE;
	if (wcslen(sep + 1) != FILE_HASH_LEN + 1) // + ']'
		return EXIT_FAILURE;

	wchar_t buff[21];
	wcsncpy(buff, str + 1, c);
	buff[c] = L'\0';
	*pSz = (size_t)wcstoll(buff, NULL, 10);

	*pHash = sep + 1;

	return EXIT_SUCCESS;
}

enum ErrorId tagfileWritingTail(struct TagFileStruct *tf)
{
	FILE *fdRead = tagfileGetReadFd(tf);
	if (!feof(fdRead))
	{
		const wchar_t *buff = tf->readBuffer.pointer;
		do
		{
			if (tf->curLineNum != 0)
				tagfileWriteString(tf, buff);
		} while (tagfileReadString(tf) == ErrorNone);
	}

	if (tf->lastError == ErrorEOF)
		tf->lastError = ErrorNone;
	return tf->lastError;
}

int tagfileItemOutput(FILE *fd, const struct ItemStruct *item)
{
	int res;
	if (fd != stdout)
		res = fwprintf(fd, L"[%zu:%S]\n", item->fileSize, item->hash);
	else
		res = fprintf(fd, "[%zu:%S]\n", item->fileSize, item->hash);
	if (res >= 0)
	{
		unsigned int i = 0;
		const wchar_t *fName;
		while ((fName = itemGetFileName(item, i++)) != NULL)
		{
			if (fd != stdout)
				res = fwprintf(fd, L"!FileName=%S\n", fName);
			else
				res = fprintf(fd, "!FileName=%S\n", fName);
			if (res < 0)
			{
				perror("tagfile");
				return EXIT_FAILURE;
			}
		}
		unsigned int cnt = item->propsCount;
		if (cnt > 0)
		{
			unsigned int i;
			wchar_t tempValueBuff[2048];
			for (i = 0; i < cnt; ++i)
			{
				if (itemPropertyValueToString(item, i, tempValueBuff, sizeof(tempValueBuff) / sizeof(wchar_t)) != EXIT_SUCCESS)
				{
					fputs("Error: itemPropertyValueToString failed\n", stderr);
					return EXIT_FAILURE;
				}
				if (fd != stdout)
					res = fwprintf(fd, L"%S=%S\n", itemPropertyGetName(item, i), tempValueBuff);
				else
					res = fprintf(fd, "%S=%S\n", itemPropertyGetName(item, i), tempValueBuff);
				if (res < 0)
				{
					perror("tagfile");
					return EXIT_FAILURE;
				}
			}
		}
		return EXIT_SUCCESS;
	}
	else
		perror("tagfile");

	return EXIT_FAILURE;
}

struct TagFileStruct *tagfileInitStruct(enum TagFileMode mode)
{
	struct TagFileStruct *tf = malloc(sizeof(struct TagFileStruct));
	if (tf != NULL)
	{
		tf->fd                 = NULL;
		tf->dirPath            = NULL;
		tf->dirPathChar        = NULL;
		tf->dirLen             = 0;
		tf->filePath           = NULL;
		tf->filePathChar       = NULL;
		tf->fileName           = NULL;
		tf->filePath           = NULL;
		tf->fileName           = NULL;
		tf->mode               = mode;
		tf->lastError          = ErrorNone;
		tf->curLineNum         = 0;
		tf->readBuffer.length  = 0;
		tf->readBuffer.pointer = NULL;
		tf->curItemSize        = 0;
		tf->curItemHash        = NULL;
		tf->findFlag           = 0;
		tf->fdModif            = NULL;
		tf->fdInsert           = NULL;
	}
	return tf;
}

enum ErrorId initPath(struct TagFileStruct *tf, const wchar_t *dPath, const wchar_t *fName)
{
	size_t dirLen = 0;
	size_t slashLen = 0;
	if (dPath != NULL && dPath[0] != L'\0')
	{
		dirLen = wcslen(dPath);
		if (dPath[dirLen-1] != L'/')
			++slashLen;
	}

	size_t fnameLen = tagFileNameLen;
	if (fName != NULL)
		fnameLen = wcslen(fName);

	enum ErrorId res = ErrorOther;
	if (dirLen + slashLen + fnameLen < PATH_MAX)
	{
		res = ErrorNone;
		tf->dirPath = malloc(PATH_MAX * sizeof(wchar_t));
		if (tf->dirPath != NULL)
		{
			if (dirLen != 0)
			{
				wcscpy(tf->dirPath, dPath);
				if (slashLen != 0)
				{
					tf->dirPath[dirLen++] = L'/';
					tf->dirPath[dirLen]   = L'\0';
				}
			}
			else
			{
				tf->dirPath[0] = L'\0';
			}
		}
		else
			res = ErrorInternal;
		tf->dirLen = dirLen;

		if (res == ErrorNone)
		{
			res = ErrorInternal;
			tf->filePath = malloc(PATH_MAX * sizeof(wchar_t));
			if (tf->filePath != NULL)
			{
				if (dirLen != 0)
					wcscpy(tf->filePath, tf->dirPath);
				tf->fileName = tf->filePath + dirLen;
				if (fName != NULL)
					wcscpy(tf->fileName, fName);
				else
					mbstowcs(tf->fileName, tagFileName, PATH_MAX - tf->dirLen);
				res = ErrorNone;
			}
		}
	}
	tf->lastError = res;
	return res;
}

static enum ErrorId updateCharPath(struct TagFileStruct *tf)
{
	enum ErrorId res = ErrorInternal;
	if (tf->dirPathChar != NULL || (tf->dirPathChar = malloc(PATH_MAX * sizeof(char))) != NULL)
	{
		if (tf->filePathChar != NULL || (tf->filePathChar = malloc(PATH_MAX * sizeof(char))) != NULL)
		{
			res = ErrorOther;
			size_t sz = wcstombs(tf->dirPathChar, tf->dirPath, PATH_MAX);
			if (sz != (size_t) -1 && sz != PATH_MAX)
			{
				sz = wcstombs(tf->filePathChar, tf->filePath, PATH_MAX);
				if (sz != (size_t) -1 && sz != PATH_MAX)
					res = ErrorNone;
			}
		}
	}
	tf->lastError = res;
	return res;
}

enum ErrorId tagfileAllocateReadBuffer(struct TagFileStruct *tf)
{
	size_t len = tf->readBuffer.length + READ_BUFFER_INCREASE;
	if (len > READ_BUFFER_MAX_LENGTH)
	{
		tf->lastError = ErrorInvalidIndex;
		return ErrorInvalidIndex;
	}

	wchar_t *newBuff;
	if (tf->readBuffer.pointer == NULL)
		newBuff = malloc(len * sizeof(wchar_t));
	else
		newBuff = realloc(tf->readBuffer.pointer, len * sizeof(wchar_t));
	if (newBuff == NULL)
	{
		tf->lastError = ErrorInternal;
		return ErrorInternal;
	}

	tf->readBuffer.length = len;
	if (tf->readBuffer.pointer == NULL)
		newBuff[0] = L'\0';
	tf->readBuffer.pointer = newBuff;
	tf->lastError = ErrorNone;
	return ErrorNone;
}

struct TagFileStruct *tagfileCloneForSubdir(const struct TagFileStruct *tf, const char *subdir)
{
	struct TagFileStruct *tfRes = tagfileInitStruct(tf->mode);
	if (tfRes != NULL)
	{
		if (initPath(tfRes, tf->dirPath, tf->fileName) != ErrorNone)
		{
			tagfileFree(tfRes);
			return NULL;
		}

		size_t dirLen   = tfRes->dirLen;
		size_t fileLen  = wcslen(tfRes->fileName);
		size_t subLen   = strlen(subdir);
		size_t slashLen = 0;
		if (subdir[subLen-1] != '/')
			++slashLen;
		if (dirLen + subLen + slashLen + fileLen >= PATH_MAX)
		{
			tagfileFree(tfRes);
			return NULL;
		}

		size_t buf_sz = PATH_MAX - dirLen;
		subLen = mbstowcs(tfRes->dirPath + dirLen, subdir, buf_sz);
		if (subLen == (size_t) -1 || subLen == buf_sz)
		{
			tagfileFree(tfRes);
			return NULL;
		}
		dirLen += subLen;
		if (slashLen != 0)
		{
			tfRes->dirPath[dirLen++] = L'/';
			tfRes->dirPath[dirLen]   = L'\0';
		}
		tfRes->dirLen = dirLen;

		wcscpy(tfRes->filePath, tfRes->dirPath);
		tfRes->fileName = tfRes->filePath + dirLen;
		wcscpy(tfRes->fileName, tf->fileName);

		if (updateCharPath(tfRes) != ErrorNone)
		{
			tagfileFree(tfRes);
			return NULL;
		}

		if (tagfileOpen(tfRes) == ErrorNone)
			tagfileReadHeader(tfRes);
	}
	return tfRes;
}

enum ErrorId tagfileOpen(struct TagFileStruct *tf)
{
	tf->curLineNum = 0;

	tf->fd = fopen(tf->filePathChar, "r");
	if (tf->fd == NULL)
	{
		if (errno == ENOENT)
		{
			tf->lastError = ErrorNotFound;
			fputs("Index file not found\n", stderr);
			return ErrorNotFound;
		}
		tf->lastError = ErrorOther;
		perror("index file");
		return ErrorOther;
	}

	if (tf->readBuffer.pointer == NULL && tagfileAllocateReadBuffer(tf) != ErrorNone)
	{
		fclose(tf->fd);
		tf->fd = NULL;
		return tf->lastError;
	}

	if (tf->mode == ReadWrite)
	{
		if (tagfileOpenTempWriteFile(tf) != ErrorNone)
			return tf->lastError;
	}

	return tf->lastError;
}

enum ErrorId tagfileReadHeader(struct TagFileStruct *tf)
{
	if (tagfileReadString(tf) == ErrorNone)
	{
		if (wcscmp(tf->readBuffer.pointer, L"!tags-info") != 0)
		{
			fprintf(stderr, "Error: file %S, line %i - invalid format\n", tf->filePath, tf->curLineNum);
			tf->lastError = ErrorInvalidIndex;
		}
	}
	return tf->lastError;
}

void tagfileClose(struct TagFileStruct *tf)
{
	if (tf->fd != NULL)
	{
		fclose(tf->fd);
		tf->fd = NULL;
	}
	if (tf->readBuffer.pointer != NULL)
	{
		free(tf->readBuffer.pointer);
		tf->readBuffer.pointer = NULL;
		tf->readBuffer.length  = 0;
	}
}

enum ErrorId tagfileReadString(struct TagFileStruct *tf)
{
	FILE *fdRead = tagfileGetReadFd(tf);
	++tf->curLineNum;
	tf->curItemSize = 0;
	tf->curItemHash = NULL;
	tf->findFlag    = 0;
	wchar_t *buff   = tf->readBuffer.pointer;
	size_t bufLen   = tf->readBuffer.length;
	size_t i = 0;
	while (fgetws(&buff[i], bufLen - i, fdRead) != NULL)
	{
		size_t strLen = wcslen(&buff[i]);
		if (strLen != 0)
		{
			i += strLen;
			if (buff[i-1] != L'\n')
			{
				if (bufLen - i <= 1)
				{
					if (tagfileAllocateReadBuffer(tf) != ErrorNone)
						return tf->lastError;
					bufLen = tf->readBuffer.length;
				}
				continue;
			}
			buff[i-1] = L'\0';
		}
		tf->lastError = ErrorNone;
		return ErrorNone;
	}

	enum ErrorId res = ErrorOther;
	if (feof(fdRead) != 0)
		res = ErrorEOF;
	else
		perror("tagfile");
	tf->lastError = res;
	return res;
}

enum ErrorId tagfileWriteString(struct TagFileStruct *tf, const wchar_t *str)
{
	enum ErrorId res = ErrorNone;
	FILE *fdWrite = tagfileGetWriteFd(tf);
	if (fputws(str, fdWrite) == -1 || fputwc(L'\n', fdWrite) == WEOF)
	{
		res = ErrorOther;
		perror("tmpfile");
	}
	tf->lastError = res;
	return res;
}

enum ErrorId tagfileItemBodyLoad(struct TagFileStruct *tf, struct ItemStruct *item)
{
	wchar_t *buff = tf->readBuffer.pointer;
	while (tagfileReadString(tf) == ErrorNone)
	{
		if (buff[0] == L'[')
		{
			tf->lastError = ErrorNone;
			return ErrorNone;
		}

		if (buff[0] != L'\0' && buff[0] != L'#')
		{

			wchar_t *valPos = wcschr(buff, L'=');
			if (valPos == NULL || valPos == buff)
			{
				fprintf(stderr, "Error: file %S, line %i - invalid format\n", tf->filePath, tf->curLineNum);
				tf->lastError = ErrorInvalidIndex;
				return ErrorInvalidIndex;
			}

			*valPos++ = L'\0';
			wchar_t *namePos = buff;
			if (namePos[0] == L'!')
			{
				++namePos;
				if (wcscmp(namePos, L"FileName") == 0 && wcslen(valPos) != 0)
				{
					if (itemAddFileName(item, valPos) != EXIT_SUCCESS)
					{
						fputs("Error: tagsItemAddFileName failed\n", stderr);
						tf->lastError = ErrorInternal;
						return ErrorInternal;
					}
				}
				else
				{
					fprintf(stderr, "Error: file %S, line %i - invalid format\n", tf->filePath, tf->curLineNum);
					tf->lastError = ErrorInvalidIndex;
					return ErrorInvalidIndex;
				}
			}
			else
			{
				if (itemSetProperty(item, namePos, valPos) != EXIT_SUCCESS)
				{
					tf->lastError = ErrorInternal;
					fputs("Error: ItemSetProperty failed\n", stderr);
					return EXIT_FAILURE;
				}
			}
		}
	}
	return tf->lastError;
}

struct ItemStruct *tagfileGetNextItem(struct TagFileStruct *tf)
{
	if (!tagfileFindNextItemPosition(tf, 0, NULL))
		return NULL;

	return tagfileItemLoad(tf);
}

FILE *tagfileGetReadFd(const struct TagFileStruct *tf)
{
	FILE *fd = tf->fd;
	if (tf->fdInsert != NULL)
		fd = tf->fdModif;
	return fd;
}

FILE *tagfileGetWriteFd(const struct TagFileStruct *tf)
{
	FILE *fd = tf->fdInsert;
	if (fd == NULL)
		fd = tf->fdModif;
	return fd;
}

void tagfileCloseTemporaryFiles(struct TagFileStruct *tf)
{
	if (tf->fdModif != NULL)
	{
		fclose(tf->fdModif);
		tf->fdModif = NULL;
	}
	if (tf->fdInsert != NULL)
	{
		fclose(tf->fdInsert);
		tf->fdInsert = NULL;
	}
}

enum ErrorId tagfileOpenTempWriteFile(struct TagFileStruct *tf)
{
	tf->fdModif = tmpfile();
	if (tf->fdModif == NULL)
	{
		tf->lastError = ErrorOther;
		perror("tmpfile");
	}
	else
		tf->lastError = ErrorNone;
	return tf->lastError;
}
