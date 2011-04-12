#include <pwent.h>

int handle_priv_drop(const char *name){
	cap_flag_value_t val;
	cap_t cap;

	if(strlen(name) == 0){ // empty string disables permissions drop
		return 0;
	}
	if((cap = cap_get_pid(getpid())) == NULL){
		return -1;
	}
	if(cap_get_flag(cap,CAP_SETUID,CAP_EFFECTIVE,&val)){
		cap_free(cap);
		return -1;
	}
	cap_free(cap);
	if(val == CAP_SET){
		struct passwd *pw = getpwnam(name);

		if(pw == NULL){
			return -1;
		}
		if(setuid(pw->pw_uid)){
			return -1;
		}
	}
	return 0;
}

