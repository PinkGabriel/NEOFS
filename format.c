#include<stdio.h>
#include"neo_fs.h"
#include<time.h>

#define DEBUG

struct neo_super_block neo_sb_info;
struct neo_group_desc neo_gd_info;

int block_group_format(int bgnum)
{
	struct neo_super_block tmp_sb = neo_sb_info;
	if (pow(3,(int)(log(bgnum)/log(3))) == bgnum)
		
}

int main(int argc,char *argv[])
{
	FILE *fp = NULL;
	long length;
	int groupcnt;
	/*n记录完整的块组的个数*/
	int i,n;
	__u32 blkcnt,inocnt;
	__u16 remainder,iremainder;

	if (argc > 2){
		printf("argument error\n");
		return -1;
	}

#ifdef DEBUG
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
	/*n记录完整的块组的个数*/
	if (remainder == 0)
		n = groupcnt;
	else
		n = groupcnt - 1;
	for (i = 0; i < n; i++){
		block_group_format(i);
	}




}



