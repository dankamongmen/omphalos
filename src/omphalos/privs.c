#include <pwd.h>
#include <errno.h>
#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <sys/prctl.h>
#include <sys/capability.h>

static int
prepare_caps(const cap_value_t *caparray,unsigned n){
	cap_value_t ourarray[] = { CAP_SETUID, };
	cap_t cap;

	if((cap = cap_get_proc()) == NULL){
		fprintf(stderr,"Couldn't acquire capabilities (%s?)\n",strerror(errno));
		return -1;
	}
	if(cap_clear(cap)){
		fprintf(stderr,"Couldn't reset capabilities (%s?)\n",strerror(errno));
		cap_free(cap);
		return -1;
	}
	if(cap_set_flag(cap,CAP_EFFECTIVE,sizeof(ourarray) / sizeof(*ourarray),ourarray,CAP_SET)){
		fprintf(stderr,"Couldn't prep e-capabilities (%s?)\n",strerror(errno));
		cap_free(cap);
		return -1;
	}
	if(cap_set_flag(cap,CAP_PERMITTED,sizeof(ourarray) / sizeof(*ourarray),ourarray,CAP_SET)){
		fprintf(stderr,"Couldn't prep p-capabilities (%s?)\n",strerror(errno));
		cap_free(cap);
		return -1;
	}
	if(n){
		ourarray[0] = CAP_SETPCAP;

		if(cap_set_flag(cap,CAP_EFFECTIVE,1,ourarray,CAP_SET)){
			fprintf(stderr,"Couldn't prep e-capabilities (%s?)\n",strerror(errno));
			cap_free(cap);
			return -1;
		}
		if(cap_set_flag(cap,CAP_PERMITTED,1,ourarray,CAP_SET)){
			fprintf(stderr,"Couldn't prep p-capabilities (%s?)\n",strerror(errno));
			cap_free(cap);
			return -1;
		}
		// older cap_set_flag() is missing the const on its third argument :/
		if(cap_set_flag(cap,CAP_EFFECTIVE,n,(cap_value_t *)caparray,CAP_SET)){
			fprintf(stderr,"Couldn't prep e-capabilities (%s?)\n",strerror(errno));
			cap_free(cap);
			return -1;
		}
		if(cap_set_flag(cap,CAP_PERMITTED,n,(cap_value_t *)caparray,CAP_SET)){
			fprintf(stderr,"Couldn't prep p-capabilities (%s?)\n",strerror(errno));
			cap_free(cap);
			return -1;
		}
	}
	if(cap_set_proc(cap)){
		fprintf(stderr,"Couldn't preset process capabilities (%s?)\n",strerror(errno));
		cap_free(cap);
		return -1;
	}
	cap_free(cap);
	return 0;
}

static int
drop_privs(const char *name,const cap_value_t *caparray,unsigned n){
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
	// Clear all capabilities save those we need, or else we'll keep them
	// across the setuid() due to prctl(PR_SET_KEEPCAPS)
	if(prepare_caps(caparray,n)){
		return -1;
	}
	// Change our user ID
	if(setuid(pw->pw_uid)){
		fprintf(stderr,"Couldn't setuid to %u (%s?)\n",pw->pw_uid,strerror(errno));
		return -1;
	}
	if(n){
		if((cap = cap_get_proc()) == NULL){
			fprintf(stderr,"Couldn't get process capabilities (%s?)\n",strerror(errno));
			return -1;
		}
		// older cap_set_flag() is missing the const on its third argument :/
		if(cap_set_flag(cap,CAP_EFFECTIVE,n,(cap_value_t *)caparray,CAP_SET)){
			fprintf(stderr,"Couldn't set up capabilities (%s?)\n",strerror(errno));
			cap_free(cap);
			return -1;
		}
		if(cap_set_proc(cap)){
			fprintf(stderr,"Couldn't set process capabilities (%s?)\n",strerror(errno));
			cap_free(cap);
			return -1;
		}
		cap_free(cap);
	}
	return 0;
}

int handle_priv_drop(const char *name,const cap_value_t *caparray,unsigned n){
	cap_flag_value_t val;
	cap_t cap;

	if(!name || strlen(name) == 0){ // NULL, empty string disables permdrop
		return 0;
	}
	if((cap = cap_get_proc()) == NULL){
		fprintf(stderr,"Couldn't get process capabilities (%s?)\n",strerror(errno));
		return -1;
	}
	// we could just rely on setuid() failing, but setpcap() fails
	// that due to absence of CAP_SETPCAP, which confuses the user
	if(cap_get_flag(cap,CAP_SETUID,CAP_EFFECTIVE,&val) || val != CAP_SET){
		fprintf(stderr,"Don't have CAP_SETUID; won't change UID (try -u '')!\n");
		cap_free(cap);
		return -1;
	}
	if(cap_get_flag(cap,CAP_SETPCAP,CAP_EFFECTIVE,&val) || val != CAP_SET){
		fprintf(stderr,"Don't have CAP_SETPCAP; won't change UID (try -u '')!\n");
		cap_free(cap);
		return -1;
	}
	cap_free(cap);
	if(drop_privs(name,caparray,n)){
		return -1;
	}
	return 0;
}

