#ifndef _HEADER_CFILE
#define _HEADER_CFILE
#define CFILE_TRANS_BUFF_SIZE 4096 * 8
#define CFILE_RAW_BUFF_SIZE 4096 * 8
 //8 * 4096
#define NO_COMPRESSOR      0
#define GZIP_COMRPESSOR    1
#define BZIP2_COMPRESSOR   2
#define CFILE_RONLY 1
#define CFILE_WONLY 2

/*lseek type stuff
SEEK_SET
	The offset is set to offset bytes.
SEEK_CUR
	The offset is set to its current location plus off-
	set bytes.
SEEK_END
	The offset is set to the size of the file plus off-
	set bytes.*/
#define CSEEK_SET 0
#define CSEEK_CUR 1
#define CSEEK_END 2

#define MIN(x,y) ((x) < (y) ? (x) : (y))

struct cfile {
	//raw_fh stuff
	int raw_fh;
	unsigned long raw_fh_len;
	unsigned int  compressor_type;
	unsigned int  access_flags;
	unsigned long raw_fh_start;
	unsigned long raw_fh_end;
	//raw buff.
	unsigned char *raw_buff;
	unsigned int  raw_size;
	unsigned char *raw_filled;
	unsigned char *raw_pos;
	unsigned long raw_fh_pos;
	//translated/uncompressed buff.
	unsigned char *trans_buff;
	unsigned int  trans_size;
	unsigned char *trans_filled;
	unsigned char *trans_pos;
	unsigned long trans_fh_pos;
	/*
	

    int fh;
    unsigned long fh_pos;
    unsigned int compressor_type;
    unsigned char raw_buff[CFILE_RAW_BUFF_SIZE];
    unsigned int raw_buff_size;
    unsigned int raw_buff_filled;
    unsigned int raw_buff_pos;
    unsigned long raw_buff_fh_pos;
    unsigned int access_flags;*/
};

signed int copen(struct cfile *cfile, int fh, unsigned long fh_start,
   unsigned long fh_end, unsigned int compressor_type, unsigned int access_flags);
signed int cclose(struct cfile *cfile);
unsigned long cread(struct cfile *cfile, unsigned char *out_buff, unsigned long len);
unsigned long cwrite(struct cfile *cfile, unsigned char *in_buff, unsigned long len);
inline void crefresh(struct cfile *cfile);
unsigned long cposition(struct cfile *cfile);
unsigned long cseek(struct cfile *cfile, signed long offset, int offset_type);

#endif