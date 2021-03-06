--source include/have_row_based.inc
--source include/have_binlog_format_row.inc
--source include/master-slave.inc

#
# Basic tests of row-level logging
#

#
# First we test tables with only an index.
#

eval CREATE TABLE t1 (C1 CHAR(1), C2 CHAR(1), INDEX (C1)$extra_index_t1) ENGINE = $type ;
SELECT * FROM t1;
sync_slave_with_master;
SELECT * FROM t1;

# Testing insert
connection master;
INSERT INTO t1 VALUES ('A','B'), ('X','Y'), ('X','X');
INSERT INTO t1 VALUES ('A','C'), ('X','Z'), ('A','A');
SELECT * FROM t1 ORDER BY C1,C2;
sync_slave_with_master;
SELECT * FROM t1 ORDER BY C1,C2;

# Testing delete
# Observe that are several rows having the value for the index but only one
# should be deleted.
connection master;
DELETE FROM t1 WHERE C1 = C2;
SELECT * FROM t1 ORDER BY C1,C2;
sync_slave_with_master;
SELECT * FROM t1 ORDER BY C1,C2;

#
# Testing update.
# Note that we have a condition on a column that is not part of the index for
# the table. The right row should be updated nevertheless.
#
connection master;
UPDATE t1 SET C2 = 'I' WHERE C1 = 'A' AND C2 = 'C';
SELECT * FROM t1 ORDER BY C1,C2;
sync_slave_with_master;
SELECT * FROM t1 ORDER BY C1,C2;

# Testing update with a condition that does not match any rows, but
# which has a match for the index.
connection master;
UPDATE t1 SET c2 = 'Q' WHERE c1 = 'A' AND c2 = 'N';
SELECT * FROM t1 ORDER BY c1,c2;
sync_slave_with_master;
SELECT * FROM t1 ORDER BY c1,c2;

#
# Testing table with primary key
#
connection master;
eval CREATE TABLE t2 (c1 INT, c12 char(1), c2 INT, PRIMARY KEY (c1)) ENGINE = $type ;
INSERT INTO t2
  VALUES (1,'A',2),  (2,'A',4),  (3,'A',9),  (4,'A',15), (5,'A',25),
         (6,'A',35), (7,'A',50), (8,'A',64), (9,'A',81);
SELECT * FROM t2 ORDER BY c1,c2;
SELECT * FROM t2 WHERE c2 = c1 * c1 ORDER BY c1,c2;
sync_slave_with_master;
SELECT * FROM t2 ORDER BY c1,c2;
SELECT * FROM t2 WHERE c2 = c1 * c1 ORDER BY c1,c2;

connection master;
UPDATE t2 SET c2 = c1*c1 WHERE c2 != c1*c1;
SELECT * FROM t2 WHERE c2 = c1 * c1 ORDER BY c1,c2;
sync_slave_with_master;
SELECT * FROM t2 WHERE c2 = c1 * c1 ORDER BY c1,c2;

# Testing update with a condition that does not match any rows, but
# which has a match for the primary key.
connection master;
UPDATE t2 SET c12 = 'Q' WHERE c1 = 1 AND c2 = 999;
SELECT * FROM t2 ORDER BY c1,c2;
sync_slave_with_master;
SELECT * FROM t2 ORDER BY c1,c2;

connection master;
DELETE FROM t2 WHERE c1 % 4 = 0;
SELECT * FROM t2 ORDER BY c1,c2;
sync_slave_with_master;
SELECT * FROM t2 ORDER BY c1,c2;

connection master;
UPDATE t2 SET c12='X';

#
# Testing table with a multi-column primary key.
#
connection master;
eval CREATE TABLE t3 (C1 CHAR(1), C2 CHAR(1), pk1 INT, C3 CHAR(1), pk2 INT, PRIMARY KEY (pk1,pk2)) ENGINE = $type ;

INSERT INTO t3 VALUES ('A','B',1,'B',1), ('X','Y',2,'B',1), ('X','X',3,'B',1);
INSERT INTO t3 VALUES ('A','C',1,'B',2), ('X','Z',2,'B',2), ('A','A',3,'B',2);
SELECT * FROM t3 ORDER BY C1,C2;
sync_slave_with_master;
SELECT * FROM t3 ORDER BY C1,C2;

connection master;
DELETE FROM t3 WHERE C1 = C2;
SELECT * FROM t3 ORDER BY C1,C2;
sync_slave_with_master;
SELECT * FROM t3 ORDER BY C1,C2;

connection master;
UPDATE t3 SET C2 = 'I' WHERE C1 = 'A' AND C2 = 'C';
SELECT * FROM t3 ORDER BY C1,C2;
sync_slave_with_master;
SELECT * FROM t3 ORDER BY C1,C2;

