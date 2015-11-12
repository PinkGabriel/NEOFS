#include<stdio.h>
#include<stdlib.h>
#include<string.h>
#include"neo_fs.h"
#include<time.h>
#include<math.h>

//#define DEBUG

void print_sb(struct neo_super_block neo_sb_info)
{
	printf("super block:\n");
	printf("sb inodes count: %d\n",neo_sb_info.s_inodes_count);
	printf("sb blocks count: %d\n",neo_sb_info.s_blocks_count);
	printf("sb groups count: %d\n",neo_sb_info.s_groups_count);
	printf("sb free inodes count: %d\n",neo_sb_info.s_free_inodes_count);
	printf("sb free blocks count: %d\n",neo_sb_info.s_free_blocks_count);
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

void print_bmp(unsigned char *bmp)
{	
	int i;
	for (i = 0; i < BLOCK_SIZE; i++){
		if (i % 64 == 0)
			printf("\n");
		printf("%X",bmp[i]);
		if (bmp[i] == 0)
			printf(" ");
	}
}

void print_inode(struct neo_inode ino)
{
	struct tm *local_time = NULL;
	char str_time[100];
	printf("root info:\n");
	printf("uid: %d\n",ino.i_uid);
	printf("gid: %d\n",ino.i_gid);
	printf("size: %d\n",ino.i_size);
	printf("blocks: %d\n",ino.i_blocks);

	local_time = localtime(&ino.i_atime);
	strftime(str_time, sizeof(str_time), "%Y-%m-%d,%H:%M:%S", local_time);
	printf("last access time: %s\n",str_time);
	local_time = localtime(&ino.i_ctime);
	strftime(str_time, sizeof(str_time), "%Y-%m-%d,%H:%M:%S", local_time);
	printf("created time: %s\n",str_time);
	local_time = localtime(&ino.i_mtime);
	strftime(str_time, sizeof(str_time), "%Y-%m-%d,%H:%M:%S", local_time);
	printf("last modified time: %s\n",str_time);

	printf("mode: %d\n",ino.i_mode);

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
	struct neo_super_block neo_sb_info,tmp;
	struct neo_group_desc *gd,*debugtmp;
	struct neo_inode root;
	FILE *fp = NULL;
	long length;
	int i;
	char *args[3];
	unsigned char bbitmap[BLOCK_SIZE];
	unsigned char ibitmap[BLOCK_SIZE];
	int blkcnt,groupcnt,remainder;
	if (argc < 2){
		printf("please type in image file name\n");
		return -1;
	}else if (argc == 2){
		args[0] = argv[0];
		args[1] = argv[1];
		args[2] = NULL;
	}else if (argc == 3){
		args[0] = argv[0];
		args[1] = argv[1];
		args[2] = argv[2];
	}else {
		printf("too many arguments");
		return -1;
	}
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
	print_gdt(gd,groupcnt);

	for (i = 0; i < groupcnt; i++){
#ifdef DEBUG
		printf("group %d :\n",i);
#endif
		__u64 offset = i * BLOCKS_PER_GROUP * BLOCK_SIZE;
		fseek(fp,offset,SEEK_SET);
		if (offset == 0)
			fseek(fp,1024,SEEK_CUR);//引导块占用1KB
		if (is_powerof_357(i)){
			fread(&tmp,sizeof(struct neo_super_block),1,fp);
			fseek(fp,offset + 4096,SEEK_SET);
			fread(debugtmp,sizeof(struct neo_group_desc) * groupcnt,1,fp);
			fseek(fp,offset + 8192,SEEK_SET);
			fread(bbitmap,sizeof(unsigned char),BLOCK_SIZE,fp);
			fread(ibitmap,sizeof(unsigned char),BLOCK_SIZE,fp);
#ifdef DEBUG
			printf("group %d has backup of super block and GDT\n",i);
			print_sb(tmp);
			print_gdt(debugtmp,groupcnt);
#endif
		}else{
#ifdef DEBUG
			printf("group %d doesn't have backup of SB&GDT\n",i);
#endif
			fseek(fp,offset,SEEK_SET);
			fread(bbitmap,sizeof(unsigned char),BLOCK_SIZE,fp);
			fread(ibitmap,sizeof(unsigned char),BLOCK_SIZE,fp);

		}
		if (args[2] != NULL && (strcmp(args[2],"bitmap") == 0)){
			printf("\n\nGroup %d block bitmap:\n\n",i);
			print_bmp(bbitmap);
			printf("\n\nGroup %d inode bitmap:\n\n",i);
			print_bmp(ibitmap);
			printf("\n\n\n");
		}
	}



	if (args[2] != NULL && (strcmp(args[2],"root") == 0)){
		fseek(fp,ROOT_ADDR,SEEK_SET);
		fread(&root,neo_sb_info.s_inode_size,1,fp);
		print_inode(root);
	}



	free(gd);
	free(debugtmp);
}









