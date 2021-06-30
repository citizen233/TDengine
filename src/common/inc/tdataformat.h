/*
 * Copyright (c) 2019 TAOS Data, Inc. <jhtao@taosdata.com>
 *
 * This program is free software: you can use, redistribute, and/or modify
 * it under the terms of the GNU Affero General Public License, version 3
 * or later ("AGPL"), as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.
 *
 * You should have received a copy of the GNU Affero General Public License
 * along with this program. If not, see <http://www.gnu.org/licenses/>.
 */
#ifndef _TD_DATA_FORMAT_H_
#define _TD_DATA_FORMAT_H_

#include "os.h"
#include "talgo.h"
#include "ttype.h"
#include "tutil.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
  VarDataLenT len;
  uint8_t     data;
} SBinaryNullT;

typedef struct {
  VarDataLenT len;
  uint32_t    data;
} SNCharNullT;

extern const uint8_t      BoolNull;
extern const uint8_t      TinyintNull;
extern const uint16_t     SmallintNull;
extern const uint32_t     IntNull;
extern const uint64_t     BigintNull;
extern const uint64_t     TimestampNull;
extern const uint8_t      UTinyintNull;
extern const uint16_t     USmallintNull;
extern const uint32_t     UIntNull;
extern const uint64_t     UBigintNull;
extern const uint32_t     FloatNull;
extern const uint64_t     DoubleNull;
extern const SBinaryNullT BinaryNull;
extern const SNCharNullT  NcharNull;

#define STR_TO_VARSTR(x, str)                     \
  do {                                            \
    VarDataLenT __len = (VarDataLenT)strlen(str); \
    *(VarDataLenT *)(x) = __len;                  \
    memcpy(varDataVal(x), (str), __len);          \
  } while (0);

#define STR_WITH_MAXSIZE_TO_VARSTR(x, str, _maxs)                         \
  do {                                                                    \
    char *_e = stpncpy(varDataVal(x), (str), (_maxs)-VARSTR_HEADER_SIZE); \
    varDataSetLen(x, (_e - (x)-VARSTR_HEADER_SIZE));                      \
  } while (0)

#define STR_WITH_SIZE_TO_VARSTR(x, str, _size)  \
  do {                                          \
    *(VarDataLenT *)(x) = (VarDataLenT)(_size); \
    memcpy(varDataVal(x), (str), (_size));      \
  } while (0);

// ----------------- TSDB COLUMN DEFINITION
typedef struct {
  int8_t   type;    // Column type
  int16_t  colId;   // column ID
  uint16_t bytes;   // column bytes
  uint16_t offset;  // point offset in SDataRow/SKVRow after the header part.
} STColumn;

#define colType(col) ((col)->type)
#define colColId(col) ((col)->colId)
#define colBytes(col) ((col)->bytes)
#define colOffset(col) ((col)->offset)

#define colSetType(col, t) (colType(col) = (t))
#define colSetColId(col, id) (colColId(col) = (id))
#define colSetBytes(col, b) (colBytes(col) = (b))
#define colSetOffset(col, o) (colOffset(col) = (o))

// ----------------- TSDB SCHEMA DEFINITION
typedef struct {
  int      version;    // version
  int      numOfCols;  // Number of columns appended
  int      tlen;       // maximum length of a SDataRow without the header part (sizeof(VarDataOffsetT) + sizeof(VarDataLenT) + (bytes))
  uint16_t flen;       // First part length in a SDataRow after the header part
  uint16_t vlen;       // pure value part length, excluded the overhead (bytes only)
  STColumn columns[];
} STSchema;

#define schemaNCols(s) ((s)->numOfCols)
#define schemaVersion(s) ((s)->version)
#define schemaTLen(s) ((s)->tlen)
#define schemaFLen(s) ((s)->flen)
#define schemaVLen(s) ((s)->vlen)
#define schemaColAt(s, i) ((s)->columns + i)
#define tdFreeSchema(s) tfree((s))

STSchema *tdDupSchema(STSchema *pSchema);
int       tdEncodeSchema(void **buf, STSchema *pSchema);
void *    tdDecodeSchema(void *buf, STSchema **pRSchema);

