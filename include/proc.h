/*
 * ============================================================================
 * proc.h — 进程管理接口
 * ============================================================================
 *
 * 本模块模拟 Linux 进程表，但不做真正的 fork/exec/调度：
 *   - 不会在操作系统层面创建新进程
 *   - 仅在静态数组 table[] 中记录 PID、PPID、名称、状态
 *   - ps 命令读取此表；kill 命令将条目标记为 inactive
 *
 * 启动时固定存在 init (PID=1)，Shell 启动时 fork 出 shell (PID=2)。
 * ============================================================================
 */

#ifndef MINI_OS_PROC_H
#define MINI_OS_PROC_H

#include "types.h"

/*
 * process_t — 进程表中的一个条目
 *
 *   pid    — Process ID，全局唯一，从 2 起递增分配（1 保留给 init）
 *   ppid   — Parent PID，父进程 ID；init 的 ppid 为 0
 *   name   — 进程名，如 "init"、"shell"
 *   state  — 运行状态枚举
 *   active — 1=槽位占用，0=槽位空闲（可被新 fork 复用）
 */
typedef struct process {
    int pid;
    int ppid;
    char name[MAX_NAME];
    proc_state_t state;
    int active;
} process_t;

void proc_init(void);                       /* 清空进程表，创建 init */
int proc_current(void);                     /* 返回当前逻辑 PID（默认 1） */
int proc_fork(const char *name);            /* 模拟 fork，返回新 PID 或 -1 */
int proc_kill(int pid);                     /* 终止进程，不能杀 PID 1 */
void proc_list(void);                       /* 打印进程表（ps 命令调用） */
const char *proc_state_name(proc_state_t state); /* 状态 → 单字符字符串 */

#endif /* MINI_OS_PROC_H */
