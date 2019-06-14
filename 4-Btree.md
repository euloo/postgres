# Btree

~~~
euloo@euloo:~/project/bin$ ./postgres -D ~/project/DemoDb
~~~
~~~
euloo@euloo:~/project/bin$ ./psql postgres -c "create table sample(id text, value text); create index idx on sample(id); insert into sample select 'key'||x as id, 'value'||x as value from generate_series(1,1e5) x;"
INSERT 0 100000
~~~
### src\backend\access\nbtree\nbtsearch.c
~~~
*
 *	_bt_binsrch() -- Do a binary search for a key on a particular page.
 *
 * The passed scankey must be an insertion-type scankey (see nbtree/README),
 * but it can omit the rightmost column(s) of the index.
 *
 * When nextkey is false (the usual case), we are looking for the first
 * item >= scankey.  When nextkey is true, we are looking for the first
 * item strictly greater than scankey.
 *
 * On a leaf page, _bt_binsrch() returns the OffsetNumber of the first
 * key >= given scankey, or > scankey if nextkey is true.  (NOTE: in
 * particular, this means it is possible to return a value 1 greater than the
 * number of keys on the page, if the scankey is > all keys on the page.)
 *
 * On an internal (non-leaf) page, _bt_binsrch() returns the OffsetNumber
 * of the last key < given scankey, or last key <= given scankey if nextkey
 * is true.  (Since _bt_compare treats the first data key of such a page as
 * minus infinity, there will be at least one key < scankey, so the result
 * always points at one of the keys on the page.)  This key indicates the
 * right place to descend to be sure we find all leaf keys >= given scankey
 * (or leaf keys > given scankey when nextkey is true).
 *
 * This procedure is not responsible for walking right, it just examines
 * the given page.  _bt_binsrch() has no lock or refcount side effects
 * on the buffer.
 */
OffsetNumber
_bt_binsrch(Relation rel,
			Buffer buf,
			int keysz,
			ScanKey scankey,
			bool nextkey)
{
	Page		page;
	BTPageOpaque opaque;
	OffsetNumber low,
				high;
	int32		result,
				cmpval;

	page = BufferGetPage(buf);
	opaque = (BTPageOpaque) PageGetSpecialPointer(page);

	low = P_FIRSTDATAKEY(opaque);
	high = PageGetMaxOffsetNumber(page);

	/*
	 * If there are no keys on the page, return the first available slot. Note
	 * this covers two cases: the page is really empty (no keys), or it
	 * contains only a high key.  The latter case is possible after vacuuming.
	 * This can never happen on an internal page, however, since they are
	 * never empty (an internal page must have children).
	 */
	if (high < low)
		return low;

	/*
	 * Binary search to find the first key on the page >= scan key, or first
	 * key > scankey when nextkey is true.
	 *
	 * For nextkey=false (cmpval=1), the loop invariant is: all slots before
	 * 'low' are < scan key, all slots at or after 'high' are >= scan key.
	 *
	 * For nextkey=true (cmpval=0), the loop invariant is: all slots before
	 * 'low' are <= scan key, all slots at or after 'high' are > scan key.
	 *
	 * We can fall out when high == low.
	 */
	high++;						/* establish the loop invariant for high */

	cmpval = nextkey ? 0 : 1;	/* select comparison value */

	while (high > low)
	{
		OffsetNumber mid = low + ((high - low) / 2);

		/* We have low <= mid < high, so mid points at a real slot */

		result = _bt_compare(rel, keysz, scankey, page, mid);

		if (result >= cmpval)
			low = mid + 1;
		else
			high = mid;
	}

	/*
	 * At this point we have high == low, but be careful: they could point
	 * past the last slot on the page.
	 *
	 * On a leaf page, we always return the first key >= scan key (resp. >
	 * scan key), which could be the last slot + 1.
	 */
	if (P_ISLEAF(opaque))
		return low;

	/*
	 * On a non-leaf page, return the last key < scan key (resp. <= scan key).
	 * There must be one if _bt_compare() is playing by the rules.
	 */
	Assert(low > P_FIRSTDATAKEY(opaque));

	return OffsetNumberPrev(low);
}
~~~


