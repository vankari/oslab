#include "proc/cpu.h"
#include "mem/vmem.h"
#include "fs/inode.h"
#include "fs/dir.h"
#include "fs/file.h"
#include "lib/str.h"
#include "lib/print.h"
#include "syscall/syscall.h"
#include "syscall/sysfunc.h"

// 获取第n个参数对应的fd和这个fd对应的file
// 成功返回0 失败返回-1
static int arg_fd(int n, int* pfd, file_t** pfile)
{
    // 读出fd
    int fd = 0;
    arg_uint32(n, (uint32*)(&fd));
    
    // fd 溢出
    if(fd < 0 || fd >= FILE_PER_PROC)
        return -1;
    
    // 确定fd对应的file
    file_t* file = myproc()->filelist[fd];
    if(file == NULL)
        return -1;
    
    if(pfd) *pfd = fd;
    if(pfile) *pfile = file;

    return 0;
}

// 成功返回申请到的fd
// 失败返回-1
static int fd_alloc(file_t* file)
{
    proc_t* p = myproc();

    for(int fd = 0; fd < FILE_PER_PROC; fd++) {
        if(p->filelist[fd] == NULL) {
            p->filelist[fd] = file;
            return fd;
        }
    }

    return -1;
}

// 打开或创建文件
// char* path
// uint32 open_mode
// 成功返回fd 失败返回-1
uint64 sys_open()
{
    char path[DIR_PATH_LEN];
    uint32 open_mode;

    arg_str(0, path, DIR_PATH_LEN);
    arg_uint32(1, &open_mode);

    file_t* file = file_open(path, open_mode);
    if(file == NULL)
        return -1;
    
    int fd = fd_alloc(file);
    if(fd == -1)
        file_close(file);

    return fd;
}

// 关闭一个文件
// int fd
// 成功返回0 失败返回-1
uint64 sys_close()
{
    int fd;
    file_t* file;

    if(arg_fd(0, &fd, &file) < 0)
        return -1;

    myproc()->filelist[fd] = NULL;
    file_close(file);

    return 0;
}

// 文件内容读取
// int fd
// uint32 len
// uint64 addr
// 成功返回字节数 失败返回0
uint64 sys_read()
{
    uint32 len;
    uint64 addr;
    file_t* file;

    if(arg_fd(0, NULL, &file) < 0)
        return -1;
    arg_uint32(1, &len);
    arg_uint64(2, &addr);

    return file_read(file, len, addr, true);
}

// 文件内容写入
// int fd
// uint32 len
// uint64 addr
// 成功返回字节数 失败返回0
uint64 sys_write()
{
    uint32 len;
    uint64 addr;
    file_t* file;

    if(arg_fd(0, NULL, &file) < 0)
        return -1;
    arg_uint32(1, &len);
    arg_uint64(2, &addr);

    return file_write(file, len, addr, true);
}

// 文件偏移量设置
// int fd
// uint32 offset
// int flags (见LSEEK_xxx)
// 成功返回新的偏移量, 失败返回-1
uint64 sys_lseek()
{
    file_t* file;
    uint32 offset;
    int flags;

    if(arg_fd(0, NULL, &file) < 0)
        return -1;
    arg_uint32(1, &offset);
    arg_uint32(2, (uint32*)(&flags));

    return file_lseek(file, offset, flags);
}

// int fd
// 成功返回 new_fd 失败返回 -1
uint64 sys_dup()
{
    int new_fd;
    file_t* file;

    if(arg_fd(0, NULL, &file) < 0)
        return -1;
    
    new_fd = fd_alloc(file);
    file_dup(file);

    return new_fd;
}

// 获取文件信息
// int fd
// uint64 addr
// 成功返回0 失败返回-1
uint64 sys_fstat()
{
    uint64 addr;
    file_t* file;
    
    if(arg_fd(0, NULL, &file) < 0)
        return -1;
    arg_uint64(1, &addr);

    return file_stat(file, addr);
}

// 获取目录里的目录项
// int fd
// uint64 addr
// uint32 len
// 成功返回读取的字节数, 失败返回-1
uint64 sys_getdir()
{
    file_t* file;
    uint64 addr;
    uint32 len;

    if(arg_fd(0, NULL, &file) < 0)
        return -1;
    arg_uint64(1, &addr);
    arg_uint32(2, &len);

    if(file->type != FD_DIR || file->ip == NULL)
        return -1;

    inode_lock(file->ip);
    len = dir_get_entries(file->ip, len, (void*)addr, true);
    inode_unlock(file->ip);

    return len;
}

// 创建目录
// char* path
// 成功返回0 失败返回-1
uint64 sys_mkdir()
{
    char path[DIR_PATH_LEN];
    arg_str(0, path, DIR_PATH_LEN);

    inode_t* inode = path_create_inode(path, FT_DIR, 0, 0);

    return (inode == NULL) ? -1 : 0;
}

// 修改当前所在的目录
// char* path
// 成功返回0 失败返回-1
uint64 sys_chdir()
{
    char path[DIR_PATH_LEN];
    arg_str(0, path, DIR_PATH_LEN);

    return dir_change(path);
}

// 文件链接
// char* old_path
// char* new_path
// 成功返回0 失败返回-1
uint64 sys_link()
{
    char old_path[DIR_PATH_LEN], new_path[DIR_PATH_LEN];
    arg_str(0, old_path, DIR_PATH_LEN);
    arg_str(1, new_path, DIR_PATH_LEN);

    return path_link(old_path, new_path);
}

// 文件删除链接 (link=0 则删除文件)
// char* path
// 成功返回0 失败返回-1
uint64 sys_unlink()
{
    char path[DIR_PATH_LEN];
    arg_str(0, path, DIR_PATH_LEN);

    return path_unlink(path);
}