/*
 * hashfuncs.c
 *		Functions to investigate the content of HASH indexes
 *
 * Copyright (c) 2017, PostgreSQL Global Development Group
 *
 * IDENTIFICATION
 *		contrib/pageinspect/hashfuncs.c
 */

#include "postgres.h"

#include "access/hash.h"
#include "access/htup_details.h"
#include "catalog/pg_type.h"
#include "catalog/pg_am.h"
#include "funcapi.h"
#include "miscadmin.h"
#include "utils/builtins.h"

PG_FUNCTION_INFO_V1(hash_page_type);
PG_FUNCTION_INFO_V1(hash_page_stats);
PG_FUNCTION_INFO_V1(hash_page_items);
PG_FUNCTION_INFO_V1(hash_bitmap_info);
PG_FUNCTION_INFO_V1(hash_metapage_info);

#define IS_HASH(r) ((r)->rd_rel->relam == HASH_AM_OID)

/* ------------------------------------------------
 * structure for single hash page statistics
 * ------------------------------------------------
 */
typedef struct HashPageStat
{
	int		live_items;
	int		dead_items;
	int		page_size;
	int		free_size;

	/* opaque data */
	BlockNumber hasho_prevblkno;
	BlockNumber hasho_nextblkno;
	Bucket		hasho_bucket;
	uint16		hasho_flag;
	uint16		hasho_page_id;
}	HashPageStat;


/*
 * Verify that the given bytea contains a HASH page, or die in the attempt.
 * A pointer to the page is returned.
 */
static Page
verify_hash_page(bytea *raw_page, int flags)
{
	Page		page;
	int			raw_page_size;
	int			pagetype;
	HashPageOpaque pageopaque;

	raw_page_size = VARSIZE(raw_page) - VARHDRSZ;

	if (raw_page_size != BLCKSZ)
		ereport(ERROR,
				(errcode(ERRCODE_INVALID_PARAMETER_VALUE),
				 errmsg("invalid page size"),
				 errdetail("Expected size %d, got %d",
						   BLCKSZ, raw_page_size)));

	page = VARDATA(raw_page);

	if (PageIsNew(page))
		ereport(ERROR,
				(errcode(ERRCODE_INDEX_CORRUPTED),
				 errmsg("index table contains zero page")));

	if (PageGetSpecialSize(page) != MAXALIGN(sizeof(HashPageOpaqueData)))
		ereport(ERROR,
				(errcode(ERRCODE_INDEX_CORRUPTED),
				 errmsg("index table contains corrupted page")));

	pageopaque = (HashPageOpaque) PageGetSpecialPointer(page);
	if (pageopaque->hasho_page_id != HASHO_PAGE_ID)
		ereport(ERROR,
				(errcode(ERRCODE_INVALID_PARAMETER_VALUE),
				 errmsg("page is not a hash page"),
				 errdetail("Expected %08x, got %08x.",
						   HASHO_PAGE_ID, pageopaque->hasho_page_id)));

	/* Check that page type is sane. */
	pagetype = pageopaque->hasho_flag & LH_PAGE_TYPE;
	if (pagetype != LH_OVERFLOW_PAGE && pagetype != LH_BUCKET_PAGE &&
		pagetype != LH_BITMAP_PAGE && pagetype != LH_META_PAGE)
		ereport(ERROR,
				(errcode(ERRCODE_INVALID_PARAMETER_VALUE),
				 errmsg("invalid hash page type %08x", pagetype)));

	/* If requested, verify page type. */
	if (flags != 0 && (pagetype & flags) == 0)
	{
		switch (flags)
		{
			case LH_META_PAGE:
				ereport(ERROR,
						(errcode(ERRCODE_INVALID_PARAMETER_VALUE),
						 errmsg("page is not a hash meta page")));
			case LH_BUCKET_PAGE | LH_OVERFLOW_PAGE:
				ereport(ERROR,
						(errcode(ERRCODE_INVALID_PARAMETER_VALUE),
						 errmsg("page is not a hash bucket or overflow page")));
			case LH_OVERFLOW_PAGE:
				ereport(ERROR,
						(errcode(ERRCODE_INVALID_PARAMETER_VALUE),
						 errmsg("page is not a hash overflow page")));
			default:
				elog(ERROR,
					 "hash page of type %08x not in mask %08x",
					pagetype, flags);
		}
	}

	/*
	 * If it is the metapage, also verify magic number and version.
	 */
	if (pagetype == LH_META_PAGE)
	{
		HashMetaPage metap = HashPageGetMeta(page);

		if (metap->hashm_magic != HASH_MAGIC)
			ereport(ERROR,
					(errcode(ERRCODE_INDEX_CORRUPTED),
					 errmsg("invalid magic number for metadata"),
					 errdetail("Expected 0x%08x, got 0x%08x.",
							   HASH_MAGIC, metap->hashm_magic)));

		if (metap->hashm_version != HASH_VERSION)
			ereport(ERROR,
					(errcode(ERRCODE_INDEX_CORRUPTED),
					 errmsg("invalid version for metadata"),
					 errdetail("Expected %d, got %d",
							   HASH_VERSION, metap->hashm_version)));
	}

	return page;
}

