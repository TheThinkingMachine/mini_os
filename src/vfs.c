/*
 * ============================================================================
 * vfs.c — 虚拟文件系统实现
 * ============================================================================
 *
 * 核心设计：
 *   用 vfs_node_t 节点构成内存中的目录树，不访问真实磁盘。
 *
 * 两个全局指针：
 *   root — 始终指向根目录 "/"
 *   cwd  — Current Working Directory，随 cd 命令变化
 *
 * 路径解析规则：
 *   "/etc/passwd"  → 绝对路径，从 root 出发
 *   "note.txt"     → 相对路径，从 cwd 出发
 *   "../tmp"       → 支持 . 和 ..
 *
 * 内部函数（static）：
 *   node_create, node_append_child, node_find_child — 节点 CRUD
 *   split_path, walk_path, resolve_parent           — 路径处理
 * ============================================================================
 */

#include "vfs.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* 根目录节点，parent == NULL */
static vfs_node_t *root;

/* 当前工作目录，Shell 提示符和相对路径解析依赖此变量 */
static vfs_node_t *cwd;

/* ==========================================================================
 * 第一节：内部节点操作（不对外暴露）
 * ========================================================================== */

/*
 * node_create — 在堆上分配并初始化一个 VFS 节点
 *
 * 参数：
 *   name   — 节点名（不含 '/'），如 "etc"、"passwd"
 *   type   — NODE_FILE 或 NODE_DIR
 *   parent — 父目录指针；root 节点传入 NULL
 *
 * 返回：新节点指针；内存不足时返回 NULL
 *
 * 使用 calloc 确保 content 等字段初始为 0。
 */
static vfs_node_t *node_create(const char *name, node_type_t type, vfs_node_t *parent)
{
    vfs_node_t *node = (vfs_node_t *)calloc(1, sizeof(vfs_node_t));
    if (!node)
    {
        return NULL;
    }
    strncpy(node->name, name, MAX_NAME - 1);
    node->name[MAX_NAME - 1] = '\0';
    node->type = type;
    node->parent = parent;
    /* children、next 已由 calloc 置 NULL */
    return node;
}

/*
 * node_append_child — 将 child 追加到 parent 的 children 链表末尾
 *
 * 同一目录下多个文件/子目录通过 next 指针串联：
 *   parent->children → A → B → C → NULL
 *
 * 新节点总是插在链表尾部，保持插入顺序。
 */
static void node_append_child(vfs_node_t *parent, vfs_node_t *child)
{
    if (!parent->children)
    {
        /* 第一个子节点 */
        parent->children = child;
        return;
    }
    /* 走到链表末尾 */
    vfs_node_t *cur = parent->children;
    while (cur->next)
    {
        cur = cur->next;
    }
    cur->next = child;
}

/*
 * node_find_child — 在 parent 的直接子节点中按名字查找
 *
 * 只搜索一层，不递归。
 * 返回：找到的节点指针，或 NULL（不存在）
 */
static vfs_node_t *node_find_child(vfs_node_t *parent, const char *name)
{
    for (vfs_node_t *cur = parent->children; cur; cur = cur->next)
    {
        if (strcmp(cur->name, name) == 0)
        {
            return cur;
        }
    }
    return NULL;
}

/*
 * split_path — 将路径字符串拆分为各段
 *
 * 输入/输出：
 *   path  — 可修改的缓冲区，strtok 会在 '/' 处写 '\0'
 *   parts — 输出数组，parts[0]..parts[count-1] 指向各段字符串
 *   count — 输出段数
 *
 * 示例：
 *   "/a/b/c" → parts = ["a","b","c"], count = 3
 *   "///"    → count = 0（空段被 strtok 跳过）
 *
 * 注意：path 会被 strtok 破坏，调用方应传入副本。
 */
static void split_path(char *path, char **parts, int *count)
{
    *count = 0;
    char *tok = strtok(path, "/");
    while (tok && *count < MAX_ARGS)
    {
        parts[(*count)++] = tok;
        tok = strtok(NULL, "/");
    }
}

