#include<stdio.h>
#include<math.h>
#include<stdlib.h>
#include<string.h>
#include"neo_fs.h"
#include<time.h>

#define DEBUG

struct neo_super_block neo_sb_info;
struct neo_group_desc neo_gd_info;
struct neo_group_desc *gd;
struct neo_inode root;
FILE *fp = NULL;

int block_group_format(int bgnum,int groupcnt)
{
	unsigned char bbitmap[BLOCK_SIZE];	//block bitmap
	unsigned char ibitmap[BLOCK_SIZE];	//inode bitmap
	__u64 offset = bgnum * BLOCKS_PER_GROUP * BLOCK_SIZE;
	fseek(fp,offset,SEEK_SET);
	if (offset == 0)
		fseek(fp,1024,SEEK_CUR);//引导块占用1KB
	memset(bbitmap,0,BLOCK_SIZE);
	memset(bbitmap,0xFF,32);	//前258/260个block存储元数据
	memset(ibitmap,0xFF,BLOCK_SIZE);
	memset(ibitmap,0,(BLOCK_SIZE / 4));
	if (is_powerof_357(bgnum)){	//写入超级块和GDT
		neo_sb_info.s_block_group_nr = bgnum;
		fwrite(&neo_sb_info,sizeof(struct neo_super_block),1,fp);
		fseek(fp,offset + 4096,SEEK_SET);
		fwrite(gd,sizeof(struct neo_group_desc),groupcnt,fp);
		bbitmap[32] = 0xF0;
		if (bgnum == 0)		//直接给root和预留的inode 0 分配空间
			ibitmap[0] = 0xC0;
		fseek(fp,offset + 8192,SEEK_SET);
		fwrite(bbitmap,BLOCK_SIZE,1,fp);
		fwrite(ibitmap,BLOCK_SIZE,1,fp);
	}else {
		bbitmap[32] = 0xC0;
		fseek(fp,offset,SEEK_SET);
		fwrite(bbitmap,BLOCK_SIZE,1,fp);
		fwrite(ibitmap,BLOCK_SIZE,1,fp);
	}
}

int is_powerof_357(int i)
{
	if (pow(3,(int)(float)(log(i)/log(3))) == i || pow(5,(int)(float)(log(i)/log(5))) == i || 
		pow(7,(int)(float)(log(i)/log(7))) == i || i == 0)
		return 1;
	return 0;
}

int main(int argc,char *argv[])
{
	long length;
	int groupcnt;
	/*n记录完整的块组的个数*/
	int i,n,extraoff;
	__u32 blkcnt,inocnt;
	__u16 remainder,iremainder;

	if (argc > 2){
		printf("argument error\n");
		return -1;
	}

#ifdef DEBUG_SIZE
	printf("sizeof sb %d\n",sizeof(struct neo_super_block));
	printf("sizeof gd %d\n",sizeof(struct neo_group_desc));
	printf("sizeof inode %d\n",sizeof(struct neo_inode));
#endif

	if ((fp = fopen(argv[1],"rb+")) == NULL){
		printf("image file not exist\n");
		return -1;
	}
	fseek(fp,0,SEEK_END);
	length = ftell(fp);
	if (length > MAX_FS_SIZE){
		printf("image file bigger than 16GB");
		return -1;
	}

	blkcnt = length / BLOCK_SIZE; 
	groupcnt = blkcnt / BLOCKS_PER_GROUP;
	remainder = blkcnt % BLOCKS_PER_GROUP;
	if (remainder > 2560){//bigger than 10MB 's blocks count
		groupcnt ++;
	}
	else{
		blkcnt -= remainder;
		remainder = 0;
	}
	inocnt = blkcnt / 4;
	iremainder = remainder / 4;

#ifdef DEBUG
	printf("file length is %ld bytes\n",length);
	printf("block count is %u \n",blkcnt);
	printf("group count is %d \n",groupcnt);
	printf("remainder is %u \n",remainder);
	printf("iremainder is %u \n",iremainder);
#endif
	neo_sb_info.s_inodes_count = inocnt;
	neo_sb_info.s_blocks_count = blkcnt;
	neo_sb_info.s_free_inodes_count = inocnt;
	neo_sb_info.s_free_blocks_count = blkcnt;
	neo_sb_info.s_log_block_size = LOG_BLOCK_SIZE;
	neo_sb_info.s_blocks_per_group = BLOCKS_PER_GROUP;
	neo_sb_info.s_inodes_per_group = BLOCKS_PER_GROUP / 4;
	neo_sb_info.s_magic = 0xEF59;//ext2:0xEF53
	neo_sb_info.s_inode_size = sizeof(struct neo_inode);

#ifdef DEBUG
	printf("sb inodes count: %d\n",neo_sb_info.s_inodes_count);
	printf("sb blocks count: %d\n",neo_sb_info.s_blocks_count);
	printf("sb free inodes count: %d\n",neo_sb_info.s_inodes_count);
	printf("sb free blocks count: %d\n",neo_sb_info.s_blocks_count);
	printf("sb log block size: %d\n",neo_sb_info.s_log_block_size);
	printf("sb blocks/group: %d\n",neo_sb_info.s_blocks_per_group);
	printf("sb inodes/group: %d\n",neo_sb_info.s_inodes_per_group);
	printf("sb magic#: %d\n",neo_sb_info.s_magic);
	printf("sb inode size: %d\n",neo_sb_info.s_inode_size);
#endif
	/*初始化GDT*/
	gd = (struct neo_group_desc *)malloc(sizeof(struct neo_group_desc) * groupcnt); 

	/*n记录完整的块组的个数*/
	if (remainder == 0)
		n = groupcnt;
	else
		n = groupcnt - 1;
	extraoff = 0;
	for (i = 0; i < n; i++){
		if (is_powerof_357(i))
			extraoff = 2;
		gd[i].bg_block_bitmap = (BLOCKS_PER_GROUP * i) + extraoff;
		gd[i].bg_inode_bitmap = (BLOCKS_PER_GROUP * i) + extraoff + 1;
		gd[i].bg_inode_table = (BLOCKS_PER_GROUP * i) + extraoff + 2;
		gd[i].bg_free_blocks_count = BLOCKS_PER_GROUP;
		gd[i].bg_free_inodes_count = BLOCKS_PER_GROUP / 4;
		gd[i].bg_used_dirs_count = 0;
		extraoff = 0;
	}
	if (remainder != 0){
		if (is_powerof_357(i))
			extraoff = 2;
		gd[i].bg_block_bitmap = (BLOCKS_PER_GROUP * i) + extraoff;
		gd[i].bg_inode_bitmap = (BLOCKS_PER_GROUP * i) + extraoff + 1;
		gd[i].bg_inode_table = (BLOCKS_PER_GROUP * i) + extraoff + 2;
		gd[i].bg_free_blocks_count = remainder;
		gd[i].bg_free_inodes_count = iremainder;
		gd[i].bg_used_dirs_count = 0;
	}

	for (i = 0; i < groupcnt; i++){
		block_group_format(i,groupcnt);
	}
	/*add root*/
	root.i_uid = getuid();
	root.i_gid = getgid();
	root.i_size = 0;
	root.i_blocks = 0;
	root.i_atime = time(NULL);
	root.i_ctime = root.i_atime;
	root.i_mtime = root.i_atime;
	memset(root.i_block,0,NEO_BLOCKS * sizeof(__u32));
	root.i_mode = 2;

	fseek(fp,ROOT_ADDR,SEEK_SET);
	fwrite(&root,sizeof(struct neo_inode),1,fp);

	free(gd);
}



