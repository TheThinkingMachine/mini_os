/*
 * ============================================================================
 * shell.c — 交互式 Shell 实现
 * ============================================================================
 *
 * Shell 是 Mini-Linux 的用户界面，采用 REPL（Read-Eval-Print Loop）模型。
 *
 * 与真实 Linux Shell 的区别：
 *   - 不 fork/exec 外部程序，所有命令都是内置函数
 *   - 不支持管道 |、通配符 *、后台 & 等高级特性
 *   - echo 仅支持 > 重定向，不支持 >> 追加
 *
 * 主循环（shell_run）：
 *   while (running) {
 *       print_prompt();     // 显示 mini-linux:/path$
 *       fgets(line);        // 读用户输入
 *       execute(line);      // 解析并执行
 *   }
 *   kernel_shutdown();
 *
 * 模块依赖：vfs.h（文件操作）、proc.h（进程命令）、kernel.h（关机）
 * ============================================================================
 */

#include "shell.h"

#include "kernel.h"
#include "proc.h"
#include "vfs.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

/* 跨平台清屏：Windows 用 cls，Unix 用 clear */
#ifdef _WIN32
#include <windows.h>
#define CLEAR_SCREEN() system("cls")
#else
#define CLEAR_SCREEN() system("clear")
#endif

/*
 * running — Shell 主循环控制标志
 *   1 = 继续运行
 *   0 = 用户输入 shutdown，准备退出
 */
static int running = 1;

/* ==========================================================================
 * 命令行解析
 * ========================================================================== */

/*
 * split_args — 将命令行拆分为 argc/argv 形式
 *
 * 参数：
 *   line     — 可修改的输入缓冲区（strtok 会写入 '\0'）
 *   argv     — 输出：argv[0] 为命令名，argv[1..] 为参数
 *   max_args — argv 数组容量上限
 *
 * 返回：参数个数 argc（不含末尾 NULL）
 *
 * 分隔符：空格、制表符、换行符
 * 不支持引号包裹（"hello world" 会被拆成两个参数）
 *
 * 示例："ls -la /etc" → argc=3, argv=["ls","-la","/etc",NULL]
 */
static int split_args(char *line, char *argv[], int max_args)
{
    int argc = 0;
    char *tok = strtok(line, " \t\r\n");

    while (tok && argc < max_args - 1)
    {
        argv[argc++] = tok;
        tok = strtok(NULL, " \t\r\n");
    }

    argv[argc] = NULL;   /* POSIX 风格：argv 以 NULL 结尾 */
    return argc;
}

/* ==========================================================================
 * 提示符与帮助
 * ========================================================================== */

/*
 * print_prompt — 打印 Shell 提示符
 *
 * 格式：mini-linux:<当前路径>$
 * 示例：mini-linux:/home/user$
 *
 * fflush 确保提示符立即显示（避免输出缓冲导致输入错位）。
 */
static void print_prompt(void)
{
    char path[MAX_PATH];
    vfs_pwd(path, sizeof(path));
    printf("mini-linux:%s$ ", path);
    fflush(stdout);
}

/*
 * cmd_help — 打印所有内置命令的帮助信息
 * 对应用户输入 help 命令。
 */
static void cmd_help(void)
{
    printf("Mini-Linux shell commands:\n");
    printf("  ls [-a] [-l] [path]   list directory\n");
    printf("  cd <path>             change directory\n");
    printf("  pwd                   print working directory\n");
    printf("  mkdir <dir>           create directory\n");
    printf("  touch <file>          create empty file\n");
    printf("  cat <file>            print file content\n");
    printf("  echo <text> [> file]  print or redirect to file\n");
    printf("  rm <path>             remove file or empty dir\n");
    printf("  ps                    list processes\n");
    printf("  kill <pid>            terminate process\n");
    printf("  uname [-a]            system information\n");
    printf("  whoami                current user\n");
    printf("  date                  show date/time\n");
    printf("  clear                 clear screen\n");
    printf("  help                  show this help\n");
    printf("  shutdown              power off system\n");
}

/* ==========================================================================
 * 单个命令实现
 * ========================================================================== */

