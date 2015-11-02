#include<stdio.h>
#include<stdlib.h>
#include<string.h>
#include"neo_fs.h"
#include<time.h>
#include<math.h>

void print_sb(struct neo_super_block neo_sb_info)
{
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
	for (i = 0; i < groupcnt; i++){
		printf("gd[%d] bbitmap: %d\n",i,gdt[i].bg_block_bitmap);
		printf("gd[%d] ibitmap: %d\n",i,gdt[i].bg_inode_bitmap);
		printf("gd[%d] inodetable: %d\n",i,gdt[i].bg_inode_table);
		printf("gd[%d] free blocks count: %d\n",i,gdt[i].bg_free_blocks_count);
		printf("gd[%d] free inodes count: %d\n",i,gdt[i].bg_free_inodes_count);
		printf("gd[%d] used dirs count: %d\n\n",i,gdt[i].bg_used_dirs_count);
	}
}

int main(int argc,char *argv[])
{
	struct neo_super_block neo_sb_info,tmp;
	struct neo_group_desc *gd,*debugtmp;
	FILE *fp = NULL;
	long length;
	int i;
	int blkcnt,groupcnt,remainder;
	if ((fp = fopen(argv[1],"rb")) == NULL){
		printf("image file not exist\n");
		return -1;
	}
	fseek(fp,0,SEEK_END);
	length = ftell(fp);
	printf("filesystem total length: %ld\n",length);
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
	fseek(fp,1024,SEEK_SET);
	fread(&neo_sb_info,sizeof(struct neo_super_block),1,fp);
	fseek(fp,4096,SEEK_SET);
	gd = (struct neo_group_desc *)malloc(sizeof(struct neo_group_desc) * groupcnt);
	debugtmp = (struct neo_group_desc *)malloc(sizeof(struct neo_group_desc) * groupcnt);
	fread(gd,sizeof(struct neo_group_desc) * groupcnt,1,fp);

	print_sb(neo_sb_info);
#ifdef DEBUG
	print_gdt(gd,groupcnt);
#endif
	printf("block count: %d\n",blkcnt);
	printf("group count: %d\n",groupcnt);
	for (i = 0; i < groupcnt; i++){
		printf("group %d :\n",i);
		__u64 offset = i * BLOCKS_PER_GROUP * BLOCK_SIZE;
		fseek(fp,offset,SEEK_SET);
		if (offset == 0)
			fseek(fp,1024,SEEK_CUR);//引导块占用1KB
		if (is_powerof_357(i)){
			printf("group %d has backup of super block and GDT\n",i);
#ifdef DEBUG
			fread(&tmp,sizeof(struct neo_super_block),1,fp);
			fseek(fp,offset + 4096,SEEK_SET);
			fread(debugtmp,sizeof(struct neo_group_desc) * groupcnt,1,fp);
#endif
		}
	}






	free(gd);
}









