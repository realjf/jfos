# linux内核编译过程

## linux内核目录树

- arch 根据cpu体系结构不同而分的代码
- block 部分块设备驱动程序
- crypto 加密，压缩，CRC校验算法
- documentation 内核文档
- drivers 设备驱动文件
- fs 文件系统（vfs虚拟文件系统）
- include 内核所需要的头文件(与平台无关的头文件在include/linux中)
- lib 库文件代码（与平台相关的）
- mm 实现内存管理，与硬件体系结构无关的（与硬件体系结构相关的在arch中）
- net 网络协议的代码
- samples 一些内核编程的范例
- scripts 配置内核的脚本
- security SElinux模块
- sound 音频设备的驱动文件
- usr cpio命令实现，用于制作根文件系统的命令（文件系统与内核放到一块的命令）
- virt 内核虚拟机



### linux doc 编译生成
- linux源根目录/Documentation/00-INDEX 目录索引
- linux源根目录/Documentation/HOWTO 指南

生成linux内核帮助文档，在Documentation目录下执行make htmldocs

## linux内核配置与编译
命令简介
### 清理文件
- make clean 只清理所有产生的文件
- make mrproper 清理所有产生的文件与config配置文件
- make distclean 清理所有产生的文件与config配置文件，并且编译过的补丁文件

### 配置（收集硬件信息）
- make config 基于文本模式的交互配置
- make menuconfig 基于文本模式的菜单模式（推荐使用）
- make oldconfig 使用已有的.config，但是会询问新增的配置项
- make xconfig 图形化的配置（需要安装图形化系统）

当然，也可以通过ARCH=i386指定cpu架构，如：
```sh
make ARCH=i386 config
```

### 配置方法
#### 1. 使用make menuconfig
- 按y：编译-》连接-》镜像文件
- 按m:编译
- 按n：什么都不做
- 按 空格键：y,n轮换

配置完并保存后会在linux源码根目录下生成一个.config文件

#### 2. 利用已有的配置文件模板(.config)
- linux源码根目录/arch/<cpu架构>/<具体某一的cpu文件>，把里面对应的文件copy并改名为.config至linux源码目录下
- 利用当前运行已有的文件(要用ls /boot/ -a查看)，把/boot/config-2.6.32-754.33.1.el6.x86_64拷贝并改名为.config至linux源码根目录下执行以上操作就可以用make menuconfig在拷贝.config文件上面修改文件了

### 编译内核
- make zImage
- make bzImage

区别：x86凭条上，zImage只能小于512k的内核

获取详细编译信息：make zimage V=1 或 make bzimage V=1

编译好的内核在：arch/<cpu>/boot目录下

> 在把.config配置文件cp到根目录编译内核前，必须进入make menuconfig并保存退出（否则生效不了）

### 编译并安装模块
- 编译内核模块：make modules
- 安装内核模块：make modules_install INSTALL_MOD_PATH=/lib/modules

如果要更换本机内核，将编译好的内核模块从内核源码目录复制到/lib/modules下

制作init ramdisk()：输入执行命令 mkinitrd initrd-2.6.39(任意，2.6.39可以通过查询/lib/modules下的目录得到)

> mkinitrd命令是redhat里面的，ubuntu的命令是mkinitramfs -k /lib/modules/模块安装位置 -o initrd-2.6.39（任意）
> 如果ubuntu没有该命令，通过apt-get install initrd-tools安装

### 安装内核模块
#### 手动
- cp linux根目录/arch/x86/boot/bzImage /boot/mylinux-2.6.39
- cp linux根目录/initrd-2.6.39 /boot/initrd-2.6.39

最后修改/etc/grub.conf或/etc/lilo.conf文件

#### 自动
make install ，然后可以通过uname -r查看当前内核版本