static FORCE_INLINE int comparColId(const void *key1, const void *key2) {
  if (*(int16_t *)key1 > ((STColumn *)key2)->colId) {
    return 1;
  } else if (*(int16_t *)key1 < ((STColumn *)key2)->colId) {
    return -1;
  } else {
    return 0;
  }
}

static FORCE_INLINE STColumn *tdGetColOfID(STSchema *pSchema, int16_t colId) {
  void *ptr = bsearch(&colId, (void *)pSchema->columns, schemaNCols(pSchema), sizeof(STColumn), comparColId);
  if (ptr == NULL) return NULL;
  return (STColumn *)ptr;
}

// ----------------- SCHEMA BUILDER DEFINITION
typedef struct {
  int       tCols;
  int       nCols;
  int       tlen;
  uint16_t  flen;
  uint16_t  vlen;
  int       version;
  STColumn *columns;
} STSchemaBuilder;

int       tdInitTSchemaBuilder(STSchemaBuilder *pBuilder, int32_t version);
void      tdDestroyTSchemaBuilder(STSchemaBuilder *pBuilder);
void      tdResetTSchemaBuilder(STSchemaBuilder *pBuilder, int32_t version);
int       tdAddColToSchema(STSchemaBuilder *pBuilder, int8_t type, int16_t colId, int16_t bytes);
STSchema *tdGetSchemaFromBuilder(STSchemaBuilder *pBuilder);

// ----------------- Semantic timestamp key definition
typedef uint64_t TKEY;

#define TKEY_INVALID UINT64_MAX
#define TKEY_NULL TKEY_INVALID
#define TKEY_NEGATIVE_FLAG (((TKEY)1) << 63)
#define TKEY_DELETE_FLAG (((TKEY)1) << 62)
#define TKEY_VALUE_FILTER (~(TKEY_NEGATIVE_FLAG | TKEY_DELETE_FLAG))

#define TKEY_IS_NEGATIVE(tkey) (((tkey)&TKEY_NEGATIVE_FLAG) != 0)
#define TKEY_IS_DELETED(tkey) (((tkey)&TKEY_DELETE_FLAG) != 0)
#define tdSetTKEYDeleted(tkey) ((tkey) | TKEY_DELETE_FLAG)
#define tdGetTKEY(key) (((TKEY)ABS(key)) | (TKEY_NEGATIVE_FLAG & (TKEY)(key)))
#define tdGetKey(tkey) (((TSKEY)((tkey)&TKEY_VALUE_FILTER)) * (TKEY_IS_NEGATIVE(tkey) ? -1 : 1))

#define MIN_TS_KEY ((TSKEY)0x8000000000000001)
#define MAX_TS_KEY ((TSKEY)0x3fffffffffffffff)

#define TD_TO_TKEY(key) tdGetTKEY(((key) < MIN_TS_KEY) ? MIN_TS_KEY : (((key) > MAX_TS_KEY) ? MAX_TS_KEY : key))

static FORCE_INLINE TKEY keyToTkey(TSKEY key) {
  TSKEY lkey = key;
  if (key > MAX_TS_KEY) {
    lkey = MAX_TS_KEY;
  } else if (key < MIN_TS_KEY) {
    lkey = MIN_TS_KEY;
  }

  return tdGetTKEY(lkey);
}

static FORCE_INLINE int tkeyComparFn(const void *tkey1, const void *tkey2) {
  TSKEY key1 = tdGetKey(*(TKEY *)tkey1);
  TSKEY key2 = tdGetKey(*(TKEY *)tkey2);

  if (key1 < key2) {
    return -1;
  } else if (key1 > key2) {
    return 1;
  } else {
    return 0;
  }
}
// ----------------- Sequential Data row structure

/* A sequential data row, the format is like below:
 * |<--------------------+--------------------------- len ---------------------------------->|
 * |<--     Head      -->|<---------   flen -------------->|                                 |
 * +---------------------+---------------------------------+---------------------------------+
 * | uint16_t |  int16_t |                                 |                                 |
 * +----------+----------+---------------------------------+---------------------------------+
 * |   len    | sversion |           First part            |             Second part         |
 * +----------+----------+---------------------------------+---------------------------------+
 *
 * NOTE: timestamp in this row structure is TKEY instead of TSKEY
 */
