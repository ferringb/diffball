#include <stdlib.h>
#include <assert.h>
#include <string.h>
#include <cfile.h>


int
main(int argc, char **argv)
{
	cfile mcfh, cfh;
	assert(copen_multifile_directory(&mcfh, "/home/ferringb/ecessa/releases/8.4.25") == 0);
	assert(copen(&cfh, "/home/ferringb/ecessa/releases/8.4.25.full", NO_COMPRESSOR, CFILE_RONLY) == 0);
	assert(cfile_len(&mcfh) == cfile_len(&cfh));
	printf("mcfh=%lu, cfh=%lu\n", cfile_len(&mcfh), cfile_len(&cfh));

	char *m_buf = malloc(cfile_len(&mcfh));
	char *c_buf = malloc(cfile_len(&mcfh));
	assert(m_buf && c_buf);

	memset(m_buf, 0, cfile_len(&mcfh));
	memset(c_buf, 0, cfile_len(&mcfh));

	assert(cfile_len(&cfh) == cread(&cfh, c_buf, cfile_len(&cfh)));
	assert(cfile_len(&mcfh) == cread(&mcfh, m_buf, cfile_len(&mcfh)));
	char *mp = m_buf;
	char *cp = c_buf;
	size_t len = cfile_len(&cfh);
	while(len) {
		if (*mp != *cp) {
			printf("Failed at %lu; %i != %i\n", cfile_len(&cfh) - len, *mp, *cp);
			assert(*mp == *cp);
		}
		len--;
		mp++;
		cp++;
	}

	cfile_window *m_cfw;
	cfile_window *c_cfw;
	assert(0 == cseek(&mcfh, 0, CSEEK_FSTART));
	assert(0 == cseek(&cfh, 0, CSEEK_FSTART));
	m_cfw = expose_page(&mcfh);
	c_cfw = expose_page(&cfh);
	while(len) {
		if(m_cfw->pos == m_cfw->end) {
			m_cfw = next_page(&mcfh);
			assert(m_cfw != NULL);
		}
		if (c_cfw->pos == c_cfw->end) {
			c_cfw = next_page(&cfh);
			assert(c_cfw != NULL);
		}
		if (c_cfw->buff[c_cfw->pos] != m_cfw->buff[m_cfw->pos]) {
			printf("failed at %lu; %i != %i\n", c_cfw->offset + c_cfw->pos, c_cfw->buff[c_cfw->pos], m_cfw->buff[m_cfw->pos]);
			abort();
		}
		m_cfw->pos++;
		c_cfw->pos++;
		len--;
	}

	printf("completed\n");	
	return 0;
}