/*
 * cmd_echo — 实现 echo 命令，支持输出重定向
 *
 * 两种模式：
 *   echo hello world       → 打印到 stdout
 *   echo hello > file.txt  → 写入虚拟文件
 *
 * 解析逻辑：
 *   遍历 argv[1..]，遇到 ">" 时停止收集文本，
 *   下一个参数作为输出文件名。
 *
 * 限制：
 *   - 仅支持 >，不支持 >>
 *   - 不支持 echo "a b c" 引号语法
 *
 * 返回：0 成功，1 写文件失败
 */
static int cmd_echo(int argc, char *argv[])
{
    char text[MAX_LINE] = {0};
    const char *outfile = NULL;
    size_t pos = 0;

    for (int i = 1; i < argc; i++)
    {
        if (strcmp(argv[i], ">") == 0)
        {
            /* 遇到重定向符，下一个参数是目标文件 */
            if (i + 1 < argc)
            {
                outfile = argv[i + 1];
            }
            break;
        }

        /* 多个单词之间加空格 */
        if (pos > 0 && pos < sizeof(text) - 1)
        {
            text[pos++] = ' ';
        }

        size_t len = strlen(argv[i]);
        if (pos + len >= sizeof(text) - 1)
        {
            len = sizeof(text) - pos - 1;   /* 防止溢出 */
        }
        memcpy(text + pos, argv[i], len);
        pos += len;
    }
    text[pos] = '\0';

    if (outfile)
    {
        if (vfs_write(outfile, text) != 0)
        {
            printf("echo: cannot write to '%s'\n", outfile);
            return 1;
        }
    }
    else
    {
        printf("%s\n", text);
    }
    return 0;
}

/* ==========================================================================
 * 命令分派器
 * ========================================================================== */

/*
 * execute — 解析并执行一条命令
 *
 * 参数：
 *   line — 用户输入的整行（会被 split_args 修改）
 *
 * 返回：
 *   0 — 命令执行成功或空行
 *   1 — 未知命令
 *
 * 实现方式：长长的 if-else 链，每个分支对应一个内置命令。
 * 真实 Shell 通常用函数指针表（dispatch table），此处为教学简化。
 */
