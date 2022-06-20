#!/bin/bash

MYSQL_PATH=/usr/local/mysql

PID_FILE=$MYSQL_PATH/data/localhost.pid

if [ ! -f $PID_FILE ]; then
    echo `date` "mysqld already stop"
    exit 0
fi

PID=`cat $PID_FILE`

kill -0 $PID
if [ "1" == "$?" ]; then
    echo `date` "mysqld [$PID] already stop"
    rm -f $PID_FILE
    exit 0
fi

# signal SIGTERM

echo `date` "kill -15 $PID"
kill -15 $PID

while true; do
    if [ "0" == "`ps -aux | grep $PID | grep -v grep | wc -l`" ]; then
        echo `date` "mysqld [$PID] has stop"
        break
    fi

    echo `date` "mysql not stop over, sleep 1s"
    sleep 1s
done

exit 0



