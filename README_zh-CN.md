# freerkDumper
这是一个用 C 语言编写的免费开源 Rockchip（瑞芯微）固件转储工具。它主要在各种通用的 Rockchip 电视盒和数码相框上进行了测试；目前功能尚不完善，欢迎各界贡献代码！

## 用法
```用法: ./freerkDumper [选项]
设备操作：
-i, --info         仅打印设备闪存和芯片信息
-a, --dump-all     转储所有分区
-p, --dump <part>  按名称转储指定分区（例如 system）
-o, --out <dir>    输出目录（默认：Output/）
-y, --physical     读取物理块（528 字节）而非逻辑块
-m, --mode <mode>  使用指定设备模式：msc、loader、maskrom
-s, --scan         扫描所有已连接的 RockChip 设备

镜像操作：
--unpack <file>    解包 RockChip 镜像文件 (KRNL/PARM/RKFP/bootloader)
--pack <file>      将文件打包为 RockChip 镜像格式
--encrypt          打包时使用 RC4 加密
--detect <file>    检测镜像格式并显示信息
--verify <file>    验证镜像签名和校验和

-h, --help         显示此帮助信息
```
