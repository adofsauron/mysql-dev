#!/bin/bash

MYSQL_PATH=/usr/local/mysql

PID_FILE=$MYSQL_PATH/data/localhost.pid

# pid的文件存在,不能说明mysqld已启动,必须校验pid
if [ -f $PID_FILE ]; then
    PID=`cat $PID_FILE`

    kill -0 $PID
    if [ "0" == "$?" ]; then
        echo `date` "mysqld [$PID] already start"
        exit 0
    else
        rm -f $PID_FILE
    fi
fi

nohup chroot --userspec "mysql:mysql" "/" sh -c \
    $MYSQL_PATH/bin/mysqld --user=mysql \
    --defaults-file=/etc/mysql/my.cnf \
    --basedir=$MYSQL_PATH \
    --datadir=$MYSQL_PATH/data \
    --plugin-dir=$MYSQL_PATH/lib/plugin \
    --log-error=localhost.err.log \
    -pid-file=$PID_FILE &

if [ "0" != "$?" ]; then
    echo `date` "mysqld start fail"
    exit 1
fi

WAIT_LIMIT=100
index=1
while true; do

    if [ $WAIT_LIMIT == $index ]; then
        echo `date` "wait mysqld start but limit try num $WAIT_LIMIT"
        exit 2
    fi

    if [ -f $PID_FILE ]; then
        break
    fi

    echo `date` "mysqld not start over, wait 1s"
    sleep 1s

    index=$(($index+1))
done

if [ ! -f $PID_FILE ]; then
    echo `date` "mysqld start fail"
    exit 3
fi

PID=`cat $PID_FILE`

kill -0 $PID
if [ "0" != "$?" ]; then
    echo `date` "mysqld [$PID] start fail"
    exit 4
fi

echo `date` "mysqld [$PID] start ok"
exit 0

