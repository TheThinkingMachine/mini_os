/*
 * ============================================================================
 * shell.h — 交互式 Shell 接口
 * ============================================================================
 *
 * Shell 是用户与 Mini-Linux 交互的入口，采用 REPL 模型：
 *   Read   — fgets 读取一行命令
 *   Eval   — split_args 解析 + execute 分派
 *   Print  — 命令执行结果输出到 stdout
 *   Loop   — 重复直到 shutdown 或 EOF
 *
 * 所有命令均为 Shell 内置（builtin），不调用外部程序。
 * ============================================================================
 */

#ifndef MINI_OS_SHELL_H
#define MINI_OS_SHELL_H

void shell_run(void);   /* 进入 Shell 主循环，正常退出时调用 kernel_shutdown */

#endif /* MINI_OS_SHELL_H */
