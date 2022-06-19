#!/bin/bash

systemctl  stop  mysql

systemctl  status  mysql

pkill -9 mysqld

rm -rf /usr/local/mysql/data/

useradd -M -s /sbin/nologin mysql

mkdir -p /mysql/data
chown -R mysql:mysql   /usr/local/mysql
chown -R mysql.mysql   /mysql/data
chmod 750 /mysql/data

mysqld   --initialize   --user=mysql

mkdir -p /usr/local/mysql/data/

if [ "0" != "$?" ]; then
    echo `date` "mysqld init fail"
    exit 1
fi

echo `date` "mysqld init ok"
exit 0