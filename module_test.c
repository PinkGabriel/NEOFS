#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <string.h>
#include <errno.h>
#include "neo_fs.h"
#include "atomic_ops.h"

//#define DEBUG

/*global variable*/
FILE *fp = NULL;
struct neo_super_block neo_sb_info;
struct neo_group_desc *neo_gdt;
struct block_bitmap_cache bbcache;
struct inode_bitmap_cache ibcache;

void init ()
{
	int groupcnt;
	if ((fp = fopen("/tmp/diskimg","rb+")) == NULL){
		printf("image file open failed\n");
		exit(1);
	}
	fseek(fp,1024,SEEK_SET);
	fread(&neo_sb_info,sizeof(struct neo_super_block),1,fp);
	groupcnt = neo_sb_info.s_groups_count;
	fseek(fp,4096,SEEK_SET);
	neo_gdt = (struct neo_group_desc *)malloc(sizeof(struct neo_group_desc) * groupcnt);
	fread(neo_gdt,sizeof(struct neo_group_desc),groupcnt,fp);

	bbcache.groupnr = -1;
	memset(bbcache.bbitmap,0,BLOCK_SIZE);
	ibcache.groupnr = -1;
	memset(ibcache.ibitmap,0,BLOCK_SIZE);

#ifdef DEBUG
	print_sb(neo_sb_info);
	print_gdt(neo_gdt,groupcnt);
#endif
}

void make_a_dirent_example()
{
	struct neo_inode root;
	struct neo_dir_entry tmp;
	__u64 base;
	fseek(fp,ROOT_ADDR,SEEK_SET);
	fread(&root,sizeof(struct neo_inode),1,fp);
	root.i_blocks = 1;
	root.i_block[0] = 261;
	fseek(fp,ROOT_ADDR,SEEK_SET);
	fwrite(&root,sizeof(struct neo_inode),1,fp);
	base = block_to_addr(root.i_block[0]);
	fseek(fp,base,SEEK_SET);

	//print_inode(root);

	tmp.inode = 0;
	tmp.rec_len = 126;
	tmp.name_len = 8;
	tmp.file_type = 1;
	strcpy(tmp.name,"blank111");
	fwrite(&tmp,16,1,fp);
	fseek(fp,126+base,SEEK_SET);

	tmp.inode = 35;
	tmp.rec_len = 103;
	tmp.name_len = 9;
	tmp.file_type = 2;
	strcpy(tmp.name,"starcraft");
	fwrite(&tmp,20,1,fp);
	fseek(fp,146+base,SEEK_SET);

	tmp.inode = 0;
	tmp.rec_len = 83;
	tmp.name_len = 6;
	tmp.file_type = 1;
	strcpy(tmp.name,"blank2");
	fwrite(&tmp,16,1,fp);
	fseek(fp,229+base,SEEK_SET);

	tmp.inode = 12;
	tmp.rec_len = 24;
	tmp.name_len = 15;
	tmp.file_type = 1;
	strcpy(tmp.name,"worldofwarcraft");
	fwrite(&tmp,24,1,fp);
	fseek(fp,253+base,SEEK_SET);

	tmp.inode = 33;
	tmp.rec_len = 35;
	tmp.name_len = 4;
	tmp.file_type = 1;
	strcpy(tmp.name,"auir");
	fwrite(&tmp,12,1,fp);
	fseek(fp,265+base,SEEK_SET);

	tmp.inode = 0;
	tmp.rec_len = 23;
	tmp.name_len = 7;
	tmp.file_type = 1;
	strcpy(tmp.name,"blank33");
	fwrite(&tmp,16,1,fp);
	fseek(fp,288+base,SEEK_SET);

	tmp.inode = 63;
	tmp.rec_len = 3808;
	tmp.name_len = 27;
	tmp.file_type = 2;
	strcpy(tmp.name,"finalfantasy7adventchildren");
	fwrite(&tmp,36,1,fp);
	fseek(fp,324+base,SEEK_SET);

	tmp.inode = 0;
	tmp.rec_len = 3772;
	tmp.name_len = 8;
	tmp.file_type = 1;
	strcpy(tmp.name,"blank999");
	fwrite(&tmp,16,1,fp);
}

int rmdir()
{
	inode_nr parent_ino;
	inode_nr ino;
	char *path = "/neo3";
	char *parent_path = strdup(path);
	char *name = strrchr(parent_path,'/');
	*name = '\0';
	name ++;					/*此时parent_path是父目录路径，name为目标文件名*/
	if (strlen(name) > 255)				/*文件名太长*/
		return -ENAMETOOLONG;
	if (*parent_path == '\0')
		parent_ino = 1;
	else
		parent_ino = path_resolve(parent_path);
	ino = search_dentry(parent_ino,name);
	if (ino == 0)					/*文件不存在*/
		return -ENOENT;
	if (free_inode(ino) == -1)				/*按照普通文件分配一个inode*/
		return -errno;
	delete_dentry(parent_ino,name,2);
	return 0;
}

void print_i_block(inode_nr ino)
{
	int i;
	__u64 addr;
	struct neo_inode inode;
	addr = inode_to_addr(ino);
	fseek(fp,addr,SEEK_SET);
	fread(&inode,sizeof(struct neo_inode),1,fp);
	printf("file size: %d\n",inode.i_size);
	for (i = 0; i < 12; i ++)
		printf("i block [%d] is %d\n",i,inode.i_block[i]);
	for (i = 0; i < inode.i_blocks; i ++){
		printf("i block [%d] is %ld\n",i,(i_block_to_addr(i,inode.i_block) / 4096));
	}
}

int main(int argc,char *argv[])
{
	inode_nr find;
	printf("this is a module test program\n\n");
	init();
	//printf("/abc/asds/asdfds/asdfsd/ghg\n\n");
	//path_resolve("/abc/asds/asdfds/asdfsd/ghg");
	
	//find = search_dentry(1,"worldofwarcraft");

	//find = search_dentry(8192,argv[1]);
	//printf("find inode : %d\n",find);
	//rmdir();
	//find = path_resolve(argv[2]);
	//printf("find inode : %d\n",find);
	//add_dentry(1,get_inode(1,1),"FF33",1);
	//add_dentry(1,get_inode(1,2),"FFwaooooooooooooooooooooooooooooooooooooooooooooooooooooooooooooooooo",1);
	//add_dentry(1,get_inode(1,1),"FF33333",1);
	
	//delete_dentry(1,"FF33",1);
	//free_inode(8192,2);

	find = path_resolve("/neo3");
	printf("last neo3 find inode : %d\n",find);
	find = path_resolve("/wao333");
	printf("last wao333 find inode : %d\n",find);
	//write_bitmap();
	print_i_block(2);
	return 0;
}





