/* -------------------------------------------------
 * GetHashPageStatistics()
 *
 * Collect statistics of single hash page
 * -------------------------------------------------
 */
static void
GetHashPageStatistics(Page page, HashPageStat * stat)
{
	OffsetNumber maxoff = PageGetMaxOffsetNumber(page);
	HashPageOpaque opaque = (HashPageOpaque) PageGetSpecialPointer(page);
	int			off;

	stat->dead_items = stat->live_items = 0;
	stat->page_size = PageGetPageSize(page);

	/* hash page opaque data */
	stat->hasho_prevblkno = opaque->hasho_prevblkno;
	stat->hasho_nextblkno = opaque->hasho_nextblkno;
	stat->hasho_bucket = opaque->hasho_bucket;
	stat->hasho_flag = opaque->hasho_flag;
	stat->hasho_page_id = opaque->hasho_page_id;

	/* count live and dead tuples, and free space */
	for (off = FirstOffsetNumber; off <= maxoff; off++)
	{
		ItemId		id = PageGetItemId(page, off);

		if (!ItemIdIsDead(id))
			stat->live_items++;
		else
			stat->dead_items++;
	}
	stat->free_size = PageGetFreeSpace(page);
}

/* ---------------------------------------------------
 * hash_page_type()
 *
 * Usage: SELECT hash_page_type(get_raw_page('con_hash_index', 1));
 * ---------------------------------------------------
 */
Datum
hash_page_type(PG_FUNCTION_ARGS)
{
	bytea	   *raw_page = PG_GETARG_BYTEA_P(0);
	Page		page;
	HashPageOpaque opaque;
	char	   *type;

	if (!superuser())
		ereport(ERROR,
				(errcode(ERRCODE_INSUFFICIENT_PRIVILEGE),
				 (errmsg("must be superuser to use raw page functions"))));

	page = verify_hash_page(raw_page, 0);
	opaque = (HashPageOpaque) PageGetSpecialPointer(page);

	/* page type (flags) */
	if (opaque->hasho_flag & LH_META_PAGE)
		type = "metapage";
	else if (opaque->hasho_flag & LH_OVERFLOW_PAGE)
		type = "overflow";
	else if (opaque->hasho_flag & LH_BUCKET_PAGE)
		type = "bucket";
	else if (opaque->hasho_flag & LH_BITMAP_PAGE)
		type = "bitmap";
	else
		type = "unused";

	PG_RETURN_TEXT_P(cstring_to_text(type));
}

/* ---------------------------------------------------
 * hash_page_stats()
 *
 * Usage: SELECT * FROM hash_page_stats(get_raw_page('con_hash_index', 1));
 * ---------------------------------------------------
 */
