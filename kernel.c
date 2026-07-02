/*
 * ============================================================================
 * kernel.c — 内核引导与初始文件系统
 * ============================================================================
 *
 * 职责：
 *   - 模拟内核启动日志（dmesg 风格）
 *   - 调用 vfs_init / proc_init 初始化子系统
 *   - seed_filesystem() 预置类 Linux 目录结构
 *   - 将 cwd 切到 /home/user 后启动 Shell
 *
 * 调用链：
 *   main → kernel_boot → shell_run → kernel_shutdown
 * ============================================================================
 */

#include "kernel.h"

#include "proc.h"
#include "shell.h"
#include "vfs.h"

#include <stdio.h>
#include <string.h>

/*
 * seed_filesystem — 预置标准目录树和配置文件
 *
 * 在 vfs_init() 之后调用，此时只有空的根目录 "/"。
 * 创建的目录结构：
 *
 *   /
 *   ├── bin/          （占位，暂无命令二进制）
 *   ├── etc/
 *   │   ├── hostname
 *   │   ├── os-release
 *   │   └── passwd
 *   ├── home/
 *   │   └── user/
 *   │       └── .bashrc
 *   ├── tmp/
 *   │   └── motd
 *   ├── usr/
 *   └── var/
 */
static void seed_filesystem(void)
{
    /* --- 标准顶层目录 --- */
    vfs_mkdir("/bin");
    vfs_mkdir("/etc");
    vfs_mkdir("/home");
    vfs_mkdir("/tmp");
    vfs_mkdir("/usr");
    vfs_mkdir("/var");
    vfs_mkdir("/home/user");

    /* --- 系统配置文件 --- */
    vfs_write("/etc/hostname", "mini-linux");
    vfs_write("/etc/os-release", "NAME=\"Mini-Linux\"\nVERSION=\"1.0\"\nID=mini-linux\n");
    vfs_write("/etc/passwd",
              "root:x:0:0:root:/root:/bin/sh\n"
              "user:x:1000:1000:user:/home/user:/bin/sh\n");

    /* --- 用户文件 --- */
    vfs_write("/home/user/.bashrc", "# Mini-Linux user config\n");

    /* --- 开机欢迎信息（Message Of The Day） --- */
    vfs_write("/tmp/motd",
              "Welcome to Mini-Linux!\n"
              "Type 'help' for available commands.\n");
}

/*
 * kernel_boot — 系统启动入口
 *
 * 按顺序初始化各子系统，最后进入 Shell 主循环。
 * shell_run() 会阻塞，直到用户输入 shutdown 或 stdin 关闭。
 */
void kernel_boot(void)
{
    /* 模拟内核 boot 日志，增加「操作系统」沉浸感 */
    printf("Booting Mini-Linux 1.0 ...\n");
    printf("[    0.000000] Initializing virtual filesystem\n");
    vfs_init();           /* 创建根目录 /，cwd = root */
    seed_filesystem();    /* 填充目录和初始文件 */

    printf("[    0.001000] Starting process scheduler (simulated)\n");
    proc_init();          /* 创建 init 进程 (PID 1) */

    printf("[    0.002000] Mounting root filesystem OK\n");
    vfs_cd("/home/user"); /* 登录后默认进入用户主目录 */

    /* 读取并显示 motd */
    char motd[MAX_CONTENT];
    if (vfs_read("/tmp/motd", motd, sizeof(motd)) == 0)
    {
        printf("%s", motd);
    }

    printf("Login: root\n");
    shell_run();          /* 进入交互式 Shell，此处阻塞 */
}

/*
 * kernel_shutdown — 系统关机
 * 由 shell_run() 在退出循环前调用。
 */
void kernel_shutdown(void)
{
    printf("Power down.\n");
}
