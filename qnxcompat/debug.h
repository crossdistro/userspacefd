#ifdef MSG_DEBUG
	#define debug(msg, ...) fprintf(stderr, "%s: " __FILE__ ":%d: " msg "\n", __FUNCTION__, __LINE__, ##__VA_ARGS__)
#else
	#define debug(msg, ...) ((void) 0)
#endif
