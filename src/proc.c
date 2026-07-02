/*
 * ============================================================================
 * proc.c — 进程管理实现
 * ============================================================================
 *
 * 使用固定大小静态数组 table[MAX_PROCS] 作为进程表。
 * 不使用动态内存，实现简单、可预测。
 *
 * PID 分配策略：
 *   - PID 1 永久保留给 init
 *   - 新进程从 next_pid 递增分配（2, 3, 4, ...）
 *   - 进程被杀死后槽位 active=0，可被后续 fork 复用
 *     （但 PID 不会回收重用，next_pid 只增不减）
 * ============================================================================
 */

#include "proc.h"

#include <stdio.h>
#include <string.h>

/* 进程表：最多 MAX_PROCS 个条目 */
static process_t table[MAX_PROCS];

/* 当前「逻辑上」正在执行的进程 PID（Shell 启动前为 init=1） */
static int current_pid = 1;

/* 下一个可分配的新 PID，从 2 开始（1 已给 init） */
static int next_pid = 2;

/*
 * proc_init — 初始化进程子系统
 *
 * 清空整个进程表，创建第一个进程 init：
 *   PID=1, PPID=0, name="init", state=RUNNING
 */
void proc_init(void)
{
    memset(table, 0, sizeof(table));

    table[0].pid = 1;
    table[0].ppid = 0;          /* init 没有父进程 */
    strncpy(table[0].name, "init", MAX_NAME - 1);
    table[0].state = PROC_RUNNING;
    table[0].active = 1;

    current_pid = 1;
    next_pid = 2;
}

/*
 * proc_current — 返回当前进程 PID
 * 本项目中 current_pid 基本不变，预留供未来扩展。
 */
int proc_current(void)
{
    return current_pid;
}

/*
 * proc_fork — 模拟 fork，在进程表中新增一条记录
 *
 * 参数：
 *   name — 新进程名称，如 "shell"
 *
 * 返回：
 *   成功 — 新进程的 PID（>= 2）
 *   失败 — -1（进程表已满）
 *
 * 注意：这不是操作系统级的 fork()，不会复制内存空间或创建新线程。
 */
int proc_fork(const char *name)
{
    /* 线性扫描找第一个空闲槽位 */
    for (int i = 0; i < MAX_PROCS; i++) {
        if (!table[i].active) {
            table[i].pid = next_pid++;
            table[i].ppid = current_pid;   /* 父进程为 fork 调用者 */
            strncpy(table[i].name, name, MAX_NAME - 1);
            table[i].state = PROC_RUNNING;
            table[i].active = 1;
            return table[i].pid;
        }
    }
    return -1;   /* 进程表满 */
}

/*
 * proc_kill — 终止指定 PID 的进程
 *
 * 规则：
 *   - PID <= 1 不可杀（保护 init）
 *   - 找到后将 state 设为 ZOMBIE，active 设为 0
 *
 * 返回：0 成功，-1 失败（不存在或不可杀）
 *
 * 局限：kill shell 进程后 Shell 循环仍继续运行，进程表与 Shell 生命周期未完全联动。
 */
int proc_kill(int pid)
{
    if (pid <= 1) {
        return -1;
    }
    for (int i = 0; i < MAX_PROCS; i++) {
        if (table[i].active && table[i].pid == pid) {
            table[i].state = PROC_ZOMBIE;
            table[i].active = 0;
            return 0;
        }
    }
    return -1;
}

/*
 * proc_state_name — 将进程状态枚举转为 ps 风格的单字符
 *
 *   PROC_RUNNING  → "R"
 *   PROC_SLEEPING → "S"
 *   PROC_ZOMBIE   → "Z"
 */
const char *proc_state_name(proc_state_t state)
{
    switch (state) {
    case PROC_RUNNING:
        return "R";
    case PROC_SLEEPING:
        return "S";
    case PROC_ZOMBIE:
        return "Z";
    default:
        return "?";
    }
}

/*
 * proc_list — 打印进程表（ps 命令的后端实现）
 *
 * 输出格式：
 *   PID  PPID STAT COMMAND
 *     1     0    R init
 *     2     1    R shell
 */
void proc_list(void)
{
    printf("  PID  PPID STAT COMMAND\n");
    for (int i = 0; i < MAX_PROCS; i++) {
        if (!table[i].active) {
            continue;   /* 跳过空闲槽位 */
        }
        printf("%5d %5d    %s %s\n",
               table[i].pid,
               table[i].ppid,
               proc_state_name(table[i].state),
               table[i].name);
    }
}