/*
 * walk_path — 从 start 节点出发，沿路径各段逐级导航
 *
 * 参数：
 *   start       — 起始节点（root 或 cwd）
 *   parts/count — split_path 的输出
 *   out         — 成功时写入最终到达的节点
 *   create_dirs — 1=中间缺失目录时自动创建；0=不存在则失败
 *
 * 特殊段处理：
 *   "."  — 忽略，留在当前节点
 *   ".." — 上升到 parent（已在 root 则不动）
 *
 * 返回：0 成功，-1 路径不存在或内存不足
 */
static int walk_path(vfs_node_t *start, char **parts, int count, vfs_node_t **out, int create_dirs)
{
    vfs_node_t *cur = start;

    for (int i = 0; i < count; i++)
    {
        /* 当前目录，跳过 */
        if (strcmp(parts[i], ".") == 0)
        {
            continue;
        }

        /* 上级目录 */
        if (strcmp(parts[i], "..") == 0)
        {
            if (cur->parent)
            {
                cur = cur->parent;
            }
            continue;
        }

        /* 在子节点中查找下一段 */
        vfs_node_t *next = node_find_child(cur, parts[i]);
        if (!next)
        {
            if (!create_dirs)
            {
                return -1;   /* 路径不存在 */
            }
            /* 自动创建中间目录（类似 mkdir -p） */
            next = node_create(parts[i], NODE_DIR, cur);
            if (!next)
            {
                return -1;
            }
            node_append_child(cur, next);
        }
        cur = next;
    }

    *out = cur;
    return 0;
}

/* ==========================================================================
 * 第二节：初始化与全局访问
 * ========================================================================== */

/*
 * vfs_init — 初始化 VFS，创建空的根文件系统
 * 必须在任何其他 vfs_* 调用之前执行。
 */
void vfs_init(void)
{
    root = node_create("/", NODE_DIR, NULL);
    cwd = root;   /* 初始 cwd 在根目录 */
}

vfs_node_t *vfs_root(void) { return root; }
vfs_node_t *vfs_cwd(void)  { return cwd; }

void vfs_set_cwd(vfs_node_t *node)
{
    cwd = node;
}

/* ==========================================================================
 * 第三节：路径解析
 * ========================================================================== */

/*
 * vfs_resolve — 将路径字符串解析为 VFS 节点指针
 *
 * 参数：
 *   path — 路径，如 "/etc/hostname" 或 "note.txt"
 *   out  — 输出，成功时 *out 指向目标节点
 *
 * 返回：0 成功，-1 失败
 *
 * 流程：
 *   1. 判断绝对/相对，选择 start = root 或 cwd
 *   2. split_path 拆分路径
 *   3. walk_path 逐级导航（不自动创建目录）
 */
int vfs_resolve(const char *path, vfs_node_t **out)
{
    if (!path || !path[0])
    {
        return -1;
    }

    /* 拷贝 path，因为 split_path 会破坏缓冲区 */
    char buf[MAX_PATH];
    strncpy(buf, path, MAX_PATH - 1);
    buf[MAX_PATH - 1] = '\0';

    /* 绝对路径从 root 出发，相对路径从 cwd 出发 */
    vfs_node_t *start = cwd;
    if (path[0] == '/')
    {
        start = root;
    }

    char *parts[MAX_ARGS];
    int count = 0;
    split_path(buf, parts, &count);

    /* 路径仅为 "/" 时，count==0，直接返回 root */
    if (count == 0)
    {
        *out = root;
        return 0;
    }

    return walk_path(start, parts, count, out, 0);
}

/*
 * resolve_parent — 将路径拆分为「父目录 + 叶子名」
 *
 * 用于 mkdir/touch 等「在父目录下创建新节点」的操作。
 *
 * 参数：
 *   path   — 完整路径
 *   parent — 输出：父目录节点指针
 *   leaf   — 输出：最后一段名字（新文件/目录名）
 *
 * 示例：
 *   "/etc/hostname" → parent=etc节点, leaf="hostname"
 *   "note.txt"      → parent=cwd,     leaf="note.txt"
 *   "/tmp"          → parent=root,    leaf="tmp"
 *
 * 返回：0 成功，-1 失败
 */
