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
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include "dcbuffer.h"
#include "cfile.h"
#include "bit-functions.h"
#include "defs.h"

extern unsigned int verbosity;

unsigned long inline current_command_type(CommandBuffer *buff) {
        return ((*buff->cb_tail >> buff->cb_tail_bit) & 0x01);
}

void DCBufferTruncate(CommandBuffer *buffer, unsigned long len)
{
    //get the tail to an actual node.
    DCBufferDecr(buffer);
    //v2printf("truncation: \n");
    while(len) {
		/* should that be less then or equal? */
		if (buffer->lb_tail->len <= len) {
		    len -= buffer->lb_tail->len;
//		    v2printf("    whole removal of type(%u), offset(%lu), len(%lu)\n",
//			(*buffer->cb_tail & (1 << buffer->cb_tail_bit)) >> buffer->cb_tail_bit, buffer->lb_tail->offset,
//			buffer->lb_tail->len);
		    DCBufferDecr(buffer);
		    buffer->buffer_count--;
		} else {
//		    v2printf("    partial adjust of type(%u), offset(%lu), len(%lu) is now len(%lu)\n",
//			(*buffer->cb_tail & (1 << buffer->cb_tail_bit))>buffer->cb_tail_bit, buffer->lb_tail->offset,
//			buffer->lb_tail->len, buffer->lb_tail->len - len);
		    buffer->lb_tail->len -= len;
		    len=0;
		}
    }
    DCBufferIncr(buffer);
}


void DCBufferIncr(CommandBuffer *buffer)
{
    buffer->lb_tail = (buffer->lb_end==buffer->lb_tail) ?
	 buffer->lb_start : buffer->lb_tail + 1;
    if (buffer->cb_tail_bit >= 7) {
	buffer->cb_tail_bit = 0;
	buffer->cb_tail = (buffer->cb_tail == buffer->cb_end) ? buffer->cb_start : buffer->cb_tail + 1;
    } else {
	buffer->cb_tail_bit++;
    }
}

void DCBufferDecr(CommandBuffer *buffer)
{
    buffer->lb_tail--;
    if (buffer->cb_tail_bit != 0) {
	buffer->cb_tail_bit--;
    } else {
	buffer->cb_tail = (buffer->cb_tail == buffer->cb_start) ? buffer->cb_end : buffer->cb_tail - 1;
	buffer->cb_tail_bit=7;
    }
}

void DCBufferAddCmd(CommandBuffer *buffer, int type, unsigned long offset, unsigned long len)
{
    if(buffer->lb_tail == buffer->lb_end) {
	v1printf("resizing command buffer from %lu to %lu\n", 
	    buffer->buffer_size, buffer->buffer_size * 2);
	if((buffer->cb_start = (char *)realloc(buffer->cb_start,
	    buffer->buffer_size /4 ))==NULL) {
	    v0printf("resizing command buffer failed, exiting\n");
	    exit(EXIT_FAILURE);
	} else if((buffer->lb_start = (DCLoc *)realloc(buffer->lb_start, 
	    buffer->buffer_size * 2 * sizeof(DCLoc)) )==NULL) {
	    v0printf("resizing command buffer failed, exiting\n");
	    exit(EXIT_FAILURE);
	}
	buffer->buffer_size *= 2;
	buffer->cb_head = buffer->cb_start;
	buffer->lb_head = buffer->lb_start;
	buffer->lb_tail = buffer->lb_start + buffer->buffer_count;
	buffer->cb_tail = buffer->cb_start + (buffer->buffer_count/8);
	buffer->lb_end = buffer->lb_start + buffer->buffer_size -1;
	buffer->cb_end = buffer->cb_start + (buffer->buffer_size/8) -1;
    }
    buffer->lb_tail->offset = offset;
    buffer->lb_tail->len = len;
    if (type==DC_ADD)
		*buffer->cb_tail &= ~(1 << buffer->cb_tail_bit);
    else
		*buffer->cb_tail |= (1 << buffer->cb_tail_bit);
    buffer->buffer_count++;
    DCBufferIncr(buffer);
}

void DCBufferCollapseAdds(CommandBuffer *buffer)
{
	unsigned long count, *plen;
	unsigned int continued_add;
	count = buffer->buffer_count;
	buffer->lb_tail = buffer->lb_start;
	buffer->cb_tail = buffer->cb_head;
	buffer->cb_tail_bit = buffer->cb_head_bit;
	continued_add=0;
	plen = NULL;
	while(count--) {
	    if((*buffer->cb_tail & (1 << buffer->cb_tail_bit))==DC_ADD) {
		if(continued_add) {
		    *plen += buffer->lb_tail->len;
		    buffer->lb_tail->len = 0;
		} else {
		    continued_add = 1;
		    plen = &buffer->lb_tail->len;
		}
	    } else {
		continued_add=0;
	    }
	    DCBufferIncr(buffer);
	}
}

unsigned long 
DCBufferReset(CommandBuffer *buffer)
{
    buffer->lb_tail = buffer->lb_start;
    buffer->cb_tail = buffer->cb_head;
    buffer->cb_tail_bit = buffer->cb_head_bit;
    return buffer->buffer_count;
}

void DCBufferFree(CommandBuffer *buffer)
{
    if(buffer->flags & ADD_CFH_FREE_FLAG)
	free(buffer->add_cfh);
    free(buffer->cb_start);
    free(buffer->lb_start);
}

void DCBufferInit(CommandBuffer *buffer, unsigned long buffer_size, 
    unsigned long src_size, unsigned long ver_size)
{
    buffer->buffer_count=0;
    buffer->flags =0;
    buffer->src_size = src_size;
    buffer->ver_size = ver_size;
    buffer_size = (buffer_size > 0 ? (buffer_size/8) : 0) + 1;
    buffer->buffer_size = buffer_size * 8;
    /* non-intuitive, but note I'm using *buffer_size* rather then 
	buffer->buffer_size.  it makes one hell of a difference. */
    if((buffer->cb_start = (unsigned char *)malloc(buffer_size))==NULL){
	perror("shite, malloc failed\n");
	exit(EXIT_FAILURE);
    }
    buffer->cb_head = buffer->cb_tail = buffer->cb_start;
    buffer->cb_end = buffer->cb_start + buffer_size - 1;
    buffer->cb_head_bit = buffer->cb_tail_bit = 0;
    if((buffer->lb_start = (DCLoc *)malloc(sizeof(DCLoc) * 
	buffer->buffer_size))==NULL){
	perror("shite, malloc failed\n");
	exit(EXIT_FAILURE);
    }
    buffer->lb_head = buffer->lb_tail = buffer->lb_start;
    buffer->lb_end = buffer->lb_start + buffer->buffer_size - 1;
}

