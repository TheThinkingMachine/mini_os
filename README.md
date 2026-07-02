# Mini-Linux (mini_os)

一个用 **纯 C 语言** 编写的、类 Linux 操作系统模拟器。它在用户态程序中模拟内核启动、虚拟文件系统、进程表和 Shell 交互，适合用于学习操作系统基本概念。

> **注意**：这不是真实的操作系统内核，不访问硬件、不运行在多核/特权模式下，所有数据保存在内存中，程序退出后文件系统即消失。

---

## 目录

- [功能概览](#功能概览)
- [项目结构](#项目结构)
- [编译与运行](#编译与运行)
- [启动流程](#启动流程)
- [架构设计](#架构设计)
- [模块说明](#模块说明)
- [Shell 命令参考](#shell-命令参考)
- [预置文件系统](#预置文件系统)
- [使用示例](#使用示例)
- [限制与已知行为](#限制与已知行为)
- [扩展建议](#扩展建议)

---

## 功能概览

| 模块 | 功能 |
|------|------|
| **Kernel** | 模拟启动日志、初始化子系统、挂载根文件系统、启动 Shell |
| **VFS** | 内存中的虚拟文件系统，支持目录树、文件读写、路径解析 |
| **Process** | 简化的进程表，`init` + `shell` 等进程，`ps` / `kill` |
| **Shell** | 交互式命令行，内置类 Linux 命令 |

---

## 项目结构

```
mini_os/
├── include/              # 头文件
│   ├── types.h           # 全局常量与枚举
│   ├── vfs.h             # 虚拟文件系统接口
│   ├── proc.h            # 进程管理接口
│   ├── shell.h           # Shell 接口
│   └── kernel.h          # 内核启动/关闭接口
├── src/
│   ├── main.c            # 程序入口
│   ├── kernel.c          # 内核初始化与预置文件系统
│   ├── vfs.c             # 虚拟文件系统实现
│   ├── proc.c            # 进程表实现
│   └── shell.c           # Shell 与内置命令
├── build.bat             # Windows 编译脚本
├── Makefile              # Linux / macOS 编译
└── README.md             # 本文档
```

---

## 编译与运行

### 依赖

- GCC（MinGW / MSYS2 / Linux gcc 均可）
- C99 标准库

### Windows

```powershell
cd mini_os
.\build.bat
.\build\mini_linux.exe
```

### Linux / macOS

```bash
cd mini_os
make
./mini_linux
```

清理编译产物：

```bash
make clean
```

---

## 启动流程

```
main()
  └── kernel_boot()
        ├── 打印启动日志
        ├── vfs_init()          # 创建根目录 /
        ├── seed_filesystem()   # 预置 /etc、/home 等目录和文件
        ├── proc_init()         # 创建 init 进程 (PID 1)
        ├── vfs_cd("/home/user")# 切换到用户主目录
        ├── 读取 /tmp/motd 欢迎信息
        └── shell_run()         # 进入交互式 Shell
              └── kernel_shutdown()  # shutdown 后关机
```

启动后默认工作目录为 `/home/user`，提示符格式：

```text
mini-linux:/home/user$
```

---

## 架构设计

```
┌─────────────────────────────────────────┐
│                  Shell                  │
│  (命令解析、内置命令、用户交互)              │
└──────────────┬──────────────┬───────────┘
               │              │
       ┌───────▼──────┐  ┌────▼─────┐
       │     VFS      │  │   Proc   │
       │ (内存文件树)   │  │ (进程表)  │
       └───────▲──────┘  └────▲─────┘
               │              │
       ┌───────┴──────────────┴─────┐
       │          Kernel            │
       │   (初始化、预置数据)          │
       └────────────────────────────┘
```

### 与真实 Linux 的对比

| 特性 | 真实 Linux | Mini-Linux |
|------|-----------|------------|
| 运行模式 | 内核态 + 用户态 | 单一用户态进程 |
| 文件系统 | 磁盘 + VFS + inode | 内存链表树 |
| 命令执行 | fork + exec | Shell 内直接函数调用 |
| 进程调度 | 抢占式多任务 | 静态进程表，无真实调度 |
| 权限模型 | uid/gid/权限位 | 固定 root，无权限检查 |
| 持久化 | 写入磁盘 | 程序退出即丢失 |

---

## 模块说明

### 1. types.h — 全局定义

| 常量 | 值 | 说明 |
|------|-----|------|
| `MAX_NAME` | 64 | 文件/进程名最大长度 |
| `MAX_PATH` | 256 | 路径最大长度 |
| `MAX_LINE` | 512 | Shell 输入行最大长度 |
| `MAX_CONTENT` | 4096 | 单文件最大内容 |
| `MAX_PROCS` | 32 | 最大进程数 |
| `MAX_ARGS` | 16 | 命令行最大参数数 |

枚举：

- `node_type_t`：`NODE_FILE` / `NODE_DIR`
- `proc_state_t`：`PROC_RUNNING` / `PROC_SLEEPING` / `PROC_ZOMBIE`

### 2. VFS — 虚拟文件系统

#### 核心数据结构

```c
typedef struct vfs_node {
    char name[MAX_NAME];       // 节点名
    node_type_t type;          // 文件或目录
    char content[MAX_CONTENT];   // 文件内容（目录不使用）
    struct vfs_node *parent;   // 父目录
    struct vfs_node *children; // 第一个子节点
    struct vfs_node *next;     // 同级兄弟节点
} vfs_node_t;
```

文件系统是一棵 **多叉树 + 兄弟链表**：

- 父子关系：`parent` / `children`
- 同级节点：`next` 串联

#### 全局状态

- `root`：根目录 `/`
- `cwd`：当前工作目录（Current Working Directory）

#### 主要 API

| 函数 | 说明 |
|------|------|
| `vfs_init()` | 创建根目录，cwd 指向 root |
| `vfs_resolve(path, &node)` | 解析绝对/相对路径到节点 |
| `vfs_mkdir(path)` | 创建目录 |
| `vfs_touch(path)` | 创建空文件（已存在则成功） |
| `vfs_write(path, data)` | 写入文件（不存在则自动创建） |
| `vfs_read(path, buf, len)` | 读取文件内容 |
| `vfs_rm(path)` | 删除空目录或文件 |
| `vfs_ls(path, -a, -l)` | 列出目录 |
| `vfs_cd(path)` | 切换当前目录 |
| `vfs_pwd(buf, len)` | 获取当前路径 |

#### 路径规则

- 以 `/` 开头：从根目录解析（绝对路径）
- 否则：从 `cwd` 解析（相对路径）
- 支持 `.` 和 `..`

### 3. Process — 进程管理

#### 数据结构

```c
typedef struct process {
    int pid;
    int ppid;
    char name[MAX_NAME];
    proc_state_t state;
    int active;
} process_t;
```

#### 行为说明

- 启动时创建 `init`（PID 1）
- Shell 启动时调用 `proc_fork("shell")` 注册 shell 进程（PID 2）
- `proc_kill(pid)` 不能杀死 PID 1
- `ps` 命令打印进程表

> 进程仅为**模拟记录**，没有真实的 fork/exec 或 CPU 调度。

### 4. Shell — 命令解释器

Shell 运行在 REPL 循环中：

1. 打印提示符（含当前路径）
2. `fgets` 读取用户输入
3. `split_args` 按空格拆分参数
4. `execute` 匹配并执行内置命令
5. 输入 `shutdown` 或 EOF 时退出

所有命令均为 **Shell 内置实现**，不会启动子进程。

### 5. Kernel — 内核引导

`kernel_boot()` 负责按顺序初始化各子系统；`seed_filesystem()` 预置标准 Linux 风格目录结构。

---

## Shell 命令参考

| 命令 | 说明 | 示例 |
|------|------|------|
| `ls [-a] [-l] [path]` | 列出目录内容 | `ls -la /etc` |
| `cd [path]` | 切换目录（无参数则回到 `/`） | `cd /tmp` |
| `pwd` | 打印当前路径 | `pwd` |
| `mkdir <dir>` | 创建目录 | `mkdir /tmp/test` |
| `touch <file>` | 创建空文件 | `touch note.txt` |
| `cat <file>` | 显示文件内容 | `cat /etc/hostname` |
| `echo <text> [> file]` | 输出或重定向写入 | `echo hi > /tmp/a.txt` |
| `rm <path>` | 删除文件或空目录 | `rm note.txt` |
| `ps` | 列出进程 | `ps` |
| `kill <pid>` | 终止进程 | `kill 2` |
| `uname [-a]` | 系统信息 | `uname -a` |
| `whoami` | 当前用户（固定 root） | `whoami` |
| `date` | 显示当前时间 | `date` |
| `clear` | 清屏 | `clear` |
| `help` | 显示帮助 | `help` |
| `shutdown` / `poweroff` | 关闭系统 | `shutdown` |

### ls 选项说明

- `-a`：显示以 `.` 开头的隐藏文件
- `-l`：长格式，显示类型、大小、文件名

---

## 预置文件系统

启动后自动创建以下结构：

```
/
├── bin/
├── etc/
│   ├── hostname        → "mini-linux"
│   ├── os-release      → 系统版本信息
│   └── passwd            → 用户列表
├── home/
│   └── user/
│       └── .bashrc       → 用户配置（隐藏文件）
├── tmp/
│   └── motd              → 欢迎信息
├── usr/
└── var/
```

默认登录目录：`/home/user`

---

## 使用示例

```text
Booting Mini-Linux 1.0 ...
[    0.000000] Initializing virtual filesystem
[    0.001000] Starting process scheduler (simulated)
[    0.002000] Mounting root filesystem OK
Welcome to Mini-Linux!
Type 'help' for available commands.
Login: root
mini-linux:/home/user$ ls -a
.bashrc
mini-linux:/home/user$ cd /etc
mini-linux:/etc$ cat hostname
mini-linux
mini-linux:/etc$ echo Hello World > /tmp/hello.txt
mini-linux:/etc$ cat /tmp/hello.txt
Hello World
mini-linux:/etc$ ps
  PID  PPID STAT COMMAND
    1     0    R init
    2     1    R shell
mini-linux:/etc$ shutdown
System is going down...
Power down.
```

---

## 限制与已知行为

1. **无持久化**：所有文件修改仅存在于内存，程序退出后丢失。
2. **单用户固定 root**：没有真正的用户认证和权限控制。
3. **无真实多进程**：`fork` 只在进程表中添加记录，不创建线程或子进程。
4. **文件大小限制**：单文件最大 4096 字节（`MAX_CONTENT`）。
5. **目录删除限制**：只能删除空目录；非空目录无法 `rm`。
6. **隐藏文件**：默认 `ls` 不显示以 `.` 开头的文件，需加 `-a`。
7. **无管道与通配符**：不支持 `|`、`*.txt` 等 Shell 高级特性。
8. **`kill 2` 会终止 shell 进程记录**，但 Shell 循环仍会继续运行（进程模拟与 Shell 生命周期未完全联动）。

---

## 扩展建议

以下是常见的改进方向：

- [ ] 实现 `mv`、`cp`、`tree` 命令
- [ ] 支持管道 `cmd1 | cmd2`
- [ ] 文件/目录权限位（`chmod`）
- [ ] 将虚拟文件系统序列化到磁盘（持久化）
- [ ] 命令历史（上下箭头）
- [ ] Tab 补全
- [ ] 更真实的 `fork` / 子 Shell 模拟

---

## 许可证

Educational project — 可自由学习、修改和扩展。
