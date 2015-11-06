#include <math.h>
#include <time.h>
#include <stdio.h>
#include <string.h>
#include "neo_fs.h"
#include "atomic_ops.h"
#include <errno.h>

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
	unsigned int blkcnt,offset,info[3];
	__u64 inoaddr;
	__u64 blkaddr;
	block_nr *p;
	struct neo_inode dirinode;
	struct neo_dir_entry dentry;
	int i,n;
	inoaddr = inode_to_addr(ino);
	fseek(fp,inoaddr,SEEK_SET);
	fread(&dirinode,neo_sb_info.s_inode_size,1,fp);
	//print_inode(dirinode);
	if (dirinode.blocks == 0)
		return 0;
	blkcnt = dirinode.blocks;
	if (blkcnt <= 12)
		n = blkcnt;
	else
		n = 12;
	for (i = 0; i < n; i++){
		blkaddr = block_to_addr(dirinode.block[i]);
		if (blk_search_dentry(blkaddr,name,info) == 0)
			return 0;
	}
	if (blkcnt > 12){//dir file's max blocks count is 13,block[12] for indirect addr.
		n = blkcnt - 12;
		p = (__u32 *)malloc(4 * n);	//4 = sizeof(__32)
		fseek(fp,block_to_addr(dirinode.block[12]),SEEK_SET);
		fread(p,(4 * n),1,fp);
		for (i = 0, i < n, i++){
			blkaddr = block_to_addr(p[i]);
			if (blk_search_dentry(blkaddr,name,info) == 0)
				return 0;
		}
	}
	free(p);
}

int blk_search_dentry(__64 blkaddr,char *name,unsigned int info[])
{

	unsigned int offset_prev = 0;		//记录块内上一个目录项的偏移；
	unsigned int offset_cur = 0;		//记录块内当前目录项的偏移；
	unsigned int order = 0;			//当前目录项是这个块内的第几个。以上对应info3个成员，需要维护这3个值
	unsigned short true_len;		//计算record的真实长度
	unsigned char length;			//计算name的真实长度
	struct neo_dir_entry *cur;		//临时存放读取的条目
	void *block;

	block = (void *)malloc(BLOCK_SIZE);
	fseek(fp,blkaddr,SEEK_SET);
	fread(block,BLOCK_SIZE,1,fp);		//将此块读入内存

	cur = block;
	if(cur->name_len == 0)			//第一个是空块
		(char *)block += cur->rec_len;
	offset_prev += cur->rec_len;
	offset_cur += cur->rec_len;
	cur = block;
	block += 8;
	order++;
	while ((offset_cur + cur->rec_len) != 4096 ){		//当cur下一项还有
		length = (4 - cur->name_len%4) + cur->name_len;	//计算此name实际长度
		true_len = 8 + length;
		cur = block;
		if(cur->name)
			info <- prev,cur,tmp->inode,order;
			return 0;
		offset_prev = offset_cur;
		offset_cur += tmp->rec_len;
		fseek(fp, tmp->rec_len – true_len, SEEK_CUR);//指向下一个记录
		fread(tmp,8,1,fp);//至此cur已指向第一个目录项
		order++;//0 + 1 = 1
	}//循环跳出后再把最后一个目录项比较一下。完成~


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









