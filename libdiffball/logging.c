unsigned int _diffball_logging_level = 0;

void diffball_set_logging_level(unsigned int level)
{
    _diffball_logging_level = level;
}

unsigned int
diffball_get_logging_level() { return _diffball_logging_level; }
