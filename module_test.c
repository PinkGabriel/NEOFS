#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <string.h>
#include <errno.h>
#include "neo_fs.h"
#include "atomic_ops.h"

#define DEBUG

/*global variable*/
FILE *fp = NULL;
struct neo_super_block neo_sb_info;
struct neo_group_desc *neo_gdt;

void init ()
{
	int groupcnt;
	if ((fp = fopen(DISKIMG,"rb+")) == NULL){
		printf("image file open failed\n");
		exit(1);
	}
	fseek(fp,1024,SEEK_SET);
	fread(&neo_sb_info,sizeof(struct neo_super_block),1,fp);
	groupcnt = neo_sb_info.s_blocks_count / neo_sb_info.s_blocks_per_group;
	if (neo_sb_info.s_blocks_count % neo_sb_info.s_blocks_per_group)
		groupcnt ++;
	fseek(fp,4096,SEEK_SET);
	neo_gdt = (struct neo_group_desc *)malloc(sizeof(struct neo_group_desc) * groupcnt);
	fread(neo_gdt,sizeof(struct neo_group_desc),groupcnt,fp);
#ifdef DEBUG
	print_sb(neo_sb_info);
	print_gdt(neo_gdt,groupcnt);
#endif
}


int main()
{
	init();
	printf("this is a module test program\n\n");
	//printf("/abc/asds/asdfds/asdfsd/ghg\n\n");
	//path_resolve("/abc/asds/asdfds/asdfsd/ghg");
	//inode_to_addr(1);
	search_dentry(1,"wao");
	return 0;
}





















