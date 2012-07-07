#include <assert.h>
#include <wchar.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <omphalos/diag.h>
#include <omphalos/popen.h>

static char *
sanitize_cmd(const char *cmd){
	char *tmp,*san = NULL;
	size_t left,len = 0;
	mbstate_t ps;
	size_t conv;

	memset(&ps,0,sizeof(ps));
	left = strlen(cmd);
	do{
		unsigned escape;
		wchar_t w;

		if((conv = mbrtowc(&w,cmd,left,&ps)) == (size_t)-1){
			diagnostic("Error converting multibyte: %s",cmd);
			free(san);
		}
		left -= conv;
		if(w == L'(' || w == L')'){
			escape = 1;
		}else if(w == '$'){
			escape = 1;
		}else{
			escape = 0;
		}
		if((tmp = realloc(san,sizeof(*san) * (len + conv + escape))) == NULL){
			free(san);
			return NULL;
		}
		san = tmp;
		if(escape){
			san[len] = '\\';
			++len;
		}
		memcpy(san + len,cmd,conv);
		len += conv;
		cmd += conv;
	}while(conv);
	if((tmp = realloc(san,sizeof(*san) * (len + 1))) == NULL){
		free(san);
		return NULL;
	}
	san = tmp;
	san[len] = '\0';
	return san;
}

int popen_drain(const char *cmd){
	char buf[BUFSIZ],*safecmd;
	FILE *fd;

	if((safecmd = sanitize_cmd(cmd)) == NULL){
		return -1;
	}
	if((fd = popen(safecmd,"re")) == NULL){
		diagnostic("Couldn't run %s (%s?)",safecmd,strerror(errno));
		free(safecmd);
		return -1;
	}
	while(fgets(buf,sizeof(buf),fd)){
		diagnostic("%s",buf);
	}
	if(!feof(fd)){
		diagnostic("Error reading from '%s' (%s?)",cmd,strerror(errno));
		fclose(fd);
		return -1;
	}
	if(fclose(fd)){
		diagnostic("Error running '%s'",cmd);
		return -1;
	}
	return 0;
}

char *spopen_drain(const char *cmd){
	char *buf = NULL,*tmp,*safecmd;
	size_t s = BUFSIZ,o = 0;
	FILE *fd;

	if((safecmd = sanitize_cmd(cmd)) == NULL){
		return NULL;
	}
	if((fd = popen(safecmd,"re")) == NULL){
		diagnostic("Couldn't run %s (%s?)",safecmd,strerror(errno));
		free(safecmd);
		return NULL;
	}
	while( (tmp = realloc(buf,s)) ){
		buf = tmp;
		while(fgets(buf + o,s - o,fd)){
			o += strlen(buf + o);
			if(o >= s){
				break;
			}
		}
		if(!feof(fd)){
		       if(o < s){
				diagnostic("Error reading from '%s' (%s?)",cmd,strerror(errno));
				fclose(fd);
				return NULL;
		       }
		}else if(fclose(fd)){
			diagnostic("Error running '%s'",cmd);
			return NULL;
		}else{
			return buf;
		}
		s += BUFSIZ;
	}
	free(buf);
	fprintf(stderr,"Error allocating %zu\n",s);
	return NULL;
}

int vpopen_drain(const char *fmt,...){
	char buf[BUFSIZ];
	va_list va;

	va_start(va,fmt);
	if(vsnprintf(buf,sizeof(buf),fmt,va) >= (int)sizeof(buf)){
		va_end(va);
		diagnostic("Bad command: %s ...",fmt);
		return -1;
	}
	va_end(va);
	return popen_drain(buf);
}

char *vspopen_drain(const char *fmt,...){
	char buf[BUFSIZ];
	va_list va;

	va_start(va,fmt);
	if(vsnprintf(buf,sizeof(buf),fmt,va) >= (int)sizeof(buf)){
		va_end(va);
		diagnostic("Bad command: %s ...",fmt);
		return NULL;
	}
	va_end(va);
	return spopen_drain(buf);
}
