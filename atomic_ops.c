#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "atomic_ops.h"
#include "neo_fs.h"
#include <errno.h>
#include <time.h>
#include <math.h>

inode_nr path_resolve(char *path)
{
	inode_nr parent,res;
	int length;
	char *pos;
	char tmp[MAX_FILE_NAME];
	if (strcmp(path,"/") == 0)//root's inode is 1. 0 is reserved for judge
		return 1;
	path ++;
	parent = 1;
	while ((pos = strchr(path,'/')) != NULL)
	{
		if ((length = (pos - path)) > 255){
			errno = ENAMETOOLONG;
			return 0;
		}
		strncpy(tmp,path,pos - path);
		parent = search_dentry(parent,tmp);
		//printf("%s\n",tmp);
		if (parent == 0){
			errno = ENOENT;
			return 0;
		}
		path = ++pos;
	}
	//printf("%s\n",path);
	if ((res = search_dentry(parent,path)) != 0)
		return res;
	else{
		errno = ENOENT;
		return 0;
	}
}

inode_nr search_dentry(inode_nr ino, char *name)
{
	unsigned int blkcnt,info[4];
	__u64 inoaddr;
	__u64 blkaddr;
	block_nr *p;
	struct neo_inode dirinode;
	int i,n;
	inoaddr = inode_to_addr(ino);
	fseek(fp,inoaddr,SEEK_SET);
	fread(&dirinode,neo_sb_info.s_inode_size,1,fp);
	//print_inode(dirinode);
	if (dirinode.i_blocks == 0)
		return 0;
	blkcnt = dirinode.i_blocks;
	if (blkcnt <= 12)
		n = blkcnt;
	else
		n = 12;
	//printf("blkcnt : %d",blkcnt);
	for (i = 0; i < n; i++){
		blkaddr = block_to_addr(dirinode.i_block[i]);
		if (blk_search_dentry(blkaddr,name,info) == 0)
			return info[0];
	}
	if (blkcnt > 12){//dir file's max blocks count is 13,block[12] for indirect addr.
		n = blkcnt - 12;
		p = (__u32 *)malloc(4 * n);	//4 = sizeof(__32)
		fseek(fp,block_to_addr(dirinode.i_block[12]),SEEK_SET);
		fread(p,(4 * n),1,fp);
		for (i = 0; i < n; i++){
			blkaddr = block_to_addr(p[i]);
			if (blk_search_dentry(blkaddr,name,info) == 0)
				return info[0];
		}
	}
	free(p);
	return 0;
}

int blk_search_dentry(__u64 blkaddr,char *name,unsigned int info[])
{//在存放目录项的block中查找文件名为name的目录项，成功返回0，失败返回-1。

	unsigned int offset_prev = 0;		//记录块内上一个目录项的偏移；
	unsigned int offset_cur = 0;		//记录块内当前目录项的偏移；
	struct neo_dir_entry *cur;		//临时存放读取的目录项
	char cname[MAX_FILE_NAME] = {'\0'};
	void *block;				//此处未考虑移植扩展性，void *只在gcc中可以运算，ansi C并不支持
						//故指针移动通过计算block实现，然后cur跟进

	block = (void *)malloc(BLOCK_SIZE);
	cur = block;
	//printf("blkaddr : %ld",blkaddr);
	fseek(fp,blkaddr,SEEK_SET);
	fread(block,BLOCK_SIZE,1,fp);		//将此块读入内存

	if(cur->inode == 0){			//第一个是空块，此时将block指向第一个目录项
		block += cur->rec_len;
	}
	offset_prev += cur->rec_len;
	offset_cur += cur->rec_len;
	do {	//当cur还有下一项
		//length = (4 - cur->name_len%4) + cur->name_len;
		//true_len = 8 + length;
		cur = block;
		strncpy(cname,cur->name,cur->name_len);
		if (strcmp(cname,name) == 0){
			info[0] = cur->inode;
			info[1] = cur->rec_len;
			info[2] = offset_prev;
			info[3] = offset_cur;
			return 0;
		}
 /*
		printf("prev: %d   ",offset_prev);
		printf("cur: %d\n\n",offset_cur);
		printf("cur->rec_len: %d   ",cur->rec_len);
		printf("cur->name_len: %d   ",cur->name_len);
		printf("cur->name: %s\n\n",cur->name);
// */
		offset_prev = offset_cur;
		offset_cur += cur->rec_len;
		block += cur->rec_len;
		memset(cname,0,MAX_FILE_NAME);
	}
	while ((offset_prev + cur->rec_len) != 4096 );
	return -1;
}


