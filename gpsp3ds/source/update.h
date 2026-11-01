#pragma once

enum { UPD_OFF, UPD_CHECKING, UPD_OFFLINE, UPD_FAILED, UPD_CURRENT, UPD_NEWER };

void        update_check_start(const char *current_version);   /* background thread */
void        update_check_stop(void);                           /* at exit */
int         update_state(void);                                /* UPD_* */
const char *update_latest(void);                               /* "0.1.7" when UPD_NEWER / UPD_CURRENT */
