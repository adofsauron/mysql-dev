#!/bin/bash

MYSQL_PATH=/usr/local/mysql

# notice: data will lost

if [ -d $MYSQL_PATH/data/ ]; then
    rm -rf $MYSQL_PATH/data.back
    mv $MYSQL_PATH/data $MYSQL_PATH/data.back
fi

echo `date` "useradd -M -s /sbin/nologin mysql"
useradd -M -s /sbin/nologin mysql

mkdir -p $MYSQL_PATH/data/
chown -R mysql:mysql   $MYSQL_PATH
chown -R mysql.mysql   $MYSQL_PATH/data/
chmod 750 $MYSQL_PATH/data/

chown -R  mysql:mysql /var/log/mysql

echo `date` "mysqld --initialize --user=mysql"

mysqld --initialize --user=mysql

RET=$?

if [ "0" != "$RET" ]; then
    echo `date` "mysqld init fail, RET=$RET"
    exit 1
fi

echo `date` "mysqld init ok"
exit 0

# IlLHkh8Zd6&*
