/*

    Implementation of POSIX directory browsing functions and types for Win32.

    Author:  Kevlin Henney (kevlin@acm.org, kevlin@curbralan.com)
    History: Created March 1997. Updated June 2003 and July 2012.

    Copyright Kevlin Henney, 1997, 2003, 2012. All rights reserved.

    Permission to use, copy, modify, and distribute this software and its
    documentation for any purpose is hereby granted without fee, provided
    that this copyright and permissions notice appear in all copies and
    derivatives.

    This software is supplied "as is" without express or implied warranty.

    But that said, if there are any problems please get in touch.


*/
#pragma warning(disable : 4996)
#include "_dirent.h"
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include "utility.h"

#ifdef __cplusplus
extern "C" {
#endif

DIR *opendir(const char *name) {
    DIR *dir = NULL;

    if (name && name[0]) {
        size_t base_length = strlen(name);
        const char *all    = /* search pattern must end with suitable wildcard */
            strchr("/\\", name[base_length - 1]) ? "*" : "/*";
        dir       = safeMalloc(sizeof *dir);
        dir->name = safeMalloc(base_length + strlen(all) + 1);
        strcat(strcpy(dir->name, name), all);

        if ((dir->handle = (handle_type)_findfirst(dir->name, &dir->info)) != -1) {
            dir->result.d_name = 0;
        } else { /* rollback */
            free(dir->name);
            free(dir);
            dir = NULL;
        }
    } else {
        errno = EINVAL;
    }

    return dir;
}

int closedir(DIR *dir) {
    int result = -1;

    if (dir) {
        if (dir->handle != -1) {
            result = _findclose(dir->handle);
        }

        free(dir->name);
        free(dir);
    }

    if (result == -1) /* map all errors to EBADF */
    {
        errno = EBADF;
    }

    return result;
}

struct dirent *readdir(DIR *dir) {
    struct dirent *result = 0;

    if (dir && dir->handle != -1) {
        if (!dir->result.d_name || _findnext(dir->handle, &dir->info) != -1) {
            result         = &dir->result;
            result->d_name = dir->info.name;
        }
    } else {
        errno = EBADF;
    }

    return result;
}

void rewinddir(DIR *dir) {
    if (dir && dir->handle != -1) {
        _findclose(dir->handle);
        dir->handle        = (handle_type)_findfirst(dir->name, &dir->info);
        dir->result.d_name = 0;
    } else {
        errno = EBADF;
    }
}

#ifdef __cplusplus
}
#endif