#
# Testing table without index or primary key
#
connection master;
eval CREATE TABLE t6 (C1 CHAR(1), C2 CHAR(1), C3 INT$extra_index_t6) ENGINE = $type;

# Testing insert
INSERT INTO t6 VALUES ('A','B',1), ('X','Y',2), ('X','X',3);
INSERT INTO t6 VALUES ('A','C',4), ('X','Z',5), ('A','A',6);
SELECT * FROM t6 ORDER BY C3;
sync_slave_with_master;
SELECT * FROM t6 ORDER BY C3;

# Testing delete
# Observe that are several rows having the value for the index but only one
# should be deleted.
connection master;
DELETE FROM t6 WHERE C1 = C2;
SELECT * FROM t6 ORDER BY C3;
sync_slave_with_master;
SELECT * FROM t6 ORDER BY C3;

#
# Testing update.
# Note that we have a condition on a column that is not part of the index for
# the table. The right row should be updated nevertheless.
#
connection master;
UPDATE t6 SET C2 = 'I' WHERE C1 = 'A' AND C2 = 'C';
SELECT * FROM t6 ORDER BY C3;
sync_slave_with_master;
SELECT * FROM t6 ORDER BY C3;

# now mixing the 3 tables without begin/commit
connection master;
eval CREATE TABLE t5 (C1 CHAR(1), C2 CHAR(1), C3 INT PRIMARY KEY) ENGINE = $type ;
INSERT INTO t5 VALUES ('A','B',1), ('X','Y',2), ('X','X',3);
INSERT INTO t5 VALUES ('A','C',4), ('X','Z',5), ('A','A',6);

UPDATE t5,t2,t3 SET t5.C2='Q', t2.c12='R', t3.C3 ='S' WHERE t5.C1 = t2.c12 AND t5.C1 = t3.C1;
SELECT * FROM t5,t2,t3 WHERE t5.C2='Q' AND t2.c12='R' AND t3.C3 ='S' ORDER BY t5.C3,t2.c1,t3.pk1,t3.pk2;
sync_slave_with_master;
SELECT * FROM t5,t2,t3 WHERE t5.C2='Q' AND t2.c12='R' AND t3.C3 ='S' ORDER BY t5.C3,t2.c1,t3.pk1,t3.pk2;

#
# Testing special column types
#

connection master;
eval CREATE TABLE t4 (C1 CHAR(1) PRIMARY KEY, B1 BIT(1), B2 BIT(1) NOT NULL DEFAULT 0, C2 CHAR(1) NOT NULL DEFAULT 'A') ENGINE = $type ;

INSERT INTO t4 SET C1 = 1;
SELECT C1,HEX(B1),HEX(B2) FROM t4 ORDER BY C1;
sync_slave_with_master;
SELECT C1,HEX(B1),HEX(B2) FROM t4 ORDER BY C1;

#
# Testing conflicting operations
#
connection master;
eval CREATE TABLE t7 (C1 INT PRIMARY KEY, C2 INT) ENGINE = $type ;
sync_slave_with_master;
--echo --- on slave: original values ---
INSERT INTO t7 VALUES (1,3), (2,6), (3,9);
SELECT * FROM t7 ORDER BY C1;

connection master;
--echo --- on master: new values inserted ---
INSERT INTO t7 VALUES (1,2), (2,4), (3,6);
SELECT * FROM t7 ORDER BY C1;
sync_slave_with_master;
--echo --- on slave: old values should be overwritten by replicated values ---
SELECT * FROM t7 ORDER BY C1;

#
# A more complicated test where the table has several keys and we are
# causing a conflict for a key that is not "last".
#
connection master;
--echo --- on master ---
eval CREATE TABLE t8 (a INT PRIMARY KEY, b INT UNIQUE, c INT UNIQUE) ENGINE = $type ;

# First we make sure that the constraints are correctly set.
INSERT INTO t8 VALUES (99,99,99);
--error 1062
INSERT INTO t8 VALUES (99,22,33);
--error 1062
INSERT INTO t8 VALUES (11,99,33);
--error 1062
INSERT INTO t8 VALUES (11,22,99);
SELECT * FROM t8 ORDER BY a;

sync_slave_with_master;
--echo --- on slave ---
SELECT * FROM t8 ORDER BY a;
INSERT INTO t8 VALUES (1,2,3), (2,4,6), (3,6,9);
SELECT * FROM t8 ORDER BY a;

connection master;
--echo --- on master ---
# We insert a row that will cause conflict on the primary key but not
# on the other keys.
INSERT INTO t8 VALUES (2,4,8);
sync_slave_with_master;
--echo --- on slave ---
SELECT * FROM t8 ORDER BY a;

#
# Test conflicting operations when changing in a table referenced by a
# foreign key.  We'll reuse the above table and just add a table that
# references it.
#

#
# cleanup
#

connection master;
DROP TABLE IF EXISTS t1,t2,t3,t4,t5,t6,t7,t8;