~~~
/*
 *	_bt_binsrch() -- Do a binary search for a key on a particular page.
 *
 * The passed scankey must be an insertion-type scankey (see nbtree/README),
 * but it can omit the rightmost column(s) of the index.
 *
 * When nextkey is false (the usual case), we are looking for the first
 * item >= scankey.  When nextkey is true, we are looking for the first
 * item strictly greater than scankey.
 *
 * On a leaf page, _bt_binsrch() returns the OffsetNumber of the first
 * key >= given scankey, or > scankey if nextkey is true.  (NOTE: in
 * particular, this means it is possible to return a value 1 greater than the
 * number of keys on the page, if the scankey is > all keys on the page.)
 *
 * On an internal (non-leaf) page, _bt_binsrch() returns the OffsetNumber
 * of the last key < given scankey, or last key <= given scankey if nextkey
 * is true.  (Since _bt_compare treats the first data key of such a page as
 * minus infinity, there will be at least one key < scankey, so the result
 * always points at one of the keys on the page.)  This key indicates the
 * right place to descend to be sure we find all leaf keys >= given scankey
 * (or leaf keys > given scankey when nextkey is true).
 *
 * This procedure is not responsible for walking right, it just examines
 * the given page.  _bt_binsrch() has no lock or refcount side effects
 * on the buffer.
 */
OffsetNumber
_bt_binsrch(Relation rel,
			Buffer buf,
			int keysz,
			ScanKey scankey,
			bool nextkey)
{
	Page		page;
	BTPageOpaque opaque;
	OffsetNumber low,
				high;
	int32		result,
				cmpval;

	page = BufferGetPage(buf);
	opaque = (BTPageOpaque) PageGetSpecialPointer(page);

	low = P_FIRSTDATAKEY(opaque);
	high = PageGetMaxOffsetNumber(page);

	/*
	 * If there are no keys on the page, return the first available slot. Note
	 * this covers two cases: the page is really empty (no keys), or it
	 * contains only a high key.  The latter case is possible after vacuuming.
	 * This can never happen on an internal page, however, since they are
	 * never empty (an internal page must have children).
	 */
	if (high < low)
		return low;

	/*
	 * Binary search to find the first key on the page >= scan key, or first
	 * key > scankey when nextkey is true.
	 *
	 * For nextkey=false (cmpval=1), the loop invariant is: all slots before
	 * 'low' are < scan key, all slots at or after 'high' are >= scan key.
	 *
	 * For nextkey=true (cmpval=0), the loop invariant is: all slots before
	 * 'low' are <= scan key, all slots at or after 'high' are > scan key.
	 *
	 * We can fall out when high == low.
	 */
	high++;						/* establish the loop invariant for high */

	cmpval = nextkey ? 0 : 1;	/* select comparison value */

	while (high > low)
	{
		OffsetNumber mid = low + ((high - low) / 2);
		static int compareCalls = 0;

		/* We have low <= mid < high, so mid points at a real slot */

		compareCalls++;
		elog(NOTICE, "_bt_compare call %d", compareCalls);

		result = _bt_compare(rel, keysz, scankey, page, mid);

		if (result >= cmpval)
			low = mid + 1;
		else
			high = mid;
	}

	/*
	 * At this point we have high == low, but be careful: they could point
	 * past the last slot on the page.
	 *
	 * On a leaf page, we always return the first key >= scan key (resp. >
	 * scan key), which could be the last slot + 1.
	 */
	if (P_ISLEAF(opaque))
		return low;

	/*
	 * On a non-leaf page, return the last key < scan key (resp. <= scan key).
	 * There must be one if _bt_compare() is playing by the rules.
	 */
	Assert(low > P_FIRSTDATAKEY(opaque));

	return OffsetNumberPrev(low);
}

~~~

~~~
make clean
make
make install
initdb
~~~