typedef void *SDataRow;
/* A memory data row, the format is like below:
 *|---------+---------------------+--------------------------- len ---------------------------------->|
 *|<- type->|<--     Head      -->|<---------   flen -------------->|                                 |
 *|---------+---------------------+---------------------------------+---------------------------------+
 *| uint8_t | uint16_t |  int16_t |                                 |                                 |
 *|---------+----------+----------+---------------------------------+---------------------------------+
 *| flag    |   len    | sversion |           First part            |             Second part         |
 *|---------+----------+----------+---------------------------------+---------------------------------+
 *
 * NOTE: timestamp in this row structure is TKEY instead of TSKEY
 */
typedef void *SMemRow;

#define TD_DATA_ROW_HEAD_SIZE (sizeof(uint16_t) + sizeof(int16_t))

#define dataRowLen(r) (*(uint16_t *)(r))
#define dataRowVersion(r) *(int16_t *)POINTER_SHIFT(r, sizeof(int16_t))
#define dataRowTuple(r) POINTER_SHIFT(r, TD_DATA_ROW_HEAD_SIZE)
#define dataRowTKey(r) (*(TKEY *)(dataRowTuple(r)))
#define dataRowKey(r) tdGetKey(dataRowTKey(r))
#define dataRowSetLen(r, l) (dataRowLen(r) = (l))
#define dataRowSetVersion(r, v) (dataRowVersion(r) = (v))
#define dataRowCpy(dst, r) memcpy((dst), (r), dataRowLen(r))
#define dataRowMaxBytesFromSchema(s) (schemaTLen(s) + TD_DATA_ROW_HEAD_SIZE)
#define dataRowDeleted(r) TKEY_IS_DELETED(dataRowTKey(r))

// SDataRow tdNewDataRowFromSchema(STSchema *pSchema);
// void     tdFreeDataRow(SDataRow row);
void     tdInitDataRow(SDataRow row, STSchema *pSchema);
// SDataRow tdDataRowDup(SDataRow row);
SMemRow tdMemRowDup(SMemRow row);

// offset here not include dataRow header length
static FORCE_INLINE int tdAppendColVal(SDataRow row, const void *value, int8_t type, int32_t bytes, int32_t offset) {
  ASSERT(value != NULL);
  int32_t toffset = offset + TD_DATA_ROW_HEAD_SIZE;
  char *  ptr = (char *)POINTER_SHIFT(row, dataRowLen(row));

  if (IS_VAR_DATA_TYPE(type)) {
    *(VarDataOffsetT *)POINTER_SHIFT(row, toffset) = dataRowLen(row);
    memcpy(ptr, value, varDataTLen(value));
    dataRowLen(row) += varDataTLen(value);
  } else {
    if (offset == 0) {
      ASSERT(type == TSDB_DATA_TYPE_TIMESTAMP);
      TKEY tvalue = tdGetTKEY(*(TSKEY *)value);
      memcpy(POINTER_SHIFT(row, toffset), (void *)(&tvalue), TYPE_BYTES[type]);
    } else {
      memcpy(POINTER_SHIFT(row, toffset), value, TYPE_BYTES[type]);
    }
  }

  return 0;
}

// ----------------- Data column structure
typedef struct SDataCol {
  int8_t          type;       // column type
  int16_t         colId;      // column ID
  int             bytes;      // column data bytes defined
  int             offset;     // data offset in a SDataRow (including the header size)
  int             spaceSize;  // Total space size for this column
  int             len;        // column data length
  VarDataOffsetT *dataOff;    // For binary and nchar data, the offset in the data column
  void *          pData;      // Actual data pointer
  TSKEY           ts;         // only used in last NULL column
} SDataCol;

#define isAllRowOfColNull(pCol) ((pCol)->len == 0)
static FORCE_INLINE void dataColReset(SDataCol *pDataCol) { pDataCol->len = 0; }

