#!/bin/bash

# 切换到项目根目录
cd "$(dirname "$0")"

# 设置编译目标为ESP32-S3
echo "正在设置编译目标为ESP32-S3..."
idf.py set-target esp32s3

# 配置ESP-SparkSpot板子
echo "正在配置ESP-SparkSpot板子..."
sed -i 's/CONFIG_BOARD_TYPE_.*=y/# &/g' sdkconfig
echo "CONFIG_BOARD_TYPE_ESP_SPARKSPOT=y" >> sdkconfig
echo "CONFIG_ESPTOOLPY_FLASHSIZE_8MB=y" >> sdkconfig
echo "CONFIG_PARTITION_TABLE_CUSTOM_FILENAME=\"partitions_8M.csv\"" >> sdkconfig
echo "CONFIG_PARTITION_TABLE_CUSTOM=y" >> sdkconfig

# 编译项目
echo "正在编译项目..."
idf.py build

# 询问是否要烧录
read -p "是否要烧录固件到设备? (y/n): " -n 1 -r
echo
if [[ $REPLY =~ ^[Yy]$ ]]
then
    echo "正在烧录固件..."
    idf.py flash
    
    echo "烧录完成！按下设备上的RESET按钮重启设备。"
    
    read -p "是否要监视设备输出? (y/n): " -n 1 -r
    echo
    if [[ $REPLY =~ ^[Yy]$ ]]
    then
        echo "正在监视设备输出..."
        idf.py monitor
    fi
fi

echo "ESP-SparkSpot构建过程完成！" 