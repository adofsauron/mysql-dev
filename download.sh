#!/bin/bash

HERE=`pwd`

# mysql

mkdir -p origin
cd origin

rm -rf mysql-server-mysql-8.0.28.tar.gz

echo `date` "wget -O mysql-server-mysql-8.0.28.tar.gz https://github.com/mysql/mysql-server/archive/refs/tags/mysql-server-mysql-8.0.28.tar.gz"

wget -O mysql-server-mysql-8.0.28.tar.gz https://github.com/mysql/mysql-server/archive/refs/tags/mysql-server-mysql-8.0.28.tar.gz

if [ "0" != "$?" ]; then
    echo `date` "download mysql-server-mysql-8.0.28.tar.gz fail"
    exit 1
fi

# 3rd 

cd $HERE

mkdir -p 3rd
cd 3rd

rm -rf boost_1_73_0.tar.bz2

echo `date` "wget -O boost_1_73_0.tar.bz2 https://boostorg.jfrog.io/artifactory/main/release/1.73.0/source/boost_1_73_0.tar.bz2"

wget -O boost_1_73_0.tar.bz2 https://boostorg.jfrog.io/artifactory/main/release/1.73.0/source/boost_1_73_0.tar.bz2

if [ "0" != "$?" ]; then
    echo `date` "download boost_1_73_0.tar.bz2 fail"
    exit 1
fi

cd $HERE

exit 0