void dataColInit(SDataCol *pDataCol, STColumn *pCol, void **pBuf, int maxPoints);
void dataColAppendVal(SDataCol *pCol, const void *value, int numOfRows, int maxPoints);
void dataColSetOffset(SDataCol *pCol, int nEle);

bool isNEleNull(SDataCol *pCol, int nEle);
void dataColSetNEleNull(SDataCol *pCol, int nEle, int maxPoints);

static const void *tdGetNullVal(int8_t type) {
  switch (type) {
    case TSDB_DATA_TYPE_BOOL:
      return &BoolNull;
    case TSDB_DATA_TYPE_TINYINT:
      return &TinyintNull;
    case TSDB_DATA_TYPE_SMALLINT:
      return &SmallintNull;
    case TSDB_DATA_TYPE_INT:
      return &IntNull;
    case TSDB_DATA_TYPE_BIGINT:
      return &BigintNull;
    case TSDB_DATA_TYPE_FLOAT:
      return &FloatNull;
    case TSDB_DATA_TYPE_DOUBLE:
      return &DoubleNull;
    case TSDB_DATA_TYPE_BINARY:
      return &BinaryNull;
    case TSDB_DATA_TYPE_TIMESTAMP:
      return &TimestampNull;
    case TSDB_DATA_TYPE_NCHAR:
      return &NcharNull;
    case TSDB_DATA_TYPE_UTINYINT:
      return &UTinyintNull;
    case TSDB_DATA_TYPE_USMALLINT:
      return &USmallintNull;
    case TSDB_DATA_TYPE_UINT:
      return &UIntNull;
    case TSDB_DATA_TYPE_UBIGINT:
      return &UBigintNull;
    default:
      ASSERT(0);
      return NULL;
  }
}
// Get the data pointer from a column-wised data
static FORCE_INLINE const void *tdGetColDataOfRow(SDataCol *pCol, int row) {
  if (isAllRowOfColNull(pCol)) {
    return tdGetNullVal(pCol->type);
  }
  if (IS_VAR_DATA_TYPE(pCol->type)) {
    return POINTER_SHIFT(pCol->pData, pCol->dataOff[row]);
  } else {
    return POINTER_SHIFT(pCol->pData, TYPE_BYTES[pCol->type] * row);
  }
}

static FORCE_INLINE int32_t dataColGetNEleLen(SDataCol *pDataCol, int rows) {
  ASSERT(rows > 0);

  if (IS_VAR_DATA_TYPE(pDataCol->type)) {
    return pDataCol->dataOff[rows - 1] + varDataTLen(tdGetColDataOfRow(pDataCol, rows - 1));
  } else {
    return TYPE_BYTES[pDataCol->type] * rows;
  }
}

typedef struct {
  int maxRowSize;
  int maxCols;    // max number of columns
  int maxPoints;  // max number of points
  int bufSize;

  int       numOfRows;
  int       numOfCols;  // Total number of cols
  int       sversion;   // TODO: set sversion
  void *    buf;
  SDataCol *cols;
} SDataCols;

#define keyCol(pCols) (&((pCols)->cols[0]))  // Key column
#define dataColsTKeyAt(pCols, idx) ((TKEY *)(keyCol(pCols)->pData))[(idx)]  // the idx row of column-wised data
#define dataColsKeyAt(pCols, idx) tdGetKey(dataColsTKeyAt(pCols, idx))
static FORCE_INLINE TKEY dataColsTKeyFirst(SDataCols *pCols) {
  if (pCols->numOfRows) {
    return dataColsTKeyAt(pCols, 0);
  } else {
    return TKEY_INVALID;
  }
}

static FORCE_INLINE TSKEY dataColsKeyFirst(SDataCols *pCols) {
  if (pCols->numOfRows) {
    return dataColsKeyAt(pCols, 0);
  } else {
    return TSDB_DATA_TIMESTAMP_NULL;
  }
}

static FORCE_INLINE TKEY dataColsTKeyLast(SDataCols *pCols) {
  if (pCols->numOfRows) {
    return dataColsTKeyAt(pCols, pCols->numOfRows - 1);
  } else {
    return TKEY_INVALID;
  }
}