~~~
euloo@euloo:~/project/bin$ ./psql postgres -c "select * from sample where id = 'key7777'"
NOTICE:  _bt_compare call 3
NOTICE:  _bt_compare call 4
NOTICE:  _bt_compare call 5
NOTICE:  _bt_compare call 6
NOTICE:  _bt_compare call 7
NOTICE:  _bt_compare call 8
NOTICE:  _bt_compare call 9
NOTICE:  _bt_compare call 10
NOTICE:  _bt_compare call 11
NOTICE:  _bt_compare call 12
NOTICE:  _bt_compare call 13
NOTICE:  _bt_compare call 14
NOTICE:  _bt_compare call 15
NOTICE:  _bt_compare call 16
NOTICE:  _bt_compare call 17
NOTICE:  _bt_compare call 18
NOTICE:  _bt_compare call 19
NOTICE:  _bt_compare call 20
NOTICE:  _bt_compare call 21
NOTICE:  _bt_compare call 22
NOTICE:  _bt_compare call 23
NOTICE:  _bt_compare call 24
NOTICE:  _bt_compare call 25
NOTICE:  _bt_compare call 26
NOTICE:  _bt_compare call 27
NOTICE:  _bt_compare call 28
NOTICE:  _bt_compare call 29
NOTICE:  _bt_compare call 30
NOTICE:  _bt_compare call 31
NOTICE:  _bt_compare call 32
NOTICE:  _bt_compare call 33
NOTICE:  _bt_compare call 34
NOTICE:  _bt_compare call 35
NOTICE:  _bt_compare call 36
NOTICE:  _bt_compare call 37
NOTICE:  _bt_compare call 38
NOTICE:  _bt_compare call 39
NOTICE:  _bt_compare call 40
NOTICE:  _bt_compare call 41
NOTICE:  _bt_compare call 42
NOTICE:  _bt_compare call 43
NOTICE:  _bt_compare call 44
NOTICE:  _bt_compare call 45
NOTICE:  _bt_compare call 46
NOTICE:  _bt_compare call 47
NOTICE:  _bt_compare call 48
NOTICE:  _bt_compare call 49
NOTICE:  _bt_compare call 50
NOTICE:  _bt_compare call 51
NOTICE:  _bt_compare call 52
NOTICE:  _bt_compare call 53
NOTICE:  _bt_compare call 54
NOTICE:  _bt_compare call 55
NOTICE:  _bt_compare call 56
NOTICE:  _bt_compare call 57
NOTICE:  _bt_compare call 58
NOTICE:  _bt_compare call 59
NOTICE:  _bt_compare call 60
NOTICE:  _bt_compare call 61
NOTICE:  _bt_compare call 62
NOTICE:  _bt_compare call 63
NOTICE:  _bt_compare call 64
NOTICE:  _bt_compare call 65
NOTICE:  _bt_compare call 66
NOTICE:  _bt_compare call 67
NOTICE:  _bt_compare call 68
NOTICE:  _bt_compare call 69
NOTICE:  _bt_compare call 70
NOTICE:  _bt_compare call 71
NOTICE:  _bt_compare call 72
NOTICE:  _bt_compare call 73
NOTICE:  _bt_compare call 74
NOTICE:  _bt_compare call 75
NOTICE:  _bt_compare call 76
NOTICE:  _bt_compare call 77
NOTICE:  _bt_compare call 78
NOTICE:  _bt_compare call 79
LINE 1: select * from sample where id = 'key7777'
                      ^
NOTICE:  _bt_compare call 80
LINE 1: select * from sample where id = 'key7777'
                      ^
NOTICE:  _bt_compare call 81
LINE 1: select * from sample where id = 'key7777'
                      ^
NOTICE:  _bt_compare call 82
LINE 1: select * from sample where id = 'key7777'
                      ^
NOTICE:  _bt_compare call 83
LINE 1: select * from sample where id = 'key7777'
                      ^
NOTICE:  _bt_compare call 84
LINE 1: select * from sample where id = 'key7777'
                      ^
NOTICE:  _bt_compare call 85
LINE 1: select * from sample where id = 'key7777'
                      ^
NOTICE:  _bt_compare call 86
LINE 1: select * from sample where id = 'key7777'
                      ^
NOTICE:  _bt_compare call 87
LINE 1: select * from sample where id = 'key7777'
                      ^
