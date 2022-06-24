#!/bin/bash

MYSQL_PATH=/usr/local/mysql

PID=`ps -aux | grep $MYSQL_PATH/bin/mysqld | grep -v grep | awk -F ' ' '{print $2}'`

echo "PID=$PID"

top -Hp $PID

