#ifndef PATH_UTILS_H
#define PATH_UTILS_H

#include "config.h"

// 根据用户输入构建目标路径（可能不存在）
int ftp_build_path(const ftp_session *session, const char *param, char *out, size_t out_sz);

// 解析并验证已存在的文件路径，写入 out
int ftp_resolve_existing(const ftp_session *session, const char *param, char *out, size_t out_sz);

// 解析并验证已存在的目录路径
int ftp_resolve_existing_directory(const ftp_session *session, const char *param, char *out, size_t out_sz);

// 解析创建路径：返回目标及其父目录（用于 MKD/上传新文件）
int ftp_resolve_for_creation(const ftp_session *session, const char *param,
                             char *full_out, size_t full_out_sz,
                             char *parent_out, size_t parent_out_sz);

// 获取当前工作目录的真实绝对路径
int ftp_get_cwd_realpath(const ftp_session *session, char *out, size_t out_sz);

// 将绝对路径转换为相对于根目录的路径
int ftp_make_relative_path(const ftp_session *session, const char *abs_path, char *rel_out, size_t rel_out_sz);

#endif // PATH_UTILS_H