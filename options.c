/*
  Copyright (C) 2003 Brian Harring

  This program is free software; you can redistribute it and/or
  modify it under the terms of the GNU General Public License
  as published by the Free Software Foundation; either version 2
  of the License, or (at your option) any later version.

  This program is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with this program; if not, write to the Free Software
  Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, US 
*/
#include "defs.h"
#include <stdlib.h>
#include <popt.h>

void 
usage(poptContext p_opt, int exitcode, const char *error, const char *addl)
{    
    poptPrintUsage(p_opt, stderr, 0);
    if(error) {
	if(addl)
	    fprintf(stderr, "%s: %s\n", error,addl);
	else
	    fprintf(stderr, "%s\n", error);
    }
    exit(exitcode);
}

void
print_version(const char *prog)
{
    v0printf("%s version %s\n", prog, VERSION);
    exit(0);
}
