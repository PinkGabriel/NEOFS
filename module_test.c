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

void make_a_dirent_example()
{
	struct neo_inode root;
	struct neo_dir_entry tmp;
	fseek(fp,ROOT_ADDR,SEEK_SET);
	fread(&root,sizeof(struct neo_inode),1,fp);
	root.i_blocks = 1;
	root.i_block[0] = 261;
	fwrite(&root,sizeof(struct neo_inode),1,fp);
	fseek(fp,block_to_addr(root.i_block[0]),SEEK_SET);
	tmp.inode = 2;
	tmp.rec_len = 333;
	tmp.name_len = 0;
	tmp.file_type = 1;
	tmp.char[] = "blank111";
	fwrite(&tmp,16,1,fp);
	fseek(fp,325,SEEK_CUR);
	tmp.inode = 2;
	tmp.rec_len = 333;
	tmp.name_len = 0;
	tmp.file_type = 1;
	tmp.char[] = "blank111";
	fwrite(&tmp,16,1,fp);
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





