static int resolve_parent(const char *path, vfs_node_t **parent, char *leaf)
{
    char buf[MAX_PATH];
    strncpy(buf, path, MAX_PATH - 1);
    buf[MAX_PATH - 1] = '\0';

    /* 找最后一个 '/' 的位置 */
    char *slash = strrchr(buf, '/');
    if (!slash)
    {
        /* 无 '/'：纯文件名，父目录是当前 cwd */
        *parent = cwd;
        strncpy(leaf, buf, MAX_NAME - 1);
        leaf[MAX_NAME - 1] = '\0';
        return 0;
    }

    /* 提取 '/' 后面的叶子名 */
    strncpy(leaf, slash + 1, MAX_NAME - 1);
    leaf[MAX_NAME - 1] = '\0';
    if (leaf[0] == '\0')
    {
        return -1;   /* 路径以 '/' 结尾，无叶子名，如 "/etc/" */
    }

    /* 截断得到父路径 */
    *slash = '\0';
    if (buf[0] == '\0')
    {
        /* 原路径形如 "/foo"，父目录是 root */
        *parent = root;
        return 0;
    }

    return vfs_resolve(buf, parent);
}

/* ==========================================================================
 * 第四节：文件/目录 CRUD
 * ========================================================================== */

/*
 * vfs_mkdir — 创建目录
 * 父目录必须已存在；同名节点已存在则失败。
 */
int vfs_mkdir(const char *path)
{
    vfs_node_t *parent = NULL;
    char leaf[MAX_NAME];

    if (resolve_parent(path, &parent, leaf) != 0)
    {
        return -1;
    }
    if (node_find_child(parent, leaf))
    {
        return -1;   /* 同名已存在 */
    }

    vfs_node_t *dir = node_create(leaf, NODE_DIR, parent);
    if (!dir)
    {
        return -1;
    }
    node_append_child(parent, dir);
    return 0;
}

/*
 * vfs_touch — 创建空文件
 * 文件已存在时不报错（与 Linux touch 行为一致）。
 */
int vfs_touch(const char *path)
{
    vfs_node_t *parent = NULL;
    char leaf[MAX_NAME];

    if (resolve_parent(path, &parent, leaf) != 0)
    {
        return -1;
    }
    if (node_find_child(parent, leaf))
    {
        return 0;   /* 已存在，视为成功 */
    }

    vfs_node_t *file = node_create(leaf, NODE_FILE, parent);
    if (!file)
    {
        return -1;
    }
    node_append_child(parent, file);
    return 0;
}

/*
 * vfs_write — 写入文件内容（覆盖写）
 *
 * 若文件不存在，先 touch 创建再写入。
 * 内容长度受 MAX_CONTENT 限制，超出部分截断。
 */
int vfs_write(const char *path, const char *data)
{
    vfs_node_t *node = NULL;

    if (vfs_resolve(path, &node) != 0 || node->type != NODE_FILE)
    {
        /* 目标不是已存在的文件 → 尝试创建 */
        if (vfs_touch(path) != 0)
        {
            return -1;
        }
        if (vfs_resolve(path, &node) != 0)
        {
            return -1;
        }
    }

    strncpy(node->content, data, MAX_CONTENT - 1);
    node->content[MAX_CONTENT - 1] = '\0';
    return 0;
}

/*
 * vfs_read — 读取文件内容到 buf
 * 目标必须是已存在的 NODE_FILE，否则返回 -1。
 */
int vfs_read(const char *path, char *buf, size_t buflen)
{
    vfs_node_t *node = NULL;

    if (vfs_resolve(path, &node) != 0 || node->type != NODE_FILE)
    {
        return -1;
    }

    strncpy(buf, node->content, buflen - 1);
    buf[buflen - 1] = '\0';
    return 0;
}

/*
 * remove_from_parent — 从父目录的 children 链表中摘除并 free 目标节点
 *
 * 标准链表删除：维护 prev 指针，处理头节点和中间节点两种情况。
 */
static void remove_from_parent(vfs_node_t *parent, vfs_node_t *target)
{
    vfs_node_t *prev = NULL;
    for (vfs_node_t *cur = parent->children; cur; cur = cur->next)
    {
        if (cur == target)
        {
            if (prev)
            {
                prev->next = cur->next;   /* 中间或尾部节点 */
            }
            else
            {
                parent->children = cur->next;   /* 头节点 */
            }
            free(cur);
            return;
        }
        prev = cur;
    }
}

