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

WAIT_LIMIT=100
index=1
while true; do
    if [ $WAIT_LIMIT == $index ]; then
        echo `date` "wait mysqld stop but limit try num $WAIT_LIMIT"
        exit 1
    fi

    if [ "0" == "`ps -aux | grep $PID | grep -v grep | wc -l`" ]; then
        echo `date` "mysqld [$PID] stop ok"
        break
    fi

    echo `date` "mysql not stop over, wait 1s"
    sleep 1s

    index=$(($index+1))
done

exit 0