NOTICE:  _bt_compare call 88
LINE 1: select * from sample where id = 'key7777'
                      ^
NOTICE:  _bt_compare call 89
LINE 1: select * from sample where id = 'key7777'
                      ^
NOTICE:  _bt_compare call 90
LINE 1: select * from sample where id = 'key7777'
                      ^
NOTICE:  _bt_compare call 91
LINE 1: select * from sample where id = 'key7777'
                      ^
NOTICE:  _bt_compare call 92
LINE 1: select * from sample where id = 'key7777'
                      ^
NOTICE:  _bt_compare call 93
LINE 1: select * from sample where id = 'key7777'
                      ^
NOTICE:  _bt_compare call 94
LINE 1: select * from sample where id = 'key7777'
                      ^
NOTICE:  _bt_compare call 95
LINE 1: select * from sample where id = 'key7777'
                      ^
NOTICE:  _bt_compare call 96
LINE 1: select * from sample where id = 'key7777'
                      ^
NOTICE:  _bt_compare call 97
LINE 1: select * from sample where id = 'key7777'
                      ^
NOTICE:  _bt_compare call 98
LINE 1: select * from sample where id = 'key7777'
                      ^
NOTICE:  _bt_compare call 99
LINE 1: select * from sample where id = 'key7777'
                      ^
NOTICE:  _bt_compare call 100
LINE 1: select * from sample where id = 'key7777'
                      ^
NOTICE:  _bt_compare call 101
LINE 1: select * from sample where id = 'key7777'
                      ^
NOTICE:  _bt_compare call 102
LINE 1: select * from sample where id = 'key7777'
                      ^
NOTICE:  _bt_compare call 103
LINE 1: select * from sample where id = 'key7777'
                      ^
NOTICE:  _bt_compare call 104
LINE 1: select * from sample where id = 'key7777'
                      ^
NOTICE:  _bt_compare call 105
LINE 1: select * from sample where id = 'key7777'
                      ^
NOTICE:  _bt_compare call 106
LINE 1: select * from sample where id = 'key7777'
                      ^
NOTICE:  _bt_compare call 107
LINE 1: select * from sample where id = 'key7777'
                      ^
NOTICE:  _bt_compare call 108
LINE 1: select * from sample where id = 'key7777'
                      ^
NOTICE:  _bt_compare call 109
LINE 1: select * from sample where id = 'key7777'
                      ^
NOTICE:  _bt_compare call 110
LINE 1: select * from sample where id = 'key7777'
                      ^
NOTICE:  _bt_compare call 111
LINE 1: select * from sample where id = 'key7777'
                      ^
NOTICE:  _bt_compare call 112
LINE 1: select * from sample where id = 'key7777'
                      ^
NOTICE:  _bt_compare call 113
LINE 1: select * from sample where id = 'key7777'
                      ^
NOTICE:  _bt_compare call 114
LINE 1: select * from sample where id = 'key7777'
                      ^
NOTICE:  _bt_compare call 115
LINE 1: select * from sample where id = 'key7777'
                      ^
NOTICE:  _bt_compare call 116
LINE 1: select * from sample where id = 'key7777'
                      ^
NOTICE:  _bt_compare call 117
LINE 1: select * from sample where id = 'key7777'
                      ^
NOTICE:  _bt_compare call 118
LINE 1: select * from sample where id = 'key7777'
                      ^
