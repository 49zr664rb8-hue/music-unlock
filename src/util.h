#ifndef MU_UTIL_H
#define MU_UTIL_H

#include <stddef.h>
#include <wchar.h>

const wchar_t *mu_path_filename(const wchar_t *path);
void mu_path_ext_lower(const wchar_t *path, wchar_t *ext, size_t cch);
void mu_stem(const wchar_t *path, wchar_t *stem, size_t cch);
int mu_file_exists(const wchar_t *path);
int mu_mkdir_recursive(const wchar_t *dir);
int mu_read_head(const wchar_t *path, unsigned char *buf, size_t n, size_t *got);
int mu_b64_decode(const unsigned char *in, size_t inlen, unsigned char **out, size_t *outlen);
void mu_join_path(wchar_t *dst, size_t cch, const wchar_t *dir, const wchar_t *name);
unsigned long long mu_file_size(const wchar_t *path);
void mu_human_size(unsigned long long bytes, wchar_t *buf, size_t cch);

#endif
