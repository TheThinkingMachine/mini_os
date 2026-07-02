/*
 * ============================================================================
 * vfs.h — 虚拟文件系统（Virtual File System）接口
 * ============================================================================
 *
 * Mini-Linux 的文件系统完全运行在内存中，程序退出后所有数据丢失。
 * 不提供磁盘 I/O，也不做权限检查。
 *
 * 数据结构：以 vfs_node_t 为节点的多叉树
 *   - 父子关系：parent / children
 *   - 兄弟关系：next 链表（同一目录下的多个文件/子目录）
 *
 * 全局状态（定义在 vfs.c）：
 *   - root：根目录 "/"
 *   - cwd ：Current Working Directory，当前工作目录
 * ============================================================================
 */

#ifndef MINI_OS_VFS_H
#define MINI_OS_VFS_H

#include "types.h"

/*
 * vfs_node_t — 文件系统树中的一个节点（文件或目录）
 *
 * 内存布局示意（/etc 目录下有两个文件）：
 *
 *   etc (NODE_DIR)
 *    ├── children → hostname (NODE_FILE) → next → passwd (NODE_FILE) → NULL
 *    └── content 未使用（目录不存内容）
 *
 * 字段说明：
 *   name     — 节点名，不含路径前缀，如 "etc"、"hostname"
 *   type     — NODE_FILE 或 NODE_DIR
 *   content  — 仅文件有效，固定大小字符数组，相当于文件内容
 *   parent   — 指向父目录；root 节点的 parent 为 NULL
 *   children — 指向该目录下第一个子节点；文件节点不使用
 *   next     — 指向同级下一个兄弟节点；最后一个兄弟的 next 为 NULL
 */
typedef struct vfs_node
{
    char name[MAX_NAME];
    node_type_t type;
    char content[MAX_CONTENT];
    struct vfs_node *parent;
    struct vfs_node *children;
    struct vfs_node *next;
} vfs_node_t;

/* --- 初始化与全局访问 --- */

void vfs_init(void);                  /* 创建根目录，cwd 指向 root */
vfs_node_t *vfs_root(void);         /* 获取根目录节点指针 */
vfs_node_t *vfs_cwd(void);          /* 获取当前工作目录节点指针 */
void vfs_set_cwd(vfs_node_t *node); /* 手动设置 cwd（一般通过 vfs_cd） */

/* --- 路径与文件操作 --- */

/*
 * vfs_resolve — 将路径字符串解析为 VFS 节点
 *   path: 绝对路径（/ 开头）或相对路径（从 cwd 出发）
 *   out : 输出参数，成功时写入目标节点指针
 *   返回: 0 成功，-1 路径不存在或非法
 */
int vfs_resolve(const char *path, vfs_node_t **out);

int vfs_mkdir(const char *path);                              /* 创建目录 */
int vfs_touch(const char *path);                              /* 创建空文件 */
int vfs_write(const char *path, const char *data);            /* 写文件 */
int vfs_read(const char *path, char *buf, size_t buflen);     /* 读文件 */
int vfs_rm(const char *path);                                 /* 删除文件或空目录 */
int vfs_ls(const char *path, int show_all, int long_fmt);     /* 列目录 */
int vfs_cd(const char *path);                                 /* 切换 cwd */
int vfs_pwd(char *buf, size_t buflen);                        /* 获取 cwd 路径字符串 */

#endif /* MINI_OS_VFS_H */
