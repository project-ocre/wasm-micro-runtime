#include "platform_api_extension.h"

// Converts a POSIX stat structure to a WASI filestat structure
void
posix_convert_stat(os_file_handle handle, const struct stat *in,
             __wasi_filestat_t *out);