static FORCE_INLINE TSKEY dataColsKeyLast(SDataCols *pCols) {
  if (pCols->numOfRows) {
    return dataColsKeyAt(pCols, pCols->numOfRows - 1);
  } else {
    return TSDB_DATA_TIMESTAMP_NULL;
  }
}

SDataCols *tdNewDataCols(int maxRowSize, int maxCols, int maxRows);
void       tdResetDataCols(SDataCols *pCols);
int        tdInitDataCols(SDataCols *pCols, STSchema *pSchema);
SDataCols *tdDupDataCols(SDataCols *pCols, bool keepData);
SDataCols *tdFreeDataCols(SDataCols *pCols);
void       tdAppendMemRowToDataCol(SMemRow row, STSchema *pSchema, SDataCols *pCols);
int        tdMergeDataCols(SDataCols *target, SDataCols *source, int rowsToMerge, int *pOffset);

// ----------------- K-V data row structure
/*
 * +----------+----------+---------------------------------+---------------------------------+
 * | uint16_t |  int16_t |                                 |                                 |
 * +----------+----------+---------------------------------+---------------------------------+
 * |    len   |   ncols  |           cols index            |             data part           |
 * +----------+----------+---------------------------------+---------------------------------+
 */
typedef void *SKVRow;

typedef struct {
  int16_t  colId;
  uint16_t offset;
} SColIdx;

#define TD_KV_ROW_HEAD_SIZE (sizeof(uint16_t) + sizeof(int16_t))

#define kvRowLen(r) (*(uint16_t *)(r))
#define kvRowNCols(r) (*(int16_t *)POINTER_SHIFT(r, sizeof(uint16_t)))
#define kvRowSetLen(r, len) kvRowLen(r) = (len)
#define kvRowSetNCols(r, n) kvRowNCols(r) = (n)
#define kvRowColIdx(r) (SColIdx *)POINTER_SHIFT(r, TD_KV_ROW_HEAD_SIZE)
#define kvRowValues(r) POINTER_SHIFT(r, TD_KV_ROW_HEAD_SIZE + sizeof(SColIdx) * kvRowNCols(r))
#define kvRowCpy(dst, r) memcpy((dst), (r), kvRowLen(r))
#define kvRowColVal(r, colIdx) POINTER_SHIFT(kvRowValues(r), (colIdx)->offset)
#define kvRowColIdxAt(r, i) (kvRowColIdx(r) + (i))
#define kvRowFree(r) tfree(r)
#define kvRowEnd(r) POINTER_SHIFT(r, kvRowLen(r))
#define kvRowVersion(r) (-1)

#define kvRowTKey(r) (*(TKEY *)(kvRowValues(r)))
#define kvRowKey(r) tdGetKey(kvRowTKey(r))
#define kvRowDeleted(r) TKEY_IS_DELETED(kvRowTKey(r))

SKVRow tdKVRowDup(SKVRow row);
int    tdSetKVRowDataOfCol(SKVRow *orow, int16_t colId, int8_t type, void *value);
int    tdEncodeKVRow(void **buf, SKVRow row);
void * tdDecodeKVRow(void *buf, SKVRow *row);
void   tdSortKVRowByColIdx(SKVRow row);

static FORCE_INLINE int comparTagId(const void *key1, const void *key2) {
  if (*(int16_t *)key1 > ((SColIdx *)key2)->colId) {
    return 1;
  } else if (*(int16_t *)key1 < ((SColIdx *)key2)->colId) {
    return -1;
  } else {
    return 0;
  }
}

static FORCE_INLINE void *tdGetKVRowValOfCol(SKVRow row, int16_t colId) {
  void *ret = taosbsearch(&colId, kvRowColIdx(row), kvRowNCols(row), sizeof(SColIdx), comparTagId, TD_EQ);
  if (ret == NULL) return NULL;
  return kvRowColVal(row, (SColIdx *)ret);
}

