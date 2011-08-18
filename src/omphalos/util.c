#include <stdio.h>
#include <omphalos/util.h>

char *fgetl(char **buf,int *s,FILE *fp){
	int r = 0;

	do{
		if(*s - r < 2){
			char *tmp;

			if((tmp = realloc(*buf,*s + BUFSIZ)) == NULL){
				return NULL;
			}
			*buf = tmp;
			*s += BUFSIZ;
		}
		if(fgets(*buf + r,*s - r,fp) == NULL){
			if(!ferror(fp) && r){
				return *buf;
			}
			return NULL;
		}
		if(strchr(*buf + r,'\n')){
			return *buf;
		}
	}while(r += strlen(*buf + r));
	return NULL;
}
