#include <string.h>
#include <stdbool.h>
#include "utility.h"
#ifdef _MSC_VER
/// <summary>
/// Equivalent to POSIX basename for visual C
/// </summary>
/// <param name="path">full path to file</param>
/// <returns>pointer filename only</returns>

char const *basename(char const *path) {
    char *s;
    if (path[0] && path[1] == ':') // strip any device
        path += 2;
    while ((s = strpbrk(path, "/\\"))) // allow / or \ directory separators
        path = s + 1;
    return path;
}
#endif


/// <summary>
/// generated a new file name by replacing/adding the ext
/// unless force is false and an ext exists already
/// </summary>
/// <param name="path">base filename including path</param>
/// <param name="ext">new extent including '.' to append</param>
/// <param name="force">if true will always replace/add ext, else only add if no existing ext</param>
/// <returns></returns>
char *makeFilename(char const *path, char const *ext, bool force) {
    char *s       = strrchr(basename(path), '.');
    if (s && !force)    // keep existing ext
        ext = s;
    int prefixLen = (s ? (int)(s - path) : (int)strlen(path));
    char *newFile = safeMalloc(prefixLen + strlen(ext) + 1);
    strncpy(newFile, path, prefixLen);
    strcpy(newFile + prefixLen, ext);
    return newFile;
}