// offset here not include kvRow header length
static FORCE_INLINE int tdAppendKvColVal(SKVRow row, const void *value, int16_t colId, int8_t type, int32_t offset) {
  ASSERT(value != NULL);
  int32_t  toffset = offset + TD_KV_ROW_HEAD_SIZE;
  SColIdx *pColIdx = (SColIdx *)POINTER_SHIFT(row, toffset);
  char *   ptr = (char *)POINTER_SHIFT(row, kvRowLen(row));

  pColIdx->colId = colId;
  pColIdx->offset = kvRowLen(row);

  if (IS_VAR_DATA_TYPE(type)) {
    memcpy(ptr, value, varDataTLen(value));
    kvRowLen(row) += varDataTLen(value);
  } else {
    if (offset == 0) {
      ASSERT(type == TSDB_DATA_TYPE_TIMESTAMP);
      TKEY tvalue = tdGetTKEY(*(TSKEY *)value);
      memcpy(ptr, (void *)(&tvalue), TYPE_BYTES[type]);
    } else {
      memcpy(ptr, value, TYPE_BYTES[type]);
    }
    kvRowLen(row) += TYPE_BYTES[type];
  }

  return 0;
}

// ----------------- K-V data row builder
typedef struct {
  int16_t  tCols;
  int16_t  nCols;
  SColIdx *pColIdx;
  uint16_t alloc;
  uint16_t size;
  void *   buf;
} SKVRowBuilder;

int    tdInitKVRowBuilder(SKVRowBuilder *pBuilder);
void   tdDestroyKVRowBuilder(SKVRowBuilder *pBuilder);
void   tdResetKVRowBuilder(SKVRowBuilder *pBuilder);
SKVRow tdGetKVRowFromBuilder(SKVRowBuilder *pBuilder);

static FORCE_INLINE int tdAddColToKVRow(SKVRowBuilder *pBuilder, int16_t colId, int8_t type, void *value) {
  if (pBuilder->nCols >= pBuilder->tCols) {
    pBuilder->tCols *= 2;
    pBuilder->pColIdx = (SColIdx *)realloc((void *)(pBuilder->pColIdx), sizeof(SColIdx) * pBuilder->tCols);
    if (pBuilder->pColIdx == NULL) return -1;
  }

  pBuilder->pColIdx[pBuilder->nCols].colId = colId;
  pBuilder->pColIdx[pBuilder->nCols].offset = pBuilder->size;

  pBuilder->nCols++;

  int tlen = IS_VAR_DATA_TYPE(type) ? varDataTLen(value) : TYPE_BYTES[type];
  if (tlen > pBuilder->alloc - pBuilder->size) {
    while (tlen > pBuilder->alloc - pBuilder->size) {
      pBuilder->alloc *= 2;
    }
    pBuilder->buf = realloc(pBuilder->buf, pBuilder->alloc);
    if (pBuilder->buf == NULL) return -1;
  }

  memcpy(POINTER_SHIFT(pBuilder->buf, pBuilder->size), value, tlen);
  pBuilder->size += tlen;

  return 0;
}

// ----------------- Data row structure

// ----------------- Sequential Data row structure
/* A sequential data row, the format is like below:
 * |<--------------------+--------------------------- len ---------------------------------->|
 * |<--     Head      -->|<---------   flen -------------->|                                 |
 * +---------------------+---------------------------------+---------------------------------+
 * | uint16_t |  int16_t |                                 |                                 |
 * +----------+----------+---------------------------------+---------------------------------+
 * |   len    | sversion |           First part            |             Second part         |
 * +----------+----------+---------------------------------+---------------------------------+
 *
 * NOTE: timestamp in this row structure is TKEY instead of TSKEY
 */
// ----------------- K-V data row structure
/*
 * +----------+----------+---------------------------------+---------------------------------+
 * |  int16_t |  int16_t |                                 |                                 |
 * +----------+----------+---------------------------------+---------------------------------+
 * |    len   |   ncols  |           cols index            |             data part           |
 * +----------+----------+---------------------------------+---------------------------------+
 */

#define TD_MEM_ROW_TYPE_SIZE sizeof(uint8_t)
#define TD_MEM_ROW_HEAD_SIZE (TD_MEM_ROW_TYPE_SIZE + sizeof(uint16_t) + sizeof(int16_t))

