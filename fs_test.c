#include <stdio.h>
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

int main()
{
	//mknod("/tmp/fuse/neo333",0,0);
	//mknod("/tmp/fuse/neo",0,0);
	int fd;
	char *f;
	f = (char *)malloc(33333333);
	memset(f,'3',33333333);
	fd = open("/tmp/fuse/neo33/wao3",O_RDWR);
	write(fd,f,33333333);
	return 0;
}
