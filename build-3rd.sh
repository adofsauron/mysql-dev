#!/bin/bash


HERE=`pwd`

cd 3rd

# rm boost_1_73_0 -rf

if [ ! -d boost_1_73_0 ]; then

    echo `date` "tar -xvjf boost_1_73_0.tar.bz2"
    tar -xvjf boost_1_73_0.tar.bz2

    if [ "0" != "$?" ]; then
        echo `date` "tar -xvjf boost_1_73_0.tar.bz2 fail"
        exit 1
    fi
fi

cd boost_1_73_0

bash ./bootstrap.sh --with-libraries=all

chmod +x ./b2

./b2

./b2 header

./b2 install --prefix=/usr

cd $HERE