#define SMEM_ROW_DATA 0U  // SDataRow
#define SMEM_ROW_KV 1U    // SKVRow
#define TD_DO_NOTHING \
  do {                \
  } while (0)

#define memRowType(r) (*(uint8_t *)(r))
#define memRowBody(r) POINTER_SHIFT(r, TD_MEM_ROW_TYPE_SIZE)
#define memRowLen(r) (*(uint16_t *)POINTER_SHIFT(r, TD_MEM_ROW_TYPE_SIZE))
#define memRowTLen(r) (*(uint16_t *)POINTER_SHIFT(r, TD_MEM_ROW_TYPE_SIZE) + (uint16_t)TD_MEM_ROW_TYPE_SIZE)

#define isDataRow(r) (SMEM_ROW_DATA == memRowType(r))
#define isKvRow(r) (SMEM_ROW_KV == memRowType(r))
#define memRowVersion(r) (isDataRow(r) ? dataRowVersion(memRowBody(r)) : kvRowVersion(r))  // schema version

#define memRowTuple(r) (isDataRow(r) ? dataRowTuple(memRowBody(r)) : kvRowValues(memRowBody(r)))

#define memRowTKey(r) (isDataRow(r) ? dataRowTKey(memRowBody(r)) : kvRowTKey(memRowBody(r)))
#define memRowKey(r) (isDataRow(r) ? dataRowKey(memRowBody(r)) : kvRowKey(memRowBody(r)))

#define memRowSetType(r, t) (memRowType(r) = (t))
#define memRowSetLen(r, l) (memRowLen(r) = (l))
#define memRowSetVersion(r, v) (isDataRow(r) ? dataRowSetVersion(r, v) : TD_DO_NOTHING)
#define memRowCpy(dst, r) memcpy((dst), (r), memRowTLen(r))
#define memRowMaxBytesFromSchema(s) (schemaTLen(s) + TD_MEM_ROW_HEAD_SIZE)
#define memRowDeleted(r) TKEY_IS_DELETED(memRowTKey(r))

// #define memRowNCols(r) (*(int16_t *)POINTER_SHIFT(r, TD_MEM_ROW_TYPE_SIZE + sizeof(int16_t)))      // for SKVRow
// #define memRowSetNCols(r, n) memRowNCols(r) = (n)                                                  // for SKVRow
// #define memRowColIdx(r) (SColIdx *)POINTER_SHIFT(r, TD_MEM_ROW_HEAD_SIZE)                          // for SKVRow
// #define memRowValues(r) POINTER_SHIFT(r, TD_MEM_ROW_HEAD_SIZE + sizeof(SColIdx) * memRowNCols(r))  // for SKVRow

// NOTE: offset here including the header size
static FORCE_INLINE void *tdGetRowDataOfCol(void *row, int8_t type, int32_t offset) {
  if (IS_VAR_DATA_TYPE(type)) {
    return POINTER_SHIFT(row, *(VarDataOffsetT *)POINTER_SHIFT(row, offset));
  } else {
    return POINTER_SHIFT(row, offset);
  }
  return NULL;
}
static FORCE_INLINE void *tdGetKvRowDataOfCol(void *row, int8_t type, int32_t offset) {
  return POINTER_SHIFT(row, offset);
}

static FORCE_INLINE void *tdGetMemRowDataOfCol(void *row, int8_t type, int32_t offset) {
  if (isDataRow(row)) {
    return tdGetRowDataOfCol(row, type, offset);
  } else if (isKvRow(row)) {
    return tdGetKvRowDataOfCol(row, type, offset);
  } else {
    ASSERT(0);
  }
  return NULL;
}

// #define kvRowCpy(dst, r) memcpy((dst), (r), kvRowLen(r))
// #define kvRowColVal(r, colIdx) POINTER_SHIFT(kvRowValues(r), (colIdx)->offset)
// #define kvRowColIdxAt(r, i) (kvRowColIdx(r) + (i))
// #define kvRowFree(r) tfree(r)
// #define kvRowEnd(r) POINTER_SHIFT(r, kvRowLen(r))

#ifdef __cplusplus
}
#endif

#endif  // _TD_DATA_FORMAT_H_
