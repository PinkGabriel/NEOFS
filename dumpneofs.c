#include<stdio.h>
#include<stdlib.h>
#include<string.h>
#include"neo_fs.h"
#include<time.h>
#include<math.h>

int main(int argc,char *argv[])
{
	FILE *fp;
	long length;
	if ((fp = fopen(argv[1],"rb")) == NULL){
		printf("image file not exist\n");
		return -1;
	}
	fseek(fp,0,SEEK_END);
	length = ftell(fp);
	printf("filesystem total length: %d\n",length);
}
