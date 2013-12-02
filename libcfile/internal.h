#include "defs.h"
#include "cfile.h"

int internal_copen_no_comp(cfile *cfh);
int internal_copen_gzip(cfile *cfh);
int internal_copen_bzip2(cfile *cfh);
int internal_copen_xz(cfile *cfh);

inline signed int ensure_lseek_position(cfile *cfh);
inline void flag_lseek_needed(cfile *cfh);
inline void set_last_lseeker(cfile *cfh);

#define LAST_LSEEKER(cfh) (CFH_IS_CHILD(cfh) ?                              \
	*((cfh)->lseek_info.last_ptr) : (cfh)->lseek_info.parent.last)
//#define LAST_LSEEKER(cfh) (CFH_IS_CHILD(cfh) &&   *((cfh)->lseek_info.last_ptr) || (cfh)->lseek_info.parent.last)
    
#define IS_LAST_LSEEKER(cfh) ( (cfh)->cfh_id == LAST_LSEEKER((cfh)) || ((cfh)->state_flags & CFILE_MEM_ALIAS) )

signed int raw_ensure_position(cfile *cfh);
    
