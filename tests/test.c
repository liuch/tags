/*
 * test.c
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
#include <wchar.h>

#include "../src/property.h"
#include "../src/item.h"
#include "../src/fields.h"
#include "../src/file.h"
#include "../src/where.h"

const char *testNm = NULL;

unsigned int tests_cnt = 0;
unsigned int errors_cnt = 0;

void testProp();
void testItem();
void testFields();
void testWhere();
unsigned int propCommon(struct PropertyStruct *prop);
void printFailed(const char *descr);

int main()
{
	testProp();
	testItem();
	testFields();
	testWhere();

	fprintf(stdout, "Tests: %i, errors: %i\n", tests_cnt, errors_cnt);
	if (errors_cnt != 0)
		return EXIT_FAILURE;
	return EXIT_SUCCESS;
}

void testProp()
{
	{
		++tests_cnt;
		testNm = "propInit";
		struct PropertyStruct *prop = propInit(L"!testName_123", L"testVal_1");
		if (prop != NULL)
		{
			++errors_cnt;
			printFailed("!Name");
			propFree(prop);
		}

		prop = propInit(L"testName_123", NULL);
		if (prop == NULL || prop->valCount != 1)
		{
			++errors_cnt;
			printFailed("null value");
		}
		if (prop != NULL)
			propFree(prop);

		++tests_cnt;
		testNm = "propInit";
		prop = propInit(L"testName_123", L"");
		if (prop == NULL || prop->valCount != 1)
		{
			++errors_cnt;
			printFailed("empty value");
		}
		if (prop != NULL)
			propFree(prop);
	}

	++tests_cnt;
	testNm = "propInit_Value";
	struct PropertyStruct *propEnum = propInit(L"testName_123", L"testVal_1,testVal_3,testVal_2");
	if (propEnum == NULL)
	{
		++errors_cnt;
		printFailed("init");
	}
	else
	{
		unsigned int err = propCommon(propEnum);
		if (propEnum->valCount != 3)
		{
			++err;
			printFailed("valCount != 3");
		}
		if (err != 0)
			++errors_cnt;
	}

	if (propEnum == NULL)
		return;

	{
		++tests_cnt;
		testNm = "propGetValueIndex";
		struct SubvalHandle **subvals = propGetValueIndex(propEnum, None);
		if (subvals == NULL || subvals[0] == NULL || subvals[1] == NULL || subvals[2] == NULL)
		{
			++errors_cnt;
			printFailed("no sort");
		}
		subvals[0]->userData = 99;
		subvals[1]->userData = 1;
		subvals = propGetValueIndex(propEnum, ByValue);
		if (subvals == NULL || subvals[0] == NULL || subvals[1] == NULL || subvals[2] == NULL)
		{
			++errors_cnt;
			printFailed("by value");
		}
		subvals = propGetValueIndex(propEnum, ByUser);
		if (subvals == NULL || subvals[0] == NULL || subvals[1] == NULL || subvals[2] == NULL)
		{
			++errors_cnt;
			printFailed("by user");
		}
	}

	{
		++tests_cnt;
		testNm = "propIsSubval";
		struct SubvalHandle *subvalEnum = propIsSubval(propEnum, L"testVal_3");
		if (subvalEnum == NULL)
		{
			++errors_cnt;
			printFailed("Enum");
		}
		subvalEnum = propIsSubval(propEnum, L"TESTvAL_3");
		if (subvalEnum == NULL)
		{
			++errors_cnt;
			printFailed("case-sensitive");
		}

		if (subvalEnum != NULL)
		{
			++tests_cnt;
			testNm = "subvalString";
			const wchar_t *str = subvalString(subvalEnum);
			if (str == NULL || wcscmp(str, L"testVal_3") != 0)
			{
				++errors_cnt;
				printFailed("Enum");
			}
		}
	}

	{
		++tests_cnt;
		testNm = "propAddSubval";
		struct PropertyStruct *prop = propAddSubval(propEnum, L"testVal_4");
		if (prop == NULL || prop->valCount != 4)
		{
			++errors_cnt;
			printFailed("to Enum");
		}
		else
		{
			propEnum = prop;
			prop = propAddSubval(propEnum, L"");
			if (prop == NULL || prop->valCount != 4)
			{
				++errors_cnt;
				printFailed("empty to Enum");
			}
			else
			{
				propEnum = prop;
				prop = propAddSubval(propEnum, L"  ");
				if (prop == NULL || prop->valCount != 4)
				{
					++errors_cnt;
					printFailed("wspaces to Enum");
				}
				else
				{
					propEnum = prop;
					prop = propAddSubval(propEnum, L"  _ whitespaces _   ");
					if (prop == NULL || prop->valCount != 5 || propIsSubval(prop, L"_ whitespaces _") == NULL)
					{
						++errors_cnt;
						printFailed("wspaces2 to Enum");
					}
				}
			}
		}
		if (prop != NULL)
			propEnum = prop;
	}

	{
		++tests_cnt;
		testNm = "propAddSubvalues";
		int res = propAddSubvalues(&propEnum, L"testVal_5,testVal_6");
		if (res != EXIT_SUCCESS || propEnum->valCount != 7)
		{
			++errors_cnt;
			printFailed("to Enum");
		}
		res = propAddSubvalues(&propEnum, L"testVal_2");
		if (res != EXIT_SUCCESS || propEnum->valCount != 7)
		{
			++errors_cnt;
			printFailed("duplicate to Enum");
		}
	}

	{
		++tests_cnt;
		testNm = "propDelSubvalues";
		int res = propDelSubvalues(&propEnum, L"testVal_4,testVal_6,testVal_5,testVal_10,_ whitespaces _");
		if (res != EXIT_SUCCESS || propEnum->valCount != 3 || wcscmp(propGetSubval(propEnum, 0, None), L"testVal_1") != 0 || wcscmp(propGetSubval(propEnum, 1, None), L"testVal_3") != 0 || wcscmp(propGetSubval(propEnum, 2, None), L"testVal_2") != 0)
		{
			++errors_cnt;
			printFailed("from Enum");
		}
	}

	{
		++tests_cnt;
		testNm = "propGetSubval";
		const wchar_t *val = propGetSubval(propEnum, 3, None);
		if (val != NULL)
		{
			++errors_cnt;
			printFailed("outOfRange Enum");
		}
		val = propGetSubval(propEnum, 2, None);
		if (val == NULL || wcscmp(val, L"testVal_2") != 0)
		{
			++errors_cnt;
			printFailed("no sort");
		}
		val = propGetSubval(propEnum, 2, ByValue);
		if (val == NULL || wcscmp(val, L"testVal_3") != 0)
		{
			++errors_cnt;
			printFailed("by value");
		}
		val = propGetSubval(propEnum, 2, ByUser);
		if (val == NULL || wcscmp(val, L"testVal_1") != 0)
		{
			++errors_cnt;
			printFailed("by user");
		}
	}

	{
		++tests_cnt;
		testNm = "propIsEmpty";
		struct PropertyStruct *prop1 = propInit(L"testName_10", L"testVal_10");
		if (propIsEmpty(prop1))
		{
			++errors_cnt;
			printFailed("not empty");
		}
		propFree(prop1);
		prop1 = propInit(L"testName_10", L"");
		if (!propIsEmpty(prop1))
		{
			++errors_cnt;
			printFailed("empty 1");
		}
		propFree(prop1);
	}

	{
		++tests_cnt;
		testNm = "propIsEqualValue";
		struct PropertyStruct *prop1 = propInit(L"testName_10", L"");
		struct PropertyStruct *prop2 = propInit(L"testName_20", L"");
		if (!propIsEqualValue(prop1, prop2))
		{
			++errors_cnt;
			printFailed("empty");
		}
		propFree(prop1);
		prop1 = propInit(L"testName_10", L"testVal_10");
		if (propIsEqualValue(prop1, prop2))
		{
			++errors_cnt;
			printFailed("test e=!e");
		}
		propFree(prop1);
		prop1 = propInit(L"testName_10", L"testVal_10,testVal_11");
		propFree(prop2);
		prop2 = propInit(L"testName_20", L"testVal_10");
		if (propIsEqualValue(prop1, prop2))
		{
			++errors_cnt;
			printFailed("test 2=1");
		}
		propFree(prop2);
		prop2 = propInit(L"testName_20", L"testVal_10,testVal_12");
		if (propIsEqualValue(prop1, prop2))
		{
			++errors_cnt;
			printFailed("test 11=12");
		}
		propFree(prop2);
		prop2 = propInit(L"testName_20", L"testVal_11,testVal_10");
		if (!propIsEqualValue(prop1, prop2))
		{
			++errors_cnt;
			printFailed("test 2=2");
		}
		propFree(prop1);
		propFree(prop2);
	}

	propFree(propEnum);
}

void testItem()
{
	++tests_cnt;
	testNm = "itemInit";
	struct ItemStruct *item = itemInit(4, L"a94a8fe5ccb19ba61c4c0873d391e987982fbbd3");
	if (item == NULL)
	{
		++errors_cnt;
		printFailed("null item");
		return;
	}
	if (item->fileSize != 4)
	{
		++errors_cnt;
		printFailed("fileSize");
	}
	if (wcscmp(item->hash, L"a94a8fe5ccb19ba61c4c0873d391e987982fbbd3") != 0)
	{
		++errors_cnt;
		printFailed("fileHash");
	}
	if (item->fileNameMax != 0)
	{
		++errors_cnt;
		printFailed("fileNameMax");
	}
	if (item->fileNameCount != 0)
	{
		++errors_cnt;
		printFailed("fileNameCount");
	}
	if (item->fileNames != NULL)
	{
		++errors_cnt;
		printFailed("fileNames");
	}
	if (item->propsMax != 0)
	{
		++errors_cnt;
		printFailed("propsMax");
	}
	if (item->propsCount != 0)
	{
		++errors_cnt;
		printFailed("propsCount");
	}
	if (item->props != NULL)
	{
		++errors_cnt;
		printFailed("props");
	}
	itemFree(item);

	++tests_cnt;
	testNm = "itemInitFromRawData";
	item = itemInitFromRawData(4, L"a94a8fe5ccb19ba61c4c0873d391e987982fbbd3", L"testfile1", L"testName_10=testVal_10,testVal_11,@testName_30=", L"testName_1=testVal_1,testVal_3,, ,  ,testVal_2@testName_40=@testName_20=testVal_20,testVal_21,");
	if (item == NULL)
	{
		++errors_cnt;
		printFailed("null item");
		return;
	}
	if (item->fileNameCount != 1)
	{
		++errors_cnt;
		printFailed("fileNameCount");
	}
	if (item->propsCount != 5)
	{
		++errors_cnt;
		printFailed("propsCount");
	}

	{
		++tests_cnt;
		testNm = "itemPropertyGetName";
		if (itemPropertyGetName(item, 5) != NULL)
		{
			++errors_cnt;
			printFailed("out of range");
		}
		if (wcscmp(itemPropertyGetName(item, 0), L"testName_1") != 0)
		{
			++errors_cnt;
			printFailed("first prop");
		}
		if (wcscmp(itemPropertyGetName(item, 4), L"testName_30") != 0)
		{
			++errors_cnt;
			printFailed("last prop");
		}
	}

	{
		++tests_cnt;
		testNm = "itemGetPropArrayAddrByNum";
		if (itemGetPropArrayAddrByNum(item, 5) != NULL)
		{
			++errors_cnt;
			printFailed("out of range");
		}
		if (itemGetPropArrayAddrByNum(item, 4) == NULL)
		{
			++errors_cnt;
			printFailed("last prop");
		}
		++tests_cnt;
		testNm = "checkValues";
		struct PropertyStruct **pProp = itemGetPropArrayAddrByNum(item, 0);
		struct PropertyStruct *prop = *pProp;
		if (wcscmp(propGetName(prop), L"testName_1") != 0)
		{
			++errors_cnt;
			printFailed("testName_1 name");
		}
		else
		{
			if (prop->valCount != 3)
			{
				++errors_cnt;
				printFailed("testName_1 valCount");
			}
			else
			{
				if (wcscmp(propGetSubval(prop, 0, None), L"testVal_1") != 0 || wcscmp(propGetSubval(prop, 1, None), L"testVal_3") != 0 || wcscmp(propGetSubval(prop, 2, None), L"testVal_2") != 0)
				{
					++errors_cnt;
					printFailed("testName_1 value");
				}
			}
		}
		pProp = itemGetPropArrayAddrByNum(item, 1);
		prop = *pProp;
		if (wcscmp(propGetName(prop), L"testName_40") != 0)
		{
			++errors_cnt;
			printFailed("testName_40 name");
		}
		else
		{
			if (prop->valCount != 1)
			{
				++errors_cnt;
				printFailed("testName_40 valCount");
			}
			else
			{
				if (wcslen(propGetSubval(prop, 0, None)) != 0)
				{
					++errors_cnt;
					printFailed("testName_40 value");
				}
			}
		}
		pProp = itemGetPropArrayAddrByNum(item, 2);
		prop = *pProp;
		if (wcscmp(propGetName(prop), L"testName_20") != 0)
		{
			++errors_cnt;
			printFailed("testName_20 name");
		}
		else
		{
			if (prop->valCount != 2)
			{
				++errors_cnt;
				printFailed("testName_20 valCount");
			}
			else
			{
				if (wcscmp(propGetSubval(prop, 0, None), L"testVal_20") != 0 || wcscmp(propGetSubval(prop, 1, None), L"testVal_21") != 0)
				{
					++errors_cnt;
					printFailed("testName_20 value");
				}
			}
		}
		pProp = itemGetPropArrayAddrByNum(item, 3);
		prop = *pProp;
		if (wcscmp(propGetName(prop), L"testName_10") != 0)
		{
			++errors_cnt;
			printFailed("testName_10 name");
		}
		else
		{
			if (prop->valCount != 2)
			{
				++errors_cnt;
				printFailed("testName_10 valCount");
			}
			else
			{
				if (wcscmp(propGetSubval(prop, 0, None), L"testVal_10") != 0 || wcscmp(propGetSubval(prop, 1, None), L"testVal_11") != 0)
				{
					++errors_cnt;
					printFailed("testName_10 value");
				}
			}
		}
		pProp = itemGetPropArrayAddrByNum(item, 4);
		prop = *pProp;
		if (wcscmp(propGetName(prop), L"testName_30") != 0)
		{
			++errors_cnt;
			printFailed("testName_30 name");
		}
		else
		{
			if (prop->valCount != 1)
			{
				++errors_cnt;
				printFailed("testName_30 valCount");
			}
			else
			{
				if (wcslen(propGetSubval(prop, 0, None)) != 0)
				{
					++errors_cnt;
					printFailed("testName_30 value");
				}
			}
		}
	}

	++tests_cnt;
	testNm = "itemGetPropertyPosByName";
	if (itemGetPropertyPosByName(item, L"testName_1") == NULL
		|| itemGetPropertyPosByName(item, L"testName_10") == NULL || itemGetPropertyPosByName(item, L"testName_20") == NULL
		|| itemGetPropertyPosByName(item, L"testName_30") == NULL || itemGetPropertyPosByName(item, L"testName_40") == NULL)
	{
		++errors_cnt;
		printFailed("case-insensitive");
	}
	if (itemGetPropertyPosByName(item, L"TESTnAME_1") == NULL
		|| itemGetPropertyPosByName(item, L"TESTnAME_10") == NULL || itemGetPropertyPosByName(item, L"TESTnAME_20") == NULL
		|| itemGetPropertyPosByName(item, L"TESTnAME_30") == NULL || itemGetPropertyPosByName(item, L"TESTnAME_40") == NULL)
	{
		++errors_cnt;
		printFailed("case-sensitive");
	}

	++tests_cnt;
	testNm = "itemIsFileName";
	if (itemIsFileName(item, L"testfile2") != 0)
	{
		++errors_cnt;
		printFailed("testfile2");
	}
	if (itemIsFileName(item, L"testfile1") == 0)
	{
		++errors_cnt;
		printFailed("testfile1");
	}

	++tests_cnt;
	testNm = "itemAddFileName";
	if (itemAddFileName(item, L"testfile2") != EXIT_SUCCESS)
	{
		++errors_cnt;
		printFailed("testfile2");
	}

	++tests_cnt;
	testNm = "itemIsFileName2";
	if (itemIsFileName(item, L"testfile1") == 0)
	{
		++errors_cnt;
		printFailed("testfile1");
	}
	if (itemIsFileName(item, L"testfile2") == 0)
	{
		++errors_cnt;
		printFailed("testfile2");
	}

	++tests_cnt;
	testNm = "itemGetFileName";
	if (wcscmp(itemGetFileName(item, 0), L"testfile1") != 0)
	{
		++errors_cnt;
		printFailed("testfile1");
	}
	if (wcscmp(itemGetFileName(item, 1), L"testfile2") != 0)
	{
		++errors_cnt;
		printFailed("testfile2");
	}

	++tests_cnt;
	testNm = "itemRemoveFileName";
	itemRemoveFileName(item, L"testfile1");
	if (item->fileNameCount != 1)
	{
		++errors_cnt;
		printFailed("fileNameCount");
	}
	if (!itemIsFileName(item, L"testfile2"))
	{
		++errors_cnt;
		printFailed("fileName");
	}

	++tests_cnt;
	testNm = "itemClearFileNames";
	itemClearFileNames(item);
	if (item->fileNameCount != 0)
	{
		++errors_cnt;
		printFailed("fileNameCount");
	}
	if (item->fileNames != NULL)
	{
		++errors_cnt;
		printFailed("fileNames");
	}

	++tests_cnt;
	testNm = "itemGetFileName2";
	if (itemGetFileName(item, 0) != NULL)
	{
		++errors_cnt;
		printFailed("null");
	}

	++tests_cnt;
	testNm = "itemSetProperty1";
	if (itemSetProperty(item, L"testName_60", NULL) != EXIT_SUCCESS)
	{
		++errors_cnt;
		printFailed("init");
	}
	else
	{
		struct PropertyStruct **pProp = itemGetPropArrayAddrByNum(item, 5);
		struct PropertyStruct *prop = *pProp;
		if (wcscmp(propGetName(prop), L"testName_60") != 0)
		{
			++errors_cnt;
			printFailed("name");
		}
		else
		{
			if (prop->valCount != 1)
			{
				++errors_cnt;
				printFailed("valCount");
			}
			else
			{
				if (wcslen(propGetSubval(prop, 0, None)) != 0)
				{
					++errors_cnt;
					printFailed("value");
				}
			}
		}
	}

	++tests_cnt;
	testNm = "itemSetProperty2";
	if (itemSetProperty(item, L"testName_60", L"testVal_1,,testVal_2, ,testVal_3,") != EXIT_SUCCESS)
	{
		++errors_cnt;
		printFailed("init");
	}
	else
	{
		struct PropertyStruct **pProp = itemGetPropArrayAddrByNum(item, 5);
		struct PropertyStruct *prop = *pProp;
		if (wcscmp(propGetName(prop), L"testName_60") != 0)
		{
			++errors_cnt;
			printFailed("name");
		}
		else
		{
			if (prop->valCount != 3)
			{
				++errors_cnt;
				printFailed("valCount");
			}
		}
	}

	++tests_cnt;
	testNm = "itemSetProperty3";
	if (itemSetProperty(item, L"testName_60", L"") != EXIT_SUCCESS)
	{
		++errors_cnt;
		printFailed("init");
	}
	else
	{
		struct PropertyStruct **pProp = itemGetPropArrayAddrByNum(item, 5);
		struct PropertyStruct *prop = *pProp;
		if (wcscmp(propGetName(prop), L"testName_60") != 0)
		{
			++errors_cnt;
			printFailed("name");
		}
		else
		{
			if (prop->valCount != 1)
			{
				++errors_cnt;
				printFailed("valCount");
			}
			else
			{
				if (wcslen(propGetSubval(prop, 0, None)) != 0)
				{
					++errors_cnt;
					printFailed("value");
				}
			}
		}
	}

	++tests_cnt;
	testNm = "itemSetProperty4";
	if (itemSetProperty(item, L"testName_60", L"   ") != EXIT_SUCCESS)
	{
		++errors_cnt;
		printFailed("init");
	}
	else
	{
		struct PropertyStruct **pProp = itemGetPropArrayAddrByNum(item, 5);
		struct PropertyStruct *prop = *pProp;
		if (wcscmp(propGetName(prop), L"testName_60") != 0)
		{
			++errors_cnt;
			printFailed("name");
		}
		else
		{
			if (prop->valCount != 1)
			{
				++errors_cnt;
				printFailed("valCount");
			}
			else
			{
				if (wcslen(propGetSubval(prop, 0, None)) != 0)
				{
					++errors_cnt;
					printFailed("value");
				}
			}
		}
	}

	++tests_cnt;
	testNm = "itemAddPropertiesRaw";
	if (itemAddPropertiesRaw(item, L"testName_50=@testName_70=testVal_71,  ,testVal_72,testVal_71, testVal_72 , ,@testName_1=testVal_1,testVal_9") != EXIT_SUCCESS)
	{
		++errors_cnt;
		printFailed("");
	}
	else
	{
		if (item->propsCount != 8)
		{
			++errors_cnt;
			printFailed("propsCount");
		}
		else
		{
			struct PropertyStruct **pProp = itemGetPropArrayAddrByNum(item, 0);
			struct PropertyStruct *prop = *pProp;
			if (prop->valCount != 4)
			{
				++errors_cnt;
				printFailed("valCount 1");
			}
			if (wcscmp(propGetSubval(prop, 3, None), L"testVal_9") != 0)
			{
				++errors_cnt;
				printFailed("testVal_9");
			}
			pProp = itemGetPropArrayAddrByNum(item, 6);
			prop = *pProp;
			if (wcscmp(propGetName(prop), L"testName_50") != 0)
			{
				++errors_cnt;
				printFailed("name 50");
			}
			else
			{
				if (prop->valCount != 1)
				{
					++errors_cnt;
					printFailed("valCount 50");
				}
				else if (wcslen(propGetSubval(prop, 0, None)) != 0)
				{
					++errors_cnt;
					printFailed("subval 50");
				}
			}
			pProp = itemGetPropArrayAddrByNum(item, 7);
			prop = *pProp;
			if (wcscmp(propGetName(prop), L"testName_70") != 0)
			{
				++errors_cnt;
				printFailed("name 70");
			}
			else
			{
				if (prop->valCount != 2)
				{
					++errors_cnt;
					printFailed("valCount 70");
				}
				else if (wcscmp(propGetSubval(prop, 1, None), L"testVal_72") != 0)
				{
					++errors_cnt;
					printFailed("subval 70");
				}
			}
		}
	}

	++tests_cnt;
	testNm = "itemSetPropertiesRaw";
	if (itemSetPropertiesRaw(item, L"testName_50=testVal_51,  ,testVal_52,testVal_51, testVal_52 , ,@testName_80=@testName_70= ") != EXIT_SUCCESS)
	{
		++errors_cnt;
		printFailed("");
	}
	else
	{
		if (item->propsCount != 9)
		{
			++errors_cnt;
			printFailed("propsCount");
		}
		else
		{
			struct PropertyStruct **pProp = itemGetPropArrayAddrByNum(item, 6);
			struct PropertyStruct *prop = *pProp;
			if (wcscmp(propGetName(prop), L"testName_50") != 0)
			{
				++errors_cnt;
				printFailed("name 50");
			}
			else if (prop->valCount != 2)
			{
				++errors_cnt;
				printFailed("valCount 50");
			}
			else if (wcscmp(propGetSubval(prop, 1, None), L"testVal_52") != 0)
			{
				++errors_cnt;
				printFailed("subval 50");
			}
			pProp = itemGetPropArrayAddrByNum(item, 7);
			prop = *pProp;
			if (wcscmp(propGetName(prop), L"testName_70") != 0)
			{
				++errors_cnt;
				printFailed("name 70");
			}
			else if (prop->valCount != 1)
			{
				++errors_cnt;
				printFailed("valCount 70");
			}
			else if (wcslen(propGetSubval(prop, 0, None)) != 0)
			{
				++errors_cnt;
				printFailed("subval 70");
			}
			pProp = itemGetPropArrayAddrByNum(item, 8);
			prop = *pProp;
			if (wcscmp(propGetName(prop), L"testName_80") != 0)
			{
				++errors_cnt;
				printFailed("name 80");
			}
			else if (prop->valCount != 1)
			{
				++errors_cnt;
				printFailed("valCount 80");
			}
			else if (wcslen(propGetSubval(prop, 0, None)) != 0)
			{
				++errors_cnt;
				printFailed("subval 80");
			}
		}
	}

	{
		++tests_cnt;
		testNm = "itemMerge";
		struct ItemStruct *item1 = itemInitFromRawData(4, L"a94a8fe5ccb19ba61c4c0873d391e987982fbbd3", L"testfile1", NULL, L"testName_10=testVal_10,testVal_11@testName_20=");
		struct ItemStruct *item2 = itemInitFromRawData(4, L"a94a8fe5ccb19ba61c4c0873d391e987982fbbd3", L"testfile2", NULL, L"testName_10=testVal_10,testVal_12@testName_20=testVal_20,testVal_21@testName_30=@testName_40=testVal_40");
		if (itemMerge(item1, item2) != EXIT_SUCCESS)
		{
			itemFree(item2);
			++errors_cnt;
			printFailed("");
		}
		else
		{
			if (item1->fileNameCount != 2)
			{
				++errors_cnt;
				printFailed("fileNameCount");
			}
			else
			{
				if (wcscmp(itemGetFileName(item1, 0), L"testfile1") != 0)
				{
					++errors_cnt;
					printFailed("fileName 1");
				}
				if (wcscmp(itemGetFileName(item1, 1), L"testfile2") != 0)
				{
					++errors_cnt;
					printFailed("fileName 2");
				}
			}
			if (item1->propsCount != 4)
			{
				++errors_cnt;
				printFailed("propsCount");
			}
			else
			{
				struct PropertyStruct **pProp = itemGetPropArrayAddrByNum(item1, 0);
				struct PropertyStruct *prop = *pProp;
				if (wcscmp(propGetName(prop), L"testName_10") != 0)
				{
					++errors_cnt;
					printFailed("name 10");
				}
				else if (prop->valCount != 3)
				{
					++errors_cnt;
					printFailed("valCount 10");
				}
				pProp = itemGetPropArrayAddrByNum(item1, 1);
				prop = *pProp;
				if (wcscmp(propGetName(prop), L"testName_20") != 0)
				{
					++errors_cnt;
					printFailed("name 20");
				}
				else if (prop->valCount != 2)
				{
					++errors_cnt;
					printFailed("valCount 20");
				}
				pProp = itemGetPropArrayAddrByNum(item1, 2);
				prop = *pProp;
				if (wcscmp(propGetName(prop), L"testName_30") != 0)
				{
					++errors_cnt;
					printFailed("name 30");
				}
				else if (prop->valCount != 1)
				{
					++errors_cnt;
					printFailed("valCount 30");
				}
				pProp = itemGetPropArrayAddrByNum(item1, 3);
				prop = *pProp;
				if (wcscmp(propGetName(prop), L"testName_40") != 0)
				{
					++errors_cnt;
					printFailed("name 40");
				}
				else if (prop->valCount != 1)
				{
					++errors_cnt;
					printFailed("valCount 40");
				}
			}
		}
		itemFree(item1);
	}

	{
		++tests_cnt;
		testNm = "itemIsEqual";
		struct ItemStruct *item1 = itemInitFromRawData(4, L"a94a8fe5ccb19ba61c4c0873d391e987982fbbd3", L"testfile1", NULL, L"testName_10=testVal_10,testVal_11@testName_20=testVal_20,testVal_21@testName_30=");
		struct ItemStruct *item2 = itemInit(4, L"a94a8fe5ccb19ba61c4c0873d391e987982fbbd3");
		if (itemIsEqual(item1, item2))
		{
			++errors_cnt;
			printFailed("check empty");
		}
		itemFree(item2);
		item2 = itemInitFromRawData(5, L"a94a8fe5ccb19ba61c4c0873d391e987982fbbd3", L"testfile1", NULL, L"testName_10=testVal_10,testVal_11@testName_20=testVal_20,testVal_21@testName_30=");
		if (itemIsEqual(item1, item2))
		{
			++errors_cnt;
			printFailed("check size");
		}
		itemFree(item2);
		item2 = itemInitFromRawData(4, L"badhashbadhashbadhashbadhashbadhashbadha", L"testfile1", NULL, L"testName_10=testVal_10,testVal_11@testName_20=testVal_20,testVal_21@testName_30=");
		if (itemIsEqual(item1, item2))
		{
			++errors_cnt;
			printFailed("check hash");
		}
		itemFree(item2);
		item2 = itemInitFromRawData(4, L"a94a8fe5ccb19ba61c4c0873d391e987982fbbd3", L"testfile1", NULL, L"testName_10=@testName_20=testVal_20,testVal_21@testName_30=");
		if (itemIsEqual(item1, item2))
		{
			++errors_cnt;
			printFailed("check 10");
		}
		itemFree(item2);
		item2 = itemInitFromRawData(4, L"a94a8fe5ccb19ba61c4c0873d391e987982fbbd3", L"testfile1", NULL, L"testName_10=testVal_11@testName_20=testVal_20,testVal_21@testName_30=");
		if (itemIsEqual(item1, item2))
		{
			++errors_cnt;
			printFailed("check 11");
		}
		itemFree(item2);
		item2 = itemInitFromRawData(4, L"a94a8fe5ccb19ba61c4c0873d391e987982fbbd3", L"testfile1", NULL, L"testName_10=testVal_10,testVal_11@testName_20=testVal_20@testName_30=");
		if (itemIsEqual(item1, item2))
		{
			++errors_cnt;
			printFailed("check 20");
		}
		itemFree(item2);
		item2 = itemInitFromRawData(4, L"a94a8fe5ccb19ba61c4c0873d391e987982fbbd3", L"testfile1", NULL, L"testName_10=testVal_10,testVal_11@testName_20=testVal_20,testVal_21");
		if (itemIsEqual(item1, item2))
		{
			++errors_cnt;
			printFailed("check 30");
		}
		itemFree(item2);
		item2 = itemInitFromRawData(4, L"a94a8fe5ccb19ba61c4c0873d391e987982fbbd3", L"testfile1", NULL, L"testName_20=testVal_20,testVal_21@testName_30=@testName_10=testVal_10,testVal_11");
		if (!itemIsEqual(item1, item2))
		{
			++errors_cnt;
			printFailed("check full");
		}
		itemFree(item2);
		item2 = itemInitFromRawData(4, L"a94a8fe5ccb19ba61c4c0873d391e987982fbbd3", L"testfile2", NULL, L"testName_20=testVal_20,testVal_21@testName_30=@testName_10=testVal_10,testVal_11");
		if (!itemIsEqual(item1, item2))
		{
			++errors_cnt;
			printFailed("check file");
		}
		itemFree(item2);
		itemFree(item1);
	}

	++tests_cnt;
	testNm = "itemDelPropertiesRaw";
	if (itemDelPropertiesRaw(item, L"testName_1=testVal_9@testName_10=testVal_11@testName_20@testName_30@testName_40@testName_50=testVal_51,testVal_52@testName_60@testName_80@testName_90") != EXIT_SUCCESS)
	{
		++errors_cnt;
		printFailed("");
	}
	else
	{
		if (item->propsCount != 4)
		{
			++errors_cnt;
			printFailed("propsCount");
		}
		else
		{
			struct PropertyStruct **pProp = itemGetPropArrayAddrByNum(item, 0);
			struct PropertyStruct *prop = *pProp;
			if (wcscmp(propGetName(prop), L"testName_1") != 0)
			{
				++errors_cnt;
				printFailed("name 1");
			}
			else if (prop->valCount != 3)
			{
				++errors_cnt;
				printFailed("valCount 1");
			}
			pProp = itemGetPropArrayAddrByNum(item, 1);
			prop = *pProp;
			if (wcscmp(propGetName(prop), L"testName_10") != 0)
			{
				++errors_cnt;
				printFailed("name 10");
			}
			else if (prop->valCount != 1)
			if (prop->valCount != 1)
			{
				++errors_cnt;
				printFailed("valCount 10");
			}
			pProp = itemGetPropArrayAddrByNum(item, 2);
			prop = *pProp;
			if (wcscmp(propGetName(prop), L"testName_50") != 0)
			{
				++errors_cnt;
				printFailed("name 50");
			}
			else if (prop->valCount != 1)
			{
				++errors_cnt;
				printFailed("valCount 50");
			}
			else if (wcslen(propGetSubval(prop, 0, None)) != 0)
			{
				++errors_cnt;
				printFailed("subval 50");
			}
			pProp = itemGetPropArrayAddrByNum(item, 3);
			prop = *pProp;
			if (wcscmp(propGetName(prop), L"testName_70") != 0)
			{
				++errors_cnt;
				printFailed("name 70");
			}
			else if (prop->valCount != 1)
			{
				++errors_cnt;
				printFailed("valCount 70");
			}
			else if (wcslen(propGetSubval(prop, 0, None)) != 0)
			{
				++errors_cnt;
				printFailed("subval 70");
			}
		}
	}

	{
		++tests_cnt;
		testNm = "itemPropertyValueToString";
		wchar_t *buff = malloc(4096);
		if (buff == NULL)
		{
			++errors_cnt;
			printFailed("malloc");
		}
		else
		{
			if (itemPropertyValueToString(item, 0, buff, 4096) != EXIT_SUCCESS)
			{
				++errors_cnt;
				printFailed("get 1");
			}
			else if (wcscmp(buff, L"testVal_1,testVal_2,testVal_3") != 0)
			{
				++errors_cnt;
				printFailed("str 1");
			}
			if (itemPropertyValueToString(item, 1, buff, 4096) != EXIT_SUCCESS)
			{
				++errors_cnt;
				printFailed("get 10");
			}
			else if (wcscmp(buff, L"testVal_10") != 0)
			{
				++errors_cnt;
				printFailed("str 10");
			}
			if (itemPropertyValueToString(item, 2, buff, 4096) != EXIT_SUCCESS)
			{
				++errors_cnt;
				printFailed("get 70");
			}
			else if (wcslen(buff) != 0)
			{
				++errors_cnt;
				printFailed("str 70");
			}
			free(buff);
		}
	}

	itemFree(item);
}

void testWhere()
{
	{
		++tests_cnt;
		testNm = "whereTest0";
		struct WhereStruct *whr;
		whr = whereInit(L"@");
		if (whr != NULL)
		{
			whereFree(whr);
			++errors_cnt;
			printFailed("@");
		}
		whr = whereInit(L"=");
		if (whr != NULL)
		{
			whereFree(whr);
			++errors_cnt;
			printFailed("=");
		}
		whr = whereInit(L"prop10=@");
		if (whr != NULL)
		{
			whereFree(whr);
			++errors_cnt;
			printFailed("prop10=@");
		}
		whr = whereInit(L"@prop10=");
		if (whr != NULL)
		{
			whereFree(whr);
			++errors_cnt;
			printFailed("@prop10=");
		}
	}
	++tests_cnt;
	testNm = "whereTest1";
	struct ItemStruct *item = itemInitFromRawData(4, L"a94a8fe5ccb19ba61c4c0873d391e987982fbbd3", L"testfile1", NULL, L"prop10=value11,value12,value13@prop20=value21,value22,value23@prop30=value31,value32,value33@prop40=");
	if (item == NULL)
	{
		++errors_cnt;
		printFailed("itemInit");
		return;
	}
	{
		struct WhereStruct *whr = whereInit(L"prop10=value11");
		if (whr == NULL)
		{
			++errors_cnt;
			printFailed("whereInit");
		}
		else
		{
			if (whr->condCount != 1)
			{
				++errors_cnt;
				printFailed("condCount");
			}
			else if (whereIsFiltered(whr, item))
			{
				++errors_cnt;
				printFailed("filtered");
			}
			whereFree(whr);
		}
	}
	{
		++tests_cnt;
		testNm = "whereTest2";
		struct WhereStruct *whr = whereInit(L"prop10");
		if (whr == NULL)
		{
			++errors_cnt;
			printFailed("whereInit");
		}
		else
		{
			if (whr->condCount != 1)
			{
				++errors_cnt;
				printFailed("condCount");
			}
			else if (whereIsFiltered(whr, item))
			{
				++errors_cnt;
				printFailed("filtered");
			}
			whereFree(whr);
		}
	}
	{
		++tests_cnt;
		testNm = "whereTest3";
		struct WhereStruct *whr = whereInit(L"prop10=value13");
		if (whr == NULL)
		{
			++errors_cnt;
			printFailed("whereInit");
		}
		else
		{
			if (whereIsFiltered(whr, item))
			{
				++errors_cnt;
				printFailed("filtered");
			}
			whereFree(whr);
		}
	}
	{
		++tests_cnt;
		testNm = "whereTest4";
		struct WhereStruct *whr = whereInit(L"prop10=value11,value13");
		if (whr == NULL)
		{
			++errors_cnt;
			printFailed("whereInit");
		}
		else
		{
			if (whereIsFiltered(whr, item))
			{
				++errors_cnt;
				printFailed("filtered");
			}
			whereFree(whr);
		}
	}
	{
		++tests_cnt;
		testNm = "whereTest5";
		struct WhereStruct *whr = whereInit(L"prop10=value19,value11");
		if (whr == NULL)
		{
			++errors_cnt;
			printFailed("whereInit");
		}
		else
		{
			if (whereIsFiltered(whr, item))
			{
				++errors_cnt;
				printFailed("filtered");
			}
			whereFree(whr);
		}
	}
	{
		++tests_cnt;
		testNm = "whereTest6";
		struct WhereStruct *whr = whereInit(L"prop10=value11@prop10=value12");
		if (whr == NULL)
		{
			++errors_cnt;
			printFailed("whereInit");
		}
		else
		{
			if (whereIsFiltered(whr, item))
			{
				++errors_cnt;
				printFailed("filtered");
			}
			whereFree(whr);
		}
	}
	{
		++tests_cnt;
		testNm = "whereTest7";
		struct WhereStruct *whr = whereInit(L"prop10=value19");
		if (whr == NULL)
		{
			++errors_cnt;
			printFailed("whereInit");
		}
		else
		{
			if (!whereIsFiltered(whr, item))
			{
				++errors_cnt;
				printFailed("filtered");
			}
			whereFree(whr);
		}
	}
	{
		++tests_cnt;
		testNm = "whereTest8";
		struct WhereStruct *whr = whereInit(L"prop10=value11@prop10=value19");
		if (whr == NULL)
		{
			++errors_cnt;
			printFailed("whereInit");
		}
		else
		{
			if (!whereIsFiltered(whr, item))
			{
				++errors_cnt;
				printFailed("filtered");
			}
			whereFree(whr);
		}
	}
	{
		++tests_cnt;
		testNm = "whereTest9";
		struct WhereStruct *whr = whereInit(L"prop10=value11@prop20=value22@prop30=value33");
		if (whr == NULL)
		{
			++errors_cnt;
			printFailed("whereInit");
		}
		else
		{
			if (whr->condCount != 3)
			{
				++errors_cnt;
				printFailed("condCount");
			}
			else if (whereIsFiltered(whr, item))
			{
				++errors_cnt;
				printFailed("filtered");
			}
			whereFree(whr);
		}
	}
	{
		++tests_cnt;
		testNm = "whereTest10";
		struct WhereStruct *whr = whereInit(L"prop10=value11,value12@prop20=value22,value23@prop30=value33,value39");
		if (whr == NULL)
		{
			++errors_cnt;
			printFailed("whereInit");
		}
		else
		{
			if (whereIsFiltered(whr, item))
			{
				++errors_cnt;
				printFailed("filtered");
			}
			whereFree(whr);
		}
	}
	{
		++tests_cnt;
		testNm = "whereTest11";
		struct WhereStruct *whr = whereInit(L"prop10=value12@prop30=value39");
		if (whr == NULL)
		{
			++errors_cnt;
			printFailed("whereInit");
		}
		else
		{
			if (!whereIsFiltered(whr, item))
			{
				++errors_cnt;
				printFailed("filtered");
			}
			whereFree(whr);
		}
	}
	{
		++tests_cnt;
		testNm = "whereTest12";
		struct WhereStruct *whr = whereInit(L"prop10=");
		if (whr == NULL)
		{
			++errors_cnt;
			printFailed("whereInit");
		}
		else
		{
			if (!whereIsFiltered(whr, item))
			{
				++errors_cnt;
				printFailed("filtered");
			}
			whereFree(whr);
		}
	}
	{
		++tests_cnt;
		testNm = "whereTest13";
		struct WhereStruct *whr = whereInit(L"prop90=");
		if (whr == NULL)
		{
			++errors_cnt;
			printFailed("whereInit");
		}
		else
		{
			if (whereIsFiltered(whr, item))
			{
				++errors_cnt;
				printFailed("filtered");
			}
			whereFree(whr);
		}
	}
	{
		++tests_cnt;
		testNm = "whereTest14";
		struct WhereStruct *whr = whereInit(L"prop90=@prop10=value11");
		if (whr == NULL)
		{
			++errors_cnt;
			printFailed("whereInit");
		}
		else
		{
			if (whereIsFiltered(whr, item))
			{
				++errors_cnt;
				printFailed("filtered");
			}
			whereFree(whr);
		}
	}
	{
		++tests_cnt;
		testNm = "whereTest15";
		struct WhereStruct *whr = whereInit(L"prop40");
		if (whr == NULL)
		{
			++errors_cnt;
			printFailed("whereInit");
		}
		else
		{
			if (whereIsFiltered(whr, item))
			{
				++errors_cnt;
				printFailed("filtered");
			}
			whereFree(whr);
		}
	}
	{
		++tests_cnt;
		testNm = "whereTest16";
		struct WhereStruct *whr = whereInit(L"prop40@prop40=");
		if (whr == NULL)
		{
			++errors_cnt;
			printFailed("whereInit");
		}
		else
		{
			if (whereIsFiltered(whr, item))
			{
				++errors_cnt;
				printFailed("filtered");
			}
			whereFree(whr);
		}
	}

	itemFree(item);
}

void testFields()
{
	++tests_cnt;
	testNm = "fieldsInit";
	struct FieldListStruct *fields = fieldsInit(L"@FileName1,@testName_1");
	if (fields != NULL)
	{
		fieldsFree(fields);
		++errors_cnt;
		printFailed("init 1");
	}
	fields = fieldsInit(L"@FileName,@FileSize,@FileSize,testName_10,,,testName_20,");
	if (fields == NULL)
	{
		++errors_cnt;
		printFailed("init 2");
		return;
	}
	else
	{
		if (fields->fieldsCount != 4)
		{
			++errors_cnt;
			printFailed("fieldsCount");
		}
		if (fields->colCount != 8)
		{
			++errors_cnt;
			printFailed("colCount");
		}
	}
	{
		++tests_cnt;
		testNm = "fieldsPrintRow";
		struct ItemStruct *item = itemInitFromRawData(4, L"a94a8fe5ccb19ba61c4c0873d391e987982fbbd3", L"testfile1", NULL, L"testName_10=testVal_10,,testVal_11,@testName_20=@testName_30=testVal_30");
		FILE *fd1 = tmpfile();
		if (fieldsPrintRow(fields, item, L"", fd1) != EXIT_SUCCESS)
		{
			++errors_cnt;
			printFailed("print 1");
		}
		itemSetPropertiesRaw(item, L"testName_20=, testVal_20 ,");
		if (fieldsPrintRow(fields, item, L"", fd1) != EXIT_SUCCESS)
		{
			++errors_cnt;
			printFailed("print 2");
		}
		fseek(fd1, 0L, SEEK_SET);
		char hash1[41]; hash1[0] = '\0';
		char hash2[41]; hash2[0] = '\0';
		sha1file(fd1, &hash1[0]);
		FILE *fd2 = tmpfile();
		char *testRes = "testfile1\t4\t4\ttestVal_10,testVal_11\t-\t-\t-\t-\n"
			"testfile1\t4\t4\ttestVal_10,testVal_11\t-\t-\ttestVal_20\t-\n";
		fwrite(testRes, 1, strlen(testRes), fd2);
		fseek(fd2, 0L, SEEK_SET);
		sha1file(fd2, &hash2[0]);
		if (strcmp(hash1, hash2) != 0)
		{
			++errors_cnt;
			printFailed("res");
		}
		fclose(fd2);
		fclose(fd1);
		itemFree(item);
	}

	fieldsFree(fields);
}

unsigned int propCommon(struct PropertyStruct *prop)
{
	unsigned int err = 0;
	if (prop->maxSize == 0)
	{
		++err;
		printFailed("maxSize == 0");
	}
	if (prop->curSize == 0)
	{
		++err;
		printFailed("curSize == 0");
	}
	if (prop->valuesOffset == 0)
	{
		++err;
		printFailed("valuesOffset == 0");
	}
	if (prop->arrayDirect != NULL)
	{
		++err;
		printFailed("arrayDirect != NULL");
	}
	if (prop->arrayByValue != NULL)
	{
		++err;
		printFailed("arrayByValue != NULL");
	}
	if (prop->arrayByUser != NULL)
	{
		++err;
		printFailed("arrayByUser != NULL");
	}
	if (prop->userData != 0)
	{
		++err;
		printFailed("userData != 0");
	}
	if (wcscmp(propGetName(prop), L"testName_123") != 0)
	{
		++err;
		printFailed("propGetName");
	}
	return err;
}

void printFailed(const char *descr)
{
	fprintf(stderr, "Test \"%s\"", testNm);
	if (descr != NULL)
	{
		fprintf(stderr, ", \t\"");
		fprintf(stderr, descr);
		fprintf(stderr, "\"");
	}
	fprintf(stderr, " \t\tFAILED\n");
}