Datum
hash_page_stats(PG_FUNCTION_ARGS)
{
	bytea	   *raw_page = PG_GETARG_BYTEA_P(0);
	Page		page;
	int			j;
	Datum		values[9];
	bool		nulls[9];
	HashPageStat stat;
	HeapTuple	tuple;
	TupleDesc	tupleDesc;

	if (!superuser())
		ereport(ERROR,
				(errcode(ERRCODE_INSUFFICIENT_PRIVILEGE),
				 (errmsg("must be superuser to use raw page functions"))));

	page = verify_hash_page(raw_page, LH_BUCKET_PAGE | LH_OVERFLOW_PAGE);

	/* keep compiler quiet */
	stat.hasho_prevblkno = stat.hasho_nextblkno = InvalidBlockNumber;
	stat.hasho_flag = stat.hasho_page_id = stat.free_size = 0;

	GetHashPageStatistics(page, &stat);

	/* Build a tuple descriptor for our result type */
	if (get_call_result_type(fcinfo, NULL, &tupleDesc) != TYPEFUNC_COMPOSITE)
		elog(ERROR, "return type must be a row type");
	tupleDesc = BlessTupleDesc(tupleDesc);

	MemSet(nulls, 0, sizeof(nulls));

	j = 0;
	values[j++] = Int32GetDatum(stat.live_items);
	values[j++] = Int32GetDatum(stat.dead_items);
	values[j++] = Int32GetDatum(stat.page_size);
	values[j++] = Int32GetDatum(stat.free_size);
	values[j++] = Int64GetDatum((int64) stat.hasho_prevblkno);
	values[j++] = Int64GetDatum((int64) stat.hasho_nextblkno);
	values[j++] = Int64GetDatum((int64) stat.hasho_bucket);
	values[j++] = Int32GetDatum((int32) stat.hasho_flag);
	values[j++] = Int32GetDatum((int32) stat.hasho_page_id);

	tuple = heap_form_tuple(tupleDesc, values, nulls);

	PG_RETURN_DATUM(HeapTupleGetDatum(tuple));
}

/*
 * cross-call data structure for SRF
 */
struct user_args
{
	Page		page;
	OffsetNumber offset;
};

/*-------------------------------------------------------
 * hash_page_items()
 *
 * Get IndexTupleData set in a hash page
 *
 * Usage: SELECT * FROM hash_page_items(get_raw_page('con_hash_index', 1));
 *-------------------------------------------------------
 */
Datum
hash_page_items(PG_FUNCTION_ARGS)
{
	bytea	   *raw_page = PG_GETARG_BYTEA_P(0);
	Page		page;
	Datum		result;
	Datum		values[3];
	bool		nulls[3];
	uint32		hashkey;
	HeapTuple	tuple;
	FuncCallContext *fctx;
	MemoryContext mctx;
	struct user_args *uargs;

	if (!superuser())
		ereport(ERROR,
				(errcode(ERRCODE_INSUFFICIENT_PRIVILEGE),
				 (errmsg("must be superuser to use raw page functions"))));

	if (SRF_IS_FIRSTCALL())
	{
		TupleDesc	tupleDesc;

		fctx = SRF_FIRSTCALL_INIT();

		page = verify_hash_page(raw_page, LH_BUCKET_PAGE | LH_OVERFLOW_PAGE);

		mctx = MemoryContextSwitchTo(fctx->multi_call_memory_ctx);

		uargs = palloc(sizeof(struct user_args));

		uargs->page = page;

		uargs->offset = FirstOffsetNumber;

		fctx->max_calls = PageGetMaxOffsetNumber(uargs->page);

		/* Build a tuple descriptor for our result type */
		if (get_call_result_type(fcinfo, NULL, &tupleDesc) != TYPEFUNC_COMPOSITE)
			elog(ERROR, "return type must be a row type");
		tupleDesc = BlessTupleDesc(tupleDesc);

		fctx->attinmeta = TupleDescGetAttInMetadata(tupleDesc);

		fctx->user_fctx = uargs;

		MemoryContextSwitchTo(mctx);
	}

	fctx = SRF_PERCALL_SETUP();
	uargs = fctx->user_fctx;

	if (fctx->call_cntr < fctx->max_calls)
	{
		ItemId		id;
		IndexTuple	itup;
		int			j;

		id = PageGetItemId(uargs->page, uargs->offset);

		if (!ItemIdIsValid(id))
			elog(ERROR, "invalid ItemId");

		itup = (IndexTuple) PageGetItem(uargs->page, id);

		MemSet(nulls, 0, sizeof(nulls));

		j = 0;
		values[j++] = UInt16GetDatum(uargs->offset);
		values[j++] = PointerGetDatum(&itup->t_tid);

		hashkey = _hash_get_indextuple_hashkey(itup);
		values[j] = UInt32GetDatum(hashkey);

		tuple = heap_form_tuple(fctx->attinmeta->tupdesc, values, nulls);
		result = HeapTupleGetDatum(tuple);

		uargs->offset = uargs->offset + 1;

		SRF_RETURN_NEXT(fctx, result);
	}
	else
	{
		pfree(uargs);
		SRF_RETURN_DONE(fctx);
	}
}

