# Query plan analysis
~~~
euloo@euloo:~$ cat tenk.sql
CREATE TABLE tenk1 (
	unique1		int4,
	unique2		int4,
	two			int4,
	four		int4,
	ten			int4,
	twenty		int4,
	hundred		int4,
	thousand	int4,
	twothousand	int4,
	fivethous	int4,
	tenthous	int4,
	odd			int4,
	even		int4,
	stringu1	name,
	stringu2	name,
	string4		name
);



COPY tenk1 FROM '/home/euloo/project/pgsql/src/test/regress/data/tenk.data';
create table tenk2 as select * from tenk1;

~~~
### ./postgres -D ~/project/DemoDb
~~~
euloo@euloo:~/project/bin$ ./postgres -D ~/project/DemoDb
LOG:  database system was shut down at 2019-06-13 21:20:38 +05
LOG:  MultiXact member wraparound protections are now enabled
LOG:  database system is ready to accept connections
LOG:  autovacuum launcher started

~~~
### psql postgres<tenk.sql
~~~
euloo@euloo:~$ psql postgres<tenk.sql
CREATE TABLE
COPY 10000
SELECT 10000
euloo@euloo:~$ psql postgres
psql (9.6.12)
Type "help" for help.

postgres=# EXPLAIN ANALYZE SELECT *
postgres-# FROM tenk1 t1, tenk2 t2
postgres-# WHERE t1.unique1 < 100 AND t1.unique2 = t2.unique2 ORDER BY t1.fivethous;
                                                         QUERY PLAN                               
                          
--------------------------------------------------------------------------------------------------
--------------------------
 Sort  (cost=958.13..958.39 rows=101 width=488) (actual time=3.593..3.623 rows=100 loops=1)
   Sort Key: t1.fivethous
   Sort Method: quicksort  Memory: 77kB
   ->  Hash Join  (cost=471.26..954.77 rows=101 width=488) (actual time=1.261..3.496 rows=100 loop
s=1)
         Hash Cond: (t2.unique2 = t1.unique2)
         ->  Seq Scan on tenk2 t2  (cost=0.00..445.00 rows=10000 width=244) (actual time=0.005..1.
292 rows=10000 loops=1)
         ->  Hash  (cost=470.00..470.00 rows=101 width=244) (actual time=1.240..1.240 rows=100 loo
ps=1)
               Buckets: 1024  Batches: 1  Memory Usage: 35kB
               ->  Seq Scan on tenk1 t1  (cost=0.00..470.00 rows=101 width=244) (actual time=0.008
..1.207 rows=100 loops=1)
                     Filter: (unique1 < 100)
                     Rows Removed by Filter: 9900
 Planning time: 0.313 ms
 Execution time: 3.704 ms
(13 rows)

postgres=# \q

~~~