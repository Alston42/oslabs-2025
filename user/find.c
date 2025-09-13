#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"
#include "kernel/fs.h"

char *fmtname(char *path) {
  static char buf[DIRSIZ + 1];
  char *p;

  // Find first character after last slash.
  for (p = path + strlen(path); p >= path && *p != '/'; p--)
    ;
  p++;

  // Return blank-padded name.
  if (strlen(p) >= DIRSIZ) return p;
  memmove(buf, p, strlen(p));
  memset(buf + strlen(p), ' ', DIRSIZ - strlen(p));
  return buf;
}

int strmat(char *str, char *pattern) {
    int m = strlen(pattern), n = strlen(str);
    int pi[m];
    for (int i = 0; i < m; i++) pi[i] = 0;
    int j = 0;
    for (int i = 1; i < m; i++) {
        while (j > 0 && pattern[j] != pattern[i]) j = pi[j-1];
        if (pattern[j] == pattern[i]) j++;
        pi[i] = j;
    }
    j = 0;
    for (int i = 0; i < n; i++) {
        while (j > 0 && pattern[j] != str[i]) j = pi[j-1];
        if (pattern[j] == str[i]) j++;
        if (j == m) return i - m + 1;
    }
    return -1;
}



void find(char *path, char *name) {
  char buf[512], *p;  // 用于构建完整路径的缓冲区和指针
  int fd;             // 文件描述符
  struct dirent de;   // 目录项结构，用于存储目录中的文件信息
  struct stat st;     // 文件状态结构，用于存储文件的元数据

  // 尝试打开指定路径的文件或目录
  if ((fd = open(path, 0)) < 0) {
    fprintf(2, "ls: cannot open %s\n", path);
    return;
  }

  // 获取文件或目录的状态信息
  if (fstat(fd, &st) < 0) {
    fprintf(2, "ls: cannot stat %s\n", path);
    close(fd);
    return;
  }

  // 根据文件类型进行处理
  switch (st.type) {
    // 如果是普通文件，直接匹配
    case T_FILE:
      if (strmat(fmtname(path), name) != -1)
        printf("%s\n", path);
      break;

    // 如果是目录，遍历目录中的所有文件
    case T_DIR:
      // 检查路径长度是否超过缓冲区大小
      if (strlen(path) + 1 + DIRSIZ + 1 > sizeof buf) {
        printf("find: path too long\n");
        break;
      }
      // 构建目录路径的完整形式
      strcpy(buf, path);
      p = buf + strlen(buf);
      *p++ = '/';  // 添加路径分隔符
      // 读取目录中的每个条目
      while (read(fd, &de, sizeof(de)) == sizeof(de)) {
        if (de.inum == 0) continue;  // 跳过空条目
        // 将文件名复制到路径缓冲区
        memmove(p, de.name, DIRSIZ);
        p[DIRSIZ] = 0;
        // 获取文件状态
        int sub_fd = open(buf, 0);
        if (sub_fd < 0) continue;
        struct stat sub_st;
        int r = fstat(sub_fd, &sub_st);
        close(sub_fd);
        if (r < 0) continue;
        if (strmat(p, name) != -1) printf("%s\n", buf);
        if (sub_st.type == T_DIR && strcmp(p, ".") != 0 && strcmp(p, "..") != 0) find(buf, name);
      }
      break;
  }
  close(fd);  // 关闭文件描述符
}

int main(int argc, char *argv[]) {

  if (argc != 3) {
    printf("Find needs two argument!\n");  // 检查参数数量是否正确
    exit(-1);
  }
  find(argv[1], argv[2]);
  exit(0);
}