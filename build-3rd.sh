#!/bin/bash


HERE=`pwd`

cd 3rd

rm boost_1_73_0 -rf

echo `date` "tar -xvjf boost_1_73_0.tar.bz2"
tar -xvjf boost_1_73_0.tar.bz2

if [ "0" != "$?" ]; then
    echo `date` "tar -xvjf boost_1_73_0.tar.bz2 fail"
    exit 1
fi

cd boost_1_73_0

./bootstrap.sh

./b2

./b2 header

./b2 install

cd $HERE