/*
 * vfs_rm — 删除文件或空目录
 *
 * 限制：
 *   - 不能删除 root
 *   - 不能删除非空目录（children != NULL）
 */
int vfs_rm(const char *path)
{
    vfs_node_t *node = NULL;

    if (vfs_resolve(path, &node) != 0)
    {
        return -1;
    }
    if (node == root)
    {
        return -1;
    }
    if (node->type == NODE_DIR && node->children)
    {
        return -1;   /* 目录非空 */
    }

    remove_from_parent(node->parent, node);
    return 0;
}

/* ==========================================================================
 * 第五节：目录浏览与 cwd 操作
 * ========================================================================== */

/*
 * vfs_ls — 列出目录内容
 *
 * 参数：
 *   path     — 要列出的目录；NULL 或 "" 表示 cwd
 *   show_all — 1 显示 dotfile（以 '.' 开头的文件）
 *   long_fmt — 1 长格式：类型 + 大小 + 名字
 *
 * 短格式输出："file1  file2  file3  \n"
 * 长格式输出："d     0  dirname\n" / "-    12  filename\n"
 */
int vfs_ls(const char *path, int show_all, int long_fmt)
{
    vfs_node_t *node = NULL;

    if (path == NULL || path[0] == '\0')
    {
        node = cwd;
    }
    else if (vfs_resolve(path, &node) != 0)
    {
        return -1;
    }

    if (node->type != NODE_DIR)
    {
        return -1;   /* 不能对文件 ls */
    }

    /* 遍历 children 链表 */
    for (vfs_node_t *cur = node->children; cur; cur = cur->next)
    {
        /* Linux 默认隐藏 dotfile */
        if (!show_all && cur->name[0] == '.')
        {
            continue;
        }

        if (long_fmt)
        {
            const char *type = cur->type == NODE_DIR ? "d" : "-";
            size_t size = cur->type == NODE_FILE ? strlen(cur->content) : 0;
            printf("%s  %4zu  %s\n", type, size, cur->name);
        }
        else
        {
            printf("%s  ", cur->name);
        }
    }

    if (!long_fmt)
    {
        printf("\n");
    }
    return 0;
}

/*
 * vfs_cd — 切换当前工作目录
 * 目标必须是已存在的 NODE_DIR。
 */
int vfs_cd(const char *path)
{
    vfs_node_t *node = NULL;

    if (vfs_resolve(path, &node) != 0 || node->type != NODE_DIR)
    {
        return -1;
    }

    cwd = node;
    return 0;
}

/*
 * vfs_pwd — 获取当前工作目录的路径字符串
 *
 * 算法：
 *   1. 从 cwd 沿 parent 向上走到 root，把每一级 name 压入 stack
 *   2. 若 depth==0（cwd 就是 root），返回 "/"
 *   3. 否则从 stack 顶到底正向拼接："/home/user"
 *
 * 示例：cwd 指向 /home/user 下的 user 节点
 *   向上收集：["user", "home"]
 *   反向拼接："/home/user"
 */
int vfs_pwd(char *buf, size_t buflen)
{
    char stack[MAX_ARGS][MAX_NAME];
    int depth = 0;

    /* 向上回溯，收集路径分量（不含 root 的 "/"） */
    for (vfs_node_t *cur = cwd; cur && cur->parent; cur = cur->parent)
    {
        if (depth < MAX_ARGS)
        {
            strncpy(stack[depth++], cur->name, MAX_NAME - 1);
            stack[depth - 1][MAX_NAME - 1] = '\0';
        }
    }

    buf[0] = '\0';

    if (depth == 0)
    {
        /* cwd 就是 root */
        strncpy(buf, "/", buflen - 1);
        buf[buflen - 1] = '\0';
        return 0;
    }

    /* 从根到叶正向拼接 */
    size_t pos = 0;
    for (int i = depth - 1; i >= 0; i--)
    {
        int n = snprintf(buf + pos, buflen - pos, "/%s", stack[i]);
        if (n < 0 || (size_t)n >= buflen - pos)
        {
            break;   /* 缓冲区不足，截断 */
        }
        pos += (size_t)n;
    }
    return 0;
}
