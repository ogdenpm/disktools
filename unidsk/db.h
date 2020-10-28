#include <stdbool.h>


#define KEYSIZE    27      // base 64 SHA1 hash length
typedef char *KeyPtr, Key[KEYSIZE + 1];

void closeDb();
KeyPtr firstKey();
char *firstLoc(KeyPtr key);


int getFileKey(const char *fname, KeyPtr key);
const char *getDbLoc(const char *loc);
const KeyPtr getDbLocKey(const char *loc);
bool isDir(const char *fname);
bool isFile(const char *fname);
bool isValidKey(const KeyPtr key);
bool isValidPair(const KeyPtr key, const char *loc);
KeyPtr nextKey();
char *nextLoc();
#ifdef _REBUILD
void openDb(bool rebuild);
#else
void openDb();
#endif