unsigned int _cfile_logging_level = 0;

void cfile_set_logging_level(unsigned int level)
{
    _cfile_logging_level = level;
}

unsigned int
cfile_get_logging_level() { return _cfile_logging_level; }