NOTICE:  _bt_compare call 119
NOTICE:  _bt_compare call 120
NOTICE:  _bt_compare call 121
NOTICE:  _bt_compare call 122
NOTICE:  _bt_compare call 123
NOTICE:  _bt_compare call 124
NOTICE:  _bt_compare call 125
NOTICE:  _bt_compare call 126
NOTICE:  _bt_compare call 127
NOTICE:  _bt_compare call 128
NOTICE:  _bt_compare call 129
NOTICE:  _bt_compare call 130
NOTICE:  _bt_compare call 131
NOTICE:  _bt_compare call 132
NOTICE:  _bt_compare call 133
NOTICE:  _bt_compare call 134
NOTICE:  _bt_compare call 135
NOTICE:  _bt_compare call 136
NOTICE:  _bt_compare call 137
NOTICE:  _bt_compare call 138
NOTICE:  _bt_compare call 139
NOTICE:  _bt_compare call 140
NOTICE:  _bt_compare call 141
NOTICE:  _bt_compare call 142
NOTICE:  _bt_compare call 143
NOTICE:  _bt_compare call 144
NOTICE:  _bt_compare call 145
NOTICE:  _bt_compare call 146
NOTICE:  _bt_compare call 147
NOTICE:  _bt_compare call 148
NOTICE:  _bt_compare call 149
NOTICE:  _bt_compare call 150
NOTICE:  _bt_compare call 151
NOTICE:  _bt_compare call 152
NOTICE:  _bt_compare call 153
NOTICE:  _bt_compare call 154
NOTICE:  _bt_compare call 155
NOTICE:  _bt_compare call 156
NOTICE:  _bt_compare call 157
NOTICE:  _bt_compare call 158
NOTICE:  _bt_compare call 159
NOTICE:  _bt_compare call 160
NOTICE:  _bt_compare call 161
NOTICE:  _bt_compare call 162
NOTICE:  _bt_compare call 163
NOTICE:  _bt_compare call 164
NOTICE:  _bt_compare call 165
NOTICE:  _bt_compare call 166
NOTICE:  _bt_compare call 167
NOTICE:  _bt_compare call 168
NOTICE:  _bt_compare call 169
NOTICE:  _bt_compare call 170
NOTICE:  _bt_compare call 171
NOTICE:  _bt_compare call 172
NOTICE:  _bt_compare call 173
NOTICE:  _bt_compare call 174
NOTICE:  _bt_compare call 175
NOTICE:  _bt_compare call 176
NOTICE:  _bt_compare call 177
NOTICE:  _bt_compare call 178
NOTICE:  _bt_compare call 179
NOTICE:  _bt_compare call 180
NOTICE:  _bt_compare call 181
NOTICE:  _bt_compare call 182
NOTICE:  _bt_compare call 183
NOTICE:  _bt_compare call 184
NOTICE:  _bt_compare call 185
NOTICE:  _bt_compare call 186
NOTICE:  _bt_compare call 187
NOTICE:  _bt_compare call 188
NOTICE:  _bt_compare call 189
NOTICE:  _bt_compare call 190
NOTICE:  _bt_compare call 191
NOTICE:  _bt_compare call 192
NOTICE:  _bt_compare call 193
NOTICE:  _bt_compare call 194
NOTICE:  _bt_compare call 195
NOTICE:  _bt_compare call 196
NOTICE:  _bt_compare call 197
NOTICE:  _bt_compare call 198
NOTICE:  _bt_compare call 199
NOTICE:  _bt_compare call 200
NOTICE:  _bt_compare call 201
NOTICE:  _bt_compare call 202
NOTICE:  _bt_compare call 203
NOTICE:  _bt_compare call 204
NOTICE:  _bt_compare call 205
NOTICE:  _bt_compare call 206
NOTICE:  _bt_compare call 207
NOTICE:  _bt_compare call 208
NOTICE:  _bt_compare call 209
NOTICE:  _bt_compare call 210
NOTICE:  _bt_compare call 211
NOTICE:  _bt_compare call 212
NOTICE:  _bt_compare call 213
NOTICE:  _bt_compare call 214
NOTICE:  _bt_compare call 215
NOTICE:  _bt_compare call 216
NOTICE:  _bt_compare call 217
NOTICE:  _bt_compare call 218
NOTICE:  _bt_compare call 219
NOTICE:  _bt_compare call 220
NOTICE:  _bt_compare call 221
NOTICE:  _bt_compare call 222
NOTICE:  _bt_compare call 223
NOTICE:  _bt_compare call 224
NOTICE:  _bt_compare call 225
NOTICE:  _bt_compare call 226
NOTICE:  _bt_compare call 227
NOTICE:  _bt_compare call 228
NOTICE:  _bt_compare call 229
NOTICE:  _bt_compare call 230
NOTICE:  _bt_compare call 231
NOTICE:  _bt_compare call 232
NOTICE:  _bt_compare call 233
NOTICE:  _bt_compare call 234
NOTICE:  _bt_compare call 235
NOTICE:  _bt_compare call 236
NOTICE:  _bt_compare call 237
NOTICE:  _bt_compare call 238
NOTICE:  _bt_compare call 239
NOTICE:  _bt_compare call 240
NOTICE:  _bt_compare call 241
NOTICE:  _bt_compare call 242
NOTICE:  _bt_compare call 243
NOTICE:  _bt_compare call 244
NOTICE:  _bt_compare call 245
NOTICE:  _bt_compare call 246
NOTICE:  _bt_compare call 247
NOTICE:  _bt_compare call 248
NOTICE:  _bt_compare call 249
NOTICE:  _bt_compare call 250
NOTICE:  _bt_compare call 251
NOTICE:  _bt_compare call 252
NOTICE:  _bt_compare call 253
NOTICE:  _bt_compare call 254
NOTICE:  _bt_compare call 255
NOTICE:  _bt_compare call 256
NOTICE:  _bt_compare call 257
NOTICE:  _bt_compare call 258
NOTICE:  _bt_compare call 259
NOTICE:  _bt_compare call 260
NOTICE:  _bt_compare call 261
NOTICE:  _bt_compare call 262
NOTICE:  _bt_compare call 263
NOTICE:  _bt_compare call 264
NOTICE:  _bt_compare call 265
NOTICE:  _bt_compare call 266
NOTICE:  _bt_compare call 267
NOTICE:  _bt_compare call 268
NOTICE:  _bt_compare call 269
NOTICE:  _bt_compare call 270
NOTICE:  _bt_compare call 271
NOTICE:  _bt_compare call 272
NOTICE:  _bt_compare call 273
NOTICE:  _bt_compare call 274
NOTICE:  _bt_compare call 275
NOTICE:  _bt_compare call 276
NOTICE:  _bt_compare call 277
NOTICE:  _bt_compare call 278
NOTICE:  _bt_compare call 279
NOTICE:  _bt_compare call 280
NOTICE:  _bt_compare call 281
NOTICE:  _bt_compare call 282
NOTICE:  _bt_compare call 283
NOTICE:  _bt_compare call 284
NOTICE:  _bt_compare call 285
NOTICE:  _bt_compare call 286
NOTICE:  _bt_compare call 287
NOTICE:  _bt_compare call 288
NOTICE:  _bt_compare call 289
NOTICE:  _bt_compare call 290
NOTICE:  _bt_compare call 291
NOTICE:  _bt_compare call 292
NOTICE:  _bt_compare call 293
NOTICE:  _bt_compare call 294
NOTICE:  _bt_compare call 295
NOTICE:  _bt_compare call 296
NOTICE:  _bt_compare call 297
NOTICE:  _bt_compare call 298
NOTICE:  _bt_compare call 299
NOTICE:  _bt_compare call 300
NOTICE:  _bt_compare call 301
NOTICE:  _bt_compare call 302
NOTICE:  _bt_compare call 303
NOTICE:  _bt_compare call 304
NOTICE:  _bt_compare call 305
NOTICE:  _bt_compare call 306
NOTICE:  _bt_compare call 307
NOTICE:  _bt_compare call 308
NOTICE:  _bt_compare call 309
NOTICE:  _bt_compare call 310
NOTICE:  _bt_compare call 311
NOTICE:  _bt_compare call 312
NOTICE:  _bt_compare call 313
NOTICE:  _bt_compare call 314
NOTICE:  _bt_compare call 315
NOTICE:  _bt_compare call 316
NOTICE:  _bt_compare call 317
NOTICE:  _bt_compare call 318
NOTICE:  _bt_compare call 319
NOTICE:  _bt_compare call 320
NOTICE:  _bt_compare call 321
NOTICE:  _bt_compare call 322
NOTICE:  _bt_compare call 323
   id    |   value   
---------+-----------
 key7777 | value7777
(1 row)


~~~