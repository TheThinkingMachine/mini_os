/*
 * ============================================================================
 * kernel.h — 内核引导接口
 * ============================================================================
 *
 * kernel_boot() 按顺序完成「类 Linux 开机」：
 *   1. 初始化 VFS 并预置目录树
 *   2. 初始化进程表（init 进程）
 *   3. 切换到 /home/user，显示 motd
 *   4. 启动 Shell（阻塞）
 *
 * kernel_shutdown() 在 Shell 退出时打印关机信息。
 * ============================================================================
 */

#ifndef MINI_OS_KERNEL_H
#define MINI_OS_KERNEL_H

void kernel_boot(void);
void kernel_shutdown(void);

#endif /* MINI_OS_KERNEL_H */