block_nr get_block(char *path)
{
	return 0;
}

void print_sb(struct neo_super_block neo_sb_info)
{
	printf("super block:\n");
	printf("sb inodes count: %d\n",neo_sb_info.s_inodes_count);
	printf("sb blocks count: %d\n",neo_sb_info.s_blocks_count);
	printf("sb free inodes count: %d\n",neo_sb_info.s_inodes_count);
	printf("sb free blocks count: %d\n",neo_sb_info.s_blocks_count);
	printf("sb log block size: %d\n",neo_sb_info.s_log_block_size);
	printf("sb blocks/group: %d\n",neo_sb_info.s_blocks_per_group);
	printf("sb inodes/group: %d\n",neo_sb_info.s_inodes_per_group);
	printf("sb magic#: %d\n",neo_sb_info.s_magic);
	printf("sb inode size: %d\n\n",neo_sb_info.s_inode_size);
}

void print_gdt(struct neo_group_desc *gdt,int groupcnt)
{
	int i;
	printf("GDT:\n");
	for (i = 0; i < groupcnt; i++){
		printf("group %d :\n",i);
		printf("block bitmap: %d\n",gdt[i].bg_block_bitmap);
		printf("inode bitmap: %d\n",gdt[i].bg_inode_bitmap);
		printf("inode table: %d\n",gdt[i].bg_inode_table);
		printf("free blocks count: %d\n",gdt[i].bg_free_blocks_count);
		printf("free inodes count: %d\n",gdt[i].bg_free_inodes_count);
		printf("used dirs count: %d\n\n",gdt[i].bg_used_dirs_count);
	}
}

void print_inode(struct neo_inode ino)
{
	struct tm *local_time = NULL;
	char str_time[100];
	printf("inode info:\n");
	printf("uid: %d\n",ino.i_uid);
	printf("gid: %d\n",ino.i_gid);
	printf("size: %d\n",ino.i_size);
	printf("blocks: %d\n",ino.i_blocks);
	printf("block[0]: %d\n",ino.i_block[0]);

	local_time = localtime(&ino.i_atime);
	strftime(str_time, sizeof(str_time), "%Y-%m-%d,%H:%M:%S", local_time);
	printf("last access time:   ");
	printf(" %-19s\n",str_time);
	local_time = localtime(&ino.i_ctime);
	strftime(str_time, sizeof(str_time), "%Y-%m-%d,%H:%M:%S", local_time);
	printf("created time:       ");
	printf(" %-19s\n",str_time);
	local_time = localtime(&ino.i_mtime);
	strftime(str_time, sizeof(str_time), "%Y-%m-%d,%H:%M:%S", local_time);
	printf("last modified time: ");
	printf(" %-19s\n",str_time);

	printf("mode: %d\n",ino.i_mode);
}

__u64 inode_to_addr(inode_nr ino)
{
	int groupnr,r;
	int offset = BLOCK_SIZE * 2;			//block和inode的位图
	groupnr = ino / neo_sb_info.s_inodes_per_group;
	r = ino % neo_sb_info.s_inodes_per_group;
	if(is_powerof_357(groupnr))
		offset += BLOCK_SIZE * 2;		//SB&GDT 2Blocks
	offset += (BLOCK_SIZE * BLOCKS_PER_GROUP * groupnr + r * neo_sb_info.s_inode_size);
	//printf("inode %d addr is %d\n\n",ino,offset);
	return offset;
}

__u64 inline block_to_addr(block_nr blk)
{
	return (blk * 4096);
}

int is_powerof_357(int i)
{
	if (pow(3,(int)(float)(log(i)/log(3))) == i || pow(5,(int)(float)(log(i)/log(5))) == i || 
		pow(7,(int)(float)(log(i)/log(7))) == i || i == 0)
		return 1;
	return 0;
}









