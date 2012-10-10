#include <stdint.h>

/* Contains the data that is handed to the user */
struct profile_data {
    uint64_t cycles;
    uint64_t hits;
};

int libprofile_enable(void);
int libprofile_disable(void);
int libprofile_register(void *f);
int libprofile_unregister(void *f);
int libprofile_get_profiling(void *f, struct profile_data *pdata);
