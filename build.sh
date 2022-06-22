#!/bin/bash

MYSQL_PATH=/usr/local/mysql

HERE=`pwd`

cd trunk

cd mysql-server-mysql-8.0.28

# LOCAL=`pwd`
# scl enable gcc-toolset-10 bash
# cd $LOCAL

if [ ! -d build ]; then
    mkdir -p build
    cd build
    # verify prefix path
    cmake .. -DCMAKE_INSTALL_PREFIX=$MYSQL_PATH \
    -DWITH_SYSTEM_LIBS_DEFAULT=ON \
    -DDOWNLOAD_BOOST=1 -DWITH_BOOST=../boost
else
    cd build
fi

# build
make -j"$(nproc)"

if [ "0" != "$?" ]; then
    echo `date` "build fail"
    exit 1
fi

make install

if [ "0" != "$?" ]; then
    echo `date` "make install fail"
    exit 1
fi

if [ "0" == "`grep "$MYSQL_PATH/bin"  -rn /etc/profile  | wc -l`" ]; then
    echo -e '\n\nexport PATH='$MYSQL_PATH'/bin:$PATH\n' >> /etc/profile
    source /etc/profile
fi


if [ "1" != "`which mysqld | wc -l`" ]; then
    echo `date` "export mysql/bin fail"
    exit 1
fi

## systemctl 

# if [ -f "/etc/init.d/mysqld" ]; then
#     rm -rf /etc/init.d/mysqld.back
#     mv /etc/init.d/mysqld /etc/init.d/mysqld.back
# fi

# cp $MYSQL_PATH/support-files/mysql.server /etc/init.d/mysqld
# chmod +x /etc/init.d/mysqld
# systemctl enable mysqld

echo "over"
cd $HERE

exit 0

