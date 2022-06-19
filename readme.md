# mysql dev

## 环境
1. centos8

## 编译步骤
1. 执行 bash download.sh, 下载mysql8.0源码，及对应版本的boost源码. 如已下载, 请跳过
2. 执行 bash install-base.sh，下载编译需要的基础软件
3. 执行 bash build-3rd.sh, 编译对应版本的boost
4. 执行 bash build.sh, 编译mysql源码

## mysql服务管理
1. 初始化: bash my-init.sh
   1. 注意：每次运行初始化脚本,相当于一次重建, 旧数据将丢失
2. 启动mysqld服务: bash my-start.sh
3. 停止mysqld服务: bash my-stop.sh
4. 重启mysqld服务: bash my-restart.sh

