#!/bin/bash

MYSQL_PATH=/usr/local/mysql

ps -aux | grep $MYSQL_PATH/bin/mysqld | grep -v grep

