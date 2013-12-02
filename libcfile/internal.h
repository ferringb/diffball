#include "defs.h"
#include "cfile.h"

int internal_copen_no_comp(cfile *cfh);
int internal_copen_gzip(cfile *cfh);
int internal_copen_bzip2(cfile *cfh);
int internal_copen_xz(cfile *cfh);

