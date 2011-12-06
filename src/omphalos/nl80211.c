#include <errno.h>
#include <string.h>
#include <assert.h>
#include <pthread.h>
#include <sys/socket.h>
#include <omphalos/diag.h>
#include <netlink/socket.h>
#include <netlink/netlink.h>
#include <linux/rtnetlink.h>
#include <omphalos/nl80211.h>
#include <netlink/genl/genl.h>
#include <netlink/genl/ctrl.h>

static struct nl_sock *nl;
static struct nl_cache *nlc;
static struct genl_family *nl80211;
static pthread_mutex_t nllock = PTHREAD_MUTEX_INITIALIZER;

static int
nl80211_cmd(void){
	struct nl_msg *msg;
	struct nl_cb *cb;

	if((msg = nlmsg_alloc()) == NULL){
		diagnostic("Couldn't allocate netlink msg (%s?)",strerror(errno));
		return -1;
	}
	if((cb = nl_cb_alloc(NL_CB_VERBOSE)) == NULL){
		diagnostic("Couldn't allocate netlink cb (%s?)",strerror(errno));
		goto err;
	}
	nlmsg_free(msg);
	return 0;

err:
	if(cb){
		nl_cb_put(cb);
	}
	nlmsg_free(msg);
	return -1;
}

int open_nl80211(void){
	assert(pthread_mutex_lock(&nllock) == 0);
	if(nl){ // already initialized
		pthread_mutex_unlock(&nllock);
		return 0;
	}
	if((nl = nl_socket_alloc()) == NULL){
		diagnostic("Couldn't allocate generic netlink (%s?)",strerror(errno));
		goto err;
	}
	if(genl_connect(nl)){
		diagnostic("Couldn't connect generic netlink (%s?)",strerror(errno));
		goto err;
	}
	if(genl_ctrl_alloc_cache(nl,&nlc)){
		diagnostic("Couldn't allocate netlink cache (%s?)",strerror(errno));
		goto err;
	}
	if((nl80211 = genl_ctrl_search_by_name(nlc,"nl80211")) == NULL){
		diagnostic("Couldn't find nl80211 (%s?)",strerror(errno));
		goto err;
	}
	assert(nl80211_cmd() == 0); // FIXME, just exploratory
	assert(pthread_mutex_unlock(&nllock) == 0);
	return 0;

err:
	if(nlc){
		nl_cache_free(nlc);
		nlc = NULL;
	}
	if(nl){
		nl_socket_free(nl);
		nl = NULL;
	}
	assert(pthread_mutex_unlock(&nllock) == 0);
	return -1;
}

int close_nl80211(void){
	assert(pthread_mutex_lock(&nllock) == 0);
	if(!nl){ // never constructed, or already destroyed
		assert(pthread_mutex_unlock(&nllock) == 0);
		return 0;
	}
	nl_cache_free(nlc);
	nl_socket_free(nl);
	assert(pthread_mutex_unlock(&nllock) == 0);
	return 0;
}
