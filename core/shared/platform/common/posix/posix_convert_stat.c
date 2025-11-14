#include "platform_api_extension.h"

// Converts a POSIX timespec to a WASI timestamp.
__wasi_timestamp_t
static posix_convert_timespec(const struct timespec *ts)
{
    if (ts->tv_sec < 0)
        return 0;
    if ((__wasi_timestamp_t)ts->tv_sec >= UINT64_MAX / 1000000000)
        return UINT64_MAX;
    return (__wasi_timestamp_t)ts->tv_sec * 1000000000
           + (__wasi_timestamp_t)ts->tv_nsec;
}

// Converts a POSIX stat structure to a WASI filestat structure
void
posix_convert_stat(os_file_handle handle, const struct stat *in,
             __wasi_filestat_t *out)
{
    out->st_dev = in->st_dev;
    out->st_ino = in->st_ino;
    out->st_nlink = (__wasi_linkcount_t)in->st_nlink;
    out->st_size = (__wasi_filesize_t)in->st_size;
#ifdef __APPLE__
    out->st_atim = posix_convert_timespec(&in->st_atimespec);
    out->st_mtim = posix_convert_timespec(&in->st_mtimespec);
    out->st_ctim = posix_convert_timespec(&in->st_ctimespec);
#else
    out->st_atim = posix_convert_timespec(&in->st_atim);
    out->st_mtim = posix_convert_timespec(&in->st_mtim);
    out->st_ctim = posix_convert_timespec(&in->st_ctim);
#endif

    // Convert the file type. In the case of sockets there is no way we
    // can easily determine the exact socket type.
    if (S_ISBLK(in->st_mode)) {
        out->st_filetype = __WASI_FILETYPE_BLOCK_DEVICE;
    }
    else if (S_ISCHR(in->st_mode)) {
        out->st_filetype = __WASI_FILETYPE_CHARACTER_DEVICE;
    }
    else if (S_ISDIR(in->st_mode)) {
        out->st_filetype = __WASI_FILETYPE_DIRECTORY;
    }
    else if (S_ISFIFO(in->st_mode)) {
        out->st_filetype = __WASI_FILETYPE_SOCKET_STREAM;
    }
    else if (S_ISLNK(in->st_mode)) {
        out->st_filetype = __WASI_FILETYPE_SYMBOLIC_LINK;
    }
    else if (S_ISREG(in->st_mode)) {
        out->st_filetype = __WASI_FILETYPE_REGULAR_FILE;
    }
    else if (S_ISSOCK(in->st_mode)) {
        int socktype;
        socklen_t socktypelen = sizeof(socktype);

        if (getsockopt(handle, SOL_SOCKET, SO_TYPE, &socktype, &socktypelen)
            < 0) {
            out->st_filetype = __WASI_FILETYPE_UNKNOWN;
            return;
        }

        switch (socktype) {
            case SOCK_DGRAM:
                out->st_filetype = __WASI_FILETYPE_SOCKET_DGRAM;
                break;
            case SOCK_STREAM:
                out->st_filetype = __WASI_FILETYPE_SOCKET_STREAM;
                break;
            default:
                out->st_filetype = __WASI_FILETYPE_UNKNOWN;
                return;
        }
    }
    else {
        out->st_filetype = __WASI_FILETYPE_UNKNOWN;
    }
}