/* ------------------------------------------------
 * hash_bitmap_info()
 *
 * Get bitmap information for a particular overflow page
 *
 * Usage: SELECT * FROM hash_bitmap_info('con_hash_index'::regclass, 5);
 * ------------------------------------------------
 */
Datum
hash_bitmap_info(PG_FUNCTION_ARGS)
{
	Oid			indexRelid = PG_GETARG_OID(0);
	BlockNumber ovflblkno = (BlockNumber) PG_GETARG_INT64(1);
	HashMetaPage metap;
	Buffer		buf,
				metabuf;
	BlockNumber bitmapblkno;
	Page		page;
	bool		bit = false;
	HashPageOpaque	opaque;
	TupleDesc	tupleDesc;
	Relation	indexRel;
	uint32		ovflbitno;
	int32		bitmappage,
				bitmapbit;
	HeapTuple	tuple;
	int			j;
	Datum		values[3];
	bool		nulls[3];

	if (!superuser())
		ereport(ERROR,
				(errcode(ERRCODE_INSUFFICIENT_PRIVILEGE),
				 (errmsg("must be superuser to use raw page functions"))));

	indexRel = index_open(indexRelid, AccessShareLock);

	if (!IS_HASH(indexRel))
		elog(ERROR, "relation \"%s\" is not a hash index",
			 RelationGetRelationName(indexRel));

	if (RELATION_IS_OTHER_TEMP(indexRel))
		ereport(ERROR,
				(errcode(ERRCODE_FEATURE_NOT_SUPPORTED),
				 errmsg("cannot access temporary tables of other sessions")));

	if (RelationGetNumberOfBlocks(indexRel) <= (BlockNumber) (ovflblkno))
		ereport(ERROR,
				(errcode(ERRCODE_INVALID_PARAMETER_VALUE),
				 errmsg("block number %u is out of range for relation \"%s\"",
						ovflblkno, RelationGetRelationName(indexRel))));

	buf = ReadBufferExtended(indexRel, MAIN_FORKNUM, ovflblkno, RBM_NORMAL, NULL);
	LockBuffer(buf, BUFFER_LOCK_SHARE);
	_hash_checkpage(indexRel, buf, LH_PAGE_TYPE);
	page = BufferGetPage(buf);
	opaque = (HashPageOpaque) PageGetSpecialPointer(page);

	if (opaque->hasho_flag != LH_OVERFLOW_PAGE)
		ereport(ERROR,
				(errcode(ERRCODE_INVALID_PARAMETER_VALUE),
				 errmsg("page is not an overflow page"),
				 errdetail("Expected %08x, got %08x.",
							LH_OVERFLOW_PAGE, opaque->hasho_flag)));

	if (BlockNumberIsValid(opaque->hasho_prevblkno))
		bit = true;

	UnlockReleaseBuffer(buf);

	/* Read the metapage so we can determine which bitmap page to use */
	metabuf = _hash_getbuf(indexRel, HASH_METAPAGE, HASH_READ, LH_META_PAGE);
	metap = HashPageGetMeta(BufferGetPage(metabuf));

	/* Identify overflow bit number */
	ovflbitno = _hash_ovflblkno_to_bitno(metap, ovflblkno);

	bitmappage = ovflbitno >> BMPG_SHIFT(metap);
	bitmapbit = ovflbitno & BMPG_MASK(metap);

	if (bitmappage >= metap->hashm_nmaps)
		ereport(ERROR,
				(errcode(ERRCODE_INVALID_PARAMETER_VALUE),
				 errmsg("invalid overflow bit number %u", ovflbitno)));

	bitmapblkno = metap->hashm_mapp[bitmappage];

	_hash_relbuf(indexRel, metabuf);

	index_close(indexRel, AccessShareLock);

	/* Build a tuple descriptor for our result type */
	if (get_call_result_type(fcinfo, NULL, &tupleDesc) != TYPEFUNC_COMPOSITE)
		elog(ERROR, "return type must be a row type");
	tupleDesc = BlessTupleDesc(tupleDesc);

	MemSet(nulls, 0, sizeof(nulls));

	j = 0;
	values[j++] = UInt32GetDatum(bitmapblkno);
	values[j++] = Int32GetDatum(bitmapbit);
	values[j++] = BoolGetDatum(bit);

	tuple = heap_form_tuple(tupleDesc, values, nulls);

	PG_RETURN_DATUM(HeapTupleGetDatum(tuple));
}

