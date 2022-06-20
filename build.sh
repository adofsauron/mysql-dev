#!/bin/bash


HERE=`pwd`

cd origin

rm -rf mysql-server-mysql-8.0.28

echo `date` "tar -xzvf mysql-server-mysql-8.0.28.tar.gz"
tar -xzvf mysql-server-mysql-8.0.28.tar.gz

if [ "0" != "$?" ]; then
    echo `date` "tar -xzvf mysql-server-mysql-8.0.28.tar.gz fail"
    exit 1
fi

cd mysql-server-mysql-8.0.28

rm -rf build
mkdir -p build
cd build

# verify prefix path
cmake .. -DCMAKE_INSTALL_PREFIX=/usr/local/mysql

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

echo -e '\n\nexport PATH=/usr/local/mysql/bin:$PATH\n' >> /etc/profile && source /etc/profile

if [ "1" != "`which mysqld | wc -l`" ]; then
    echo `date` "export mysql/bin fail"
    exit 1
fi

## systemctl 

# if [ -f "/etc/init.d/mysqld" ]; then
#     rm -rf /etc/init.d/mysqld.back
#     mv /etc/init.d/mysqld /etc/init.d/mysqld.back
# fi

# cp /usr/local/mysql/support-files/mysql.server /etc/init.d/mysqld
# chmod +x /etc/init.d/mysqld
# systemctl enable mysqld

echo "over"
cd $HERE

exit 0

