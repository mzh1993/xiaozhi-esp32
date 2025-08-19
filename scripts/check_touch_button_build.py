#!/usr/bin/env python3
"""
检查触摸按钮编译配置脚本

此脚本用于检查touch_button组件是否正确配置到编译系统中
"""

import os
import sys
import subprocess

def check_touch_button_dependencies():
    """检查touch_button依赖配置"""
    print("=== 检查touch_button依赖配置 ===\n")
    
    # 检查主项目的idf_component.yml
    main_yml = "main/idf_component.yml"
    if not os.path.exists(main_yml):
        print(f"✗ 找不到文件: {main_yml}")
        return False
    
    with open(main_yml, 'r', encoding='utf-8') as f:
        content = f.read()
    
    if "espressif/touch_button" in content:
        print("✓ 主项目idf_component.yml包含touch_button依赖")
    else:
        print("✗ 主项目idf_component.yml缺少touch_button依赖")
        return False
    
    # 检查touch_button组件的idf_component.yml
    touch_yml = "components/touch_button/idf_component.yml"
    if not os.path.exists(touch_yml):
        print(f"✗ 找不到文件: {touch_yml}")
        return False
    
    with open(touch_yml, 'r', encoding='utf-8') as f:
        content = f.read()
    
    if "espressif/button" in content and "espressif/touch_button_sensor" in content:
        print("✓ touch_button组件依赖配置正确")
    else:
        print("✗ touch_button组件依赖配置有问题")
        return False
    
    return True

def check_touch_button_files():
    """检查touch_button相关文件"""
    print("\n=== 检查touch_button相关文件 ===\n")
    
    files_to_check = [
        "components/touch_button/touch_button.h",
        "components/touch_button/touch_button.c",
        "components/touch_button/CMakeLists.txt",
        "main/boards/common/touch_button.h",
        "main/boards/common/touch_button.cc",
        "main/boards/bread-compact-wifi/compact_wifi_board.cc",
        "main/boards/bread-compact-wifi/config.h"
    ]
    
    all_exist = True
    for file_path in files_to_check:
        if os.path.exists(file_path):
            print(f"✓ {file_path}")
        else:
            print(f"✗ {file_path}")
            all_exist = False
    
    return all_exist

def check_touch_button_integration():
    """检查touch_button集成情况"""
    print("\n=== 检查touch_button集成情况 ===\n")
    
    board_file = "main/boards/bread-compact-wifi/compact_wifi_board.cc"
    if not os.path.exists(board_file):
        print(f"✗ 找不到文件: {board_file}")
        return False
    
    with open(board_file, 'r', encoding='utf-8') as f:
        content = f.read()
    
    checks = [
        ("包含touch_button.h", "#include \"touch_button.h\"" in content),
        ("TouchButton类声明", "TouchButton head_touch_button_" in content),
        ("TouchButton类声明", "TouchButton hand_touch_button_" in content),
        ("TouchButton类声明", "TouchButton belly_touch_button_" in content),
        ("触摸传感器初始化", "InitializeTouchSensor" in content),
        ("触摸通道定义", "TOUCH_CHANNEL_HEAD" in content),
        ("触摸通道定义", "TOUCH_CHANNEL_HAND" in content),
        ("触摸通道定义", "TOUCH_CHANNEL_BELLY" in content),
    ]
    
    all_passed = True
    for check_name, passed in checks:
        if passed:
            print(f"✓ {check_name}")
        else:
            print(f"✗ {check_name}")
            all_passed = False
    
    return all_passed

def check_config_file():
    """检查配置文件"""
    print("\n=== 检查配置文件 ===\n")
    
    config_file = "main/boards/bread-compact-wifi/config.h"
    if not os.path.exists(config_file):
        print(f"✗ 找不到文件: {config_file}")
        return False
    
    with open(config_file, 'r', encoding='utf-8') as f:
        content = f.read()
    
    checks = [
        ("TOUCH_CHANNEL_HEAD定义", "#define TOUCH_CHANNEL_HEAD" in content),
        ("TOUCH_CHANNEL_HAND定义", "#define TOUCH_CHANNEL_HAND" in content),
        ("TOUCH_CHANNEL_BELLY定义", "#define TOUCH_CHANNEL_BELLY" in content),
    ]
    
    all_passed = True
    for check_name, passed in checks:
        if passed:
            print(f"✓ {check_name}")
        else:
            print(f"✗ {check_name}")
            all_passed = False
    
    return all_passed

def test_compilation():
    """测试编译"""
    print("\n=== 测试编译 ===\n")
    
    try:
        # 清理之前的构建
        print("清理之前的构建...")
        subprocess.run(["idf.py", "clean"], check=True, capture_output=True)
        
        # 尝试编译
        print("开始编译...")
        result = subprocess.run(["idf.py", "build"], capture_output=True, text=True)
        
        if result.returncode == 0:
            print("✓ 编译成功!")
            return True
        else:
            print("✗ 编译失败:")
            print(result.stderr)
            return False
            
    except Exception as e:
        print(f"✗ 编译过程中出错: {e}")
        return False

def main():
    """主函数"""
    print("=== 触摸按钮编译配置检查 ===\n")
    
    # 检查当前目录
    if not os.path.exists("CMakeLists.txt"):
        print("错误: 请在项目根目录运行此脚本")
        return 1
    
    # 检查依赖配置
    if not check_touch_button_dependencies():
        print("\n依赖配置检查失败")
        return 1
    
    # 检查文件
    if not check_touch_button_files():
        print("\n文件检查失败")
        return 1
    
    # 检查集成
    if not check_touch_button_integration():
        print("\n集成检查失败")
        return 1
    
    # 检查配置
    if not check_config_file():
        print("\n配置文件检查失败")
        return 1
    
    print("\n=== 所有检查通过! ===")
    print("\ntouch_button组件已正确配置到编译系统中")
    
    # 询问是否进行编译测试
    response = input("\n是否进行编译测试? (y/n): ")
    if response.lower() in ['y', 'yes']:
        if test_compilation():
            print("\n编译测试通过!")
        else:
            print("\n编译测试失败，请检查错误信息")
            return 1
    
    return 0

if __name__ == "__main__":
    sys.exit(main())
