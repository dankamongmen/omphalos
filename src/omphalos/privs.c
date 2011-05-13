#include <pwd.h>
#include <errno.h>
#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <sys/prctl.h>
#include <sys/capability.h>

static int
drop_privs(const char *name){
	const cap_value_t caparray[] = { CAP_NET_ADMIN, CAP_NET_RAW, };
	struct passwd *pw;
	cap_t cap;

	// FIXME probably ought use getpwnam_r()...
	if((pw = getpwnam(name)) == NULL){
		return -1;
	}
	if(prctl(PR_SET_KEEPCAPS,1)){
		fprintf(stderr,"Couldn't set PR_SET_KEEPCAPS (%s?)\n",strerror(errno));
		return -1;
	}
	if(setuid(pw->pw_uid)){
		fprintf(stderr,"Couldn't setuid to %u (%s?)\n",pw->pw_uid,strerror(errno));
		return -1;
	}
	if((cap = cap_get_proc()) == NULL){
		return -1;
	}
	if(cap_set_flag(cap,CAP_EFFECTIVE,sizeof(caparray) / sizeof(*caparray),
				caparray,CAP_SET)){
		cap_free(cap);
		return -1;
	}
	if(cap_set_proc(cap)){
		cap_free(cap);
		return -1;
	}
	cap_free(cap);
	return 0;
}

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
	if(val == CAP_SET){
		if(drop_privs(name)){
			cap_free(cap);
			return -1;
		}
	}
	cap_free(cap);
	return 0;
}

