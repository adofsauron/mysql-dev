#!/bin/bash

systemctl  stop  mysql

systemctl  status  mysql

pkill -9 mysqld

# notify: data will lost
rm -rf /usr/local/mysql/data/

useradd -M -s /sbin/nologin mysql

mkdir -p /usr/local/mysql/data/
chown -R mysql:mysql   /usr/local/mysql
chown -R mysql.mysql   /usr/local/mysql/data/
chmod 750 /usr/local/mysql/data/

mysqld   --initialize   --user=mysql

if [ "0" != "$?" ]; then
    echo `date` "mysqld init fail"
    exit 1
fi

echo `date` "mysqld init ok"
exit 0