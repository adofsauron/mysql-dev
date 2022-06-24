#!/bin/bash

MYSQL_PATH=/usr/local/mysql

PID_FILE=$MYSQL_PATH/data/localhost.pid

# pid的文件存在,不能说明mysqld已启动,必须校验pid

while true;do

    if [ ! -f $PID_FILE ]; then
        break
    fi

    PID=`cat $PID_FILE`

    kill -0 $PID
    if [ "0" != "$?" ]; then
        echo `date` "mysqld [$PID] file has, but not start, rm pid file $PID_FILE"
        rm -f $PID_FILE
        break
    fi

    PID_NUM=`ps -aux | grep $MYSQL_PATH/bin/mysqld | grep $PID | grep -v grep | wc -l`

    if [ "1" == "$PID_NUM" ];then 
        echo `date` "mysqld [$PID] already start"
        exit 0
    fi

    break
done

# cnf file
MYSQL_CONF=./trunk/conf/my.cnf

nohup chroot --userspec "mysql:mysql" "/" sh -c \
    $MYSQL_PATH/bin/mysqld --user=mysql \
    --defaults-file=$MYSQL_CONF \
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
