#!/bin/bash

bash my-stop.sh

if [ "0" != "$?" ]; then
    echo `date` "mysql restart fail, mysql stop fail"
    exit 1
fi

bash my-start.sh

if [ "0" != "$?" ]; then
    echo `date` "mysql restart fail, mysql start fail"
    exit 2
fi

echo `date` "mysql restart ok"
exit 0