/* ------------------------------------------------
 * hash_metapage_info()
 *
 * Get the meta-page information for a hash index
 *
 * Usage: SELECT * FROM hash_metapage_info(get_raw_page('con_hash_index', 0))
 * ------------------------------------------------
 */
Datum
hash_metapage_info(PG_FUNCTION_ARGS)
{
	bytea	   *raw_page = PG_GETARG_BYTEA_P(0);
	Page		page;
	HashMetaPageData *metad;
	TupleDesc	tupleDesc;
	HeapTuple	tuple;
	int			i,
				j;
	Datum		values[16];
	bool		nulls[16];
	Datum       spares[HASH_MAX_SPLITPOINTS];
	Datum       mapp[HASH_MAX_BITMAPS];

	if (!superuser())
		ereport(ERROR,
				(errcode(ERRCODE_INSUFFICIENT_PRIVILEGE),
				 (errmsg("must be superuser to use raw page functions"))));

	page = verify_hash_page(raw_page, LH_META_PAGE);

	/* Build a tuple descriptor for our result type */
	if (get_call_result_type(fcinfo, NULL, &tupleDesc) != TYPEFUNC_COMPOSITE)
		elog(ERROR, "return type must be a row type");
	tupleDesc = BlessTupleDesc(tupleDesc);

	metad = HashPageGetMeta(page);

	MemSet(nulls, 0, sizeof(nulls));

	j = 0;
	values[j++] = UInt32GetDatum(metad->hashm_magic);
	values[j++] = UInt32GetDatum(metad->hashm_version);
	values[j++] = Float8GetDatum(metad->hashm_ntuples);
	values[j++] = UInt16GetDatum(metad->hashm_ffactor);
	values[j++] = UInt16GetDatum(metad->hashm_bsize);
	values[j++] = UInt16GetDatum(metad->hashm_bmsize);
	values[j++] = UInt16GetDatum(metad->hashm_bmshift);
	values[j++] = UInt32GetDatum(metad->hashm_maxbucket);
	values[j++] = UInt32GetDatum(metad->hashm_highmask);
	values[j++] = UInt32GetDatum(metad->hashm_lowmask);
	values[j++] = UInt32GetDatum(metad->hashm_ovflpoint);
	values[j++] = UInt32GetDatum(metad->hashm_firstfree);
	values[j++] = UInt32GetDatum(metad->hashm_nmaps);
	values[j++] = UInt16GetDatum(metad->hashm_procid);

	for (i = 0; i < HASH_MAX_SPLITPOINTS; i++)
		spares[i] = UInt32GetDatum(metad->hashm_spares[i]);
	values[j++] = PointerGetDatum(construct_array(spares,
												  HASH_MAX_SPLITPOINTS,
												  INT8OID,
												  8, true, 'd'));

	for (i = 0; i < HASH_MAX_BITMAPS; i++)
		mapp[i] = UInt32GetDatum(metad->hashm_mapp[i]);
	values[j++] = PointerGetDatum(construct_array(mapp,
												  HASH_MAX_BITMAPS,
												  INT8OID,
												  8, true, 'd'));

	tuple = heap_form_tuple(tupleDesc, values, nulls);

	PG_RETURN_DATUM(HeapTupleGetDatum(tuple));
}