static int execute(char *line)
{
    char *argv[MAX_ARGS];
    int argc = split_args(line, argv, MAX_ARGS);

    /* 空行直接返回 */
    if (argc == 0)
    {
        return 0;
    }

    const char *cmd = argv[0];

    /* --- help：显示帮助 --- */
    if (strcmp(cmd, "help") == 0)
    {
        cmd_help();
    }

    /* --- clear：清屏 --- */
    else if (strcmp(cmd, "clear") == 0)
    {
        CLEAR_SCREEN();
    }

    /* --- pwd：打印当前路径 --- */
    else if (strcmp(cmd, "pwd") == 0)
    {
        char path[MAX_PATH];
        vfs_pwd(path, sizeof(path));
        printf("%s\n", path);
    }

    /* --- cd：切换目录 --- */
    else if (strcmp(cmd, "cd") == 0)
    {
        if (argc < 2)
        {
            /* 无参数时回到根目录（简化行为） */
            if (vfs_cd("/") != 0)
            {
                printf("cd: cannot access '/'\n");
            }
        }
        else if (vfs_cd(argv[1]) != 0)
        {
            printf("cd: %s: No such file or directory\n", argv[1]);
        }
    }

    /* --- ls：列目录 --- */
    else if (strcmp(cmd, "ls") == 0)
    {
        int show_all = 0;   /* -a：显示隐藏文件 */
        int long_fmt = 0;   /* -l：长格式 */
        const char *path = NULL;

        /* 解析选项和路径参数，支持 ls -la /etc 等组合 */
        for (int i = 1; i < argc; i++)
        {
            if (argv[i][0] == '-')
            {
                /* 选项字符串中任意位置含 'a' 或 'l' 即生效 */
                if (strchr(argv[i], 'a'))
                    show_all = 1;
                if (strchr(argv[i], 'l'))
                    long_fmt = 1;
            }
            else
            {
                path = argv[i];   /* 非选项参数视为路径 */
            }
        }

        if (vfs_ls(path, show_all, long_fmt) != 0)
        {
            printf("ls: cannot access '%s'\n", path ? path : ".");
        }
    }

    /* --- mkdir：创建目录 --- */
    else if (strcmp(cmd, "mkdir") == 0)
    {
        if (argc < 2)
        {
            printf("mkdir: missing operand\n");
        }
        else if (vfs_mkdir(argv[1]) != 0)
        {
            printf("mkdir: cannot create directory '%s'\n", argv[1]);
        }
    }

    /* --- touch：创建空文件 --- */
    else if (strcmp(cmd, "touch") == 0)
    {
        if (argc < 2)
        {
            printf("touch: missing file operand\n");
        }
        else if (vfs_touch(argv[1]) != 0)
        {
            printf("touch: cannot touch '%s'\n", argv[1]);
        }
    }

    /* --- cat：显示文件内容 --- */
    else if (strcmp(cmd, "cat") == 0)
    {
        if (argc < 2)
        {
            printf("cat: missing file operand\n");
        }
        else
        {
            char buf[MAX_CONTENT];
            if (vfs_read(argv[1], buf, sizeof(buf)) != 0)
            {
                printf("cat: %s: No such file\n", argv[1]);
            }
            else
            {
                printf("%s", buf);
                /* 若文件末尾无换行，补一个，避免提示符紧跟内容 */
                if (buf[0] != '\0' && buf[strlen(buf) - 1] != '\n')
                {
                    printf("\n");
                }
            }
        }
    }

    /* --- echo：输出或重定向 --- */
    else if (strcmp(cmd, "echo") == 0)
    {
        cmd_echo(argc, argv);
    }

    /* --- rm：删除文件或空目录 --- */
    else if (strcmp(cmd, "rm") == 0)
    {
        if (argc < 2)
        {
            printf("rm: missing operand\n");
        }
        else if (vfs_rm(argv[1]) != 0)
        {
            printf("rm: cannot remove '%s'\n", argv[1]);
        }
    }

    /* --- ps：列出进程 --- */
    else if (strcmp(cmd, "ps") == 0)
    {
        proc_list();
    }

    /* --- kill：终止进程 --- */
    else if (strcmp(cmd, "kill") == 0)
    {
        if (argc < 2)
        {
            printf("kill: usage: kill <pid>\n");
        }
        else
        {
            int pid = atoi(argv[1]);
            if (proc_kill(pid) != 0)
            {
                printf("kill: (%d) - No such process\n", pid);
            }
        }
    }

    /* --- uname：系统信息 --- */
    else if (strcmp(cmd, "uname") == 0)
    {
        int all = (argc > 1 && strcmp(argv[1], "-a") == 0);
        if (all)
        {
            printf("Mini-Linux 1.0 x86_64 mini-linux GNU/Linux\n");
        }
        else
        {
            printf("Mini-Linux\n");
        }
    }

    /* --- whoami：当前用户（固定 root） --- */
    else if (strcmp(cmd, "whoami") == 0)
    {
        printf("root\n");
    }

    /* --- date：显示系统时间 --- */
    else if (strcmp(cmd, "date") == 0)
    {
        time_t now = time(NULL);
        printf("%s", ctime(&now));   /* ctime 自带末尾换行 */
    }

    /* --- shutdown / poweroff：关机 --- */
    else if (strcmp(cmd, "shutdown") == 0 || strcmp(cmd, "poweroff") == 0)
    {
        printf("System is going down...\n");
        running = 0;   /* 下一轮循环退出 */
    }

    /* --- 未知命令 --- */
    else
    {
        printf("%s: command not found\n", cmd);
        return 1;
    }

    return 0;
}

/* ==========================================================================
 * Shell 主循环
 * ========================================================================== */

/*
 * shell_run — 进入 Shell 主循环
 *
 * 流程：
 *   1. proc_fork("shell") — 在进程表注册 shell 进程
 *   2. 循环：提示符 → 读入 → 执行
 *   3. fgets 返回 NULL（EOF/Ctrl+D）时也退出
 *   4. 退出后调用 kernel_shutdown()
 *
 * 由 kernel_boot() 调用，阻塞直到用户关机。
 */
void shell_run(void)
{
    /* 在进程表中记录 shell 进程（PID 通常为 2） */
    proc_fork("shell");
    running = 1;

    while (running)
    {
        print_prompt();

        char line[MAX_LINE];
        if (!fgets(line, sizeof(line), stdin))
        {
            /* EOF：stdin 关闭（如管道结束） */
            printf("\n");
            break;
        }

        execute(line);
    }

    kernel_shutdown();
}
